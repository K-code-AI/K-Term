/* K-Term — Wayland frontend, event loop, main. */
#include "k-term.h"
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>

#define FRAME_INTERVAL_NS 8000000   /* 8 ms redraw batching window */
#define SCROLL_LINES_PER_DETENT 3   /* scrollback lines moved per mouse-wheel notch */

typedef struct Wayland {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_egl_window *egl_window;

    struct wl_data_device_manager *data_mgr;
    struct wl_data_device *data_dev;

    struct xkb_context *xkb_ctx;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;

    struct wl_cursor_theme *cursor_theme;
    struct wl_cursor_image *cursor_image;
    struct wl_surface *cursor_surface;

    int comp_ver;               /* bound wl_compositor interface version */
    int pw, ph;                 /* pending configure size (px) */
    int cur_w, cur_h;           /* applied size (px) */
    int first_done;
    /* The one outstanding wl_surface_frame callback, NULL when none is in flight. It replaces the old
       `frame_armed` flag and `swapped` counter: the callback OBJECT is the state, so it cannot drift
       out of sync with what was actually committed (see the invariant above request_frame). */
    struct wl_callback *frame_cb;
    int swapped_once;           /* at least one successful swap (the first configure must draw) */
    int resized;                /* a configure changed the pixel size and no buffer of that size has
                                   been attached yet: the compositor is waiting for one, so the frame
                                   path MUST answer with a swap even when nothing else is dirty */
    int dirty;
    int running;
    int is_fullscreen, is_maximized, is_activated;   /* xdg_toplevel configure states (debug only) */
    int32_t ptr_x, ptr_y;       /* last pointer position in surface coords (px) */
    int ptr_btn;                /* held button mapped to SGR index (0/1/2), or -1 */
    int selecting;              /* 1 while a left-button text selection drag is in progress */
    double scroll_accum;        /* pending scrollback scroll in lines (+ = up/history) */
    double frame_axis_val;      /* axis value (px) accumulated within the current scroll frame */
    int    frame_discrete;      /* discrete wheel steps accumulated within the current frame */
    int    frame_has_axis;      /* an axis event occurred in the current frame */
    int    frame_discrete_seen; /* an axis_discrete event occurred in the current frame */
} Wayland;

static Wayland W;
static Term term;
static Pty pty;
static Font font;
static Render rend;
static Config conf;
static const char *opt_cmd = NULL;   /* -e/--exec: argv storage lives until exit, so const is fine */
static int base_font_size;      /* configured size, restored by Ctrl+0 */

/* clipboard (Wayland data-device) state */
static char *clip_text;         /* selection text offered to the clipboard (heap) */
static size_t clip_len;
static struct wl_data_source *clip_source;
static struct wl_data_offer *sel_offer;    /* current clipboard selection offer (may be NULL) */
static char sel_offer_mime[128];           /* a text mime offered by sel_offer */
static int sel_offer_text;                 /* 1 when sel_offer offers a text mime */
static char last_offer_mime[128];          /* text mime recorded from the most recent data_offer */
static int last_offer_text;
static int paste_fd[2] = { -1, -1 };       /* pipe: [0] read end (in epoll), [1] given to receive */
/* The whole paste is buffered before it reaches the PTY: the ESC[200~ / ESC[201~ delimiters have to
   surround the ENTIRE block, so the text cannot be streamed through as it arrives. */
static char *paste_buf;
static size_t paste_len, paste_cap;
static int paste_overflow;                 /* paste bigger than PASTE_BUF_MAX: send it raw */
#define PASTE_BUF_MAX (8u << 20)           /* 8 MiB of clipboard text */
static int g_ep = -1;                      /* main epoll fd (for adding the paste pipe) */

/* Key repeat. A Wayland compositor never synthesizes repeat events the way the X
   server does: wl_keyboard.key delivers exactly one press and one release per physical keypress, and
   repeat_info only reports what the user configured system-wide. A client that wants "hold the key and
   it repeats" therefore has to generate the repeats itself. krfd is a CLOCK_MONOTONIC timer that fires
   repeat_delay ms after a repeatable press and then every 1/repeat_rate seconds; rep_key is the held
   key it re-sends (the LAST repeatable press wins, releasing it stops the repeat). The timing comes
   from K-Term's own config rather than from repeat_info so the feel is identical on every compositor
   and the settings are testable; repeat_delay = 0 turns K-Term's repeat off. */
static int krfd = -1;
static int rep_key = -1;        /* Wayland keycode being repeated, -1 = none */
static uint32_t rep_serial;     /* serial of that key's press (clipboard actions need one) */

/* ---------------- helpers ---------------- */

static const struct wl_callback_listener frame_listener;
static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t format, int fd, uint32_t size);
static void selection_copy(uint32_t serial);
static void clipboard_paste(void);

static void wl_flush(void)
{
    while (wl_display_flush(W.display) < 0 && errno == EAGAIN) {
        struct pollfd pfd = { .fd = wl_display_get_fd(W.display), .events = POLLOUT };
        poll(&pfd, 1, 100);
    }
}

/* ---- frame pacing -------------------------------------------------------------
   Three rules, each the result of a real freeze here (ISSUES.md "Fixed"):
   1. A wl_surface_frame callback is armed ONLY together with a commit that attaches a NEW buffer,
      i.e. only from redraw() after a successful swap. A damage-driven compositor runs a render pass
      only when something on the output changed, and frame callbacks are delivered during such a pass,
      so a callback armed on an EMPTY commit can stay pending forever — and every draw path here is
      gated on "no callback in flight", so the display freezes. That was the original fullscreen bug.
   2. eglSwapBuffers is NOT guaranteed to return: it waits until the compositor gives a buffer back,
      and that wl_buffer.release can only be read from THIS thread. So a swap must never run while a
      wl_display_prepare_read() is outstanding — the release would sit unread in the socket while the
      thread is parked inside the driver, and the wait is permanent (measured: the second swap from the
      timerfd branch never returned, /proc/<pid>/wchan = futex_do_wait; black screen, no keys, no PTY
      reads — the whole terminal looked dead). The event loop therefore finishes the wayland read
      (read_events, or cancel_read when the display fd was not the reason epoll woke) and dispatches
      pending events BEFORE it runs the pty/timerfd/signalfd/paste/repeat branches. Swapping inside a
      listener is fine — the read is already complete there — and frame_done() is the preferred moment,
      because the compositor has just shown the previous buffer.
   3. The very first buffer must be attached by the FIRST xdg_surface_configure handler: a window is not
      mapped until a commit carries a buffer, so a client that defers the first draw to its timer never
      maps and therefore never receives a frame callback either (measured with a standalone EGL client).
      Nothing is in flight at that point, so that swap cannot wait for a release. */
static void request_frame(void)
{
    if (W.frame_cb || !W.surface)
        return;
    W.frame_cb = wl_surface_frame(W.surface);
    wl_callback_add_listener(W.frame_cb, &frame_listener, NULL);
}

static void term_size_changed(void)
{
    if (!W.egl_window)
        return;
    int cols = imax_(1, W.cur_w / font.cell_w);
    int rows = imax_(1, W.cur_h / font.cell_h);
    if (cols != term.g.cols || rows != term.g.rows) {
        term_resize(&term, rows, cols);
        pty_setsize(&pty, rows, cols);
        klog(L_DEBUG, "grid resized to %dx%d (window %dx%d px, cell %dx%d, unused %d x %d px)",
             cols, rows, W.cur_w, W.cur_h, font.cell_w, font.cell_h,
             W.cur_w - cols * font.cell_w, W.cur_h - rows * font.cell_h);
    }
}

/* Slide the viewport through scrollback by moving Grid.scroll_off. delta > 0 scrolls UP into
   history (toward row[0], the oldest retained line); delta < 0 scrolls DOWN toward the live
   bottom. Clamped to the grid invariant 0 <= scroll_off <= live_off, where live_off is the
   anchor that follows the newest output. Returns the movement actually applied (same sign as
   delta, 0 when clamped at an end) so callers can detect a scroll that hit a limit. */
static int scrollback_nav(int delta)
{
    int live = term.g.live_off;
    if (live <= 0)
        return 0;   /* nothing above the live view yet */
    int old = term.g.scroll_off;
    int so = iclamp_(old - delta, 0, live);
    int applied = old - so;
    if (applied) {
        term.g.scroll_off = so;
        W.selecting = 0;
        term_select_clear(&term);   /* selection is viewport-relative; drop it when scrolling */
        W.dirty = 1;
    }
    return applied;
}

