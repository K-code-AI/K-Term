/* Kai-Term — lightweight Wayland terminal emulator. Shared header. */
#ifndef KAI_TERM_H
#define KAI_TERM_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>   /* mode_t, named by FmEntry/FmPreview — included rather than trusted to come
                            in through string.h, which is what happens to work on glibc today */

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

/* ---------- graphics: the kitty image protocol (src/vt.c) ----------
   An application (timg -p k, kitty icat, chafa) sends an image as one or more APC
   sequences:  ESC _ G <key>=<value>;...;<key>=<value> : <base64> ESC \
   timg's actual emission, captured from timg 1.6.3 (see AGENT.md s68):
       ESC_Ga=T,i=<id>,q=2,f=100,m=1;<4096 base64 chars>ESC\   (first chunk)
       ESC_Gq=2,m=0;<rest>ESC\                                (continuation: no a=, no i=)
   so m is the "more chunks follow" flag and f=100 means the payload is a PNG. */

/* One image the application transmitted. The PIXELS live in the model (vt.c) so the grid
   lifetime rules and the unit tests own them; render.c keeps a GL texture per image keyed by
   `serial` and re-uploads when `version` changes (an animation frame replaces the pixels). */
typedef struct KImage {
    int      serial;      /* unique within the Term, never reused: the renderer's texture key */
    int      app_id;      /* the i= the application used (0 = none) */
    int      w, h;        /* decoded pixel size */
    uint8_t *rgba;        /* w*h*4, PREMULTIPLIED: the shader composites c + bg*(1-c.a), the same
                             rule the color-emoji atlas follows, so no GL blending is needed */
    /* The image is anchored to an ABSOLUTE grid row (Grid.row_base space), not to a viewport row:
       a viewport row is invalidated by every scroll, while the absolute row survives rotation,
       eviction and reflow, so the image scrolls with the text it was printed next to and comes
       back when the user scrolls up into history. */
    int64_t  doc_row;
    int      col;         /* grid column of its left edge */
    int      px, py;      /* x=/y= pixel offset inside the anchor cell */
    int      cols, rows;  /* explicit cell span from c=/r= (0 = derive from px and cell size) */
    int      shown;       /* 0 = transmitted with a=t but never placed -> never drawn */
    int      z;           /* z-index, smaller drawn first (kitty default 0) */
    int      alt;         /* 1 = belongs to the alternate screen (drawn only while it is active) */
    int      version;     /* bumped when the pixels are replaced */
    struct KImage *next;
} KImage;

/* A renderer-side GL texture for one model image (render.c owns it). `seen` is stamped with the
   frame number, so an entry no image claimed this frame is deleted after the frame. */
typedef struct {
    int      serial;
    unsigned tex;
    int      w, h, version;
    int      seen;
} ImgTex;

/* Where one image sits in the CURRENT viewport, as computed by term_visible_images().
   crow may be negative (the image's top has scrolled above the viewport) and the span may run
   past the bottom: render.c clips the quad, so it needs the unclipped rect in cell units. */
typedef struct {
    KImage *img;
    int crow, ccol;    /* viewport row/column of the image's top-left cell */
    int crows, ccols;  /* the span to draw */
} ImgView;

/* A chunked image transmission in progress: the control block of the FIRST chunk plus the
   bytes collected so far. Continuation chunks carry only m=, so they inherit everything else. */
typedef struct KittyTx {
    int      active;      /* a transmission is in progress */
    int      more;        /* m=1: more chunks follow */
    uint8_t *buf; size_t len, cap;
    int      act;         /* a= : 't','T','f','c','d','q' (0 = continuation chunk) */
    int      fmt;         /* f= : 24 RGB, 32 RGBA, 48/64/100 PNG */
    int      id;          /* i= */
    int      pid;         /* u= placement id */
    int      use_last;    /* U=1: reuse the most recent image id (timg's grid mode) */
    int      pw, ph;      /* s= / r= pixel size, for raw formats (r= means height there) */
    int      r_raw;       /* r= as received: the key means height for the raw formats and rows for
                             the self-describing ones, and f= may come after it, so the choice is
                             made after the whole control block has been read */
    int      cols, rows;  /* c= / r= cell span, for plot actions (r= means rows there) */
    int      xoff, yoff;  /* x= / y= cell offsets */
    int      z;           /* z= */
    int      q;           /* q= : 1 = answer OK, 2 = answer only on failure */
    int      del;         /* a=d scope: 'A' all, 'I' by id, 'R' at/below cursor, 'C' at cursor */
} KittyTx;

