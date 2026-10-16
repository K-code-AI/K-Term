/* Kai-Term — lightweight Wayland terminal emulator. Shared header. */
#ifndef KAI_TERM_H
#define KAI_TERM_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- logging ---------- */
enum { L_ERROR = 0, L_WARN, L_INFO, L_DEBUG };
extern int kai_term_loglevel;
void klog(int level, const char *fmt, ...);

/* ---------- colors & cell attributes ---------- */
#define DEF_FG      0xffe5e5e5u   /* ARGB */
#define DEF_BG      0xff101014u
#define CURSOR_COL  0xffd8d8d8u
#define SEL_BG      0xff3a4d66u   /* selection highlight background */

/* Default xdg-shell app_id. Single-sourced here because config_default(), the
   `app_id =` empty-value fallback in config.c and the last-resort fallback in main.c must all agree;
   three string literals that can drift apart is how a default silently changes. */
#define DEF_APP_ID  "Kai-Term"

/* Default window title. mux_title() builds the toplevel title from it; the tab bar shows the same state
   INSIDE the window, the title is what a compositor or a taskbar shows. Single-sourced for the same reason
   as DEF_APP_ID. */
#define DEF_TITLE   "Kai-Term"

/* Max continuation codepoints stored on one emoji anchor cell (base excluded). Covers the
   common ZWJ sequences: family 👨\u200d👩\u200d👧\u200d👦 = 6 continuation codepoints. */
#define CLUSTER_MAX 8

enum {
    ATTR_BOLD      = 1u << 0,
    ATTR_DIM       = 1u << 1,
    ATTR_ITALIC    = 1u << 2,
    ATTR_UNDERLINE = 1u << 3,
    ATTR_INVERSE   = 1u << 4,
    ATTR_HIDDEN    = 1u << 5,
    ATTR_STRIKE    = 1u << 6,
    ATTR_WIDE      = 1u << 7,   /* occupies 2 cells */
};

typedef struct {
    uint32_t fg, bg;
    uint32_t attr;
} Pen;

/* Term.mouse_mode bitmask (DEC private modes). */
enum {
    MOUSE_PRESS = 1 << 0,   /* ?1000: report button press/release */
    MOUSE_DRAG  = 1 << 1,   /* ?1002: also report motion while a button is held */
    MOUSE_SGR   = 1 << 2,   /* ?1006: use SGR (1006) encoding, else legacy X10 */
};

/* The button codes wl_pointer.button delivers are the Linux kernel's BTN_* values
   (linux/input-event-codes.h), and they are NOT in the order the terminal protocols number the buttons:
   BTN_LEFT 0x110, BTN_RIGHT 0x111, BTN_MIDDLE 0x112, while both the SGR (1006) and the legacy X10
   encodings want 0 = left, 1 = middle, 2 = right. mouse_sgr_button() (vt.c) is the one mapping; the
   constants live here so nothing re-derives them from the definition order. */
enum {
    EVDEV_BTN_LEFT   = 0x110,
    EVDEV_BTN_RIGHT  = 0x111,
    EVDEV_BTN_MIDDLE = 0x112,
};
int mouse_sgr_button(uint32_t evdev_code);   /* 0 left, 1 middle, 2 right, 3 any other button */

typedef struct {
    uint32_t ch;    /* codepoint; 0 = blank */
    uint32_t fg, bg;
    uint32_t attr;
    /* Grapheme-cluster continuation codepoints for an emoji anchor cell (ZWJ sequences,
       variation selectors, skin-tone modifiers). Inline (no heap) so Cell stays safely
       copyable by value across row realloc, DCH/ICH shifts and rotations. nextra==0 means a
       single-codepoint cell. The anchor's ch is the cluster's first codepoint. */
    uint32_t extra[CLUSTER_MAX];
    uint8_t  nextra;
} Cell;

/* ---------- VT parser ---------- */
struct Term;   /* forward */

enum PState {
    PS_GROUND, PS_ESC, PS_CSI, PS_OSC, PS_UTF8, PS_IGNORE
};

typedef struct Parser {
    struct Term *t;
    int state;
    int npar, par[32];
    int priv;              /* CSI ? */
    int bang;              /* CSI ! */
    int gt;                /* CSI > (DA2 prefix) */
    int utf8_need, utf8_cp;
    char osc[512];
    int osc_len;
} Parser;

