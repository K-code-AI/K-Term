/* K-Term — lightweight Wayland terminal emulator. Shared header. */
#ifndef K_TERM_H
#define K_TERM_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- logging ---------- */
enum { L_ERROR = 0, L_WARN, L_INFO, L_DEBUG };
extern int k_term_loglevel;
void klog(int level, const char *fmt, ...);

/* ---------- colors & cell attributes ---------- */
#define DEF_FG      0xffe5e5e5u   /* ARGB */
#define DEF_BG      0xff101014u
#define CURSOR_COL  0xffd8d8d8u
#define SEL_BG      0xff3a4d66u   /* selection highlight background */

/* Default xdg-shell app_id. Single-sourced here because config_default(), the
   `app_id =` empty-value fallback in config.c and the last-resort fallback in main.c must all agree;
   three string literals that can drift apart is how a default silently changes. */
#define DEF_APP_ID  "K-Term"

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
   place and leave its old output behind (ISSUES.md #4). */
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
   Term (term_init) and must not silently re-enable it — same pattern as k_term_loglevel. */
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
    /* K-Term drives its own key repeat. repeat_delay = milliseconds a key must be
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
    /* Where the grid starts inside the buffer, in px (the Config.padding margin). main.c computes it
       with term_grid_geometry() — the SAME call it uses for cols/rows and for the pointer -> cell
       mapping — and hands it over with render_set_origin(), so the quads and the pointer mapping can
       never disagree. 0,0 is the old edge-to-edge layout. */
    int gx, gy;
    int u_mvp, u_atlas, u_catlas, u_sel;
    void *set_div;               /* glVertexAttribDivisorARB or NULL */
    void *draw_inst;             /* glDrawArraysInstancedARB or NULL */
    int inst_ok;                 /* 1 when both ARB instancing extensions are available */
    float opacity;               /* window background alpha 0..1 (from Config.opacity) */
    uint32_t bg;                 /* configured background ARGB, used to clear the buffer so the
                                    Config.padding margin matches the cell backgrounds */
    void *ecfg;                  /* EGLConfig */
    void *nwin;                  /* EGLNativeWindowType (wl_egl_window) */
} Render;

int  render_init(Render *r, void *wl_display, void *egl_window, int w, int h, float opacity,
                 uint32_t bg);
int render_draw(Render *r, Term *t, Font *f);   /* 1 = a new buffer was attached, 0 = swap failed */
void render_resize(Render *r, int w, int h);
void render_set_origin(Render *r, int gx, int gy);   /* grid offset in buffer px (see Render.gx) */
void render_destroy(Render *r);

/* ---------- misc utils ---------- */
static inline int imin_(int a, int b) { return a < b ? a : b; }
static inline int imax_(int a, int b) { return a > b ? a : b; }
static inline int iclamp_(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
/* Float clamp. NEVER use iclamp_ for a float: its parameters are int, so iclamp_(0.5f, 0.0f, 1.0f)
   silently truncates to 0 (that is exactly how window opacity 0.5 became fully transparent). */
static inline float fclamp_(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
int  utf8_encode(uint32_t cp, char *out);   /* returns length */

#endif /* K_TERM_H */