/* ---------- VT parser ---------- */
struct Term;   /* forward */

enum PState {
    PS_GROUND, PS_ESC, PS_CSI, PS_OSC, PS_UTF8, PS_IGNORE, PS_APC, PS_DCS
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
    /* APC payload (the kitty graphics sequences). Growable: one chunk is a few KB but a
       different application may put a whole image in one sequence. apc_drop is set when the
       cap is reached, so a garbage stream cannot exhaust memory. */
    char  *apc; size_t apc_len, apc_cap;
    int    apc_drop;
    int    apc_kind;       /* the byte after ESC _: 'G' = graphics, anything else is ignored */
    KittyTx tx;
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
    /* Absolute (document) row number of slot 0. Slots hold a contiguous window of document
       rows, oldest at slot 0, so slot j == document row row_base + j, and document row A sits
       at viewport row A - row_base - scroll_off. row_base only moves when a row is EVICTED at
       the top (a left rotation of the used region) or when the window slides over its own
       scrollback (erase_scrollback), which is exactly when the oldest rows stop existing. Images
       are anchored by document row (see KImage), which is what lets them survive scrolling,
       eviction and reflow without any per-row bookkeeping. */
    int64_t row_base;
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
    /* Images transmitted by applications (see KImage). A singly-linked list kept oldest first:
       the byte budget evicts from the front, which is also the order the user scrolls past. */
    KImage *images;
    long long image_bytes;      /* decoded bytes held by this session */
    int      image_serial;      /* next unique serial */
    int      image_last_id;     /* i= of the most recent image (a U=1 chunk refers to it) */
    /* Current cell size in px. The image protocol sizes images in PIXELS while the grid is in
       CELLS, and only main.c knows the font metrics, so it pushes the cell size here (and to the
       Pty, which reports it to the PTY in winsize). 0 = unknown: images then fall back to their
       own pixel size as a cell count. */
    int      cell_w, cell_h;
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
/* The same parser, but an UNMODIFIED key is allowed. Only for the file manager's own keys: they are
   looked at ONLY while the file manager owns the keyboard, so `Up` cannot swallow the shell's typing —
   with the file manager off, or with the terminal focused, that same key still reaches the PTY. */
int keybind_parse_plain(const char *spec, KeyBind *out);

/* The keys the file manager uses while it has the keyboard. Grouped in one struct because they are
   twelve actions of one feature, and main.c matches them as a set. */
typedef struct {
    KeyBind up, down, pageup, pagedown, home, end;   /* move the selection */
    KeyBind open;        /* Enter: descend into the selected directory */
    KeyBind parent;      /* Backspace: go to the parent directory */
    KeyBind refresh;     /* re-read the current directory */
    KeyBind preview;     /* show / hide the preview pane */
    KeyBind hidden;      /* show / hide dotfiles */
    KeyBind quit;        /* close the file manager */
} FmKeys;

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
    /* ---- the in-window file manager (src/fm.c; its band is laid out by src/layout.c) ----
       fm_on: start with the file manager shown. Default off — a user who never presses the toggle sees
       exactly the terminal of earlier sessions, and closing the file manager returns to that state.
       fm_rows: how many CELL ROWS the band asks for (layout.c clamps it again against the window).
       fm_preview: the preview pane is shown (it can also be toggled at runtime). fm_show_hidden:
       dotfiles are part of the listing from the start. fm_follow: navigating into a directory also
       sends `cd <path>` to the shell, so the prompt lands where the browser is looking. */
    int  fm_on, fm_rows, fm_preview, fm_show_hidden, fm_follow;
    /* The window-level shortcuts; these carry modifiers like every other terminal binding. */
    KeyBind key_file_manager;    /* show / hide the file manager */
    KeyBind key_fm_focus;        /* the keyboard drives the file-manager panes */
    KeyBind key_fm_release;      /* the keyboard drives the terminal pane again */
    /* The file manager's own keys. Unbinding one (`fm_key_open = none`) leaves that action
       unavailable rather than falling back to the default — same rule as the tab bindings. */
    FmKeys fm_key;
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
    /* Cell size in px, reported to the child in winsize.ws_xpixel/ws_ypixel. Applications that
       draw images (timg, chafa, kitty icat) need it to turn a pixel-sized image into a cell span:
       measured with timg 1.6.3, it auto-selects the kitty graphics protocol ONLY when the PTY
       reports a non-zero pixel geometry, and without it the image is scaled to the wrong aspect.
       0 = not reported (the behaviour before image support). */
    int cell_w, cell_h;
} Pty;

