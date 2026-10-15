/* K-Term — VT/ANSI parser, grid, scrollback. */
#include "k-term.h"

#include <errno.h>
#include <unistd.h>

int k_term_loglevel = L_WARN;

/* forward declarations for terminal ops used before definition */
static void scroll_up(Term *t, int top, int bot);
static void scroll_down(Term *t, int top, int bot);
static void linefeed(Term *t);
static void putch(Term *t, uint32_t cp);
static void cluster_invalidate(Term *t);
static void pen_default(Term *t);
static void alt_screen_enter(Term *t, int save_cursor);
static void alt_screen_leave(Term *t, int restore_cursor);

void klog(int level, const char *fmt, ...)
{
    static const char *pfx[] = { "error", "warn", "info", "debug" };
    if (level > k_term_loglevel)
        return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "k-term %s: ", pfx[level]);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

int utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ---------------- codepoint predicates ---------------- */

/* Codepoints that can START an emoji grapheme cluster (the anchor of a ZWJ / modifier
   sequence) or take part in one after a ZWJ. Deliberately generous: a codepoint that is not
   really an emoji only costs one HarfBuzz shaping attempt (cached) before the mono fallback. */
int cp_is_emoji(uint32_t cp)
{
    return (cp >= 0x2190 && cp <= 0x2BFF) ||    /* arrows, misc symbols, dingbats, symbols-A */
           (cp >= 0x1F000 && cp <= 0x1FAFF);    /* emoji planes (pictographs, emoticons, ...) */
}

/* Codepoints that never occupy a cell of their own: they modify the emoji (or keycap) written
   just before them. ZWJ additionally opens the cluster for the NEXT emoji base codepoint. */
int cp_is_cluster_extend(uint32_t cp)
{
    return cp == 0x200D ||                      /* zero width joiner */
           (cp >= 0xFE00 && cp <= 0xFE0F) ||    /* variation selectors (VS15/VS16) */
           (cp >= 0x1F3FB && cp <= 0x1F3FF) ||  /* emoji modifier skin tones */
           cp == 0x20E3 ||                      /* combining enclosing keycap */
           (cp >= 0xE0020 && cp <= 0xE007F);    /* tag chars (subdivision flags) */
}

/* Regional indicator letters (U+1F1E6..U+1F1FF). Two of them form ONE flag grapheme cluster
   (U+1F1FA U+1F1F8 = US flag), which is what the color font's flag glyph is keyed on. */