/* Apply a smooth scrollback scroll expressed in (possibly fractional) lines.
   delta_lines > 0 scrolls up (into history); < 0 scrolls down (toward the live bottom).
   Fractional remainders are carried in W.scroll_accum so high-resolution (pixel) scrolling
   accumulates into whole-line steps without losing sub-line motion. */
static void scroll_apply(double delta_lines)
{
    W.scroll_accum += delta_lines;
    int whole = (int)W.scroll_accum;   /* truncate toward zero */
    if (whole == 0)
        return;
    int applied = scrollback_nav(whole);
    W.scroll_accum -= whole;
    if (applied != whole)
        W.scroll_accum = 0;   /* hit the top/bottom: drop any pending remainder */
}

/* Return the view to the live bottom (used when the user sends any key to the PTY, or when
   new output arrives). Also drops any pending fractional scroll so it cannot linger. */
static void snap_to_live(void)
{
    W.scroll_accum = 0;
    if (term.g.scroll_off != term.g.live_off) {
        term.g.scroll_off = term.g.live_off;
        W.dirty = 1;
    }
}

/* Zoom: re-rasterize the font at a new pixel size (window px unchanged, so cols/rows
   change). Blank viewport cells are re-filled with the configured colors so columns
   revealed by zooming out then back in match the palette; existing text is left intact. */
static void zoom_apply(int newpx)
{
    newpx = iclamp_(newpx, 6, 256);
    if (newpx == conf.font_size)
        return;
    conf.font_size = newpx;
    if (font_resize(&font, newpx) < 0)
        return;
    term_size_changed();
    for (int y = 0; y < term.g.rows; y++) {
        Cell *r = grid_vprow(&term.g, y);
        for (int x = 0; x < term.g.cols; x++)
            if (r[x].ch == 0) {
                r[x].bg = conf.bg;
                r[x].fg = conf.fg;
            }
    }
    W.dirty = 1;
    klog(L_DEBUG, "zoom: font %dpx, grid %dx%d", conf.font_size, term.g.cols, term.g.rows);
}

static int swap_fail_streak;    /* consecutive failed swaps; only used to back off the log line */

static void redraw(void)
{
    if (!W.egl_window || !rend.prog)
        return;
    W.dirty = 0;
    /* A frame callback can still be pending when a configure forces a new buffer: the buffer about to
       be attached supersedes it, so drop its callback and let the new commit carry its own.
       wl_callback.destroy is always legal and libwayland discards events of a destroyed object, so an
       old done can never clear the new callback's state. */
    if (W.frame_cb) {
        wl_callback_destroy(W.frame_cb);
        W.frame_cb = NULL;
    }
    if (!render_draw(&rend, &term, &font)) {
        /* NO buffer was attached, so nothing may be armed on the commit that follows: the done of a
           commit without a buffer can stay pending forever, and every draw path here is gated on "no
           callback in flight" (that is exactly how the fullscreen freeze worked). Keep the frame dirty
           so the 8 ms timer retries, and do not print one error line per retry. */
        if ((swap_fail_streak++ % 64) == 0)
            klog(L_WARN, "swap failed (%d in a row), retrying on the next frame", swap_fail_streak);
        W.dirty = 1;
        return;
    }
    swap_fail_streak = 0;
    if (W.resized)
        klog(L_DEBUG, "configure answered: buffer %dx%d attached (grid %dx%d)", W.cur_w, W.cur_h,
             term.g.cols, term.g.rows);
    W.resized = 0;                    /* the buffer just attached has the configured size */
    W.swapped_once = 1;
    request_frame();                  /* arm BEFORE the commit: it belongs to this frame */
    wl_surface_commit(W.surface);     /* present this frame; the armed cb fires on it */
    wl_display_flush(W.display);
}

/* Applies a pending configure. Returns 1 when the pixel size actually changed — the configure handler
   needs to know, because a size change makes the buffer still attached the WRONG size. */
static int apply_pending_resize(void)
{
    if (!W.egl_window || W.pw <= 0 || W.ph <= 0)
        return 0;
    if (W.pw == W.cur_w && W.ph == W.cur_h)
        return 0;
    W.cur_w = W.pw;
    W.cur_h = W.ph;
    wl_egl_window_resize(W.egl_window, W.cur_w, W.cur_h, 0, 0);
    render_resize(&rend, W.cur_w, W.cur_h);
    term_size_changed();
    W.dirty = 1;
    W.resized = 1;      /* cleared by redraw() once a buffer of THIS size has been attached */
    /* Do NOT arm a frame callback here, and do not swap here: a wl_frame callback is only delivered
       after a buffer is committed, so arming one before any draw leaves W.frame_cb set forever and
       blocks every draw path (the startup deadlock, and the fullscreen freeze). The frame path
       (timerfd / frame_done) is what answers the configure with a buffer of the new size. */
    return 1;
}

/* ---------------- wayland listeners ---------------- */

static void frame_done(void *data, struct wl_callback *cb, uint32_t t);
static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)data; (void)time;
    if (W.frame_cb == cb)
        W.frame_cb = NULL;
    wl_callback_destroy(cb);
    /* The compositor has shown the buffer whose callback this was, so a swap now has the best possible
       chance of not waiting on a buffer release — this is the preferred moment to draw. `resized` is
       part of the condition because a configure that only changed the SIZE leaves nothing dirty: the
       compositor is waiting for a buffer of the new size and this is the first safe chance to give it
       one (fullscreen, maximize, drag-resize). */
    if (W.dirty || W.resized)
        redraw();
}

static void wm_base_ping(void *d, struct xdg_wm_base *wb, uint32_t serial)
{
    (void)d;
    xdg_wm_base_pong(wb, serial);
    wl_display_flush(W.display);
}
static const struct xdg_wm_base_listener wm_base_listener = { .ping = wm_base_ping };

static void xdg_surface_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    apply_pending_resize();
    if (!W.first_done) {
        W.first_done = 1;
        W.dirty = 1;
    }
    /* Two rules, both learned the hard way (ISSUES.md: the fullscreen freeze and the black-window
       regression):
       1. The FIRST configure must attach a buffer right here. A window is not mapped until a commit
          carries a buffer, and a client that waits for a frame callback before drawing the first frame
          never maps and never gets that callback. Nothing is in flight yet, so this swap cannot wait
          on a buffer release.
       2. Every LATER configure is answered with a commit only; the buffer of the NEW size is attached
          by the frame path — the timerfd (<= 8 ms later) or frame_done the moment the compositor
          acknowledges the buffer in flight. `W.resized` (set by apply_pending_resize) is what makes
          that happen even when the grid itself has not changed, which is exactly the fullscreen case.
          A swap here would be legal (the wayland read is complete while a listener runs) but it is not
          needed, and answering a configure with a buffer while another one is still in flight is what
          made the driver fail the swap with EGL_BAD_ALLOC in session 45. */
    if (!W.swapped_once)
        redraw();
    else
        wl_surface_commit(W.surface);
}
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h,
                              struct wl_array *states)
{
    (void)d; (void)t;
    if (w > 0 && h > 0)
        W.pw = w, W.ph = h;   /* apply after commit via apply_pending_resize */
    /* No behaviour depends on the toplevel states, but they are the only client-side evidence that the
       compositor put the window into fullscreen/maximized, so a change is logged under -d: without it a
       "the screen froze in fullscreen" report cannot be followed from the client side at all. `activated`
       is included because it changes on every focus switch (and at startup), which also proves the state
       decoding itself is running. */
    uint32_t *st;
    int fs = 0, mx = 0, act = 0;
    wl_array_for_each(st, states) {
        if (*st == XDG_TOPLEVEL_STATE_FULLSCREEN)
            fs = 1;
        else if (*st == XDG_TOPLEVEL_STATE_MAXIMIZED)
            mx = 1;
        else if (*st == XDG_TOPLEVEL_STATE_ACTIVATED)
            act = 1;
    }
    if (fs != W.is_fullscreen || mx != W.is_maximized || act != W.is_activated) {
        W.is_fullscreen = fs;
        W.is_maximized = mx;
        W.is_activated = act;
        klog(L_DEBUG, "toplevel state: fullscreen %d maximized %d activated %d (configure %dx%d px)",
             fs, mx, act, w, h);
    }
}

static void toplevel_close(void *d, struct xdg_toplevel *t)
{
    (void)d; (void)t;
    W.running = 0;
}
static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static void send_str(const char *s, size_t n)
{
    pty_write(&pty, s, n);
}

/* Translate one key press and act on it. Returns 1 when the press is something the user expects to
   keep going while the key is held (a keystroke delivered to the PTY, or scrollback navigation) and 0
   for the terminal-local one-shot actions (zoom, copy, paste), which must never auto-repeat. */
