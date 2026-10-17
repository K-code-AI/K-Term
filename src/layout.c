/* Kai-Term — layout: how one window's pixels are shared between the tab bar and the sessions.

   Pure geometry on purpose (no Wayland, no EGL, no file descriptors), so the whole tab-bar /
   side-by-side feature is checkable headlessly in tests/test_vt.c — the only kind of evidence this
   project can rely on for layout, since the user verifies anything visual themselves.

   One function decides everything about where things are drawn, for the same reason term_grid_geometry()
   exists: the drawn cell, the pointer -> cell mapping and the PTY/grid size MUST agree, and every extra
   copy of that arithmetic is a chance for them to drift apart. main.c calls layout_compute() once per
   geometry change, resizes every displayed session to its pane's cols/rows, hit-tests the pointer with
   layout_hit_bar()/layout_hit_pane(), and hands the result to render_draw(), which draws exactly these
   rectangles and nothing else.

   Rules the implementation follows (each is checked in tests/test_vt.c):
   * Config.padding is the margin around EVERYTHING (all four sides), exactly as it was for a single grid.
     With the bar off and one pane the numbers are identical to what term_grid_geometry() used to return
     for the whole window, so a single-session window cannot change shape.
   * The bar is one cell row tall and sits inside the top margin; `gap` px separate it from the grid, and
     the panes from each other.
   * Leftover pixels that do not fill a whole cell stay at the RIGHT and BOTTOM of their pane — the same
     rule the single grid uses, and the reason `col = (ptr_x - pane.x) / cell_w` names the drawn cell.
   * The terminal wins over the chrome: if the bar would leave no room for one cell row, the bar is
     dropped for that frame rather than shrinking the grid to nothing.
   * In side-by-side mode at most split_max panes are shown; when there are more sessions than that, the
     shown window of tabs slides to contain the active one.
   * With the file manager on, the grid area is split HORIZONTALLY first: a band of fm_rows cell rows on
     top (browser left, preview right) and the terminal pane(s) below it. The sessions stay ordinary
     entries in pane[], only shorter, so every hit test, focus border and pointer -> cell mapping works
     whether the band is on or off. If the window cannot hold the band AND FM_TERM_MIN_ROWS of terminal,
     the band is dropped for that frame — the terminal wins again. */
#include "kai-term.h"

/* Cut a string to at most `max` UTF-8 codepoints, never in the middle of a multi-byte sequence. A tab's
   title comes from OSC 0/2 and may be UTF-8; splitting a sequence would make the bar draw garbage.
   Continuation bytes (0x80..0xBF) belong to the codepoint already counted, so the walk only counts lead
   bytes and can never land inside a sequence. */
static void clip_utf8(char *s, int max)
{
    int n = 0;
    for (char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x80 && c < 0xc0)
            continue;
        if (n >= max) {
            *p = 0;
            return;
        }
        n++;
    }
}

/* "<n> <title>", with a '*' before the title when the session is hidden and has output the user has not
   seen yet. An unnamed session reads "shell" — every shell sets its own title (OSC 0/2) as soon as it
   prints one, so that is only the startup state. */
static void bar_label(char *out, int tab, int attention, const char *title, int maxcols)
{
    snprintf(out, BAR_LABEL_MAX, "%d %s%s", tab + 1, attention ? "*" : "",
             (title && title[0]) ? title : "shell");
    clip_utf8(out, maxcols > 1 ? maxcols : 1);
}