/* ---------- grid & terminal ---------- */
typedef struct Grid {
    int rows, cols;        /* viewport size */
    int rcap;              /* allocated rows (rows + scrollback_max) */
    Cell **row;            /* row[0..scroll_off-1] scrollback,
                              row[scroll_off..scroll_off+rows-1] viewport,
                              row[scroll_off+rows..rcap-1] free (or holds the newer
                              lines while the user is scrolled back) */
    int scroll_off;        /* index of the viewport top; 0 = oldest retained line,
                              live_off = newest (live) view */
    int live_off;          /* scroll_off value that follows the newest output.
                              scroll_up/scroll_down advance it with scroll_off; user
                              navigation moves scroll_off below it. Always
                              0 <= live_off <= scrollback_max. */
    int scrollback_max;
    /* wrap[i] != 0 -> the line in row[i] was broken by the RIGHT MARGIN, not by the application, so
       it continues at the start of row[i+1]. Without this mark a wrapped line and a hard-broken line
       are indistinguishable and a width change can only truncate; with it the two physical rows can
       be joined back into the logical line the application wrote and re-broken at the new width
       (reflow, see grid_resize). It rotates together with row[] (rot_left/rot_right) and is cleared
       whenever a slot is blanked or its line is erased. */
    uint8_t *wrap;
    /* Set to GRID_VALID between grid_init and grid_destroy. term_init frees the grid it is rebuilding
       (the RIS leak fix), so it must be able to tell "there is an old grid here to free" from "this
       Term was never initialized". A plain 0/1 flag is NOT enough: on a stack Term the garbage in
       `valid` is routinely non-zero (measured: 0x7ffd-ish stack halves), which would still free the
       garbage `row` pointer. A distinctive sentinel makes accidental garbage essentially unable to
       look like an initialized grid. */
    int valid;
} Grid;

#define GRID_VALID 0x67726964   /* 'g','r','i','d' */

typedef struct Term {
    Grid g;
    int cx, cy;            /* cursor, viewport coords (0-based) */
    int save_cx, save_cy;
    int top, bot;          /* scroll region, inclusive, 0-based */
    Pen pen;
    uint32_t palette[256];
    int wrap_next;         /* deferred wrap pending at right margin */
    int show_cursor;
    int newline_mode;      /* LNM: LF also does CR */
    int mouse_mode;        /* bitmask: MOUSE_PRESS | MOUSE_DRAG | MOUSE_SGR */
    int alt_screen;        /* 1 when the alternate screen buffer is active */
    Grid alt;              /* saved MAIN grid while alt_screen is active */
    int alt_saved_cx, alt_saved_cy;  /* main-screen cursor saved on ?1049h */
    /* Bracketed paste mode (DEC private mode 2004). An application (bash, zsh, vim, ...) turns it
       on with ESC[?2004h to ask that pasted text be told apart from typed text: the terminal wraps
       the paste in ESC[200~ ... ESC[201~, so embedded newlines stay in the application's own
       insert buffer instead of reaching its command line as "Return" presses. 0 = send raw. */
    int bracketed_paste;
    /* Text selection (viewport coordinates, inclusive). The horizontal edges are
       sub-cell: sel_af/sel_bf are the position within the anchor/current cell in
       thousandths of a cell width (0..1000), so the highlight follows the pointer
       smoothly instead of snapping to whole cells. */
    int sel_active;
    int sel_ax, sel_ay;    /* anchor cell */
    int sel_bx, sel_by;    /* current cell (extends from the anchor) */
    int sel_af, sel_bf;    /* sub-cell horizontal offset 0..1000 within anchor/current */
    char title[256];
    Parser ps;             /* parser state lives with the terminal */
    /* Grapheme-cluster assembly state: the anchor cell of the emoji cluster currently being
       built, so a ZWJ / variation selector / skin-tone modifier that follows joins that cell
       instead of landing in a cell of its own. cl_cx/cl_cy are VIEWPORT coordinates, so the
       state is dropped whenever rows move (scroll/resize) or the cursor jumps elsewhere.
       cl_w is the number of cells the anchor consumed (1 or 2); cl_join is set after a ZWJ so
       the next emoji base codepoint also joins the same anchor. */
    int cl_valid, cl_cx, cl_cy, cl_w, cl_join;
    /* Terminal query responses (CPR, DA1, DA2) queued for the PTY. */
    char resp[256];
    int resp_len;
} Term;