static int send_key(uint32_t key, uint32_t serial)
{
    if (!W.keymap || !W.xkb_state)
        return 0;
    xkb_keycode_t kc = key + 8;

    xkb_keysym_t base = xkb_state_key_get_one_sym(W.xkb_state, kc);
    xkb_mod_mask_t eff = xkb_state_serialize_mods(W.xkb_state, XKB_STATE_MODS_EFFECTIVE);
    xkb_mod_mask_t ctrl = xkb_keymap_mod_get_mask(W.keymap, "Control");
    xkb_mod_mask_t shift = xkb_keymap_mod_get_mask(W.keymap, "Shift");
    int shift_held = shift && (eff & shift);

    /* Shift+PageUp/Down/Home/End navigate scrollback instead of sending escape codes. */
    if (shift_held) {
        switch (base) {
        case XKB_KEY_Page_Up:   scrollback_nav(term.g.rows); return 1;
        case XKB_KEY_Page_Down: scrollback_nav(-term.g.rows); return 1;
        case XKB_KEY_Home:      scrollback_nav(term.g.rcap); return 1;
        case XKB_KEY_End:       scrollback_nav(-term.g.rcap); return 1;
        default: break;
        }
    }
    /* Ctrl+Shift+C copies the current selection; Ctrl+Shift+V pastes the clipboard.
       Handled before snap_to_live so a copy can read a scrolled-back selection. */
    if (ctrl && (eff & ctrl) && shift_held) {
        /* One-shot: repeating either would re-offer the selection or paste the clipboard over and
           over, so these never arm the repeat. */
        if (base == XKB_KEY_C) { selection_copy(serial); return 0; }
        if (base == XKB_KEY_V) { clipboard_paste(); return 0; }
    }
    /* any other key sent to the PTY returns the view to the live bottom */
    snap_to_live();

    if (ctrl && (eff & ctrl)) {
        /* Ctrl+letter / Ctrl+common punctuation -> control codes */
        if (base >= XKB_KEY_a && base <= XKB_KEY_z) {
            char c = (char)(base - XKB_KEY_a + 1);
            send_str(&c, 1);
            return 1;
        }
        switch (base) {
        case XKB_KEY_bracketleft:  send_str("\x1b", 1); return 1;
        case XKB_KEY_bracketright: send_str("\x1d", 1); return 1;
        case XKB_KEY_backslash:    send_str("\x1c", 1); return 1;
        case XKB_KEY_3:            send_str("\x1b", 1); return 1;   /* Ctrl+3 = ESC */
        case XKB_KEY_space:        send_str("\x00", 1); return 1;
        /* zoom is a one-shot: a held Ctrl+- would otherwise fly past every size */
        case XKB_KEY_plus: case XKB_KEY_equal: case XKB_KEY_KP_Add:
            zoom_apply(conf.font_size + 2); return 0;
        case XKB_KEY_minus: case XKB_KEY_KP_Subtract:
            zoom_apply(conf.font_size - 2); return 0;
        case XKB_KEY_0: case XKB_KEY_KP_0:
            zoom_apply(base_font_size); return 0;
        default: break;
        }
    }

    switch (base) {
    case XKB_KEY_Return:       send_str("\r", 1); return 1;
    case XKB_KEY_KP_Enter:     send_str("\r", 1); return 1;
    case XKB_KEY_BackSpace:    send_str("\x7f", 1); return 1;
    case XKB_KEY_Tab:          send_str("\t", 1); return 1;
    case XKB_KEY_ISO_Left_Tab: send_str("\x1b[Z", 3); return 1;
    case XKB_KEY_Escape:       send_str("\x1b", 1); return 1;
    case XKB_KEY_Up:           send_str("\x1b[A", 3); return 1;
    case XKB_KEY_Down:         send_str("\x1b[B", 3); return 1;
    case XKB_KEY_Right:        send_str("\x1b[C", 3); return 1;
    case XKB_KEY_Left:         send_str("\x1b[D", 3); return 1;
    case XKB_KEY_Home:         send_str("\x1b[H", 3); return 1;
    case XKB_KEY_End:          send_str("\x1b[F", 3); return 1;
    case XKB_KEY_Page_Up:      send_str("\x1b[5~", 4); return 1;
    case XKB_KEY_Page_Down:    send_str("\x1b[6~", 4); return 1;
    case XKB_KEY_Insert:       send_str("\x1b[2~", 4); return 1;
    case XKB_KEY_Delete:       send_str("\x1b[3~", 4); return 1;
    case XKB_KEY_F1:  send_str("\x1bOP", 4); return 1;
    case XKB_KEY_F2:  send_str("\x1bOQ", 4); return 1;
    case XKB_KEY_F3:  send_str("\x1bOR", 4); return 1;
    case XKB_KEY_F4:  send_str("\x1bOS", 4); return 1;
    case XKB_KEY_F5:  send_str("\x1b[15~", 6); return 1;
    case XKB_KEY_F6:  send_str("\x1b[17~", 6); return 1;
    case XKB_KEY_F7:  send_str("\x1b[18~", 6); return 1;
    case XKB_KEY_F8:  send_str("\x1b[19~", 6); return 1;
    case XKB_KEY_F9:  send_str("\x1b[20~", 6); return 1;
    case XKB_KEY_F10: send_str("\x1b[21~", 6); return 1;
    case XKB_KEY_F11: send_str("\x1b[23~", 6); return 1;
    case XKB_KEY_F12: send_str("\x1b[24~", 6); return 1;
    default: break;
    }
    if (base >= 0x10000000)   /* dead keys and other non-text keysyms */
        return 0;
    if (base == XKB_KEY_NoSymbol)
        return 0;
    char buf[64];
    int n = xkb_state_key_get_utf8(W.xkb_state, kc, buf, sizeof(buf));
    if (n <= 0)
        return 0;
    send_str(buf, (size_t)n);
    return 1;
}

/* Stop repeating and forget the held key. Any expiry that already fired is drained here, so a release
   that raced with the timer cannot deliver one extra keystroke. */
static void repeat_stop(void)
{
    rep_key = -1;
    if (krfd < 0)
        return;
    struct itimerspec its = { 0 };
    timerfd_settime(krfd, 0, &its, NULL);
    uint64_t exp;
    while (read(krfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp))
        ;
}

/* A repeatable press: (re)start the delay window. One-shot — repeat_fire() switches to the rate. */
static void repeat_start(uint32_t key, uint32_t serial)
{
    if (krfd < 0 || conf.repeat_delay <= 0)
        return;
    rep_key = (int)key;
    rep_serial = serial;
    struct itimerspec its = { 0 };
    its.it_value.tv_sec = conf.repeat_delay / 1000;
    its.it_value.tv_nsec = (long)(conf.repeat_delay % 1000) * 1000000L;
    timerfd_settime(krfd, 0, &its, NULL);
}

/* The delay elapsed: re-send the held key, then keep re-sending it every 1/repeat_rate seconds. The
   key is re-TRANSLATED on every repeat (send_key reads the current xkb state), so pressing Shift while
   holding 'a' starts producing 'A', exactly as on any other terminal. */
static void repeat_fire(void)
{
    uint64_t exp;
    while (read(krfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp))
        ;
    if (rep_key < 0)
        return;
    int rate = conf.repeat_rate > 0 ? conf.repeat_rate : 30;
    long ns = 1000000000L / rate;          /* rate 1 -> exactly one second */
    struct itimerspec its = { 0 };
    its.it_interval.tv_sec = ns / 1000000000L;    /* tv_nsec must stay < 1e9 */
    its.it_interval.tv_nsec = ns % 1000000000L;
    its.it_value = its.it_interval;
    timerfd_settime(krfd, 0, &its, NULL);
    send_key((uint32_t)rep_key, rep_serial);
}

static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t time,
                   uint32_t key, uint32_t state_wl)
{
    (void)d; (void)k; (void)time;
    if (!W.keymap || !W.xkb_state)
        return;
    if (state_wl == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* Only the most recent repeatable press repeats, which is what the X server does too. A press
           that is NOT repeatable deliberately leaves an existing repeat running: holding BackSpace and
           tapping a modifier must not interrupt the BackSpace. */
        if (send_key(key, serial))
            repeat_start(key, serial);
        return;
    }
    /* Released the key that was repeating: stop. Releasing some OTHER key leaves the repeat alone.
       K-Term tracks a single key, so releasing the newest key also ends the repeat of an older key
       that is still held — a deliberate simplification (see the krfd block at the top of the file). */
    if (state_wl == WL_KEYBOARD_KEY_STATE_RELEASED && rep_key == (int)key)
        repeat_stop();
}