int  pty_spawn(Pty *p, const char *cmd, int rows, int cols, const char *dir);
void pty_setsize(Pty *p, int rows, int cols);
/* Record the cell size and re-issue TIOCSWINSZ. Zooming changes the cell without changing cols x
   rows, and an application that already asked for the geometry must be told the new numbers. */
void pty_set_cells(Pty *p, int cell_w, int cell_h);
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

/* The file-manager band: the strip of the grid area the browser and the preview take, ABOVE the
   terminal pane. Decided here and nowhere else, for the usual reason — the painted cells, the pointer
   hit test and fm_set_size() must all agree on how many columns and rows each pane has.
   `on` is 0 when the window is too short to hold the band AND a usable terminal pane: the terminal wins,
   exactly as it wins over the tab bar. cols/rows come from term_grid_geometry() on each rect, so the
   leftover pixels sit at the right/bottom of each pane and `col = (ptr_x - list_x) / cell_w` is exact. */
typedef struct {
    int on;            /* the band is drawn at all */
    int list_x, list_y, list_w, list_h;
    int list_cols, list_rows;
    int prev_on;       /* the preview pane is shown (it can be toggled away) */
    int prev_x, prev_y, prev_w, prev_h;
    int prev_cols, prev_rows;
} FmGeom;

/* layout_hit_fm() answers. FM_HIT_NONE means the point is on a terminal pane, the bar, a margin or a gap. */
enum { FM_HIT_NONE = 0, FM_HIT_LIST = 1, FM_HIT_PREVIEW = 2 };

/* The band's height is asked for in CELL ROWS (Config.fm_rows) because that is the unit the panes are
   painted in. Both bounds and the terminal pane's minimum are layout rules, so they live here, not in
   config.c: clamping in config.c would let a different caller ask for a 1-row band and get one. */
#define FM_ROWS_MIN 3
#define FM_ROWS_MAX 64
#define FM_TERM_MIN_ROWS 3    /* the terminal pane never shrinks below this many rows */

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
    FmGeom fm;         /* the file-manager band; pane[] is what is LEFT of the grid area after it */
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
    int fm_on;         /* the file manager is shown */
    int fm_preview_on; /* its preview pane is shown too */
    int fm_rows;       /* how many cell rows the band asks for (clamped to FM_ROWS_MIN..MAX here) */
    TabInfo tab[TAB_MAX];
} LayoutReq;

void layout_compute(const LayoutReq *rq, Layout *out);
/* Hit tests in window px. Both return an INDEX INTO THE LAYOUT ARRAY (not a tab index), -1 when the
   point is in the margin, the gap or outside. */
int layout_hit_bar(const Layout *l, int px, int py);
int layout_hit_pane(const Layout *l, int px, int py);
/* FM_HIT_LIST / FM_HIT_PREVIEW when the point is over one of the file-manager panes, FM_HIT_NONE
   otherwise (including when the band is off). Checked BEFORE layout_hit_pane(): with the band on, the
   terminal pane starts below it, and a click inside the band must never reach the shell. */
int layout_hit_fm(const Layout *l, int px, int py);
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
                         int fm_on, int fm_rows, int *win_w, int *win_h);

/* ---------- file manager (src/fm.c) ----------
   An in-window browser: a listing of one directory, a context-sensitive preview of the selected entry,
   and the ordinary terminal below them. fm.c is MODEL ONLY — no Wayland, no EGL, no threads, no
   rendering. That is what makes the feature testable (tests/test_fm.c drives a real directory tree
   headlessly) and what makes it non-blocking: fm_scan_build() and fm_preview_build() are pure functions
   of a path that touch nothing but malloc and the filesystem, so src/main.c can run them on a worker
   thread and install the answer back on the main thread. The two panes are painted into ordinary Cell
   grids, which is what makes them read as part of the terminal: the renderer draws them with the same
   font, colours and passes it uses for a session's grid. */