/* grid */
void grid_init(Grid *g, int rows, int cols, int scrollback);
/* Resize the viewport. A column-count change REFLOWS: soft-wrapped rows are joined back into their
   logical line and re-broken at the new width, so nothing is lost (narrowing pushes lines into
   scrollback and evicts the oldest ones when it overflows, exactly like normal output).
   slot/col (either may be NULL) name a cell of the OLD grid — slot is an absolute index into row[],
   col a column — and come back as that same cell's position in the NEW grid. A line that was
   evicted puts the cursor on the newest surviving line. */
void grid_resize(Grid *g, int rows, int cols, int *slot, int *col);
/* Same, but WITHOUT reflow: rows are truncated/grown in place. Used for the alternate screen,
   where a full-screen application repaints itself after a SIGWINCH instead of expecting the
   terminal to re-wrap its output.
   cur_slot is the absolute row[] slot of the cell that must stay on screen (the cursor), or -1.
   On a height SHRINK the viewport gives up its BOTTOM rows while the cursor has rows below it, and
   only after that its TOP rows (into scrollback). Either way the tracked cell keeps its content
   line; if it instead got pushed out of the viewport the application would repaint at the wrong
   place and leave its old output behind (the zoom leftover-lines bug). */
void grid_resize_plain(Grid *g, int rows, int cols, int cur_slot);
void grid_destroy(Grid *g);
Cell *grid_vprow(Grid *g, int y);   /* viewport row y (0..rows-1) */

/* Window pixel size + cell size + a margin -> the grid size AND the pixel offset the grid starts at.
 * One function because THREE places must agree on this geometry: main.c's cols/rows for the grid and
 * the PTY, main.c's pointer -> cell mapping (selection, SGR mouse reporting), and render.c's quad
 * offsets. Any drift between them puts the highlight on the wrong glyph.
 * Rules: the margin is applied on all four sides (so no glyph sits under a rounded window corner);
 * the margin is clamped so at least one whole cell always fits; the leftover pixels that do not fill a
 * whole cell stay at the RIGHT and BOTTOM, which is what keeps the pointer mapping exact
 * (col = (ptr_x - xoff) / cell_w). pad = 0 is the old edge-to-edge layout (xoff = yoff = 0). */
void term_grid_geometry(int win_w, int win_h, int cell_w, int cell_h, int pad,
                        int *cols, int *rows, int *xoff, int *yoff);

/* terminal */
void term_init(Term *t, int rows, int cols, int scrollback);
void term_destroy(Term *t);
void term_resize(Term *t, int rows, int cols);
void term_parse(Term *t, const uint8_t *buf, size_t len);

/* selection */
typedef struct {
    int r0, c0, f0;   /* start: row, column, offset within that column (0..1000) */
    int r1, c1, f1;   /* end */
} SelSpan;

int  term_selected(Term *t, int col, int row);          /* is cell (col,row) selected? */
void term_selection_span(Term *t, SelSpan *s);          /* normalized reading-order span */
/* Covered sub-range of cell (col,row) in cell-fraction units, clamped to [col,col+1].
   Returns 1 when the cell is covered (then *b > *a), 0 otherwise. */
int  sel_span_cover(const SelSpan *s, int cols, int col, int row, double *a, double *b);
/* Band geometry in buffer pixels for cell (col,row): *x = left edge, *w = width (0 = none).
   Pure math (no GL) so the highlight position is unit-testable. */
void sel_band_pixels(const SelSpan *s, int cols, int col, int row, float cw,
                     float *x, float *w);
int  term_selection_cover(Term *t, int col, int row, double *a, double *b);
/* Should the selection band be PAINTED over cell (col,row)? The cell must already be covered by the
   selection; this only decides whether it holds anything worth highlighting (see the definition in
   vt.c for the exact rule). Lets the renderer skip empty space while term_selection_text() still
   copies it as a space. */
int  term_cell_highlightable(Term *t, int col, int row);
int  term_selection_text(Term *t, char *out, size_t cap); /* copies text, returns length */
void term_select_clear(Term *t);

/* Setting for CSI 3 J ("erase saved lines"): 1 (default) lets it drop the scrollback buffer, 0 keeps
   the history. It is a process-wide setting rather than Term state because `ESC c` / RIS rebuilds the
   Term (term_init) and must not silently re-enable it — same pattern as kai_term_loglevel. */
void term_set_clear_scrollback(int on);