/* ---- mouse reporting helpers (DEC ?1000/?1002/?1006) ---- */

static int map_button(uint32_t b)
{
    switch (b) {
    case 0x110: return 0;   /* BTN_LEFT   -> SGR 0 */
    case 0x111: return 1;   /* BTN_MIDDLE -> SGR 1 */
    case 0x112: return 2;   /* BTN_RIGHT  -> SGR 2 */
    default:    return 3;   /* side/extra -> SGR 3 */
    }
}

static void mouse_cell(int *col, int *row)
{
    int cw = font.cell_w > 0 ? font.cell_w : 1;
    int ch = font.cell_h > 0 ? font.cell_h : 1;
    *col = iclamp_(W.ptr_x / cw, 0, term.g.cols - 1) + 1;   /* 1-based */
    *row = iclamp_(W.ptr_y / ch, 0, term.g.rows - 1) + 1;
}

/* 0-based viewport cell under the pointer.
   Selection is CELL granular: no sub-cell fraction is computed here any more, so an
   endpoint can never land in the middle of a glyph (see sel_set_endpoints).
   This mapping is exact only because render.c anchors the grid at the window's TOP-LEFT: viewport
   row y occupies window pixels [y*cell_h, (y+1)*cell_h) and column x occupies
   [x*cell_w, (x+1)*cell_w). Any leftover pixels (cur_h % cell_h, cur_w % cell_w) sit at the
   bottom/right of the window, so a pointer there simply clamps to the last row/column. */
static void sel_cell(int *col, int *row)
{
    int cw = font.cell_w > 0 ? font.cell_w : 1;
    int ch = font.cell_h > 0 ? font.cell_h : 1;
    *col = iclamp_(W.ptr_x / cw, 0, term.g.cols - 1);
    *row = iclamp_(W.ptr_y / ch, 0, term.g.rows - 1);
}

/* Set both selection endpoints: the anchor cell and the cell the pointer is on now.
   Whole glyphs only: an endpoint is always a cell EDGE, never a point inside a
   glyph, so a highlighted range always covers complete cells and half a letter cannot be highlighted.
   The endpoint that comes FIRST in reading order gets the cell's LEFT edge (frac 0), the later one its
   RIGHT edge (frac 1000); that is what keeps the cell under the pointer inside the selection whichever
   way the drag goes. Dragging left swaps the two endpoints, so their fracs have to swap with them
   (vt.c's term_selection_span only reorders the pair). */
static void sel_set_endpoints(int ax, int ay, int bx, int by)
{
    int forward = (by > ay) || (by == ay && bx >= ax);
    term.sel_ax = ax;
    term.sel_ay = ay;
    term.sel_bx = bx;
    term.sel_by = by;
    term.sel_af = forward ? 0 : 1000;
    term.sel_bf = forward ? 1000 : 0;
}

/* code: SGR button number (0..3), or +32 for a motion event. action: 'M' press/motion,
   'm' release. SGR (1006) sends \e[<code;col;rowM/m; legacy X10 sends \e[M with each
   component offset by 32 (so a motion code of btn+32 becomes 64+btn, as X10 expects). */
static void mouse_send(int code, int col, int row, char action)
{
    char seq[40];
    int n;
    if (term.mouse_mode & MOUSE_SGR)
        n = snprintf(seq, sizeof(seq), "\x1b[<%d;%d;%d%c", code, col, row, action);
    else
        n = snprintf(seq, sizeof(seq), "\x1b[M%c%c%c", 32 + code, 32 + col, 32 + row);
    if (n > 0)
        pty_write(&pty, seq, (size_t)n);
}

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)surf;
    /* wl_pointer.enter/motion surface_x/surface_y are wl_fixed_t (16.16/8.8 fixed point,
       NOT pixels). Convert to integer surface pixels before storing: sel_cell/mouse_cell map
       the pointer with plain `ptr / cell` division, so a raw fixed-point value (~256x too
       large) clamps every click to the last row/column -> selection snaps to the bottom-right
       and jumps in huge increments instead of tracking the pointer. */
    W.ptr_x = wl_fixed_to_int(x);
    W.ptr_y = wl_fixed_to_int(y);
    if (!W.cursor_theme && W.shm) {
        const char *name = getenv("XCURSOR_THEME");
        W.cursor_theme = wl_cursor_theme_load(name ? name : "left_ptr", 24, W.shm);
        if (W.cursor_theme) {
            struct wl_cursor *c = wl_cursor_theme_get_cursor(W.cursor_theme, "left_ptr");
            if (c)
                W.cursor_image = c->images[0];
        }
    }
    if (W.cursor_image && !W.cursor_surface) {
        W.cursor_surface = wl_compositor_create_surface(W.compositor);
    }
    if (W.cursor_image && W.cursor_surface) {
        struct wl_buffer *buf = wl_cursor_image_get_buffer(W.cursor_image);
        if (buf) {
            wl_surface_attach(W.cursor_surface, buf, 0, 0);
            wl_surface_damage(W.cursor_surface, 0, 0,
                              W.cursor_image->width, W.cursor_image->height);
            wl_surface_commit(W.cursor_surface);
            wl_pointer_set_cursor(p, serial, W.cursor_surface,
                                  W.cursor_image->hotspot_x, W.cursor_image->hotspot_y);
        }
    } else {
        wl_pointer_set_cursor(p, serial, NULL, 0, 0);
    }
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf)
{
    (void)d; (void)serial; (void)surf;
    wl_pointer_set_cursor(p, 0, NULL, 0, 0);
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)p; (void)time;
    W.ptr_x = wl_fixed_to_int(x);   /* fixed-point -> integer surface px (see ptr_enter) */
    W.ptr_y = wl_fixed_to_int(y);
    /* text selection drag: extend the selection to the pointer cell */
    if (W.selecting) {
        int col, row;
        sel_cell(&col, &row);
        int before = (row < term.sel_ay) || (row == term.sel_ay && col < term.sel_ax);
        int af = before ? 1000 : 0, bf = before ? 0 : 1000;
        if (col != term.sel_bx || row != term.sel_by || af != term.sel_af || bf != term.sel_bf) {
            sel_set_endpoints(term.sel_ax, term.sel_ay, col, row);
            W.dirty = 1;
            klog(L_DEBUG, "sel drag cell=(%d,%d) frac=%d ptr=(%d,%d) cw=%d ch=%d rows=%d cols=%d",
                 col, row, term.sel_bf, W.ptr_x, W.ptr_y, font.cell_w, font.cell_h,
                 term.g.rows, term.g.cols);
        }
        return;
    }
    /* wl_pointer.motion is opcode 2 and libwayland aborts if a bound interface's listener
       leaves an event slot NULL. When ?1002 drag tracking is on and a button is held, emit
       a motion report at the pointer's cell. */
    if ((term.mouse_mode & MOUSE_DRAG) && W.ptr_btn >= 0) {
        int col, row;
        mouse_cell(&col, &row);
        mouse_send(W.ptr_btn + 32, col, row, 'M');
    }
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t time,
                       uint32_t button, uint32_t state)
{
    (void)d; (void)p; (void)time;
    int btn = map_button(button);
    int pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;

    if (term.mouse_mode & (MOUSE_PRESS | MOUSE_DRAG)) {
        /* an application grabbed the mouse: report the button to the PTY */
        W.ptr_btn = pressed ? btn : -1;
        int col, row;
        mouse_cell(&col, &row);
        mouse_send(btn, col, row, pressed ? 'M' : 'm');
        return;
    }

    /* No application grabbed the mouse: the left button drives text selection. */
    if (btn == 0) {   /* BTN_LEFT */
        int col, row;
        sel_cell(&col, &row);
        if (pressed) {
            term.sel_active = 1;
            sel_set_endpoints(col, row, col, row);   /* one whole cell under the pointer */
            W.selecting = 1;
            klog(L_DEBUG, "sel press cell=(%d,%d) frac=%d ptr=(%d,%d) cw=%d ch=%d rows=%d cols=%d",
                 col, row, term.sel_bf, W.ptr_x, W.ptr_y, font.cell_w, font.cell_h,
                 term.g.rows, term.g.cols);
        } else {
            W.selecting = 0;
            /* Releasing the drag used to ALWAYS offer the selection to the
               clipboard, which meant any stray click-drag overwrote what the user had copied.
               `copy_on_release = 1` restores that; by default the selection just stays visible and
               Ctrl+Shift+C is what copies it. */
            if (conf.copy_on_release) {
                selection_copy(serial);   /* finalize: offer the selection to the clipboard */
            } else if (term.sel_active) {
                klog(L_DEBUG, "selection finished: %d,%d -> %d,%d (copy_on_release off — "
                              "Ctrl+Shift+C to copy)",
                     term.sel_ax, term.sel_ay, term.sel_bx, term.sel_by);
            }
        }
        W.dirty = 1;
    }
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis,
                     wl_fixed_t value)
{
    (void)d; (void)p; (void)time;
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return;
    double v = wl_fixed_to_double(value);
    if (term.mouse_mode & (MOUSE_PRESS | MOUSE_DRAG)) {
        /* an application grabbed the mouse: report the wheel as SGR button 4 (up) / 5 (down) */
        int col, row;
        mouse_cell(&col, &row);
        mouse_send(v > 0 ? 5 : 4, col, row, 'M');
        return;
    }
    /* No application grabbed the mouse: accumulate the continuous (pixel) delta for this frame.
       The frame handler turns it into whole-line scrollback motion; discrete wheel steps
       (ptr_axis_discrete) take precedence when the compositor sends them. */
    W.frame_axis_val += v;
    W.frame_has_axis = 1;
}