#define FM_PATH_MAX   4096            /* a directory or file path (matches Config.working_dir) */
#define FM_NAME_MAX   256             /* one entry's name (NAME_MAX is 255 on Linux) */
#define FM_ERR_MAX    256             /* one human-readable error line */
#define FM_ENTRIES_MAX 8192           /* entries one listing keeps; the rest are dropped with a warning */
#define FM_PREVIEW_BYTES (256u * 1024u)   /* bytes of a text preview read from disk */
#define FM_PREVIEW_DIR_MAX 512        /* entries shown in a directory preview */
#define FM_PREVIEW_FILE_MAX (16u * 1024u * 1024u)  /* largest image file a preview will read */
#define FM_META_LINES 16              /* metadata lines one preview shows */
#define FM_META_WIDTH 128             /* bytes per metadata line */

/* Accent colours for the two panes, in the header so tests/test_fm.c can pin the colour a token got.
   They are ACCENTS only: the base ink and paper are the configured Config.fg/Config.bg, passed to
   fm_paint(), so the file manager matches the terminal. Chosen to read on a dark background and to
   agree with what ls and editors do (directory blue, executable green, string green, comment grey). */
#define FM_DIR_COL   0xff6cb6ffu   /* directories */
#define FM_LINK_COL  0xff3ddad7u   /* symlinks */
#define FM_EXEC_COL  0xffa5d6a7u   /* owner-executable files */
#define FM_ERR_COL   0xfff26d78u   /* anything that failed */
#define FM_KEY_COL   0xff6cb6ffu   /* keyword, section, heading */
#define FM_STR_COL   0xffc3e88du   /* string literal */
#define FM_NUM_COL   0xfff78c6cu   /* number, shell variable */
#define FM_CMT_COL   0xff8b949eu   /* comment */
#define FM_PP_COL    0xffd2a8ffu   /* preprocessor, decorator, tag */
#define FM_TYPE_COL  0xff4ec9b0u   /* type name, json key, make target */

/* What a listing entry IS. Decided from lstat(), so a symlink is reported as a symlink and never as
   whatever it points at (a dangling link must still be listable). */
enum { FMK_DIR = 0, FMK_FILE, FMK_SYMLINK, FMK_FIFO, FMK_SOCKET, FMK_DEVICE, FMK_MISSING };

/* What the preview pane shows for the selected entry. */
enum { FMP_NONE = 0, FMP_DIR, FMP_TEXT, FMP_IMAGE, FMP_META, FMP_EMPTY };

/* Which region the keyboard drives while the file manager is on. FM_FOCUS_TERM is the state the
   terminal is in when the file manager is off, so closing it is just focus = FM_FOCUS_TERM + on = 0. */
enum { FM_FOCUS_TERM = 0, FM_FOCUS_LIST, FM_FOCUS_PREVIEW };

/* Languages the text preview can colour. Chosen from the file name; FM_LANG_NONE means the text is
   shown in the plain foreground colour. */
enum { FM_LANG_NONE = 0, FM_LANG_C, FM_LANG_PY, FM_LANG_SH, FM_LANG_JSON, FM_LANG_MD,
       FM_LANG_MAKE, FM_LANG_INI, FM_LANG_HTML, FM_LANG_RS, FM_LANG_JS, FM_LANG_GO,
       FM_LANG_CSS, FM_LANG_YAML, FM_LANG_N };

/* One entry of a directory listing. Only the NAME is stored: a path would make one entry 4 KB and a
   capped listing of 8192 entries 32 MB, while fm_entry_path() rebuilds the full path in a caller's
   buffer in one snprintf. */
typedef struct {
    char name[FM_NAME_MAX];
    uint8_t kind;        /* FMK_* */
    uint8_t hidden;      /* name starts with '.' — kept in the listing, hidden from the view */
    uint8_t exec;        /* owner-executable regular file: drawn like one */
    mode_t mode;
    uint64_t size;
    int64_t mtime;       /* seconds, st_mtime */
} FmEntry;

/* The answer to one listing request. Built off the main thread, installed on it. `seq` is the request
   id the caller passed in; an answer whose seq is no longer the newest one is dropped, which is what
   keeps a slow scan of a directory the user already left from replacing the listing on screen. */