void layout_compute(const LayoutReq *rq, Layout *out)
{
    memset(out, 0, sizeof(*out));

    int cw = imax_(rq->cell_w, 1), ch = imax_(rq->cell_h, 1);
    int ww = imax_(rq->win_w, 1), wh = imax_(rq->win_h, 1);
    int pad = imax_(rq->pad, 0);
    int gap = imax_(rq->gap, 0);
    int ntab = iclamp_(rq->ntab, 0, TAB_MAX);
    int active = (rq->active >= 0 && rq->active < ntab) ? rq->active : (ntab ? 0 : -1);
    int smax = iclamp_(rq->split_max < 1 ? 1 : rq->split_max, 1, TAB_MAX);
    out->mode = (rq->mode == LAYOUT_SPLIT) ? LAYOUT_SPLIT : LAYOUT_TABS;
    out->active_tab = active;   /* the renderer highlights its item and borders its pane */

    /* A margin may never eat the last whole cell — the same cap term_grid_geometry() applies, done here
       because the bar has to fit inside what is left. */
    int padx = imin_(pad, imax_((ww - cw) / 2, 0));
    int pady = imin_(pad, imax_((wh - ch) / 2, 0));

    int bar_on = 0;
    if (ntab > 0 && rq->bar != BAR_OFF)
        bar_on = (rq->bar == BAR_ALWAYS) ? 1 : (ntab >= 2 ? 1 : 0);

    int grid_x = padx, grid_y = pady;
    int grid_w = ww - 2 * padx, grid_h = wh - 2 * pady;
    if (bar_on) {
        grid_y = pady + ch + gap;
        grid_h = wh - grid_y - pady;
        if (grid_h < ch)
            bar_on = 0;      /* no room for the bar AND one text row: keep the text row */
    }
    if (!bar_on) {
        grid_y = pady;
        grid_h = wh - 2 * pady;
    }
    if (grid_w < cw)
        grid_w = cw;
    if (grid_h < ch)
        grid_h = ch;

    out->bar_on = bar_on;
    if (bar_on) {
        out->bar_x = grid_x;
        out->bar_y = pady;
        out->bar_w = grid_w;
        out->bar_h = ch;
        out->nitem = ntab;
        /* Equal-width items; the pixels left over after whole items stay at the right, like the
           whole-cell remainder of a grid. */
        int item_w = imax_(grid_w / ntab, 1);
        int maxcols = imax_(item_w / cw, 1);
        for (int i = 0; i < ntab; i++) {
            BarItem *it = &out->item[i];
            it->x = grid_x + i * item_w;
            it->w = item_w;
            it->tab = rq->tab[i].tab;
            it->attention = rq->tab[i].attention ? 1 : 0;
            it->active = (it->tab == active) ? 1 : 0;
            bar_label(it->label, it->tab, it->attention, rq->tab[i].title, maxcols);
        }
    }

    /* ---- the file-manager band, above the terminal pane(s) ----
       The band takes fm_rows cell rows of the grid area and the sessions keep the rest. The terminal
       stays a normal PaneGeom in pane[], only shorter, which is what lets every existing hit test, the
       focus border and W.gx/W.gy keep working with the file manager on AND off. */
    int pane_y = grid_y, pane_h = grid_h;
    if (rq->fm_on) {
        int want = iclamp_(imax_(rq->fm_rows, FM_ROWS_MIN), FM_ROWS_MIN, FM_ROWS_MAX);
        int band_h = want * ch;
        /* The shell keeps at least FM_TERM_MIN_ROWS rows. If that leaves less than a usable band, the
           band is dropped for this frame rather than starving the terminal — the bar's rule again. */
        int room = grid_h - gap - FM_TERM_MIN_ROWS * ch;
        if (band_h > room)
            band_h = room;
        if (band_h >= FM_ROWS_MIN * ch && grid_w >= 2 * cw) {
            FmGeom *fg = &out->fm;
            fg->on = 1;
            fg->list_x = grid_x;
            fg->list_y = grid_y;
            fg->list_h = band_h;
            /* A preview narrower than a couple of cells is worse than no preview, so the whole band
               becomes the browser. The remainder of the half-split stays on the RIGHT of the band,
               like every other whole-cell remainder. */
            int prev_on = rq->fm_preview_on && grid_w >= 3 * cw + gap;
            fg->list_w = prev_on ? (grid_w - gap) / 2 : grid_w;
            fg->prev_on = prev_on;
            int xo, yo;
            term_grid_geometry(fg->list_w, fg->list_h, cw, ch, 0,
                               &fg->list_cols, &fg->list_rows, &xo, &yo);
            if (prev_on) {
                fg->prev_w = grid_w - gap - fg->list_w;
                fg->prev_x = grid_x + fg->list_w + gap;
                fg->prev_y = grid_y;
                fg->prev_h = band_h;
                term_grid_geometry(fg->prev_w, fg->prev_h, cw, ch, 0,
                                   &fg->prev_cols, &fg->prev_rows, &xo, &yo);
            }
            pane_y = grid_y + band_h + gap;
            pane_h = grid_h - band_h - gap;
        }
    }

    /* Which sessions get a pane, and how wide each pane is. */
    int npane = 0, start = 0, pane_w = grid_w;
    if (ntab > 0) {
        if (out->mode == LAYOUT_TABS) {
            npane = 1;
            start = imax_(active, 0);
            pane_w = grid_w;
        } else {
            npane = imin_(ntab, smax);
            /* Slide the shown window so it contains the active session: centred on it when possible,
               clamped at both ends so the first/last sessions really are reachable. */
            start = imin_(imax_(active - npane / 2, 0), imax_(ntab - npane, 0));
            pane_w = imax_((grid_w - (npane - 1) * gap) / npane, 1);
        }
    }
    out->npane = npane;
    for (int i = 0; i < npane; i++) {
        PaneGeom *pg = &out->pane[i];
        pg->x = grid_x + i * (pane_w + gap);
        pg->y = pane_y;
        pg->w = pane_w;
        pg->h = pane_h;
        pg->tab = start + i;
        /* No inner margin inside a pane: `gap` already separates the panes, and Config.padding is the
           window's own margin. xoff/yoff are therefore 0 and pane.x/pane.y ARE the grid origin, which is
           what main.c stores for the pointer mapping of that session. */
        int xoff, yoff;
        term_grid_geometry(pg->w, pg->h, cw, ch, 0, &pg->cols, &pg->rows, &xoff, &yoff);
    }
}