static int cp_is_ri(uint32_t cp)
{
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

/* Two-cell (wide) codepoints: East Asian Wide/Fullwidth plus the Emoji_Presentation ranges,
   so a color emoji reserves two grid slots instead of being squished into one. */
int cp_is_wide(uint32_t cp)
{
    if (cp >= 0x1100 &&
        (cp <= 0x115F || (cp >= 0x2E80 && cp <= 0xA4CF) ||
         (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6)))
        return 1;
    /* Emoji_Presentation = Yes (Unicode 15.1) + regional indicators. Coarse on purpose: a
       handful of text-presentation symbols in these blocks become wide, which is what most
       terminals do anyway and never breaks the grid. */
    static const uint32_t emo[][2] = {
        { 0x1F004, 0x1F004 }, { 0x1F0CF, 0x1F0CF }, { 0x1F18E, 0x1F18E },
        { 0x1F191, 0x1F19A }, { 0x1F1E6, 0x1F1FF }, { 0x1F200, 0x1F202 },
        { 0x1F210, 0x1F23B }, { 0x1F240, 0x1F248 }, { 0x1F250, 0x1F251 },
        { 0x1F260, 0x1F265 }, { 0x1F300, 0x1F320 }, { 0x1F32D, 0x1F335 },
        { 0x1F337, 0x1F37C }, { 0x1F37E, 0x1F393 }, { 0x1F3A0, 0x1F3CA },
        { 0x1F3CF, 0x1F3D3 }, { 0x1F3E0, 0x1F3F0 }, { 0x1F3F4, 0x1F3F4 },
        { 0x1F3F8, 0x1F43E }, { 0x1F440, 0x1F440 }, { 0x1F442, 0x1F4FC },
        { 0x1F4FF, 0x1F53D }, { 0x1F54B, 0x1F54E }, { 0x1F550, 0x1F567 },
        { 0x1F57A, 0x1F57A }, { 0x1F595, 0x1F596 }, { 0x1F5A4, 0x1F5A4 },
        { 0x1F5FB, 0x1F64F }, { 0x1F680, 0x1F6C5 }, { 0x1F6CC, 0x1F6CC },
        { 0x1F6D0, 0x1F6D2 }, { 0x1F6D5, 0x1F6D7 }, { 0x1F6DC, 0x1F6DF },
        { 0x1F6EB, 0x1F6EC }, { 0x1F6F4, 0x1F6FC }, { 0x1F7E0, 0x1F7EB },
        { 0x1F7F0, 0x1F7F0 }, { 0x1F90C, 0x1F93A }, { 0x1F93C, 0x1F945 },
        { 0x1F947, 0x1F9FF }, { 0x1FA70, 0x1FA7C }, { 0x1FA80, 0x1FA88 },
        { 0x1FA90, 0x1FABD }, { 0x1FABF, 0x1FACF }, { 0x1FAD0, 0x1FADB },
    };
    if (cp < 0x1F004 || cp > 0x1FADB)
        return 0;
    for (unsigned i = 0; i < sizeof(emo) / sizeof(emo[0]); i++)
        if (cp >= emo[i][0] && cp <= emo[i][1])
            return 1;
    return 0;
}

/* Width of a WHOLE grapheme cluster, not just of its base codepoint. A cluster is wide when
   the base is wide by itself (Emoji_Presentation, CJK, a lone regional indicator) OR when a
   modifier in the cluster switches the base to EMOJI PRESENTATION:
     - U+FE0F (VS16) on an emoji-capable base: U+2764 FE0F is the red emoji heart, U+2764 alone
       is a plain text dingbat. Same for U+263A FE0F, U+1F3F3 FE0F 200D 1F308 (rainbow flag) ...
     - U+20E3 (combining enclosing keycap) on a digit / '#' / '*': the keycap 1\u20E3 is an emoji.
   Without this the base's narrow width is used and a 136x128 color bitmap gets shrunk into a
   single 8px cell instead of reserving the two cells a wide emoji needs. */
int cluster_is_wide(const uint32_t *seq, int n)
{
    if (n <= 0)
        return 0;
    if (cp_is_wide(seq[0]))
        return 1;
    int vs16 = 0, keycap = 0;
    for (int i = 1; i < n; i++) {
        if (seq[i] == 0xFE0F)
            vs16 = 1;
        if (seq[i] == 0x20E3)
            keycap = 1;
    }
    if (!vs16 && !keycap)
        return 0;
    if (cp_is_emoji(seq[0]))
        return 1;
    if (keycap && ((seq[0] >= '0' && seq[0] <= '9') || seq[0] == '#' || seq[0] == '*'))
        return 1;
    return 0;
}

/* ---------------- grid ---------------- */

static Cell *row_new_cols(int cols)
{
    Cell *r = calloc((size_t)cols, sizeof(Cell));
    if (!r)
        abort();
    for (int x = 0; x < cols; x++) {
        r[x].fg = DEF_FG;
        r[x].bg = DEF_BG;
    }
    return r;
}

static Cell *row_new(Grid *g)
{
    return row_new_cols(g->cols);
}

static void row_fillblank(Grid *g, Cell *r, uint32_t bg)
{
    for (int x = 0; x < g->cols; x++) {
        r[x].ch = 0;
        r[x].attr = 0;
        r[x].nextra = 0;   /* a blank cell holds no emoji cluster continuation */
        r[x].fg = DEF_FG;
        r[x].bg = bg;
    }
}

static void row_resize(Grid *g, Cell **rp, int old_cols)
{
    Cell *r = realloc(*rp, (size_t)g->cols * sizeof(Cell));
    if (!r)
        abort();
    *rp = r;
    /* realloc does NOT zero newly grown columns; clear them so they are blank cells
       (otherwise stale heap bytes become garbage ch values -> garbage glyph quads). */
    for (int x = old_cols; x < g->cols; x++) {
        r[x].ch = 0;
        r[x].attr = 0;
        r[x].nextra = 0;
        r[x].fg = DEF_FG;
        r[x].bg = DEF_BG;
    }
    for (int x = 0; x < g->cols; x++)
        if (r[x].bg == 0)
            r[x].bg = DEF_BG;
}

/* Rotate a contiguous range of row[] slots. Both are permutations of the range,
 * so no buffer can ever end up referenced by two slots (which would double-free
 * in grid_destroy). The soft-wrap marks rotate WITH the rows (w may be NULL only
 * for a grid without scrollback bookkeeping, i.e. never in practice): a mark belongs
 * to the line, not to the slot. */
static void rot_left(Cell **a, uint8_t *w, int n)   /* slot i <- old slot i+1; old slot 0 -> slot n-1 */
{
    Cell *first = a[0];
    memmove(a, a + 1, (size_t)(n - 1) * sizeof(Cell *));
    a[n - 1] = first;
    if (!w)
        return;
    uint8_t wf = w[0];
    memmove(w, w + 1, (size_t)(n - 1));
    w[n - 1] = wf;
}

static void rot_right(Cell **a, uint8_t *w, int n)  /* slot i <- old slot i-1; old slot n-1 -> slot 0 */
{
    Cell *last = a[n - 1];
    memmove(a + 1, a, (size_t)(n - 1) * sizeof(Cell *));
    a[0] = last;
    if (!w)
        return;
    uint8_t wl = w[n - 1];
    memmove(w + 1, w, (size_t)(n - 1));
    w[0] = wl;
}

/* Blank a slot's row AND forget its soft-wrap mark: a blank line never continues into the next. */
static void grid_blank_slot(Grid *g, int i, uint32_t bg)
{
    row_fillblank(g, g->row[i], bg);
    g->wrap[i] = 0;
}

/* How much scrollback a viewport of the CURRENT height can actually hold. rcap is fixed for the grid's
   lifetime (rows_at_init + scrollback_max), so a window that becomes TALLER than the initial row count
   eats into the scrollback: the usable limit is rcap - rows, not scrollback_max. Every bound on
   scroll_off/live_off must use this. Using scrollback_max after the viewport grew is what made
   scroll_up blank row[rcap] (heap-buffer-overflow, found by an ASan build of k-term with `seq 1 5000`:
   30 configured rows -> a 62-row window -> so saturates at 1968 while scrollback_max still says 2000). */
static int sb_limit(const Grid *g)
{
    return imin_(g->scrollback_max, g->rcap - g->rows);
}

void grid_init(Grid *g, int rows, int cols, int scrollback)
{
    g->rows = rows;
    g->cols = cols;
    g->scrollback_max = scrollback;
    g->scroll_off = 0;
    g->live_off = 0;
    g->rcap = rows + scrollback;
    g->valid = GRID_VALID;
    g->row = calloc((size_t)g->rcap, sizeof(Cell *));
    if (!g->row)
        abort();
    g->wrap = calloc((size_t)g->rcap, sizeof(uint8_t));
    if (!g->wrap)
        abort();
    for (int i = 0; i < g->rcap; i++)
        g->row[i] = row_new(g);
}

/* Grid layout invariant:
 *   row[0 .. scroll_off-1]                    scrollback (oldest first)
 *   row[scroll_off .. scroll_off+rows-1]      viewport
 *   row[scroll_off+rows .. rcap-1]            free slots (stale buffers, content
 *                                             irrelevant but must be cleared before
 *                                             they re-enter the viewport)
 * with 0 <= scroll_off <= live_off <= sb_limit(g) == min(scrollback_max, rcap - rows),
 * rcap == rows_at_init + scrollback_max (fixed for the grid's lifetime) and
 * scroll_off + rows <= rcap. The sb_limit form matters: growing the viewport shrinks the
 * scrollback that fits, so scrollback_max alone is NOT a safe bound after a grow.
 *
 * live_off is the anchor that follows the newest output: scroll_up/scroll_down advance it
 * together with scroll_off, so a terminal that is simply printing always has
 * scroll_off == live_off and the viewport shows the newest lines. User scrollback navigation
 * moves scroll_off DOWN below live_off (toward the oldest lines); the slots that leave the
 * viewport at the bottom then hold the newest lines rather than being free, which is why any
 * whole-viewport scroll (and a resize) first returns scroll_off to live_off.
 *
 * grid_init allocates all rcap row buffers and they are never freed or reallocated
 * (except column resize); every content-moving operation is a rotation of a
 * contiguous slot range, so row[] stays a permutation of those buffers and each is
 * freed exactly once by grid_destroy.
 */
Cell *grid_vprow(Grid *g, int y)
{
    return g->row[g->scroll_off + y];
}

/* ---------------- reflow (column-count change) ----------------
 *
 * grid_resize used to realloc every row buffer to the new width: narrowing silently DELETED the
 * cells past the new width and widening left blanks, so text never came back. Reflow instead joins
 * the physical rows back into the logical lines the application wrote (using the Grid.wrap marks)
 * and re-breaks them at the new width, so a width change is loss-free and reversible.
 *
 * Rules:
 *  - the content is slots [0, used), used == scroll_off + rows; the caller has snapped scroll_off to
 *    live_off, so everything above the viewport is scrollback and everything below is free space;
 *  - a logical line is a maximal run of slots whose mark is set (row i continues into row i+1);
 *  - a wide glyph is an anchor cell followed by an ATTR_WIDE continuation cell: a chunk boundary is
 *    pulled back so the pair is never split (the whole glyph moves to the next line);
 *  - re-breaking can need MORE slots than the grid has (narrowing). The OLDEST rows are evicted,
 *    exactly like ordinary scrollback overflow, so the newest content always survives;
 *  - every produced row buffer is freshly allocated at the new width and all the old ones are freed,
 *    so row[] stays a permutation of exactly rcap buffers (tests/dbg.c asserts that invariant).
 */
/* A cell that was never written to and carries no colour of its own. */
static int cell_pristine(const Cell *c)
{
    return c->ch == 0 && c->nextra == 0 && c->attr == 0 && c->fg == DEF_FG && c->bg == DEF_BG;
}

/* How many leading cells of a row take part in its logical line: the trailing never-written blanks
   do not. Dropping them when rows are joined back together matters twice — it stops a 5-character
   line from becoming two physical rows in a narrow window, and it removes the padding a wide glyph
   leaves at the end of a row when an earlier reflow moved the whole pair to the next line (without
   this, re-widening turned that padding into a literal blank cell in the middle of the text). */
static int row_used(const Cell *r, int cols)
{
    int end = cols;
    while (end > 0 && cell_pristine(&r[end - 1]))
        end--;
    return end;
}

static void grid_reflow(Grid *g, int new_cols, int *slot, int *col)
{
    int old_cols = g->cols;
    int used = g->scroll_off + g->rows;
    if (used > g->rcap)
        used = g->rcap;

    /* Output is built oldest-first. At most rcap rows survive (older ones are evicted), and the kept
       window is compacted back to the front once head slots are dead, so a fixed 2*rcap array is
       enough even when narrowing multiplies the line count many times over. */
    int cap = 2 * g->rcap + 2;
    Cell **out = malloc((size_t)cap * sizeof(Cell *));
    uint8_t *ow = malloc((size_t)cap);
    int *ulen = malloc((size_t)g->rcap * sizeof(int));
    if (!out || !ow || !ulen)
        abort();
    int n = 0, head = 0;

    for (int i = 0; i < used; i++)
        ulen[i] = row_used(g->row[i], old_cols);

    /* Locate the tracked cell (the cursor) in logical coordinates: which logical line, and which
       column of the joined line. The join uses the same ulen values the chunker walks below, so the
       two agree by construction. */
    int cur_logical = -1, cur_lcol = -1;
    if (slot && *slot >= 0 && *slot < used) {
        int L = 0, base = 0;
        for (int i = 0; i < *slot; i++) {
            base += ulen[i];
            if (i + 1 < used && !g->wrap[i]) {
                L++;
                base = 0;
            }
        }
        cur_logical = L;
        cur_lcol = base + imin_(*col, ulen[*slot]);
    }
    int out_row = -1, out_col = 0;      /* where that cell lands in the new grid */

    int L = 0;
    for (int i = 0; i < used; ) {
        int j = i;
        while (j + 1 < used && g->wrap[j])
            j++;
        int total = 0;
        for (int k = i; k <= j; k++)
            total += ulen[k];

        if (total == 0) {
            /* a completely blank logical line: it still needs exactly one blank row */
            if (cur_logical == L && out_row < 0) {
                out_row = n;
                out_col = imin_(cur_lcol, new_cols - 1);
            }
        }

        int last = -1, last_x = 0, pos = 0;
        int sk = i, sc = 0;                     /* source cell: slot sk, column sc */
        do {
            Cell *nr = row_new_cols(new_cols);
            int cstart = pos, x = 0;
            while (x < new_cols && pos < total) {
                /* Wide pair guard: if this cell would be the chunk's LAST one and the cell after it
                   is that cell's ATTR_WIDE continuation, stop here instead — the whole glyph moves to
                   the next line rather than being cut in half. (x > 0 keeps progress guaranteed at
                   new_cols == 1, where a wide glyph cannot fit anyway.) */
                if (x > 0 && x + 1 >= new_cols && pos + 1 < total) {
                    int nk = sk, nc = sc + 1;
                    while (nk < j && nc >= ulen[nk]) { nk++; nc = 0; }
                    if (g->row[nk][nc].attr & ATTR_WIDE)
                        break;
                }
                nr[x++] = g->row[sk][sc];
                pos++;
                sc++;
                while (sk < j && sc >= ulen[sk]) { sk++; sc = 0; }
            }
            for (int k = x; k < new_cols; k++) {  /* calloc blanked them; only the pen is set */
                nr[k].fg = DEF_FG;
                nr[k].bg = DEF_BG;
            }
            if (cur_logical == L && out_row < 0 && cstart <= cur_lcol && cur_lcol < pos) {
                out_row = n;
                out_col = cur_lcol - cstart;
            }
            last = n;
            last_x = x;
            out[n] = nr;
            ow[n] = (pos < total) ? 1 : 0;     /* every chunk but the last continues */
            n++;
            /* eviction: keep only the newest rcap rows (the oldest lines fall off the top). This must
               run for EVERY produced row, blank rows included — otherwise `kept` can exceed rcap and
               the commit below would drop buffers instead of installing them (a leak). */
            if (n - head >= g->rcap) {
                free(out[head]);
                head++;
                if (head >= g->rcap) {      /* compact so the fixed array never overflows */
                    memmove(out, out + head, (size_t)(n - head) * sizeof(Cell *));
                    memmove(ow, ow + head, (size_t)(n - head));
                    if (out_row >= 0)
                        out_row -= head;
                    if (last >= 0)
                        last -= head;
                    n -= head;
                    head = 0;
                }
            }
        } while (pos < total);
        if (cur_logical == L && out_row < 0 && last >= 0) {
            /* the tracked cell sat in trimmed blank space: put it just past the surviving text */
            out_row = last;
            out_col = last_x;
        }
        L++;
        i = j + 1;
    }
    free(ulen);

    /* commit: the old buffers are gone, the new ones fill row[0..kept), the rest are fresh blanks */
    for (int i = 0; i < g->rcap; i++)
        free(g->row[i]);
    g->cols = new_cols;
    int kept = n - head;
    if (kept > g->rcap) {          /* eviction makes this impossible; never leak if it somehow happens */
        for (int i = g->rcap; i < kept; i++)
            free(out[head + i]);
        kept = g->rcap;
    }
    for (int i = 0; i < g->rcap; i++) {
        if (i < kept) {
            g->row[i] = out[head + i];
            g->wrap[i] = ow[head + i];
        } else {
            g->row[i] = row_new(g);
            g->wrap[i] = 0;
        }
    }
    free(out);
    free(ow);

    /* the newest content stays anchored at the bottom of the viewport */
    g->live_off = iclamp_(kept - g->rows, 0, sb_limit(g));
    g->scroll_off = g->live_off;

    if (slot) {
        if (out_row >= 0) {
            *slot = out_row;              /* index into the NEW row[] (kept rows start at 0) */
            *col = iclamp_(out_col, 0, new_cols - 1);
        } else {
            *slot = iclamp_(kept - 1, 0, g->rcap - 1);   /* its line was evicted */
            *col = 0;
        }
    }

    if (kept > used)
        klog(L_DEBUG, "reflow: %d -> %d rows at %d cols (%d evicted)", used, kept, new_cols, kept - used);
}

/* Viewport height change: the rows math, shared by the reflowing and the plain resize. */
static void grid_resize_rows(Grid *g, int rows)
{
    /* Resize math below assumes the slots below the viewport are free, which is only true
       at the live anchor. A resize while scrolled back therefore returns the view to the
       live bottom first (the scrollback content itself is preserved). */
    g->scroll_off = g->live_off;
    if (rows < g->rows) {
        /* Shrink: the viewport keeps its bottom line, so each line lost is the
         * viewport's top line. When scrollback has room the window simply slides
         * down one slot (the dropped line becomes the newest scrollback line in
         * place). When scrollback is full the oldest line is evicted by
         * left-rotating the used region; the evicted buffer lands in the last used
         * slot, where it is cleared and that slot becomes free. */
        while (g->rows > rows) {
            int used = g->scroll_off + g->rows;
            if (g->scroll_off < sb_limit(g)) {
                g->scroll_off++;
                g->live_off = g->scroll_off;
            } else {
                rot_left(g->row, g->wrap, used);
                grid_blank_slot(g, used - 1, DEF_BG);
                g->live_off = g->scroll_off;
            }
            g->rows--;
        }
    } else {
        /* Grow: anchor the live bottom line. A taller viewport fits more scrollback
         * above it, so the live anchor slides UP by the number of added rows (never
         * below 0). If the anchor can supply the extra rows the window slides up in
         * place and the revealed slots already hold the right content; otherwise all
         * scrollback is revealed and the remaining new rows come from free slots at
         * the bottom, which must be cleared. rcap is fixed, so the viewport can never
         * exceed it. */
        if (rows > g->rcap)
            rows = g->rcap;
        int add = rows - g->rows;
        int used = g->scroll_off + g->rows;
        g->live_off = iclamp_(g->live_off - add, 0, imin_(g->scrollback_max, g->rcap - rows));
        if (g->live_off + rows <= g->rcap) {
            g->scroll_off = g->live_off;
            for (int i = used; i < g->scroll_off + rows; i++)
                grid_blank_slot(g, i, DEF_BG);
        } else {
            g->scroll_off = g->rcap - rows;
            for (int i = used; i < g->rcap; i++)
                grid_blank_slot(g, i, DEF_BG);
        }
        g->live_off = iclamp_(g->scroll_off, 0, sb_limit(g));
        g->rows = rows;
    }
}

void grid_resize(Grid *g, int rows, int cols, int *slot, int *col)
{
    rows = iclamp_(rows, 1, 4096);
    cols = iclamp_(cols, 1, 4096);
    /* The reflow needs the live anchor (everything below the viewport must be free space), so a
       width change while scrolled back snaps the view to the live bottom first. Selection is
       viewport-relative and is cleared on any resize (term_resize). */
    g->scroll_off = g->live_off;
    if (cols != g->cols)
        grid_reflow(g, cols, slot, col);
    if (rows == g->rows)
        return;
    grid_resize_rows(g, rows);
}

void grid_resize_plain(Grid *g, int rows, int cols)
{
    rows = iclamp_(rows, 1, 4096);
    cols = iclamp_(cols, 1, 4096);
    if (cols != g->cols) {
        /* No reflow: rows are truncated or grown in place (see the header for why the alternate
           screen must not be re-wrapped). Marks of truncated rows are dropped: the surviving part
           of a wrapped line ends at the new margin, so claiming it continues would invent content. */
        int old_cols = g->cols;
        g->cols = cols;
        for (int i = 0; i < g->rcap; i++) {
            row_resize(g, &g->row[i], old_cols);
            if (cols < old_cols)
                g->wrap[i] = 0;
        }
    }
    if (rows == g->rows)
        return;
    grid_resize_rows(g, rows);
}

void grid_destroy(Grid *g)
{
    if (g->valid != GRID_VALID || !g->row)
        return;
    for (int i = 0; i < g->rcap; i++)
        free(g->row[i]);
    free(g->row);
    free(g->wrap);
    memset(g, 0, sizeof(*g));   /* also clears valid: the grid is gone */
}

/* ---------------- terminal core ---------------- */

static void pen_default(Term *t)
{
    t->pen.fg = DEF_FG;
    t->pen.bg = DEF_BG;
    t->pen.attr = 0;
}

void term_init(Term *t, int rows, int cols, int scrollback)
{
    /* If called as a reset (ESC c / RIS) while the alt screen is active, first
       discard the alt grid and restore the main grid so neither is leaked. */
    if (t->g.valid == GRID_VALID && t->alt_screen) {
        grid_destroy(&t->g);            /* the alt grid */
        t->g = t->alt;                  /* restore main grid */
        memset(&t->alt, 0, sizeof(t->alt));
        t->alt_screen = 0;
    }
    if (t->g.valid == GRID_VALID)
        grid_destroy(&t->g);            /* free old main grid buffers (fixes RIS leak) */
    memset(t, 0, sizeof(*t));
    grid_init(&t->g, rows, cols, scrollback);
    t->top = 0;
    t->bot = rows - 1;
    t->show_cursor = 1;
    pen_default(t);

    /* xterm 16 base colors, then 6x6x232 cube, then grayscale ramp */
    static const uint32_t base16[16] = {
        0xff000000, 0xffcc5555, 0xff55cc55, 0xffcccc55,
        0xff5555cc, 0xffcc55cc, 0xff55cccc, 0xffcccccc,
        0xff777777, 0xffff5555, 0xff55ff55, 0xffffff55,
        0xff5555ff, 0xffff55ff, 0xff55ffff, 0xffffffff,
    };
    memcpy(t->palette, base16, sizeof(base16));
    for (int i = 0; i < 216; i++) {
        int r = i / 36, g = (i / 6) % 6, b = i % 6;
        int rv = r ? 55 + 41 * r : 0;
        int gv = g ? 55 + 41 * g : 0;
        int bv = b ? 55 + 41 * b : 0;
        t->palette[16 + i] = 0xff000000u | (uint32_t)(rv << 16 | gv << 8 | bv);
    }
    for (int i = 0; i < 24; i++) {
        int v = 8 + i * 10;
        t->palette[232 + i] = 0xff000000u | (uint32_t)(v << 16 | v << 8 | v);
    }
}

void term_resize(Term *t, int rows, int cols)
{
    int old_live = t->g.live_off;
    int old_cy = t->cy;
    /* The cursor travels through the reflow as an absolute row[] slot + column: grid_resize maps it
       to the same cell in the new grid (its logical line and column are preserved, and if the line
       was evicted by narrowing it lands on the newest surviving line). Without reflow the slot does
       not move, so this reduces to the old "keep the cursor on the same content line" rule. */
    int slot = old_live + old_cy, col = t->cx;
    if (t->alt_screen) {
        /* While the alternate screen is active t->g IS that screen and t->alt holds the parked MAIN
           grid — the opposite of what the names suggest. A full-screen application gets SIGWINCH and
           repaints itself, so the alt screen must NOT be re-wrapped (that would scramble vim/htop
           output); the parked main screen, which the user returns to when the app exits, is reflowed
           like any other width change. */
        grid_resize_plain(&t->g, rows, cols);
        grid_resize(&t->alt, rows, cols, NULL, NULL);
    } else {
        grid_resize(&t->g, rows, cols, &slot, &col);
    }
    t->top = 0;
    t->bot = t->g.rows - 1;
    t->cx = iclamp_(col, 0, t->g.cols - 1);
    /* The parser's cursor is a LIVE-view coordinate (main.c snaps the view to live_off before
       every parse), and a resize always leaves the view at the live anchor, so its viewport row is
       its slot minus the anchor. (Clamping cy alone left the cursor stranded mid-screen after a
       window grow.) */
    t->cy = iclamp_(slot - t->g.live_off, 0, t->g.rows - 1);
    t->wrap_next = 0;
    cluster_invalidate(t);
    term_select_clear(t);   /* selection is viewport-relative; drop it on resize */
}

void term_select_clear(Term *t)
{
    t->sel_active = 0;
    t->sel_af = t->sel_bf = 0;
}

/* Normalize the selection into a reading-order span (r0,c0,f0)..(r1,c1,f1). The f values
   are thousandths of a cell width (0..1000). The anchor may be after the current cell, so
   ordering is by (row, col, frac). */
void term_selection_span(Term *t, SelSpan *s)
{
    s->r0 = t->sel_ay; s->c0 = t->sel_ax; s->f0 = t->sel_af;
    s->r1 = t->sel_by; s->c1 = t->sel_bx; s->f1 = t->sel_bf;
    if (s->r0 > s->r1 ||
        (s->r0 == s->r1 && (s->c0 > s->c1 || (s->c0 == s->c1 && s->f0 > s->f1)))) {
        int tr = s->r0, tc = s->c0, tf = s->f0;
        s->r0 = s->r1; s->c0 = s->c1; s->f0 = s->f1;
        s->r1 = tr;    s->c1 = tc;    s->f1 = tf;
    }
}

/* Covered sub-range of cell (col,row) in cell-fraction units, clamped to [col,col+1].
   Returns 1 when the cell is covered (then *b > *a), 0 otherwise.
   Rows strictly between the span rows are fully covered; the top/bottom rows start/end at
   the sub-cell edges. A click with no movement (start == end) covers the whole cell under
   the pointer, so a single click still selects a glyph. */
int sel_span_cover(const SelSpan *s, int cols, int col, int row, double *a, double *b)
{
    *a = 0.0;
    *b = 0.0;

    if (s->r0 == s->r1 && s->c0 == s->c1 && s->f0 == s->f1) {
        if (row != s->r0 || col != s->c0)
            return 0;
        *a = (double)col;
        *b = (double)col + 1.0;
        return 1;
    }

    double lo, hi;
    if (row < s->r0 || row > s->r1) {
        lo = 1.0;
        hi = 0.0;                       /* outside the span's rows: empty */
    } else {
        lo = (row == s->r0) ? (s->c0 + s->f0 / 1000.0) : 0.0;
        hi = (row == s->r1) ? (s->c1 + s->f1 / 1000.0) : (double)cols;
    }

    double x0 = ((double)col > lo) ? (double)col : lo;
    double x1 = ((double)col + 1.0 < hi) ? (double)col + 1.0 : hi;
    if (x1 <= x0)
        return 0;
    *a = x0;
    *b = x1;
    return 1;
}

/* Band geometry in buffer pixels: the band is ABSOLUTE (a*cw), not relative to the cell
   origin. The renderer must not add the cell origin again — doing so shifts every band one
   cell width to the right, where the next cell's background quad paints over it and only the
   left-most column's highlight survives. */
void sel_band_pixels(const SelSpan *s, int cols, int col, int row, float cw,
                     float *x, float *w)
{
    double a, b;
    *x = 0.0f;
    *w = 0.0f;
    if (!sel_span_cover(s, cols, col, row, &a, &b))
        return;
    *x = (float)a * cw;
    *w = (float)(b - a) * cw;
}

int term_selection_cover(Term *t, int col, int row, double *a, double *b)
{
    if (!t->sel_active) {
        *a = 0.0;
        *b = 0.0;
        return 0;
    }
    SelSpan s;
    term_selection_span(t, &s);
    return sel_span_cover(&s, t->g.cols, col, row, a, b);
}

/* Is glyph (col,row) part of the selection? True when the selection covers part of its
   cell, so the pointer entering a cell selects that glyph and a span ending before a cell
   drops it. */
int term_selected(Term *t, int col, int row)
{
    double a, b;
    return term_selection_cover(t, col, row, &a, &b);
}

/* Should the highlight band actually be PAINTED over cell (col,row)? The caller has already decided
   the cell is inside the selection; this answers "is there anything here to put a highlight on?".
   Rule:
     - ch == 0            -> nothing was ever written here: NO band, so a selection that runs past the
                             end of a line stops at the last real glyph instead of painting the whole
                             remainder of the window.
     - ch == ' '          -> a real space produced by an application: band IS painted (the user asked
                             for it explicitly, and it is what shows a multi-line selection is
                             still connected across the gap).
     - wide continuation  -> ch == 0 but ATTR_WIDE set: this cell belongs to the glyph in the previous
                             column, so it keeps its band, otherwise the right half of a CJK/emoji
                             glyph would lose its highlight.
   The copied text is unaffected: term_selection_text() maps ch == 0 to a space and trims trailing
   spaces per line either way. */
int term_cell_highlightable(Term *t, int col, int row)
{
    if (row < 0 || row >= t->g.rows || col < 0 || col >= t->g.cols)
        return 0;
    const Cell *c = grid_vprow(&t->g, row) + col;
    if (c->attr & ATTR_WIDE)
        return 1;
    return c->ch != 0;
}

/* Copy the selected text into out (NUL-terminated), returning its length. Each line has
   trailing spaces trimmed; lines are joined with '\n'. A glyph is copied when the span
   covers its cell (exactly the same rule as the highlight). */
int term_selection_text(Term *t, char *out, size_t cap)
{
    if (!t->sel_active) {
        if (cap)
            out[0] = 0;
        return 0;
    }
    SelSpan s;
    term_selection_span(t, &s);
    if (s.r0 < 0) { s.r0 = 0; s.c0 = 0; s.f0 = 0; }
    if (s.r1 > t->g.rows - 1) { s.r1 = t->g.rows - 1; s.c1 = t->g.cols - 1; s.f1 = 1000; }

    size_t n = 0;
    /* A cell can hold a whole emoji grapheme cluster: the anchor codepoint plus up to
       CLUSTER_MAX continuation codepoints, 4 UTF-8 bytes each, so the per-line scratch buffer
       is sized for the worst case rather than the old 4-bytes-per-cell assumption. */
    size_t linecap = (size_t)t->g.cols * 4u * (size_t)(CLUSTER_MAX + 1) + 1u;
    char *line = malloc(linecap);
    if (!line) {
        if (cap)
            out[0] = 0;
        return 0;
    }
    for (int y = s.r0; y <= s.r1; y++) {
        const Cell *r = grid_vprow(&t->g, y);
        int ln = 0;
        for (int x = 0; x < t->g.cols; x++) {
            double a, b;
            if (!sel_span_cover(&s, t->g.cols, x, y, &a, &b))
                continue;
            const Cell *c = &r[x];
            if (c->attr & ATTR_WIDE)
                continue;              /* continuation of a wide glyph: already copied */
            uint32_t ch = c->ch;
            if (ch == 0) {
                if (ln + 1 < (int)linecap)
                    line[ln++] = ' ';
                continue;
            }
            /* copy the whole cluster (base + ZWJ / variation selector / skin tone / joined
               emoji) so a pasted emoji is still the emoji, not its pieces */
            uint32_t seq[CLUSTER_MAX + 1];
            int nseq = 1;
            seq[0] = ch;
            for (int k = 0; k < (int)c->nextra && nseq <= CLUSTER_MAX; k++)
                seq[nseq++] = c->extra[k];
            for (int k = 0; k < nseq; k++) {
                char b8[4];
                int m = utf8_encode(seq[k], b8);
                if (ln + m < (int)linecap) {
                    memcpy(line + ln, b8, (size_t)m);
                    ln += m;
                }
            }
        }
        while (ln > 0 && line[ln - 1] == ' ')
            ln--;
        if ((int)(n + (size_t)ln) + 1 >= (int)cap) {
            free(line);
            break;
        }
        memcpy(out + n, line, (size_t)ln);
        n += (size_t)ln;
        if (y < s.r1) {
            if ((int)(n + 1) >= (int)cap) {
                free(line);
                break;
            }
            out[n++] = '\n';
        }
    }
    free(line);
    out[n] = 0;
    return (int)n;
}

void term_destroy(Term *t)
{
    if (t->alt_screen) {
        grid_destroy(&t->g);            /* alt grid */
        grid_destroy(&t->alt);          /* saved main grid */
    } else {
        grid_destroy(&t->g);
    }
}

/* Alternate screen buffer (xterm ?1049 / ?47 / ?1047). The active grid stays t->g so
   every existing t->g reference keeps working; the MAIN grid is parked in t->alt while
   the alt screen is active. The alt grid has no scrollback (scrollback_max 0), so its
   content scrolls in place and is discarded on exit. */
static void alt_screen_enter(Term *t, int save_cursor)
{
    if (t->alt_screen)
        return;
    if (save_cursor) {
        t->alt_saved_cx = t->cx;
        t->alt_saved_cy = t->cy;
    } else {
        t->alt_saved_cx = 0;
        t->alt_saved_cy = 0;
    }
    t->alt = t->g;                                        /* park main grid */
    grid_init(&t->g, t->alt.rows, t->alt.cols, 0);       /* fresh alt grid, no scrollback */
    for (int y = 0; y < t->g.rows; y++)
        row_fillblank(&t->g, t->g.row[y], t->pen.bg);
    t->top = 0;
    t->bot = t->g.rows - 1;
    t->cx = 0;
    t->cy = 0;
    t->wrap_next = 0;
    t->alt_screen = 1;
}

static void alt_screen_leave(Term *t, int restore_cursor)
{
    if (!t->alt_screen)
        return;
    grid_destroy(&t->g);                /* discard alt grid */
    t->g = t->alt;                      /* restore main grid */
    memset(&t->alt, 0, sizeof(t->alt));
    t->alt_screen = 0;
    t->top = 0;
    t->bot = t->g.rows - 1;
    t->cx = restore_cursor ? t->alt_saved_cx : 0;
    t->cy = restore_cursor ? t->alt_saved_cy : 0;
    t->cx = iclamp_(t->cx, 0, t->g.cols - 1);
    t->cy = iclamp_(t->cy, 0, t->g.rows - 1);
    t->wrap_next = 0;
}

static void linefeed(Term *t)
{
    cluster_invalidate(t);
    if (t->cy == t->bot)
        scroll_up(t, t->top, t->bot);
    else if (t->cy < t->g.rows - 1)
        t->cy++;
    if (t->newline_mode)
        t->cx = 0;
}

/* Fold cp into the emoji grapheme cluster whose anchor cell was written just before the
   cursor. Returns 1 when cp was consumed: it gets NO cell of its own and the cursor does not
   move, so 👍🏽 stays one cell and 👨\u200d👩\u200d👧\u200d👦 stays one cell pair (2 slots).
   Without this each ZWJ / variation selector / skin tone lands in its own cell and the
   sequence renders as broken separate glyphs. The anchor must still be the cell immediately
   before the cursor on the same row, which is what makes the state safe to keep across a
   write burst and drop as soon as anything else moves the cursor or the rows. */
static int put_cluster_join(Term *t, uint32_t cp)
{
    if (!t->cl_valid)
        return 0;
    if (t->cy != t->cl_cy || t->cx != t->cl_cx + t->cl_w)
        return 0;
    Cell *a = &grid_vprow(&t->g, t->cl_cy)[t->cl_cx];
    if (a->ch == 0 || a->nextra >= CLUSTER_MAX)
        return 0;

    int join_base = t->cl_join && cp_is_emoji(cp);   /* emoji after a ZWJ */
    /* A regional indicator also joins an RI anchor: the PAIR is one flag cluster and the color
       font keys its flag glyph on the pair (shaping one RI alone yields the letter glyph). Only
       the second one joins -- nextra == 0 means the anchor is still a bare RI -- so a third RI
       starts a new cluster, as Unicode requires. */
    int join_ri = cp_is_ri(cp) && cp_is_ri(a->ch) && a->nextra == 0;
    if (!join_base && !join_ri && !cp_is_cluster_extend(cp))
        return 0;

    a->extra[a->nextra++] = cp;
    if (cp == 0x200D)
        t->cl_join = 1;          /* the next emoji base joins this same anchor */
    else if (join_base || join_ri)
        t->cl_join = 0;

    /* The modifier may have just switched the cluster to emoji presentation, which is WIDE
       (U+2764 is 1 cell, U+2764 U+FE0F is 2). The anchor was reserved narrow when its base was
       written, so upgrade it now: the cell right after the anchor becomes the continuation cell
       and the cursor steps over it, exactly as if the wide base had been written that way.
       Only possible while the cursor still sits directly after a 1-cell anchor and the next
       column exists; otherwise the emoji stays in its single cell (shrunk to fit). */
    if (t->cl_w == 1 && t->cx == t->cl_cx + 1 && t->cl_cx + 1 < t->g.cols) {
        uint32_t seq[CLUSTER_MAX + 1];
        int nseq = 1;
        seq[0] = a->ch;
        for (int k = 0; k < (int)a->nextra && nseq <= CLUSTER_MAX; k++)
            seq[nseq++] = a->extra[k];
        if (cluster_is_wide(seq, nseq)) {
            Cell *nxt = &grid_vprow(&t->g, t->cl_cy)[t->cl_cx + 1];
            nxt->ch = 0;
            nxt->fg = a->fg;
            nxt->bg = a->bg;
            nxt->attr = ATTR_WIDE;
            nxt->nextra = 0;
            t->cx++;
            t->cl_w = 2;
            if (t->cx >= t->g.cols)
                t->wrap_next = 1;
        }
    }
    return 1;
}

static void putch(Term *t, uint32_t cp)
{
    if (put_cluster_join(t, cp))
        return;

    if (t->wrap_next && t->cx >= t->g.cols) {
        t->cx = 0;
        /* The line the cursor is leaving was broken by the RIGHT MARGIN, not by the application.
           Mark it BEFORE the linefeed: this is the only information that lets a later width change
           join the two physical rows back into one logical line (reflow). linefeed() may scroll, and
           the scroll rotations move the mark together with its row. */
        t->g.wrap[t->g.scroll_off + t->cy] = 1;
        linefeed(t);
    }
    t->wrap_next = 0;
    if (t->cx >= t->g.cols)
        t->cx = t->g.cols - 1;
    int ax = t->cx, ay = t->cy;   /* anchor cell of the new cluster (viewport coords) */
    Cell *c = &grid_vprow(&t->g, t->cy)[t->cx];
    c->ch = cp;
    c->fg = t->pen.fg;
    c->bg = t->pen.bg;
    c->attr = t->pen.attr & ~(uint32_t)ATTR_WIDE;
    c->nextra = 0;               /* a fresh cell starts a fresh cluster */
    int wide = cp_is_wide(cp);
    int consumed = 1;
    if (wide && t->cx + 1 < t->g.cols) {
        c[1].ch = 0;
        c[1].fg = t->pen.fg;
        c[1].bg = t->pen.bg;
        c[1].attr = ATTR_WIDE;
        c[1].nextra = 0;
        consumed = 2;
        t->cx += 2;
    } else {
        t->cx++;
    }
    if (t->cx >= t->g.cols)
        t->wrap_next = 1;

    /* remember the anchor so a following ZWJ / modifier / joined emoji folds into it */
    t->cl_valid = 1;
    t->cl_cx = ax;
    t->cl_cy = ay;
    t->cl_w = consumed;
    t->cl_join = 0;
}

/* Any cursor jump or row movement invalidates the in-progress cluster: cl_cx/cl_cy are
   viewport coordinates and row rotations move the anchor out from under them. */
static void cluster_invalidate(Term *t)
{
    t->cl_valid = 0;
    t->cl_join = 0;
}

static void scroll_up(Term *t, int top, int bot)
{
    Grid *g = &t->g;
    cluster_invalidate(t);
    int whole = (top == 0 && bot == g->rows - 1);
    /* Whole-viewport scrolling advances the LIVE view. If the user has scrolled back, the
       slots below the viewport hold the newest lines (they are not free), so return the
       window to the live anchor before moving it. Partial-region scrolls never touch
       scrollback and leave the scrolled-back view alone. */
    if (whole && g->scroll_off != g->live_off)
        g->scroll_off = g->live_off;
    int so = g->scroll_off;
    int sbmax = sb_limit(g);        /* rcap - rows, not scrollback_max: see sb_limit() */
    if (whole && sbmax > 0 && so < sbmax) {
        /* Whole viewport, scrollback has room: the window slides down one slot. No
         * content moves - the departing viewport top line already sits in the slot
         * that becomes the newest scrollback line, and the free slot below the
         * window becomes the new blank bottom line. */
        g->scroll_off = so + 1;
        g->live_off = g->scroll_off;
        grid_blank_slot(g, so + g->rows, t->pen.bg);
    } else if (whole && sbmax > 0) {
        /* Whole viewport, scrollback full: left-rotate the used region, evicting the
         * oldest line. Its buffer lands in the last used slot and is cleared there
         * to become the new blank bottom line. */
        int used = so + g->rows;
        rot_left(g->row, g->wrap, used);
        grid_blank_slot(g, used - 1, t->pen.bg);
        g->live_off = g->scroll_off;   /* saturated at sb_limit() */
    } else {
        /* Partial scroll region, or no scrollback configured: rotate inside
         * [top..bot]. The departing top line's buffer lands at the region bottom,
         * where it is cleared to become the new region bottom line. */
        rot_left(g->row + so + top, g->wrap + so + top, bot - top + 1);
        grid_blank_slot(g, so + bot, t->pen.bg);
    }
}

static void scroll_down(Term *t, int top, int bot)
{
    Grid *g = &t->g;
    cluster_invalidate(t);
    int whole = (top == 0 && bot == g->rows - 1);
    if (whole && g->scroll_off != g->live_off)
        g->scroll_off = g->live_off;   /* see scroll_up: the live anchor must be current */
    int so = g->scroll_off;
    if (whole && so > 0) {
        /* Whole viewport with scrollback above it: the window slides up one slot, so
         * the newest scrollback line becomes the viewport top in place and the
         * viewport's bottom line leaves the screen - its slot becomes free and is
         * cleared. Exact inverse of scroll_up's slide branch. */
        g->scroll_off = so - 1;
        g->live_off = g->scroll_off;
        grid_blank_slot(g, so + g->rows - 1, t->pen.bg);
    } else {
        /* Partial region, or nothing above the viewport: rotate inside [top..bot].
         * The region's bottom line leaves the region and its buffer lands at the
         * region top, where it is cleared as the inserted blank line. */
        rot_right(g->row + so + top, g->wrap + so + top, bot - top + 1);
        grid_blank_slot(g, so + top, t->pen.bg);
    }
}

static void erase_cells(Term *t, int y, int x0, int x1)
{
    Cell *r = grid_vprow(&t->g, y);
    for (int x = imax_(x0, 0); x <= imin_(x1, t->g.cols - 1); x++) {
        r[x].ch = 0;
        r[x].attr = 0;
        r[x].nextra = 0;
        r[x].fg = t->pen.fg;
        r[x].bg = t->pen.bg;
    }
}

/* CSI 3 J setting (see k-term.h). Default ON; main.c applies the configured value. */
static int g_clear_scrollback = 1;

void term_set_clear_scrollback(int on)
{
    g_clear_scrollback = on ? 1 : 0;
}

/* Build the bytes a paste of `text` is sent to the PTY as (see k-term.h). With bracketed paste mode
   on, the block is delimited by ESC[200~ / ESC[201~: bash/zsh/readline/vim then insert it literally,
   newlines included, instead of letting every CR run a command. The delimiters are written as three
   separate writes by the caller so a huge paste never needs a buffer holding prefix+text+suffix. */
int paste_wrap(int bracketed, const char *text, size_t len, char *out, size_t cap)
{
    size_t need = len;
    if (bracketed)
        need += strlen(PASTE_START) + strlen(PASTE_END);
    if (!out || cap < need)
        return -1;
    size_t o = 0;
    if (bracketed) {
        memcpy(out + o, PASTE_START, strlen(PASTE_START));
        o += strlen(PASTE_START);
    }
    if (text && len)
        memcpy(out + o, text, len);
    o += len;
    if (bracketed) {
        memcpy(out + o, PASTE_END, strlen(PASTE_END));
        o += strlen(PASTE_END);
    }
    return (int)o;
}

/* CSI 3 J — "erase saved lines" (the terminfo E3 capability; `clear` emits ESC[2J then ESC[3J).
   It drops the SCROLLBACK and leaves the viewport exactly as it is, so the prompt stays on screen:
   1. the viewport rows move to the top slots of row[] (they keep their buffers),
   2. every remaining row buffer is blanked and sits below the viewport as free space,
   3. scroll_off = live_off = 0, which also snaps a scrolled-back view back to the live bottom.
   row[] must stay a permutation of the allocated row buffers (tests/dbg.c asserts no slot is lost,
   duplicated or NULL), so the rearrangement is done through a scratch pointer array and validated
   before anything is committed. */
static void erase_scrollback(Term *t)
{
    Grid *g = &t->g;
    cluster_invalidate(t);              /* the anchor cell may have just moved rows */
    if (!g_clear_scrollback || g->scrollback_max <= 0)
        return;                         /* nothing retained (e.g. the alt screen) -> nothing to do */
    int so = g->scroll_off;
    if (so == 0) {
        /* no scrollback above the viewport, but rows below it can still hold the newest lines while
           the user is scrolled back; blanking the free slots below is enough */
        for (int y = g->rows; y < g->rcap; y++)
            grid_blank_slot(g, y, t->pen.bg);
        g->live_off = 0;
        return;
    }
    Cell **tmp = malloc((size_t)g->rcap * sizeof(Cell *));
    uint8_t *tmpw = malloc((size_t)g->rcap);
    if (!tmp || !tmpw)
        abort();
    int n = 0;
    for (int y = 0; y < g->rows; y++) {                      /* viewport -> slots 0..rows-1 */
        tmp[n] = g->row[so + y];
        tmpw[n] = g->wrap[so + y];      /* the mark belongs to the line, so it moves with it */
        n++;
    }
    for (int i = 0; i < g->rcap; i++) {                      /* every other buffer, below them */
        if (i < so || i >= so + g->rows) {
            tmp[n] = g->row[i];
            tmpw[n] = g->wrap[i];
            n++;
        }
    }
    if (n != g->rcap) {                                      /* never corrupt the grid on a logic bug */
        free(tmp);
        free(tmpw);
        klog(L_ERROR, "erase_scrollback: row accounting broken (%d/%d)", n, g->rcap);
        return;
    }
    memcpy(g->row, tmp, (size_t)g->rcap * sizeof(Cell *));
    memcpy(g->wrap, tmpw, (size_t)g->rcap);
    free(tmp);
    free(tmpw);
    for (int y = g->rows; y < g->rcap; y++)
        grid_blank_slot(g, y, t->pen.bg);
    g->scroll_off = 0;
    g->live_off = 0;
    klog(L_DEBUG, "ED 3: dropped %d scrollback rows", so);
}

static void erase_display(Term *t, int mode)
{
    if (mode == 3) {          /* xterm "erase saved lines" (terminfo E3, what `clear` sends) */
        erase_scrollback(t);
        return;
    }
    /* Every row whose cells are erased also loses its soft-wrap mark: after the erase nothing proves
       the line still reaches the margin, and reflow must never join lines that were not wrapped. */
    #define ERASE_ROW(y) do { erase_cells(t, (y), 0, t->g.cols - 1); \
                              t->g.wrap[t->g.scroll_off + (y)] = 0; } while (0)
    if (mode == 0) {
        erase_cells(t, t->cy, t->cx, t->g.cols - 1);
        t->g.wrap[t->g.scroll_off + t->cy] = 0;
        for (int y = t->cy + 1; y < t->g.rows; y++)
            ERASE_ROW(y);
    } else if (mode == 1) {
        for (int y = 0; y < t->cy; y++)
            ERASE_ROW(y);
        erase_cells(t, t->cy, 0, t->cx);
        t->g.wrap[t->g.scroll_off + t->cy] = 0;
    } else {
        for (int y = 0; y < t->g.rows; y++)
            ERASE_ROW(y);
    }
    #undef ERASE_ROW
}