/* ---------- paste (src/vt.c) ---------- */
/* Bracketed paste delimiters, sent around clipboard text when the application asked for mode 2004. */
#define PASTE_START "\x1b[200~"
#define PASTE_END   "\x1b[201~"
/* Build the exact bytes to hand the PTY for a paste of `text` (len bytes, not NUL-terminated):
   the raw text when bracketed==0, else PASTE_START + text + PASTE_END. Returns the length written,
   or -1 when out (cap bytes) is too small — the caller then sends the paste unwrapped rather than
   truncating it. Pure byte math, so it is unit-testable without a display. */
int paste_wrap(int bracketed, const char *text, size_t len, char *out, size_t cap);

/* ---------- configurable key bindings ---------- */

/* Modifier bits a binding may name. Deliberately NOT xkbcommon's masks: the config parser must not
   depend on xkbcommon (it is linked into the unit tests), and src/main.c translates the effective
   xkb mask into exactly these four bits before matching. Locks (Caps/Num) are not modifiers here. */
enum {
    KB_CTRL  = 1u << 0,
    KB_SHIFT = 1u << 1,
    KB_ALT   = 1u << 2,
    KB_SUPER = 1u << 3,
};

/* Keysyms for the non-printable keys a binding may name. These are the standard X keysym numbers,
   which is what xkb_state_key_get_one_sym() returns; src/main.c _Static_asserts every one of them
   against the xkbcommon constant, so a typo here cannot silently make a binding dead. */
enum {
    KB_KEY_BackSpace = 0xff08,
    KB_KEY_Tab       = 0xff09,
    KB_KEY_Return    = 0xff0d,
    KB_KEY_Escape    = 0xff1b,
    KB_KEY_Space     = 0x0020,
    KB_KEY_Prior     = 0xff55,   /* Page_Up   */
    KB_KEY_Next      = 0xff56,   /* Page_Down */
    KB_KEY_Home      = 0xff50,
    KB_KEY_End       = 0xff57,
    KB_KEY_Insert    = 0xff63,
    KB_KEY_Delete    = 0xffff,
    KB_KEY_Left      = 0xff51,
    KB_KEY_Up        = 0xff52,
    KB_KEY_Right     = 0xff53,
    KB_KEY_Down      = 0xff54,
    KB_KEY_F1        = 0xffbe,
    KB_KEY_F2        = 0xffbf,
    KB_KEY_F3        = 0xffc0,
    KB_KEY_F4        = 0xffc1,
    KB_KEY_F5        = 0xffc2,
    KB_KEY_F6        = 0xffc3,
    KB_KEY_F7        = 0xffc4,
    KB_KEY_F8        = 0xffc5,
    KB_KEY_F9        = 0xffc6,
    KB_KEY_F10       = 0xffc7,
    KB_KEY_F11       = 0xffc8,
    KB_KEY_F12       = 0xffc9,
};

typedef struct {
    uint32_t sym;    /* the key: a printable codepoint or one of KB_KEY_* */
    unsigned mods;   /* KB_* bits that must ALL be held (at least one: see keybind_parse) */
    int set;         /* 0 = the action is unbound (`key_new_tab = none`), 1 = bound */
} KeyBind;

/* Parse "Ctrl+Shift+T" (case-insensitive, modifiers in any order) into a KeyBind. Returns 1 when the
   spec is usable, 0 when it is not (out is then left unbound, and the caller keeps the default).
   A spec with NO modifier is refused on purpose: an unmodified binding would swallow plain typing.
   `keybind_parse` requires exactly one key; keymods_parse (for the tab-prefix binding) requires
   modifiers only. Both are pure string handling, so tests/test_vt.c can check them without a display. */
int keybind_parse(const char *spec, KeyBind *out);
int keymods_parse(const char *spec, unsigned *mods);