static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t source)
{
    (void)d; (void)p; (void)source;   /* must be handled at pointer v5; discrete/value drive scrolling */
}

static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis)
{
    (void)d; (void)p; (void)time; (void)axis;   /* momentum end: nothing extra to apply */
}

static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t axis, int32_t discrete)
{
    (void)d; (void)p;
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return;
    if (term.mouse_mode & (MOUSE_PRESS | MOUSE_DRAG))
        return;   /* app-grabbed wheel is already reported via ptr_axis */
    W.frame_discrete += discrete;
    W.frame_discrete_seen = 1;
}

/* wl_pointer.frame groups one scroll gesture's events. Convert the frame's accumulated scroll
   into whole-line scrollback motion (discrete wheel steps preferred, continuous pixel delta as
   fallback), then reset the per-frame accumulators. */
static void ptr_scroll_done(void *d, struct wl_pointer *p)
{
    (void)d; (void)p;
    if (!(term.mouse_mode & (MOUSE_PRESS | MOUSE_DRAG))) {
        double delta_lines = 0.0;   /* + = scroll up (into history) */
        if (W.frame_discrete_seen)
            delta_lines = -(double)W.frame_discrete * SCROLL_LINES_PER_DETENT;
        else if (W.frame_has_axis) {
            int ch = font.cell_h > 0 ? font.cell_h : 1;
            delta_lines = -W.frame_axis_val / (double)ch;
        }
        if (delta_lines != 0.0)
            scroll_apply(delta_lines);
    }
    W.frame_axis_val = 0;
    W.frame_discrete = 0;
    W.frame_has_axis = 0;
    W.frame_discrete_seen = 0;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = ptr_enter,
    .leave = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis = ptr_axis,
    .frame = ptr_scroll_done,
    .axis_source = ptr_axis_source,
    .axis_stop = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
};

/* ---------------- clipboard (Wayland data-device) ---------------- */

/* Our data source: when another client asks for the selection, the compositor calls send()
   with a pipe fd; we write the copied text to it. */
static void source_send(void *d, struct wl_data_source *src, const char *mime, int32_t fd)
{
    (void)d; (void)src; (void)mime;
    size_t off = 0;
    while (clip_text && off < clip_len) {
        ssize_t w = write(fd, clip_text + off, clip_len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += (size_t)w;
    }
    close(fd);
}
static void source_cancelled(void *d, struct wl_data_source *src)
{
    (void)d;
    wl_data_source_destroy(src);
    if (src == clip_source)
        clip_source = NULL;
}
static const struct wl_data_source_listener source_listener = {
    .send = source_send,
    .cancelled = source_cancelled,
};

/* A data offer (from another client's selection): record whether it offers text. */
static void offer_offer(void *d, struct wl_data_offer *o, const char *mime)
{
    (void)d; (void)o;
    if (mime && (!strncmp(mime, "text/", 5) || !strcmp(mime, "UTF8_STRING"))) {
        last_offer_text = 1;
        snprintf(last_offer_mime, sizeof(last_offer_mime), "%s", mime);
    }
}
static void offer_actions(void *d, struct wl_data_offer *o, uint32_t a)
{
    (void)d; (void)o; (void)a;
}
static const struct wl_data_offer_listener offer_listener = {
    .offer = offer_offer,
    .source_actions = offer_actions,
};

/* Begin receiving paste data from an offer into a pipe; the read end is added to epoll. */
static void start_paste(struct wl_data_offer *offer, const char *mime)
{
    if (paste_fd[0] >= 0)
        return;   /* already receiving */
    if (pipe(paste_fd) < 0) {
        paste_fd[0] = paste_fd[1] = -1;
        return;
    }
    fcntl(paste_fd[0], F_SETFL, O_NONBLOCK);
    wl_data_offer_receive(offer, mime, paste_fd[1]);
    close(paste_fd[1]);   /* our copy of the write end; the source keeps its own */
    paste_fd[1] = -1;
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = paste_fd[0] };
    if (g_ep >= 0)
        epoll_ctl(g_ep, EPOLL_CTL_ADD, paste_fd[0], &ev);
}

/* Paste idle watchdog. A paste is only flushed when the source closes the pipe (EOF), which is what
   every real client does. If a client leaves the write end open, the buffered block would sit there
   unflushed forever, so after a quiet period we deliver what we have. One-shot: it is re-armed by the
   next short read. */
static int pwfd = -1;

static void paste_watch_arm(void)
{
    if (pwfd < 0 || g_ep < 0)
        return;
    struct itimerspec its = { .it_value = { .tv_sec = 0, .tv_nsec = 400000000 } };
    timerfd_settime(pwfd, 0, &its, NULL);
}

static void paste_watch_disarm(void)
{
    if (pwfd < 0)
        return;
    struct itimerspec its = { 0 };
    timerfd_settime(pwfd, 0, &its, NULL);
    uint64_t exp;
    while (read(pwfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp))
        ;
}

static void dd_data_offer(void *d, struct wl_data_device *dd, struct wl_data_offer *offer)
{
    (void)d; (void)dd;
    last_offer_text = 0;
    last_offer_mime[0] = 0;
    wl_data_offer_add_listener(offer, &offer_listener, NULL);
}
static void dd_enter(void *d, struct wl_data_device *dd, uint32_t serial, struct wl_surface *surf,
                     wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer)
{
    (void)d; (void)dd; (void)serial; (void)surf; (void)x; (void)y; (void)offer;   /* DnD not supported */
}
static void dd_leave(void *d, struct wl_data_device *dd) { (void)d; (void)dd; }
static void dd_motion(void *d, struct wl_data_device *dd, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)dd; (void)time; (void)x; (void)y;
}
static void dd_drop(void *d, struct wl_data_device *dd)
{
    (void)d; (void)dd;   /* DnD not supported */
}
static void dd_selection(void *d, struct wl_data_device *dd, struct wl_data_offer *offer)
{
    (void)d; (void)dd;
    /* The compositor pushes the current clipboard selection here (there is no request_selection
       for wl_data_device). Destroy the previous selection offer, then adopt the new one. */
    if (sel_offer && sel_offer != offer)
        wl_data_offer_destroy(sel_offer);
    sel_offer = offer;                 /* may be NULL (selection cleared) */
    sel_offer_text = offer ? last_offer_text : 0;
    if (offer)
        snprintf(sel_offer_mime, sizeof(sel_offer_mime), "%s", last_offer_mime);
    else
        sel_offer_mime[0] = 0;
}
static const struct wl_data_device_listener data_device_listener = {
    .data_offer = dd_data_offer,
    .enter = dd_enter,
    .leave = dd_leave,
    .motion = dd_motion,
    .drop = dd_drop,
    .selection = dd_selection,
};

static void maybe_create_data_device(void)
{
    if (W.data_dev || !W.data_mgr || !W.seat)
        return;
    W.data_dev = wl_data_device_manager_get_data_device(W.data_mgr, W.seat);
    if (W.data_dev) {
        wl_data_device_add_listener(W.data_dev, &data_device_listener, NULL);
        klog(L_DEBUG, "clipboard: data device ready");
    }
}