typedef struct {
    char dir[FM_PATH_MAX];
    FmEntry *ent;        /* malloc'd, n entries, sorted (directories first) */
    int n;
    int truncated;       /* the directory holds more than max_entries */
    char err[FM_ERR_MAX];
    unsigned long seq;
} FmScan;

/* The answer to one preview request. Only the fields of the chosen `kind` are filled; `meta` is filled
   for every kind, because the header of every preview shows the same facts. */
typedef struct {
    char path[FM_PATH_MAX];
    int kind;            /* FMP_* */
    /* FMP_DIR */
    FmEntry *ent;
    int n;
    /* FMP_TEXT */
    char *text;          /* malloc'd, NUL-terminated, control characters already replaced */
    size_t len;
    int lang;            /* FM_LANG_* */
    /* FMP_IMAGE */
    uint8_t *rgba;       /* malloc'd RGBA8 (NOT premultiplied — the caller does that) */
    int w, h;
    char fmt[8];         /* "png" / "jpg" / "gif" / "webp", from the bytes, not the name */
    /* every kind */
    int truncated;       /* what the entry holds is bigger than what a preview reads: more text than
                            FM_PREVIEW_BYTES, more directory entries than FM_PREVIEW_DIR_MAX, or an
                            image file bigger than FM_PREVIEW_FILE_MAX */
    char meta[FM_META_LINES][FM_META_WIDTH];
    int nmeta;
    char err[FM_ERR_MAX];
    unsigned long seq;      /* the request this answers */
    unsigned long uid;      /* unique per built preview, never reused: the renderer keys the preview's
                               GL texture on this, so a re-installed preview re-uploads and a stale one
                               can never be painted from a recycled malloc address. */
} FmPreview;

/* One painted pane. The renderer walks it exactly like a session grid (row-major, cell (x,y) at
   cell[y*cols+x]), so the file manager needs no new drawing code path beyond "a grid with no cursor
   and no selection". */
typedef struct {
    Cell *cell;
    int cols, rows;
} FmPane;

typedef struct {
    int on;              /* the file manager is shown at all */
    int focus;           /* FM_FOCUS_* */
    int preview_on;      /* the preview pane is shown (it can be toggled away) */
    int show_hidden;     /* dotfiles are part of the view */
    char dir[FM_PATH_MAX];
    char err[FM_ERR_MAX];

    FmEntry *ent;        /* the installed listing (ecap allocated entries) */
    int n, ecap;
    int *vis;            /* vis[0..nvis-1] index ent[]: the view after the hidden filter */
    int nvis, vcap;
    int truncated;       /* the listing is not the whole directory */

    int sel, top;        /* selected position in vis[], first visible row of the list pane */
    int list_cols, list_rows, prev_cols, prev_rows;   /* what the layout asked for */
    FmPane list, prev;   /* what the renderer draws */

    unsigned long seq;       /* monotonic request-id issuer, SHARED by both kinds so an id can never
                                name a listing and a preview at once */
    unsigned long scan_req;  /* id of the newest LISTING request (0 = none issued yet) */
    unsigned long pv_req;    /* id of the newest PREVIEW request (0 = none issued yet) */
    int have_listing;        /* ent[] really holds a listing of fm->dir */
    FmPreview *pv;       /* the installed preview, owned here */
    int dirty;           /* the panes must be repainted */
} FileMgr;

void fm_init(FileMgr *fm, const char *dir);
void fm_destroy(FileMgr *fm);
/* The size the layout gave the two panes. Reallocs and blanks the cell grids when it changes. */
void fm_set_size(FileMgr *fm, int list_cols, int list_rows, int prev_cols, int prev_rows);

/* ---- listing: build (worker thread) then install (main thread) ---- */
FmScan *fm_scan_build(const char *dir, unsigned long seq);
/* The same with an explicit entry cap, so a test can pin the truncation behaviour without creating
   FM_ENTRIES_MAX files. fm_scan_build() is this with FM_ENTRIES_MAX. */
FmScan *fm_scan_build_n(const char *dir, unsigned long seq, int max_entries);
void fm_scan_free(FmScan *s);
/* Take ownership of the answer. Returns 0 and frees it when it answers a request that is no longer the
   newest one (the user navigated away while the scan ran). */