static void erase_line(Term *t, int mode)
{
    if (mode == 0)
        erase_cells(t, t->cy, t->cx, t->g.cols - 1);
    else if (mode == 1)
        erase_cells(t, t->cy, 0, t->cx);
    else
        erase_cells(t, t->cy, 0, t->g.cols - 1);
    /* see erase_display: an erased line no longer provably continues on the next row */
    t->g.wrap[t->g.scroll_off + t->cy] = 0;
}

static void insert_lines(Term *t, int n)
{
    if (t->cy < t->top || t->cy > t->bot)
        return;
    Grid *g = &t->g;
    cluster_invalidate(t);
    int so = g->scroll_off;
    for (int k = 0; k < n; k++) {
        /* Right-rotate [cy..bot]: every line moves down one slot, the region's
         * bottom line leaves the region and its buffer lands at cy, where it is
         * cleared to become the inserted blank line. scroll_off is untouched: IL
         * discards the departing line rather than pushing it into scrollback. */
        rot_right(g->row + so + t->cy, g->wrap + so + t->cy, t->bot - t->cy + 1);
        grid_blank_slot(g, so + t->cy, t->pen.bg);
    }
}

static void delete_lines(Term *t, int n)
{
    if (t->cy < t->top || t->cy > t->bot)
        return;
    Grid *g = &t->g;
    cluster_invalidate(t);
    int so = g->scroll_off;
    for (int k = 0; k < n; k++) {
        /* Left-rotate [cy..bot]: every line moves up one slot, the deleted line's
         * buffer lands at the region bottom, where it is cleared to become the new
         * bottom line. scroll_off is untouched. */
        rot_left(g->row + so + t->cy, g->wrap + so + t->cy, t->bot - t->cy + 1);
        grid_blank_slot(g, so + t->bot, t->pen.bg);
    }
}