/* Copy the current selection to the clipboard as a text/plain data source. */
static void selection_copy(uint32_t serial)
{
    if (!W.data_dev || !term.sel_active)
        return;
    char buf[16400];
    int n = term_selection_text(&term, buf, sizeof(buf));
    if (n <= 0)
        return;
    free(clip_text);
    clip_text = malloc((size_t)n + 1);
    if (!clip_text) { clip_len = 0; return; }
    memcpy(clip_text, buf, (size_t)n);
    clip_text[n] = 0;
    clip_len = (size_t)n;
    if (clip_source) { wl_data_source_destroy(clip_source); clip_source = NULL; }
    clip_source = wl_data_device_manager_create_data_source(W.data_mgr);
    if (!clip_source)
        return;
    wl_data_source_offer(clip_source, "text/plain");
    wl_data_source_add_listener(clip_source, &source_listener, NULL);
    wl_data_device_set_selection(W.data_dev, clip_source, serial);
    wl_display_flush(W.display);
    klog(L_DEBUG, "selection copied: %d bytes", n);
}

/* Paste the current clipboard selection (received via the selection event) into the PTY. */
static void clipboard_paste(void)
{
    if (!W.data_dev || paste_fd[0] >= 0)
        return;
    if (sel_offer && sel_offer_text)
        start_paste(sel_offer, sel_offer_mime[0] ? sel_offer_mime : "text/plain");
}

/* Send the buffered clipboard text to the PTY, wrapped in the bracketed-paste delimiters when the
   application asked for mode 2004 (bash/zsh/vim then insert the block literally instead of running
   every line as it arrives). Each write stays under the PTY's input buffer so a big paste is never
   cut in half by the tty layer — a split block would lose the delimiters' pairing. */
static void paste_flush(void)
{
    if (!paste_len)
        return;
    int bracketed = term.bracketed_paste && !paste_overflow;
    static const char start[] = PASTE_START, end[] = PASTE_END;
    if (bracketed)
        pty_write(&pty, start, sizeof(start) - 1);
    size_t off = 0;
    while (off < paste_len) {
        size_t chunk = imin_(paste_len - off, (size_t)4096);
        pty_write(&pty, paste_buf + off, chunk);
        off += chunk;
    }
    if (bracketed)
        pty_write(&pty, end, sizeof(end) - 1);
    klog(L_DEBUG, "paste: %zu bytes, bracketed %d%s", paste_len, bracketed,
         paste_overflow ? " (overflow: sent raw)" : "");
    paste_len = 0;
    paste_overflow = 0;
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s,
                     struct wl_array *keys)
{
    (void)d; (void)k; (void)serial; (void)s; (void)keys;
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{
    (void)d; (void)k; (void)serial; (void)s;
    /* Focus left the surface. The release of a held key may never be delivered, which would leave a
       repeat running forever against a keyboard the user is no longer using. */
    repeat_stop();
}

static void kb_mod(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t mods_depressed,
                   uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    (void)d; (void)k; (void)serial;
    if (W.xkb_state)
        xkb_state_update_mask(W.xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void kb_repeat(void *d, struct wl_keyboard *k, int rate, int delay)
{
    (void)d; (void)k;
    /* Informational only. K-Term drives its own repeat from repeat_delay/repeat_rate in the config
       file, so the feel does not depend on what the compositor happens to be configured with; the
       values are logged so the user can match their system settings. */
    klog(L_DEBUG, "compositor repeat_info: rate %d/s delay %d ms (k-term uses repeat_delay=%d ms, "
         "repeat_rate=%d/s from the config)", rate, delay, conf.repeat_delay, conf.repeat_rate);
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = kb_keymap,
    .enter = kb_enter,
    .leave = kb_leave,
    .key = kb_key,
    .modifiers = kb_mod,
    .repeat_info = kb_repeat,
};

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t format, int fd, uint32_t size)
{
    (void)d; (void)k; (void)format;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }
    struct xkb_keymap *km = xkb_keymap_new_from_string(W.xkb_ctx, map,
                                                       XKB_KEYMAP_FORMAT_TEXT_V1,
                                                       XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);
    if (!km) {
        klog(L_ERROR, "xkb keymap compile failed");
        return;
    }
    xkb_keymap_unref(W.keymap);
    xkb_state_unref(W.xkb_state);
    W.keymap = km;
    W.xkb_state = xkb_state_new(km);
    /* The state machine was replaced under a possibly-held key: stop the repeat rather than re-send
       an old keycode through a keymap that no longer exists. */
    repeat_stop();
}

static void seat_cap(void *d, struct wl_seat *s, uint32_t cap)
{
    (void)d;
    if ((cap & WL_SEAT_CAPABILITY_POINTER) && !W.pointer) {
        W.pointer = wl_seat_get_pointer(s);
        wl_pointer_add_listener(W.pointer, &pointer_listener, NULL);
        klog(L_DEBUG, "wl_pointer interface version %d (>=5 enables axis_discrete/frame scrolling)",
            wl_proxy_get_version((struct wl_proxy *)W.pointer));
    }
    if ((cap & WL_SEAT_CAPABILITY_KEYBOARD) && !W.keyboard) {
        W.keyboard = wl_seat_get_keyboard(s);
        wl_keyboard_add_listener(W.keyboard, &keyboard_listener, NULL);
    }
    maybe_create_data_device();
}
static void seat_name(void *d, struct wl_seat *s, const char *name) { (void)d; (void)s; (void)name; }
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_cap,
    .name = seat_name,
};

static void registry_global(void *d, struct wl_registry *reg, uint32_t id,
                            const char *iface, uint32_t version)
{
    (void)d;
    if (!strcmp(iface, wl_compositor_interface.name)) {
        W.comp_ver = imin_(version, 4);
        W.compositor = wl_registry_bind(reg, id, &wl_compositor_interface, W.comp_ver);
    } else if (!strcmp(iface, wl_shm_interface.name)) {
        W.shm = wl_registry_bind(reg, id, &wl_shm_interface, 1);
    } else if (!strcmp(iface, wl_seat_interface.name)) {
        /* Bind at v5 so the derived wl_pointer carries axis_discrete/axis_source/axis_stop/frame
           (all SINCE_VERSION 5), needed for smooth wheel + touchpad scrolling. The compositor's
           advertised version caps this (niri advertises 9). wl_keyboard has no events above v4 in
           this protocol, so a v5 seat adds no unhandled keyboard events. */
        W.seat = wl_registry_bind(reg, id, &wl_seat_interface, imin_(version, 5));
        wl_seat_add_listener(W.seat, &seat_listener, NULL);
    } else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        W.wm_base = wl_registry_bind(reg, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(W.wm_base, &wm_base_listener, NULL);
    } else if (!strcmp(iface, wl_data_device_manager_interface.name)) {
        W.data_mgr = wl_registry_bind(reg, id, &wl_data_device_manager_interface, imin_(version, 3));
        maybe_create_data_device();
    }
}
static void registry_global_remove(void *d, struct wl_registry *reg, uint32_t id) { (void)d; (void)reg; (void)id; }
static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* ---------------- setup ---------------- */

static int wayland_setup(int init_w, int init_h)
{
    W.display = wl_display_connect(NULL);
    if (!W.display) {
        klog(L_ERROR, "wl_display_connect failed (WAYLAND_DISPLAY set?)");
        return -1;
    }
    W.registry = wl_display_get_registry(W.display);
    wl_registry_add_listener(W.registry, &registry_listener, NULL);
    wl_display_roundtrip(W.display);

    if (!W.compositor || !W.wm_base) {
        klog(L_ERROR, "compositor lacks wl_compositor or xdg_wm_base");
        return -1;
    }

    W.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!W.xkb_ctx) {
        klog(L_ERROR, "xkb_context_new failed");
        return -1;
    }

    W.surface = wl_compositor_create_surface(W.compositor);
    W.xdg_surface = xdg_wm_base_get_xdg_surface(W.wm_base, W.surface);
    xdg_surface_add_listener(W.xdg_surface, &xdg_surface_listener, NULL);
    W.toplevel = xdg_surface_get_toplevel(W.xdg_surface);
    xdg_toplevel_add_listener(W.toplevel, &toplevel_listener, NULL);
    /* app_id is what the compositor matches windows on (niri/Sway rules, taskbar grouping, file
       managers' "open terminal here"). Configurable through the config file; config_default() puts
       DEF_APP_ID ("K-Term") there and an empty value is refilled there too, so the
       fallback here only covers a Config that never went through config_default. */
    xdg_toplevel_set_app_id(W.toplevel, conf.app_id[0] ? conf.app_id : DEF_APP_ID);
    klog(L_INFO, "app_id: %s", conf.app_id[0] ? conf.app_id : DEF_APP_ID);
    xdg_toplevel_set_title(W.toplevel, "K-Term");

    if (W.comp_ver >= 3)
        wl_surface_set_buffer_scale(W.surface, 1);

    W.egl_window = wl_egl_window_create(W.surface, init_w, init_h);
    if (!W.egl_window) {
        klog(L_ERROR, "wl_egl_window_create failed");
        return -1;
    }
    if (render_init(&rend, W.display, W.egl_window, init_w, init_h, conf.opacity) < 0) {
        klog(L_ERROR, "render_init failed");
        return -1;
    }

    W.cur_w = W.pw = init_w;
    W.cur_h = W.ph = init_h;

    xdg_toplevel_set_min_size(W.toplevel, 200, 100);
    wl_surface_commit(W.surface);
    wl_display_roundtrip(W.display);
    return 0;
}