/* ---------- config ---------- */
typedef struct Config {
    char font[128];
    int  font_size;        /* px */
    int  cols, rows;       /* initial size */
    int  scrollback;
    uint32_t fg, bg, cursor_color;
    float opacity;         /* 0..1 */
    char shell[256];
    char working_dir[4096]; /* shell's starting directory; empty => $HOME */
    char app_id[128];       /* xdg-shell app_id the compositor uses to identify/match the window */
    int  clear_scrollback; /* 1 (default): CSI 3 J erases the scrollback buffer */
    /* 1 = releasing the selection drag copies to the clipboard (the old behaviour).
       Default 0: selecting only SELECTS, the user copies with Ctrl+Shift+C. */
    int  copy_on_release;
    /* Kai-Term drives its own key repeat. repeat_delay = milliseconds a key must be
       held before it starts repeating (default 300; 0 = leave repeat to the compositor, which is the
       behaviour before client-side repeat existed and the escape hatch if the timings ever get in the
       way). repeat_rate = repeats per second once it starts. */
    int  repeat_delay;
    int  repeat_rate;
    /* Margin in px between the window edge and the first/last cell row/column, applied on all four
       sides. Keeps the corner glyphs out of a compositor's rounded window corners. 0 = the grid
       touches the window edge (the layout before this setting existed). The pixels left over after
       whole cells still belong to the bottom/right, so the bottom margin is padding + remainder. */
    int  padding;
    /* Multiplexing: one window hosts up to max_tabs terminal sessions (tabs), each with its own
       shell, grid, scrollback and selection; only one is visible. The four bindings below are what
       drive it, and tab_prefix_mods + a digit 1-9 jumps to that tab. All of them are config strings
       such as "Ctrl+Shift+T"; see keybind_parse. */
    int  max_tabs;
    KeyBind key_new_tab;
    KeyBind key_close_tab;
    KeyBind key_next_tab;
    KeyBind key_prev_tab;
    unsigned tab_prefix_mods;   /* 0 = the digit shortcut is disabled */
    /* How the sessions of this window are SHOWN (see the layout section below): the tab bar and the
       tab/side-by-side toggle. tab_bar is BAR_OFF/BAR_AUTO/BAR_ALWAYS (AUTO = shown from the second
       session on, so a single-session user sees no bar at all), split_max caps how many sessions are
       drawn side by side, and key_toggle_layout swaps the two views. */
    int  tab_bar;
    int  split_max;
    KeyBind key_toggle_layout;
    int  loglevel;
} Config;

void config_default(Config *c);
void config_load(Config *c, const char *path);   /* NULL path -> default location */
/* Expand a leading "~" (or "~/...") to $HOME and copy the result into out. */
void expand_tilde(const char *in, char *out, size_t cap);

/* ---------- pty ---------- */
typedef struct Pty {
    int mfd;
    pid_t child;
    int rows, cols;
} Pty;

int  pty_spawn(Pty *p, const char *cmd, int rows, int cols, const char *dir);
void pty_setsize(Pty *p, int rows, int cols);
void pty_write(Pty *p, const void *buf, size_t len);

/* ---------- multiplexing: several terminal sessions in one window (src/mux.c) ---------- */

#define TAB_MAX 64          /* hard ceiling; Config.max_tabs is clamped to 1..TAB_MAX */

/* One multiplexed session. `term` is its whole screen state (grid, scrollback, selection, alt
   screen), `pty` its shell. The FILE DESCRIPTORS and the child process belong to src/main.c: mux
   never opens, closes, spawns or reaps anything — it owns the Tab array, the active index, the
   per-tab attention flag and the Term lifetime (term_init on open, term_destroy on close). */
typedef struct Tab {
    Term term;
    Pty  pty;
    /* Output arrived while this tab was NOT the visible one. Cleared when the tab becomes visible. Both
       the tab bar (a leading * in its label) and mux_title() report it, so a session that finished a build
       while hidden is findable. src/main.c only sets it for a session that is NOT drawn at all — a pane the
       user can see needs no mark. */
    int  attention;
} Tab;

typedef struct Mux {
    Tab tab[TAB_MAX];
    int n;         /* tabs in use, dense from index 0 (closing shifts the later ones down) */
    int active;    /* the visible tab; -1 when n == 0 */
    int max;       /* Config.max_tabs, clamped to 1..TAB_MAX by mux_init */
} Mux;

void mux_init(Mux *m, int max_tabs);
/* Open a tab: term_init only, the caller spawns the PTY and fills the blank cells. The new tab
   becomes the visible one. Returns its index, or -1 when max_tabs is already reached. */
int  mux_open(Mux *m, int rows, int cols, int scrollback);
/* Close tab idx (term_destroy) and shift the later tabs down so the array stays dense. Returns the
   new active index, or -1 when the last tab is gone — the caller must then quit. The caller owns the
   PTY: capture the Pty BEFORE closing the tab (the shift moves it) and close/reap it separately. */