/* A blank cell for ICH/DCH shifts. { 0 } zeroes ch/attr/extra/nextra; GCC does not warn on
   the canonical zero initializer, unlike the old { 0, fg, bg, 0 } literal, which left the new
   cluster fields' initializer implicit. */
static Cell blank_cell(Term *t)
{
    Cell c = { 0 };
    c.fg = t->pen.fg;
    c.bg = t->pen.bg;
    return c;
}

static void set_cursor_pos(Term *t, int row, int col)
{
    int top = t->top, bot = t->bot;
    int r = iclamp_(row, top, bot);
    t->cx = iclamp_(col, 0, t->g.cols - 1);
    t->cy = r;
    t->wrap_next = 0;
    cluster_invalidate(t);
}

static void csi_dispatch(Term *t, int fin)
{
    cluster_invalidate(t);   /* any CSI may move the cursor or the rows */
    /* ps->npar is the INDEX of the last parameter (npar == 0 means exactly one
     * parameter); ps->par[k] is already normalized to 0 for omitted/empty params. */
    int def1 = t->ps.par[0];            /* raw first parameter, VT default NOT substituted (ED/EL, DSR) */
    int p1 = t->ps.par[0] ? iclamp_(t->ps.par[0], 0, 65535) : 1;   /* VT default 1 */
    int p2 = t->ps.par[1] ? iclamp_(t->ps.par[1], 0, 65535) : 1;   /* VT default 1 */

    /* Queue a response string to be written back to the PTY by main.c. */
    #define TERM_RESP(t_, fmt_, ...) do { \
        if ((t_)->resp_len < (int)sizeof((t_)->resp) - 64) { \
            (t_)->resp_len += snprintf((t_)->resp + (t_)->resp_len, \
                sizeof((t_)->resp) - (t_)->resp_len, fmt_, ##__VA_ARGS__); \
        } \
    } while(0)

    if (t->ps.priv) {
        switch (fin) {
        case 'h':
            for (int i = 0; i <= t->ps.npar; i++) {
                int p = t->ps.par[i];
                if (p == 25) t->show_cursor = 1;
                else if (p == 47 || p == 1047) alt_screen_enter(t, 0);
                else if (p == 1049) alt_screen_enter(t, 1);
                else if (p == 1048) { t->alt_saved_cx = t->cx; t->alt_saved_cy = t->cy; }
                else if (p == 1000) t->mouse_mode |= MOUSE_PRESS;
                else if (p == 1002) t->mouse_mode |= MOUSE_DRAG;
                else if (p == 1006) t->mouse_mode |= MOUSE_SGR;
                else if (p == 2004) t->bracketed_paste = 1;
            }
            break;
        case 'l':
            for (int i = 0; i <= t->ps.npar; i++) {
                int p = t->ps.par[i];
                if (p == 25) t->show_cursor = 0;
                else if (p == 47 || p == 1047) alt_screen_leave(t, 0);
                else if (p == 1049) alt_screen_leave(t, 1);
                else if (p == 1048) {
                    t->cx = iclamp_(t->alt_saved_cx, 0, t->g.cols - 1);
                    t->cy = iclamp_(t->alt_saved_cy, 0, t->g.rows - 1);
                    t->wrap_next = 0;
                }
                else if (p == 1000) t->mouse_mode &= ~MOUSE_PRESS;
                else if (p == 1002) t->mouse_mode &= ~MOUSE_DRAG;
                else if (p == 1006) t->mouse_mode &= ~MOUSE_SGR;
                else if (p == 2004) t->bracketed_paste = 0;
            }
            break;
        case 'c': /* DA1 private query (ESC[?c) */
            TERM_RESP(t, "\x1b[?64;1;2;4;6;7;12;16;18;22c");
            break;
        default: break;   /* unknown private modes ignored */
        }
        return;
    }

    switch (fin) {
    case 'A': t->cy = iclamp_(t->cy - p1, t->top, t->bot); t->wrap_next = 0; break;
    case 'B': t->cy = iclamp_(t->cy + p1, t->top, t->bot); t->wrap_next = 0; break;
    case 'C': t->cx = imin_(t->cx + p1, t->g.cols - 1); t->wrap_next = 0; break;
    case 'D': t->cx = imax_(t->cx - p1, 0); t->wrap_next = 0; break;
    case 'E': t->cy = iclamp_(t->cy + p1, t->top, t->bot); t->cx = 0; t->wrap_next = 0; break;
    case 'F': t->cy = iclamp_(t->cy - p1, t->top, t->bot); t->cx = 0; t->wrap_next = 0; break;
    case 'G': case '`': t->cx = iclamp_(p1 - 1, 0, t->g.cols - 1); t->wrap_next = 0; break;
    case 'd': t->cy = iclamp_(p1 - 1, t->top, t->bot); t->wrap_next = 0; break;
    case 'H': case 'f': set_cursor_pos(t, p1 - 1, p2 - 1); break;
    case 'J': erase_display(t, def1); break;
    case 'K': erase_line(t, def1); break;
    case 'L': insert_lines(t, imax_(p1, 1)); break;
    case 'M': delete_lines(t, imax_(p1, 1)); break;
    case 'P': /* DCH: delete chars */ {
        Cell *r = grid_vprow(&t->g, t->cy);
        int n = imax_(p1, 1);
        for (int x = t->cx; x < t->g.cols; x++) {
            int src = x + n;
            r[x] = src < t->g.cols ? r[src] : blank_cell(t);
        }
        break;
    }
    case '@': /* IL: insert chars */ {
        Cell *r = grid_vprow(&t->g, t->cy);
        int n = imax_(p1, 1);
        for (int x = t->g.cols - 1; x >= t->cx; x--) {
            int src = x - n;
            r[x] = src >= t->cx ? r[src] : blank_cell(t);
        }
        break;
    }
    case 'X': /* erase chars */
        erase_cells(t, t->cy, t->cx, t->cx + imax_(p1, 1) - 1);
        break;
    case 'S': /* SU */ { int bot = t->bot; for (int k = 0; k < imax_(p1, 1); k++) scroll_up(t, t->top, bot); break; }
    case 'T': /* SD */ { for (int k = 0; k < imax_(p1, 1); k++) scroll_down(t, t->top, t->bot); break; }
    case 'r': {
        int top = iclamp_(p1 - 1, 0, t->g.rows - 1);
        /* an omitted second parameter means the bottom row */
        int bot = t->ps.par[1] ? iclamp_(t->ps.par[1] - 1, 0, t->g.rows - 1)
                               : t->g.rows - 1;
        if (top < bot) {
            t->top = top;
            t->bot = bot;
        }
        set_cursor_pos(t, 0, 0);
        break;
    }
    case 's': t->save_cx = t->cx; t->save_cy = t->cy; break;
    case 'u': set_cursor_pos(t, t->save_cy, t->save_cx); break;
    case 'm': {
        for (int i = 0; i <= t->ps.npar; i++) {
            int p = t->ps.par[i];
            switch (p) {
            case 0: pen_default(t); break;
            case 1: t->pen.attr |= ATTR_BOLD; break;
            case 2: t->pen.attr |= ATTR_DIM; break;
            case 3: t->pen.attr |= ATTR_ITALIC; break;
            case 4: t->pen.attr |= ATTR_UNDERLINE; break;
            case 7: t->pen.attr |= ATTR_INVERSE; break;
            case 8: t->pen.attr |= ATTR_HIDDEN; break;
            case 9: t->pen.attr |= ATTR_STRIKE; break;
            case 21: case 22: t->pen.attr &= ~(uint32_t)(ATTR_BOLD | ATTR_DIM); break;
            case 23: t->pen.attr &= ~(uint32_t)ATTR_ITALIC; break;
            case 24: t->pen.attr &= ~(uint32_t)ATTR_UNDERLINE; break;
            case 27: t->pen.attr &= ~(uint32_t)ATTR_INVERSE; break;
            case 28: t->pen.attr &= ~(uint32_t)ATTR_HIDDEN; break;
            case 29: t->pen.attr &= ~(uint32_t)ATTR_STRIKE; break;
            case 38: {
                if (i + 1 <= t->ps.npar && t->ps.par[i + 1] == 2) {
                    /* 38;2;r;g;b */
                    if (i + 4 <= t->ps.npar) {
                        int r = iclamp_(t->ps.par[i + 2], 0, 255);
                        int g = iclamp_(t->ps.par[i + 3], 0, 255);
                        int b = iclamp_(t->ps.par[i + 4], 0, 255);
                        t->pen.fg = 0xff000000u | (uint32_t)(r << 16 | g << 8 | b);
                        i += 4;
                    }
                } else if (i + 2 <= t->ps.npar && t->ps.par[i + 1] == 5) {
                    /* 38;5;idx */
                    int idx = t->ps.par[i + 2];
                    if (idx >= 0 && idx < 256)
                        t->pen.fg = t->palette[idx];
                    i += 2;
                } else if (i + 1 <= t->ps.npar) {
                    /* 38;idx (shorthand) */
                    int idx = t->ps.par[i + 1];
                    if (idx >= 0 && idx < 256)
                        t->pen.fg = t->palette[idx];
                    i += 1;
                }
                break;
            }
            case 48: {
                if (i + 1 <= t->ps.npar && t->ps.par[i + 1] == 2) {
                    /* 48;2;r;g;b */
                    if (i + 4 <= t->ps.npar) {
                        int r = iclamp_(t->ps.par[i + 2], 0, 255);
                        int g = iclamp_(t->ps.par[i + 3], 0, 255);
                        int b = iclamp_(t->ps.par[i + 4], 0, 255);
                        t->pen.bg = 0xff000000u | (uint32_t)(r << 16 | g << 8 | b);
                        i += 4;
                    }
                } else if (i + 2 <= t->ps.npar && t->ps.par[i + 1] == 5) {
                    /* 48;5;idx */
                    int idx = t->ps.par[i + 2];
                    if (idx >= 0 && idx < 256)
                        t->pen.bg = t->palette[idx];
                    i += 2;
                } else if (i + 1 <= t->ps.npar) {
                    /* 48;idx (shorthand) */
                    int idx = t->ps.par[i + 1];
                    if (idx >= 0 && idx < 256)
                        t->pen.bg = t->palette[idx];
                    i += 1;
                }
                break;
            }
            case 39: t->pen.fg = DEF_FG; break;
            case 49: t->pen.bg = DEF_BG; break;
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                t->pen.fg = t->palette[p - 90 + 8];
                break;
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                t->pen.bg = t->palette[p - 100 + 8];
                break;
            default:
                if (p >= 30 && p <= 37)
                    t->pen.fg = t->palette[p - 30];
                else if (p >= 40 && p <= 47)
                    t->pen.bg = t->palette[p - 40];
                break;
            }
        }
        break;
    }
    case 'h': {
        for (int i = 0; i <= t->ps.npar; i++)
            if (t->ps.par[i] == 20)
                t->newline_mode = 1;
        break;
    }
    case 'l': {
        for (int i = 0; i <= t->ps.npar; i++)
            if (t->ps.par[i] == 20)
                t->newline_mode = 0;
        break;
    }
    case 'n':
        if (def1 == 6) {
            /* Cursor Position Report (CPR) query: ESC[6n -> respond ESC[{row};{col}R */
            TERM_RESP(t, "\x1b[%d;%dR", t->cy + 1, t->cx + 1);
        }
        break;
    case 'c':
        if (t->ps.gt) {
            /* DA2 (Secondary Device Attributes): ESC[>c */
            TERM_RESP(t, "\x1b[>0;0;0c");
        } else {
            /* DA1 (Primary Device Attributes): ESC[c or ESC[0c */
            TERM_RESP(t, "\x1b[?64;1;2;4;6;7;12;16;18;22c");
        }
        break;
    default:
        break;   /* unimplemented CSI ignored */
    }
    #undef TERM_RESP
}