static void wayland_destroy(void)
{
    if (paste_fd[0] >= 0) { close(paste_fd[0]); paste_fd[0] = -1; }
    if (paste_buf) { free(paste_buf); paste_buf = NULL; paste_cap = 0; paste_len = 0; }
    if (paste_fd[1] >= 0) { close(paste_fd[1]); paste_fd[1] = -1; }
    if (sel_offer) { wl_data_offer_destroy(sel_offer); sel_offer = NULL; }
    if (clip_source) { wl_data_source_destroy(clip_source); clip_source = NULL; }
    free(clip_text); clip_text = NULL;
    if (W.data_dev) { wl_data_device_destroy(W.data_dev); W.data_dev = NULL; }
    if (W.data_mgr) { wl_data_device_manager_destroy(W.data_mgr); W.data_mgr = NULL; }
    if (W.cursor_theme) wl_cursor_theme_destroy(W.cursor_theme);
    if (W.cursor_surface) wl_surface_destroy(W.cursor_surface);
    if (W.keyboard) wl_keyboard_destroy(W.keyboard);
    if (W.pointer) wl_pointer_destroy(W.pointer);
    if (W.seat) wl_seat_destroy(W.seat);
    if (W.shm) wl_shm_destroy(W.shm);
    xkb_state_unref(W.xkb_state);
    xkb_keymap_unref(W.keymap);
    xkb_context_unref(W.xkb_ctx);
    if (W.toplevel) xdg_toplevel_destroy(W.toplevel);
    if (W.xdg_surface) xdg_surface_destroy(W.xdg_surface);
    if (W.wm_base) xdg_wm_base_destroy(W.wm_base);
    if (W.egl_window) wl_egl_window_destroy(W.egl_window);
    if (W.surface) wl_surface_destroy(W.surface);
    if (W.compositor) wl_compositor_destroy(W.compositor);
    if (W.registry) wl_registry_destroy(W.registry);
    if (W.display) wl_display_disconnect(W.display);
}

/* ---------------- main ---------------- */

/* The launch surface is deliberately small. Everything that describes how the
   terminal LOOKS (font, size, grid, colours, opacity) is a persistent preference and lives in the
   config file; the flags that stay are the per-launch ones (what to run, where to start, which config
   to read, debug). Flags that were dropped in the past are NOT special-cased: they are simply unknown
   options, and an unknown option produces one generic message plus the option list. */
static void usage(const char *argv0, FILE *out)
{
    fprintf(out,
            "usage: %s [OPTION]...\n\n"
            "  -e, --exec COMMAND      run COMMAND instead of the default shell\n"
            "  -w, --workingdir DIR    start the shell in DIR (default: $HOME)\n"
            "  -c, --config FILE       read FILE instead of ~/.config/k-term/k-term.conf\n"
            "  -d, --debug             enable debug logging\n"
            "  -v, --version           print the version and exit\n"
            "  -h, --help              show this help and exit\n\n"
            "Long options also accept the --name=VALUE form.\n"
            "Font, font size, columns, rows, scrollback, colours and opacity are set in the\n"
            "configuration file only; command-line options override everything else.\n",
            argv0);
}

/* Matches "--name" or "--name=value"; *valp gets the value of the '=' form, NULL otherwise. */
static int longopt(const char *arg, const char *name, const char **valp)
{
    size_t n = strlen(name);
    if (strncmp(arg, name, n) != 0)
        return 0;
    if (valp)
        *valp = (arg[n] == '=') ? arg + n + 1 : NULL;
    return arg[n] == '\0' || arg[n] == '=';
}

/* Value of an option that takes one: --name=VALUE, else the next argv element. */
static const char *opt_value(const char *inline_val, char **argv, int *i, int argc,
                             const char *flag)
{
    if (inline_val)
        return inline_val;
    if (*i + 1 >= argc) {
        fprintf(stderr, "k-term: %s needs an argument\n", flag);
        return NULL;
    }
    return argv[++*i];
}