int  mux_close(Mux *m, int idx);
int  mux_goto(Mux *m, int idx);   /* clamped to the tabs that exist; returns the active index */
int  mux_next(Mux *m);            /* wrap-around; -1 when there is no tab */
int  mux_prev(Mux *m);
Tab *mux_cur(Mux *m);             /* the visible tab, NULL when there is none */
int  mux_count(const Mux *m);
int  mux_active(const Mux *m);
/* Note output on a hidden tab (ignored for the visible one). Returns 1 when that newly SET the flag,
   so a caller can update the window title once instead of on every PTY read. */
int  mux_touch(Mux *m, int idx);
/* Window title text — what a compositor or a taskbar shows (the tab bar carries the same state inside the
   window). One tab -> DEF_TITLE exactly, so a single-session user sees no change; several ->
   "Kai-Term [2/3]", with "!" appended when a hidden session has output the user has not looked at yet. */
void mux_title(const Mux *m, char *out, size_t cap);

/* ---------- layout: where each session is drawn (src/layout.c) ----------
   Pure geometry, no Wayland/EGL/fd, so the whole tab-bar/split feature is unit-testable
   (tests/test_vt.c links it). It is the single place that decides how the window's pixels are shared:
   the tab-bar strip, one rect per tab item (with its label text), and one rect per visible session
   (pane) together with the cols/rows that fit inside it. main.c resizes every displayed session to its
   pane and maps the pointer through that pane's origin; render.c draws exactly what this says. One
   place decides it, because the drawn cell, the click -> cell mapping and the PTY size must agree —
   the same reason term_grid_geometry() exists (see its comment). */

enum { LAYOUT_TABS = 0, LAYOUT_SPLIT = 1 };

/* tab_bar config value. BAR_AUTO is the default: no bar at all while there is one session (a
   single-session window looks exactly like it did before multiplexing existed), the bar appears with the
   second session. BAR_ALWAYS shows it even with one, BAR_OFF never (then the window title and the
   keyboard bindings are the only way to see/switch sessions). */
enum { BAR_OFF = 0, BAR_AUTO = 1, BAR_ALWAYS = 2 };

#define BAR_LABEL_MAX 40    /* bytes of label text per tab item (UTF-8, cut at a codepoint edge) */

/* One session's box. x/y/w/h are window pixels (top-left origin, like the pointer events); cols/rows is
   what term_grid_geometry() says fits inside it, so the leftover pixels sit at the pane's right/bottom
   exactly like they do at the window's — that is what keeps `col = (ptr_x - pane.x) / cell_w` exact. */
typedef struct {
    int x, y, w, h;
    int cols, rows;
    int tab;           /* the mux index this pane shows */
} PaneGeom;

/* One tab-bar item: a rect inside the bar strip plus the text drawn in it. */
typedef struct {
    int x, w;          /* px inside the window (the strip is one cell row tall) */
    int tab;           /* the mux index this item selects */
    int active;        /* 1 = the session the keyboard talks to (drawn highlighted) */
    int attention;     /* 1 = unread output while hidden (drawn as a leading *) */
    char label[BAR_LABEL_MAX];
} BarItem;

typedef struct {
    int mode;          /* LAYOUT_TABS / LAYOUT_SPLIT */
    int bar_on;
    int active_tab;    /* the session the keyboard talks to (-1 when there is none): the highlighted
                          item, and the pane the focus border is drawn around */
    int bar_x, bar_y, bar_w, bar_h;   /* the strip itself, window px */
    int nitem;
    BarItem item[TAB_MAX];
    int npane;
    PaneGeom pane[TAB_MAX];
} Layout;

/* What the caller knows; layout_compute turns it into a Layout. */
typedef struct {
    int tab;           /* the mux index */
    int attention;     /* Tab.attention */
    const char *title; /* Term.title (OSC 0/2), may be empty or NULL */
} TabInfo;

typedef struct {
    int mode;
    int win_w, win_h;  /* buffer px */
    int cell_w, cell_h;
    int pad;           /* Config.padding: the margin around EVERYTHING, all four sides */
    int gap;           /* px between panes, and between the bar and the grid */
    int ntab, active;
    int split_max;     /* panes side by side at most (>= 1) */
    int bar;           /* BAR_OFF / BAR_AUTO / BAR_ALWAYS */
    TabInfo tab[TAB_MAX];
} LayoutReq;

void layout_compute(const LayoutReq *rq, Layout *out);
/* Hit tests in window px. Both return an INDEX INTO THE LAYOUT ARRAY (not a tab index), -1 when the
   point is in the margin, the gap or outside. */