static void osc_end(Parser *ps)
{
    Term *t = ps->t;
    ps->osc[ps->osc_len] = 0;
    int cmd = -1;
    char *start = ps->osc;
    if (!start[0])
        return;
    char *semi = strchr(start, ';');
    const char *s;                      /* the title text: `start` when there is no numeric prefix */
    if (semi) {
        cmd = atoi(start);
        s = semi + 1;
    } else {
        cmd = atoi(start);
        s = "";
    }
    if (cmd == 0 || cmd == 1 || cmd == 2) {
        snprintf(t->title, sizeof(t->title), "%s", s);
    }
}

void term_parse(Term *t, const uint8_t *buf, size_t len)
{
    Parser *ps = &t->ps;
    ps->t = t;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = buf[i];

        switch (ps->state) {

        case PS_GROUND:
            if (b == 0x1b) { ps->state = PS_ESC; continue; }
            if (b == 0x07) continue;                      /* BEL */
            if (b == 0x08) { if (t->cx > 0) t->cx--; cluster_invalidate(t); t->wrap_next = 0; continue; }
            if (b == 0x09) {                              /* HT */
                int next = ((t->cx / 8) + 1) * 8;
                t->cx = imin_(next, t->g.cols - 1);
                cluster_invalidate(t);
                t->wrap_next = 0;
                continue;
            }
            if (b == 0x0a || b == 0x0b || b == 0x0c) { linefeed(t); continue; }
            if (b == 0x0d) { t->cx = 0; cluster_invalidate(t); t->wrap_next = 0; continue; }
            if (b < 0x20) continue;
            if (b < 0x80) { putch(t, b); continue; }
            if (b >= 0xc2 && b <= 0xdf) { ps->utf8_cp = b & 0x1f; ps->utf8_need = 1; ps->state = PS_UTF8; continue; }
            if (b >= 0xe0 && b <= 0xef) { ps->utf8_cp = b & 0x0f; ps->utf8_need = 2; ps->state = PS_UTF8; continue; }
            if (b >= 0xf0 && b <= 0xf4) { ps->utf8_cp = b & 0x07; ps->utf8_need = 3; ps->state = PS_UTF8; continue; }
            continue;   /* invalid UTF-8 byte */

        case PS_UTF8:
            if ((b & 0xc0) == 0x80) {
                ps->utf8_cp = (ps->utf8_cp << 6) | (b & 0x3f);
                if (--ps->utf8_need == 0) {
                    if (ps->utf8_cp < 0x80 || (ps->utf8_cp >= 0xD800 && ps->utf8_cp <= 0xDFFF) || ps->utf8_cp > 0x10FFFF) {
                        /* overlong / surrogate / out-of-range: drop */
                    } else {
                        putch(t, (uint32_t)ps->utf8_cp);
                    }
                    ps->state = PS_GROUND;
                }
            } else {
                ps->state = PS_GROUND;
                i--;   /* reprocess this byte in ground state */
            }
            continue;

        case PS_ESC:
            ps->state = PS_GROUND;
            switch (b) {
            case '[': ps->state = PS_CSI; ps->npar = 0;
                      ps->priv = 0; ps->bang = 0; ps->gt = 0;
                      for (int k = 0; k < 32; k++) ps->par[k] = -1;  /* -1 = not seen yet */
                      break;
            case ']': ps->state = PS_OSC; ps->osc_len = 0; break;
            case 'D': linefeed(t); break;
            case 'M': cluster_invalidate(t);
                      if (t->cy == t->top) scroll_down(t, t->top, t->bot); else if (t->cy > 0) t->cy--; break;
            case 'E': linefeed(t); t->cx = 0; break;
            case '7': t->save_cx = t->cx; t->save_cy = t->cy; break;
            case '8': set_cursor_pos(t, t->save_cy, t->save_cx); break;
            case 'c': term_init(t, t->g.rows, t->g.cols, t->g.scrollback_max); break;
            case '(': case ')': case '*': case '+': ps->state = PS_IGNORE; break;
            default: break;
            }
            continue;

        case PS_IGNORE:
            ps->state = PS_GROUND;   /* consume one charset-designator byte */
            continue;

        case PS_CSI:
            if (b >= '0' && b <= '9') {
                if (ps->npar < 32) {
                    if (ps->par[ps->npar] < 0)
                        ps->par[ps->npar] = b - '0';
                    else if (ps->par[ps->npar] < 100000)
                        ps->par[ps->npar] = ps->par[ps->npar] * 10 + (b - '0');
                }
                continue;
            }
            if (b == ';') {
                if (ps->npar < 31) {
                    ps->npar++;
                    ps->par[ps->npar] = -1;   /* param omitted unless digits follow */
                }
                continue;
            }
            if (b == '?') { ps->priv = 1; continue; }
            if (b == '!') { ps->bang = 1; continue; }
            if (b == '>') { ps->gt = 1; continue; }
            if (b >= ' ' && b <= '/') { continue; }   /* intermediate bytes: ignored */
            if (b >= 0x40 && b <= 0x7e) {
                for (int k = 0; k < 32; k++)
                    if (ps->par[k] < 0)
                        ps->par[k] = 0;   /* omitted/empty params default to 0 */
                csi_dispatch(t, b);
                ps->state = PS_GROUND;
                continue;
            }
            /* C0 inside CSI: execute it, leave CSI (simple, safe) */
            ps->state = PS_GROUND;
            i--;
            continue;

        case PS_OSC:
            if (b == 0x07) { osc_end(ps); ps->state = PS_GROUND; continue; }
            if (b == 0x1b) {
                /* peek next byte for ST (ESC \) */
                if (i + 1 < len && buf[i + 1] == '\\') {
                    i++;
                    osc_end(ps);
                    ps->state = PS_GROUND;
                } else {
                    osc_end(ps);
                    ps->state = PS_ESC;
                }
                continue;
            }
            if (ps->osc_len < (int)sizeof(ps->osc) - 1)
                ps->osc[ps->osc_len++] = (char)b;
            continue;

        default:
            ps->state = PS_GROUND;
            continue;
        }
    }
}