int main(int argc, char **argv)
{
    config_default(&conf);
    const char *opt_config = NULL;
    const char *opt_wdir = NULL;
    int opt_debug = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *val = NULL;
        if (!strcmp(a, "-h") || longopt(a, "--help", &val)) {
            usage(argv[0], stdout);
            return 0;
        } else if (!strcmp(a, "-v") || longopt(a, "--version", &val)) {
            printf("k-term 0.1.0 (prototype)\n");
            return 0;
        } else if (!strcmp(a, "-d") || longopt(a, "--debug", &val)) {
            opt_debug = 1;      /* applied AFTER config_load: the CLI must beat `log =` in the config */
        } else if (!strcmp(a, "-e") || longopt(a, "--exec", &val)) {
            const char *v = opt_value(val, argv, &i, argc, a);
            if (!v)
                return 1;
            opt_cmd = v;
        } else if (!strcmp(a, "-w") || longopt(a, "--workingdir", &val)) {
            const char *v = opt_value(val, argv, &i, argc, a);
            if (!v)
                return 1;
            opt_wdir = v;   /* one argv element: spaces/special chars need no extra work */
        } else if (!strcmp(a, "-c") || longopt(a, "--config", &val)) {
            const char *v = opt_value(val, argv, &i, argc, a);
            if (!v)
                return 1;
            opt_config = v;
        } else {
            /* One generic path for every option K-Term does not take, including the flags that were
               dropped in the past: no per-flag explanation, just the message and the option list. */
            fprintf(stderr, "k-term: unknown option '%s'\n", a);
            usage(argv[0], stderr);
            return 1;
        }
    }

    config_load(&conf, opt_config);
    if (opt_debug)
        conf.loglevel = L_DEBUG;
    k_term_loglevel = conf.loglevel;
    base_font_size = conf.font_size;
    term_set_clear_scrollback(conf.clear_scrollback);

    /* Where the shell starts: -w DIR beats config working_dir, which beats $HOME. The path is
       resolved and validated in the PARENT so a bad path produces one clean warning instead of a
       chdir failure inside the child; a file manager can pass the directory it is showing, and the
       option never changes what later launches do (the config file stays the persistent setting). */
    char wdir[4096];
    expand_tilde(opt_wdir && opt_wdir[0] ? opt_wdir : conf.working_dir, wdir, sizeof(wdir));
    if (!wdir[0]) {
        const char *home = getenv("HOME");
        snprintf(wdir, sizeof(wdir), "%s", home && home[0] ? home : ".");
    }
    struct stat wst;
    if (stat(wdir, &wst) != 0 || !S_ISDIR(wst.st_mode)) {
        klog(L_WARN, "working directory '%s' is not a directory, using $HOME", wdir);
        const char *home = getenv("HOME");
        snprintf(wdir, sizeof(wdir), "%s", home && home[0] ? home : ".");
    }
    klog(L_INFO, "shell starts in %s", wdir);

    if (font_init(&font, conf.font, conf.font_size) < 0)
        return 1;

    term_init(&term, conf.rows, conf.cols, conf.scrollback);
    term.pen.bg = conf.bg;
    term.pen.fg = conf.fg;

    int init_w = conf.cols * font.cell_w;
    int init_h = conf.rows * font.cell_h;

    if (wayland_setup(init_w, init_h) < 0)
        return 1;
    term_size_changed();   /* grid now matches window */

    /* apply configured default colors to the whole (resized) grid */
    for (int y = 0; y < term.g.rows; y++) {
        Cell *r = grid_vprow(&term.g, y);
        for (int x = 0; x < term.g.cols; x++) {
            r[x].bg = conf.bg;
            r[x].fg = conf.fg;
        }
    }

    if (pty_spawn(&pty, opt_cmd && opt_cmd[0] ? opt_cmd : (conf.shell[0] ? conf.shell : NULL),
                  term.g.rows, term.g.cols, wdir) < 0)
        return 1;

    /* epoll loop: wayland fd, pty master, timerfd, signalfd(SIGCHLD) */
    int ep = epoll_create1(0);
    g_ep = ep;
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    struct itimerspec its = {
        .it_interval = { .tv_nsec = FRAME_INTERVAL_NS },
        .it_value = { .tv_nsec = FRAME_INTERVAL_NS },
    };
    timerfd_settime(tfd, 0, &its, NULL);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_NONBLOCK);

    int wfd = wl_display_get_fd(W.display);
    struct epoll_event ev;

    /* one-shot idle timer for a buffered clipboard paste (see paste_watch_arm) */
    pwfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (pwfd >= 0) {
        ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = pwfd };
        epoll_ctl(ep, EPOLL_CTL_ADD, pwfd, &ev);
    }

    /* key repeat: idle here, armed per key press by repeat_start() and re-armed at
       the configured rate by repeat_fire() */
    krfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (krfd >= 0) {
        ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = krfd };
        epoll_ctl(ep, EPOLL_CTL_ADD, krfd, &ev);
    } else {
        klog(L_WARN, "key repeat disabled: timerfd_create: %s", strerror(errno));
    }

    ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = wfd };
    epoll_ctl(ep, EPOLL_CTL_ADD, wfd, &ev);
    ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = pty.mfd };
    epoll_ctl(ep, EPOLL_CTL_ADD, pty.mfd, &ev);
    ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = tfd };
    epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &ev);
    ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = sfd };
    epoll_ctl(ep, EPOLL_CTL_ADD, sfd, &ev);

    wl_flush();
    W.running = 1;

    static uint8_t rbuf[65536];
    struct epoll_event events[8];

    while (W.running) {
        /* Canonical wayland+epoll pattern: dispatch anything already queued in
           libwayland's buffer, then prepare a read so the later read_events() will
           not block. The previous code called read_events() inside a drain loop AFTER
           dispatch, which blocked on the display fd and starved the timerfd and
           signalfd (no timer redraw, child exit never detected). */
        while (wl_display_prepare_read(W.display) != 0) {
            if (wl_display_dispatch_pending(W.display) < 0) {
                W.running = 0;
                break;
            }
        }
        if (!W.running)
            break;
        wl_flush();

        int n = epoll_wait(ep, events, 8, -1);
        if (n < 0) {
            wl_display_cancel_read(W.display);
            if (errno == EINTR)
                continue;
            break;
        }

        int wl_ready = 0;
        for (int i = 0; i < n; i++)
            if (events[i].data.fd == wfd)
                wl_ready = 1;

        /* Finish the wayland read BEFORE any descriptor is handled. This is not cosmetic:
           eglSwapBuffers waits until the compositor gives a buffer back, and that release can only be
           read from this thread. While a wl_display_prepare_read() is outstanding the release is never
           read (the thread would be inside the driver), so the swap blocks forever and the terminal
           freezes: no picture, no keyboard, no PTY reads (measured: the second swap from the timerfd
           branch never returned, /proc/<pid>/wchan = futex_do_wait). Completing the read here also means
           a listener (frame_done / configure) may draw, which is the preferred swap site. */
        if (wl_ready) {
            if (wl_display_read_events(W.display) < 0) {
                W.running = 0;
                break;
            }
        } else {
            /* we prepared a read but epoll woke for a non-wayland fd: release it */
            wl_display_cancel_read(W.display);
        }
        if (wl_display_dispatch_pending(W.display) < 0) {
            W.running = 0;
            break;
        }

        for (int i = 0; i < n && W.running; i++) {
            int fd = events[i].data.fd;
            if (fd == wfd) {
                /* already read and dispatched above */
            } else if (fd == pty.mfd) {
                for (;;) {
                    ssize_t r = read(pty.mfd, rbuf, sizeof(rbuf));
                    if (r > 0) {
                        /* New output is written relative to the LIVE view (the parser's cursor
                           addresses row[live_off + cy]). If the user is scrolled back, those rows
                           are currently on screen, so return the view to the live anchor first;
                           otherwise the output would overwrite the lines being read. */
                        if (term.g.scroll_off != term.g.live_off)
                            snap_to_live();
                        term_parse(&term, rbuf, (size_t)r);
                        /* Drain any terminal query responses (CPR, DA) queued by the parser */
                        if (term.resp_len > 0) {
                            pty_write(&pty, term.resp, (size_t)term.resp_len);
                            term.resp_len = 0;
                        }
                        W.dirty = 1;
                        if (r < (ssize_t)sizeof(rbuf))
                            break;
                    } else if (r == 0 || (r < 0 && errno != EINTR && errno != EAGAIN)) {
                        klog(L_INFO, "pty closed");
                        W.running = 0;
                        break;
                    } else {
                        break;
                    }
                }
            } else if (fd == tfd) {
                uint64_t ticks;
                (void)!read(tfd, &ticks, sizeof(ticks));
                /* Draw when something changed AND no frame callback is in flight (frame_cb == NULL means
                   the compositor already showed the buffer we committed, so the swap has the best chance
                   of not waiting for a buffer release). `resized` is in the condition because a size
                   change is frequently the ONLY change — and the compositor will not settle until a
                   buffer of the new size arrives, so waiting for `dirty` alone would freeze the display
                   (that was the fullscreen bug). */
                if ((W.dirty || W.resized) && !W.frame_cb)
                    redraw();
            } else if (fd == sfd) {
                struct signalfd_siginfo si;
                while (read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
                    if (si.ssi_signo == SIGCHLD) {
                        int st;
                        pid_t done = waitpid(pty.child, &st, WNOHANG);
                        if (done == pty.child) {
                            klog(L_INFO, "child exited");
                            W.running = 0;
                            break;
                        }
                    }
                }
            } else if (paste_fd[0] >= 0 && fd == paste_fd[0]) {
                /* Accumulate the clipboard text; it is written to the PTY as ONE bracketed block
                   once the source closes the pipe (see paste_flush). */
                char pbuf[4096];
                int done = 0;
                while (!done) {
                    ssize_t r = read(paste_fd[0], pbuf, sizeof(pbuf));
                    if (r > 0) {
                        if (paste_len + (size_t)r > (size_t)PASTE_BUF_MAX) {
                            paste_overflow = 1;
                            klog(L_WARN, "paste too large (> %zu bytes): sending it unwrapped",
                                 (size_t)PASTE_BUF_MAX);
                        } else if (paste_len + (size_t)r > paste_cap) {
                            size_t want = paste_cap ? paste_cap : 8192;
                            while (want < paste_len + (size_t)r)
                                want *= 2;
                            char *nb = realloc(paste_buf, want);
                            if (!nb) {
                                klog(L_WARN, "paste: out of memory, paste dropped");
                                paste_len = 0;
                                done = 1;
                                break;
                            }
                            paste_buf = nb;
                            paste_cap = want;
                        }
                        if (!paste_overflow) {
                            memcpy(paste_buf + paste_len, pbuf, (size_t)r);
                            paste_len += (size_t)r;
                        }
                        if (r < (ssize_t)sizeof(pbuf))
                            paste_watch_arm();   /* drained for now; the source may still write */
                    } else if (r == 0) {
                        done = 1;          /* EOF: the whole selection has arrived */
                    } else {
                        if (errno == EINTR)
                            continue;
                        if (errno == EAGAIN)
                            break;         /* wait for more via epoll */
                        klog(L_WARN, "paste: read: %s", strerror(errno));
                        done = 1;
                    }
                }
                if (done) {
                    epoll_ctl(ep, EPOLL_CTL_DEL, paste_fd[0], NULL);
                    close(paste_fd[0]); paste_fd[0] = -1;
                    paste_watch_disarm();
                    paste_flush();
                }
            } else if (pwfd >= 0 && fd == pwfd) {
                /* the source went quiet without closing the pipe: deliver what we have */
                uint64_t exp;
                while (read(pwfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp))
                    ;
                if (paste_fd[0] >= 0) {
                    epoll_ctl(ep, EPOLL_CTL_DEL, paste_fd[0], NULL);
                    close(paste_fd[0]); paste_fd[0] = -1;
                }
                paste_flush();
            } else if (krfd >= 0 && fd == krfd) {
                /* the hold outlasted repeat_delay: re-send the held key (see repeat_start) */
                repeat_fire();
            }
        }
    }

    render_destroy(&rend);
    wayland_destroy();
    term_destroy(&term);
    font_destroy(&font);
    if (pty.mfd >= 0)
        close(pty.mfd);
    close(tfd);
    close(sfd);
    if (pwfd >= 0)
        close(pwfd);
    if (krfd >= 0)
        close(krfd);
    close(ep);
    klog(L_INFO, "k-term exiting");
    return 0;
}