int layout_hit_bar(const Layout *l, int px, int py);
int layout_hit_pane(const Layout *l, int px, int py);
/* -1 when that session is not drawn in this layout (a hidden tab in tab mode, or a session outside the
   visible pane window in split mode). */
int layout_pane_of_tab(const Layout *l, int tab);
/* The grid size EVERY session of this layout gets, drawn or not. layout_compute gives every pane the same
   height and, in split mode, the same width, so one size describes them all — and that is what lets main.c
   resize a session that is NOT on screen to the size its pane will have, so switching to it never triggers
   a resize (no SIGWINCH storm, no reflow on every tab switch). 0/0 when the layout has no pane at all. */
void layout_grid_size(const Layout *l, int *cols, int *rows);
/* The window size to ASK the compositor for so that a `cols` x `rows` session really fits, bar included.
   It is the exact inverse of layout_compute() for the startup case, and it belongs here rather than in
   main.c for the same reason the rest of the geometry does: the bar's height is one cell ROW (cell_h), and
   writing cell_w there instead leaves the grid one row short of the configured `rows` for every non-square
   cell — a mistake a check in tests/test_vt.c catches only if the arithmetic is in a module the tests link. */
void layout_initial_size(int cols, int rows, int cell_w, int cell_h, int pad, int gap, int bar,
                         int *win_w, int *win_h);

/* ---------- font / glyph atlas ---------- */
typedef struct Glyph {
    uint32_t cp;
    int w, h;              /* bitmap size */
    int bearing_x, bearing_y, advance;
    float u0, v0, u1, v1;  /* atlas texcoords */
    void *face;            /* FT_Face this glyph was rasterized from (primary or fallback) */
} Glyph;

/* A fallback font face loaded on demand for codepoints the primary font lacks. */
typedef struct FallbackFace {
    char path[4096];
    void *face;            /* FT_Face */
} FallbackFace;

/* A COLOR emoji glyph: the single glyph HarfBuzz shapes a whole grapheme cluster into,
   rasterized from a color (CBDT/CBLC or SVG) emoji font. Metrics are UNSCALED bitmap
   pixels (the strike's native size); the renderer scales them to the cell at draw time,
   so zooming never re-rasterizes the emoji bitmap. Keyed by the full codepoint sequence. */
typedef struct ColorGlyph {
    uint32_t seq[CLUSTER_MAX + 1];   /* cluster codepoints (anchor first) */
    int      nseq;
    unsigned gid;                    /* glyph id in the color face (0 = no color glyph) */
    int      w, h;                   /* bitmap size in strike px */
    int      bearing_x, bearing_y;   /* bitmap origin in strike px */
    float    u0, v0, u1, v1;         /* color-atlas texcoords */
} ColorGlyph;

typedef struct Font {
    void *ftlib;           /* FT_Library */
    void *ft;              /* FT_Face (primary) */
    int px;                /* current pixel size (used to size fallback faces) */
    int fc_ready;          /* 1 while the fontconfig library is initialized for fallback lookups */
    FallbackFace *fb;      /* fallback faces, keyed by path (linear scan; few entries) */
    int nfb, fbcap;
    int cell_w, cell_h, ascent, descent;
    int atlas_w, atlas_h;
    uint8_t *atlas;        /* alpha8, atlas_w*atlas_h */
    uint32_t atlas_tex;
    int shelf_x, shelf_y, shelf_h;
    Glyph **tab;           /* open-addressing hash by cp */
    unsigned tab_mask, tab_count;
    int atlas_dirty;       /* renderer uploads atlas when set */
    float scale;           /* zoom factor applied at raster time */

    /* ---- color emoji (HarfBuzz shaping + FreeType color bitmaps) ---- */
    int      color_ok;     /* 1 when a color emoji face + hb font are usable */
    void    *cface;        /* FT_Face of the color emoji font (bitmap-only strike) */
    void    *hbf;          /* hb_font_t for cface */
    int      color_px;     /* nominal strike height in px (scale = cell_h / color_px) */
    uint8_t *catlas;       /* RGBA8 color atlas (premultiplied, from BGRA bitmaps) */
    int      catlas_w, catlas_h;
    uint32_t catlas_tex;   /* GL texture of catlas (renderer owns, font marks dirty) */
    int      catlas_dirty;
    int      cshelf_x, cshelf_y, cshelf_h;
    ColorGlyph **ctab;     /* open-addressing hash over the cluster sequence */
    unsigned ctab_mask, ctab_count;
} Font;