int layout_hit_bar(const Layout *l, int px, int py)
{
    if (!l->bar_on || py < l->bar_y || py >= l->bar_y + l->bar_h)
        return -1;
    for (int i = 0; i < l->nitem; i++)
        if (px >= l->item[i].x && px < l->item[i].x + l->item[i].w)
            return i;
    return -1;      /* the remainder at the right end of the strip belongs to no tab */
}

int layout_hit_pane(const Layout *l, int px, int py)
{
    for (int i = 0; i < l->npane; i++) {
        const PaneGeom *pg = &l->pane[i];
        if (px >= pg->x && px < pg->x + pg->w && py >= pg->y && py < pg->y + pg->h)
            return i;
    }
    return -1;      /* margin, gap, or below the last row */
}

int layout_hit_fm(const Layout *l, int px, int py)
{
    const FmGeom *fg = &l->fm;
    if (!fg->on)
        return FM_HIT_NONE;
    if (px >= fg->list_x && px < fg->list_x + fg->list_w &&
        py >= fg->list_y && py < fg->list_y + fg->list_h)
        return FM_HIT_LIST;
    if (fg->prev_on && px >= fg->prev_x && px < fg->prev_x + fg->prev_w &&
        py >= fg->prev_y && py < fg->prev_y + fg->prev_h)
        return FM_HIT_PREVIEW;
    return FM_HIT_NONE;      /* the gap between the two panes, or below the band */
}

int layout_pane_of_tab(const Layout *l, int tab)
{
    for (int i = 0; i < l->npane; i++)
        if (l->pane[i].tab == tab)
            return i;
    return -1;
}

void layout_grid_size(const Layout *l, int *cols, int *rows)
{
    if (l->npane <= 0) {          /* no session is drawn: there is no grid to size */
        *cols = 0;
        *rows = 0;
        return;
    }
    /* Every pane of one layout has the same rect height, and in split mode the same width (the width is
       divided equally, the remainder going to the right of the LAST pane), so pane[0] describes the grid
       size of every session this window holds — including the ones not drawn right now. */
    *cols = l->pane[0].cols;
    *rows = l->pane[0].rows;
}

/* The window size to ask the compositor for at startup. Kept next to layout_compute() because it is its
   inverse for this one case: the grid must fit the window that layout_compute() will later measure, and the
   bar costs one cell ROW plus the gap — cell_h, never cell_w. The file-manager band costs fm_rows rows plus
   one more gap, for the same reason: layout_compute() subtracts exactly that from the grid area, so feeding
   this back into it returns the configured cols x rows for the TERMINAL pane. tests/test_vt.c does exactly
   that round trip, with the band on and off. */
void layout_initial_size(int cols, int rows, int cell_w, int cell_h, int pad, int gap, int bar,
                         int fm_on, int fm_rows, int *win_w, int *win_h)
{
    int cw = imax_(cell_w, 1), ch = imax_(cell_h, 1);
    int p  = imax_(pad, 0), g = imax_(gap, 0);

    *win_w = imax_(cols, 1) * cw + 2 * p;
    *win_h = imax_(rows, 1) * ch + 2 * p;
    /* BAR_AUTO shows nothing with the single starting session, so it needs no extra height here. */
    if (bar == BAR_ALWAYS)
        *win_h += ch + g;
    if (fm_on)
        *win_h += iclamp_(imax_(fm_rows, FM_ROWS_MIN), FM_ROWS_MIN, FM_ROWS_MAX) * ch + g;
}