int  fm_install_scan(FileMgr *fm, FmScan *s);
/* Ask for a listing of dir (0 = unchanged, already showing it). Bumps fm->seq; the caller hands the new
   seq to the worker. */
int  fm_go(FileMgr *fm, const char *dir, unsigned long *seq);
int  fm_go_sel(FileMgr *fm, unsigned long *seq);    /* descend into the selected directory */
int  fm_parent(FileMgr *fm, unsigned long *seq);
int  fm_refresh(FileMgr *fm, unsigned long *seq);

/* ---- selection ---- */
void fm_select(FileMgr *fm, int pos);
void fm_move(FileMgr *fm, int delta);   /* +1 down, -1 up; keeps the selection inside the view */
void fm_page(FileMgr *fm, int pages);   /* one page = the list pane's entry rows */
void fm_home(FileMgr *fm);
void fm_end(FileMgr *fm);
int  fm_count(const FileMgr *fm);       /* entries in the view */
int  fm_sel(const FileMgr *fm);         /* selected position, -1 when the view is empty */
const FmEntry *fm_sel_entry(const FileMgr *fm);
int  fm_entry_path(const FileMgr *fm, int pos, char *out, size_t cap);
/* Show or hide dotfiles. Rebuilds the view from the listing already in memory — no second scan of the
   disk — and keeps the selection a position in the new view. */
void fm_set_hidden(FileMgr *fm, int show);
/* Which entry of the view the row of the LIST PANE shows, or -1 when that row is chrome (the header or
   the footer) or past the end. The painter owns the row layout, so the hit test lives next to it:
   main.c must not re-derive where the entries start. */
int  fm_row_pos(const FileMgr *fm, int row);

/* ---- preview: build (worker thread) then install (main thread) ---- */
FmPreview *fm_preview_build(const char *path, unsigned long seq);
void fm_preview_free(FmPreview *p);
int  fm_install_preview(FileMgr *fm, FmPreview *pv);   /* same stale-answer rule as fm_install_scan */
/* The path that SHOULD be previewed right now (the selected entry, or the current directory when
   nothing is selected). 1 when it differs from what is installed, and then *seq gets the id to ask
   the worker for and path the path to read. */
int  fm_preview_request(FileMgr *fm, unsigned long *seq, char *path, size_t cap);

/* ---- painting ---- */
/* Fill both cell grids from the current state. fg/bg are the CONFIGURED colours, so the file manager
   matches the terminal instead of inventing a palette. */
void fm_paint(FileMgr *fm, uint32_t fg, uint32_t bg);
/* Where the preview picture goes inside the preview pane, in CELLS, aspect ratio preserved for these
 * cell dimensions: the renderer draws the quad and fm.c decides the box, because the box is what a test
 * can check and what keeps a 3000x200 photo from being stretched to the pane. px_w/px_h are the same box
 * in PIXELS — the cell box is that plus up to one cell of ceil() slack, and drawing the cell box would
 * stretch the picture by the slack. */
void fm_image_fit(const FileMgr *fm, int cell_w, int cell_h,
                  int *col, int *row, int *cols, int *rows, int *px_w, int *px_h);

/* ---- small shared formatters (also used by the tests) ---- */
void fm_human_size(uint64_t n, char *out, size_t cap);
void fm_human_time(int64_t t, char *out, size_t cap);
void fm_mode_string(mode_t m, char out[11]);   /* "-rwxr-xr-x" / "drwxr-xr-x" / "lrwxrwxrwx" */
int  fm_lang_for_path(const char *path);       /* FM_LANG_* from a file name */

/* Text preview colouring. State carries from line to line, because a C block comment does not stop at
 * the end of a line. fg has one entry per BYTE of line (all bytes of one codepoint get the same colour,
 * so the painter takes the colour of the lead byte). */