int  font_init(Font *f, const char *pattern, int px);
int  font_resize(Font *f, int px);   /* re-rasterize the atlas at a new pixel size */
Glyph *font_glyph(Font *f, uint32_t cp);
/* Shape seq[0..n-1] (an emoji grapheme cluster) with HarfBuzz and return its single color
   glyph, or NULL when color emoji are unavailable / the sequence has no color glyph. */
ColorGlyph *font_color_glyph(Font *f, const uint32_t *seq, int n);
void font_destroy(Font *f);

/* ---------- emoji / width predicates (src/vt.c) ---------- */
int cp_is_emoji(uint32_t cp);        /* a base codepoint that may start an emoji cluster */
int cp_is_cluster_extend(uint32_t cp);  /* ZWJ / variation selector / skin tone / tag char */
int cp_is_wide(uint32_t cp);         /* occupies 2 grid cells (CJK + Emoji_Presentation) */
/* Width of a whole grapheme cluster: 1 when the base alone is wide, or when a modifier inside
   the cluster (U+FE0F emoji presentation, U+20E3 keycap) switches an emoji-capable base to the
   wide emoji presentation. seq[0] is the anchor codepoint. */
int cluster_is_wide(const uint32_t *seq, int n);

/* ---------- renderer (EGL + GL) ---------- */
typedef struct Render {
    void *edpy, *esurf, *ectx;   /* EGLDisplay, EGLSurface, EGLContext */
    unsigned prog;
    unsigned vao, vbo;           /* vbo: dynamic per-vertex (fallback) or per-instance data */
    unsigned quad_vbo;           /* static unit-quad corners (a_pos) for the instanced path */
    float *vdata;                /* CPU staging: per-instance records (19 floats each) */
    int vcap;
    float *edata;                /* CPU staging: expanded per-vertex data (fallback only) */
    int ecap;
    int vw, vh;                  /* buffer size in px */
    /* NOTE: there is no grid origin here any more. Since the tab bar / side-by-side layout the frame is
       a list of rectangles (Layout in kai-term.h), each with its own origin, so main.c passes them per
       frame instead of setting one global offset. */
    int u_mvp, u_atlas, u_catlas, u_sel;
    void *set_div;               /* glVertexAttribDivisorARB or NULL */
    void *draw_inst;             /* glDrawArraysInstancedARB or NULL */
    int inst_ok;                 /* 1 when both ARB instancing extensions are available */
    float opacity;               /* window background alpha 0..1 (from Config.opacity) */
    uint32_t bg;                 /* configured background ARGB, used to clear the buffer so the
                                    Config.padding margin matches the cell backgrounds */
    uint32_t fg;                 /* configured foreground ARGB: the tab bar's text colour, and the base
                                    the inactive item / separator colours are mixed from. Chrome colours
                                    come from the CONFIG, never from a cell's pen — an application can
                                    repaint its own cells but not the bar that names its tab. */
    void *ecfg;                  /* EGLConfig */
    void *nwin;                  /* EGLNativeWindowType (wl_egl_window) */
} Render;

int  render_init(Render *r, void *wl_display, void *egl_window, int w, int h, float opacity,
                 uint32_t bg, uint32_t fg);
/* Draw one frame: the tab bar (if the layout says so) plus every pane's grid, in one draw call.
   `lay` comes from layout_compute() and carries every rectangle; `terms` is indexed by TAB index, so the
   grid of pane p is terms[lay->pane[p].tab] (NULL draws an empty pane). Returns 1 when a new buffer was
   attached, 0 when the swap failed. */
int render_draw(Render *r, Font *f, const Layout *lay, Term *const *terms);
void render_resize(Render *r, int w, int h);
void render_destroy(Render *r);

/* ---------- misc utils ---------- */
static inline int imin_(int a, int b) { return a < b ? a : b; }
static inline int imax_(int a, int b) { return a > b ? a : b; }
static inline int iclamp_(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
/* Float clamp. NEVER use iclamp_ for a float: its parameters are int, so iclamp_(0.5f, 0.0f, 1.0f)
   silently truncates to 0 (that is exactly how window opacity 0.5 became fully transparent). */
static inline float fclamp_(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
int  utf8_encode(uint32_t cp, char *out);   /* returns length */

#endif /* KAI_TERM_H */