typedef struct { int lang; int in_block; } FmHl;
void fm_hl_init(FmHl *st, int lang);
void fm_hl_line(FmHl *st, const char *line, size_t len, uint32_t *fg, uint32_t base);

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
    int u_mvp, u_atlas, u_catlas, u_img, u_sel;
    /* One GL texture per image the model currently shows (see KImage). Keyed by KImage.serial,
       which is unique per session and never reused, so a recycled pointer can never make the
       renderer reuse a stale texture. Entries for images that are not on screen are dropped, so
       the GPU never holds more than what is visible. */
    ImgTex *imgtex;
    int nimgtex, imgtex_cap;
    int img_frame;               /* bumped once per frame; ImgTex.seen == img_frame means "used this
                                    frame", which is how textures of pictures that left the screen go */
    /* The file manager's preview picture: at most one at a time, so it gets one texture instead of a
       cache. Re-uploaded when FmPreview.uid changes (a new preview was installed) or when the pixels
       were resized. fm_px is the premultiplied copy the shader wants — img.c hands out plain RGBA and
       the preview model keeps it that way so its tests can compare decoded bytes. */
    unsigned fm_tex;
    unsigned long fm_tex_uid;
    int fm_tex_w, fm_tex_h;
    uint8_t *fm_px;
    int fm_px_cap;
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

/* ---------- image decoding (src/img.c) ----------
   The one place bytes become pixels. Both producers share it: src/vt.c (the kitty graphics protocol,
   where an application transmits a picture inline) and src/fm.c (the file manager's preview pane, where
   the picture is read from disk). Output is always RGBA8, never premultiplied — the caller premultiplies
   when it knows where the pixels are going (see img_premultiply and the renderer's a_glyph == 3 path).
   PNG/JPEG/WebP use the system libraries; GIF is decoded in-tree (giflib is not a dependency). */

/* Caps every decoder enforces, so neither a hostile escape stream nor a 30 MB photo can make a frame
   late or exhaust the machine. One definition, because the graphics path and the preview path must
   refuse the same picture. */
#define IMG_DIM_MAX  4096            /* accepted px per side */
#define IMG_PIX_MAX  (16LL << 20)    /* accepted pixels per image */

/* 1 when these bytes are a format img_decode_mem can read. `kind` (8 bytes, may be NULL) comes back as
   "png" / "jpg" / "gif" / "webp", or "" when nothing matched. Recognition is by the bytes themselves,
   never by a file name or by what a protocol's f= field claims. */
int img_sniff(const uint8_t *mem, size_t n, char *kind);
uint8_t *img_decode_png(const uint8_t *mem, size_t n, int *w, int *h);
/* Sniff, then decode. Returns malloc'd RGBA8 (w*h*4) or NULL; the size outputs are 0 on failure.
   `kind` (8 bytes or NULL) reports which decoder ran, which is what the preview pane shows for an image. */
uint8_t *img_decode_mem(const uint8_t *mem, size_t n, int *w, int *h, char *kind);
/* c + bg*(1-c.a) is how the shader composites every texel (no GL blending, and a Wayland EGL buffer is
   premultiplied), so every picture must pass through here once. */
void img_premultiply(uint8_t *rgba, long long npix);

/* ---------- image model helpers (src/vt.c) ---------- */
void term_set_cell_px(Term *t, int w, int h);
/* Fill out[] with the images of this session that are at least partly inside the viewport,
   sorted by z then serial (the order they must be drawn in). Returns the count. Pure grid math,
   so tests/test_vt.c can pin it without a display. */
int  term_visible_images(Term *t, ImgView *out, int cap);
int  term_image_count(const Term *t);

/* ---------- renderer (EGL + GL) ---------- */
int  render_init(Render *r, void *wl_display, void *egl_window, int w, int h, float opacity,
                 uint32_t bg, uint32_t fg);
/* Draw one frame: the tab bar (if the layout says so), every pane's grid, and the file-manager band if
   the layout has one. Two vertex batches (backgrounds, then bands + glyphs + chrome) with one draw call
   per application picture between them — see the header comment of src/render.c for why the frame cannot
   be a single call.
   `lay` comes from layout_compute() and carries every rectangle; `terms` is indexed by TAB index, so the
   grid of pane p is terms[lay->pane[p].tab] (NULL draws an empty pane). `fm` is the file manager whose
   cell grids the layout laid out (NULL or fm->on == 0 draws no band); its painted panes go through the
   same two passes as a session grid, and its preview picture is drawn between the batches like any other
   picture. Returns 1 when a new buffer was attached, 0 when the swap failed. */
int render_draw(Render *r, Font *f, const Layout *lay, Term *const *terms, const FileMgr *fm);
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
