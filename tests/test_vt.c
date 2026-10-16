/* Kai-Term — unit tests for VT parser + grid (no display needed). */
#include "kai-term.h"

static Term T;
static int fails, checks;

#define CHECK(cond, msg) do {                                  \
    checks++;                                                  \
    if (!(cond)) {                                             \
        fails++;                                               \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);  \
    }                                                          \
} while (0)

static void feed(const char *s)
{
    term_parse(&T, (const uint8_t *)s, strlen(s));
}

/* Fill a dead frame with non-zero pointers so a Term declared on the stack afterwards cannot
   accidentally look zero-initialized (see the term_init guard test at the end of main). */
__attribute__((noinline)) static void dirty_stack(void)
{
    volatile void *junk[512];
    for (int i = 0; i < 512; i++)
        junk[i] = (void *)(long)(0x1000 + 8 * i);
    (void)junk;   /* the writes are the point; silence -Wunused-but-set-variable */
}

static uint32_t cellch(int y, int x) { return grid_vprow(&T.g, y)[x].ch; }
static uint32_t cellfg(int y, int x) { return grid_vprow(&T.g, y)[x].fg; }
static uint32_t cellbg(int y, int x) { return grid_vprow(&T.g, y)[x].bg; }
static uint32_t cellattr(int y, int x) { return grid_vprow(&T.g, y)[x].attr; }
static uint8_t cellnextra(int y, int x) { return grid_vprow(&T.g, y)[x].nextra; }
static uint32_t cellextra(int y, int x, int k) { return grid_vprow(&T.g, y)[x].extra[k]; }

static void row_text(int y, char *out)
{
    for (int x = 0; x < T.g.cols; x++) {
        uint32_t c = cellch(y, x);
        out[x] = c ? (c < 0x80 ? (char)c : '?') : ' ';
    }
    out[T.g.cols] = 0;
}

/* ---------- reflow helpers ---------- */

/* The LOGICAL lines of the content slots: physical rows joined through the soft-wrap marks.
   This is what the user reads as "a line", so it is the thing a width change must preserve.
   Blank lines are skipped (they carry no text to compare). */
static int logical_lines(char out[32][512])
{
    int used = T.g.scroll_off + T.g.rows, n = 0;
    for (int i = 0; i < used && n < 32; ) {
        int len = 0, j = i;
        for (;;) {
            Cell *r = T.g.row[j];
            int end = T.g.cols;
            while (end > 0 && r[end - 1].ch == 0)
                end--;
            for (int x = 0; x < end && len < 500; x++) {
                uint32_t c = r[x].ch;
                out[n][len++] = (c && c < 0x80) ? (char)c : (c ? '?' : ' ');
            }
            if (j + 1 < used && T.g.wrap[j])
                j++;
            else
                break;
        }
        out[n][len] = 0;
        if (len > 0)
            n++;
        i = j + 1;
    }
    return n;
}

/* slots that actually hold something (blank rows are free space or blank lines) */
static int content_rows(void)
{
    int c = 0;
    for (int i = 0; i < T.g.scroll_off + T.g.rows; i++) {
        Cell *r = T.g.row[i];
        int any = 0;
        for (int x = 0; x < T.g.cols; x++)
            if (r[x].ch || (r[x].attr & ATTR_WIDE))
                any = 1;
        if (any)
            c++;
    }
    return c;
}

static int wide_cells(void)      /* ATTR_WIDE continuation cells anywhere in the grid */
{
    int c = 0;
    for (int i = 0; i < T.g.rcap; i++)
        for (int x = 0; x < T.g.cols; x++)
            if (T.g.row[i][x].attr & ATTR_WIDE)
                c++;
    return c;
}

static int wide_orphans(void)    /* continuation cells separated from their anchor */
{
    int o = 0;
    for (int i = 0; i < T.g.rcap; i++) {
        Cell *r = T.g.row[i];
        for (int x = 0; x < T.g.cols; x++)
            if ((r[x].attr & ATTR_WIDE) && (x == 0 || r[x - 1].ch == 0))
                o++;
    }
    return o;
}

/* ---------- layout helpers (the tab bar and the side-by-side panes) ---------- */

/* A LayoutReq for `ntab` sessions. Titles start empty (so a label reads "<n> shell"); individual checks
   overwrite rq->tab[i].title / .attention. */
static void lreq(LayoutReq *rq, int mode, int ww, int wh, int cw, int ch, int pad, int gap,
                 int ntab, int active, int split_max, int bar)
{
    memset(rq, 0, sizeof(*rq));
    rq->mode = mode;
    rq->win_w = ww; rq->win_h = wh;
    rq->cell_w = cw; rq->cell_h = ch;
    rq->pad = pad; rq->gap = gap;
    rq->ntab = ntab; rq->active = active;
    rq->split_max = split_max; rq->bar = bar;
    for (int i = 0; i < ntab && i < TAB_MAX; i++)
        rq->tab[i].tab = i;
}

/* Codepoints, not bytes — a label is only guaranteed to fit its item in COLUMNS, and a column holds one
   codepoint however many bytes it takes. */
static int utf8_len(const char *s)
{
    int n = 0;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x80 && c < 0xc0)
            continue;
        n++;
    }
    return n;
}

int main(void)
{
    term_init(&T, 10, 20, 100);

    /* --- basic printing --- */
    feed("hello");
    CHECK(cellch(0, 0) == 'h' && cellch(0, 4) == 'o', "print hello");
    CHECK(T.cx == 5 && T.cy == 0, "cursor after hello");
    feed("\r\nworld");
    char row[64];
    row_text(1, row);
    CHECK(!memcmp(row, "world", 5), "second line");

    /* --- CUP + ED + EL --- */
    feed("\x1b[3;5H");
    CHECK(T.cx == 4 && T.cy == 2, "CUP 3;5");
    feed("X");
    CHECK(cellch(2, 4) == 'X', "char at CUP");
    feed("\x1b[2J\x1b[H");
    CHECK(cellch(0, 0) == 0 && cellch(2, 4) == 0, "ED 2 clears");
    feed("abcdef\r\x1b[2K");
    row_text(0, row);
    CHECK(row[0] == ' ', "EL 1 clears line start");

    /* --- SGR colors --- */
    feed("\x1b[2J\x1b[H\x1b[31mR\x1b[0m");
    CHECK(cellfg(0, 0) == T.palette[1], "SGR 31 -> palette red");
    feed("\x1b[38;2;10;20;30mT\x1b[0m");
    CHECK(cellfg(0, 1) == 0xff0a141eu, "truecolor fg");
    feed("\x1b[48;5;196mU\x1b[0m");
    CHECK(cellbg(0, 2) == T.palette[196], "256-color bg");
    feed("\x1b[1mB\x1b[0m");
    CHECK(cellattr(0, 3) & ATTR_BOLD, "bold attr");
    CHECK(!(cellattr(0, 4) & ATTR_BOLD), "reset clears bold");

    /* --- scrolling moves the first lines to scrollback ---
     * 12 lines each ending with \r\n: the newlines after line10, line11 and line12 each
     * scroll one line up, so 3 lines end up in scrollback and the bottom viewport row is
     * left blank. */
    feed("\x1b[2J\x1b[H");
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "line%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.scroll_off == 3, "scroll_off == 3 after 12 lines in 10 rows");
    CHECK(T.g.row[0][0].ch == 'l' && T.g.row[0][1].ch == 'i' &&
          T.g.row[0][5].ch == '1', "scrollback row[0] holds line01");
    CHECK(T.g.row[1][0].ch == 'l' && T.g.row[1][5].ch == '2', "scrollback row[1] holds line02");
    CHECK(T.g.row[2][0].ch == 'l' && T.g.row[2][5].ch == '3', "newest scrollback is line03");
    row_text(0, row);
    CHECK(!memcmp(row, "line04", 6), "viewport top is line04");
    CHECK((void *)T.g.row[3] == (void *)grid_vprow(&T.g, 0),
          "viewport starts at row[scroll_off]");

    /* --- scrollback with room: nothing is evicted --- */
    feed("\x1b[2J\x1b[H");
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "A%02d\r\n", i);
        feed(line);
    }
    feed("B13\r\nB14");
    /* ED clears the viewport but does not reset scroll_off, so it keeps accumulating:
     * 3 (previous block) + 3 + 1 = 7. */
    CHECK(T.g.scroll_off == 7, "scroll_off == 7 (ED does not reset scrollback)");
    row_text(0, row);
    CHECK(!memcmp(row, "A05", 3), "viewport top A05");
    Cell *newest = T.g.row[T.g.scroll_off - 1];
    CHECK(newest[0].ch == 'A' && newest[1].ch == '0' && newest[2].ch == '4',
          "newest scrollback line is A04");
    row_text(9, row);
    CHECK(!memcmp(row, "B14", 3), "bottom line is the newest");

    /* --- scrollback full: oldest line evicted, its buffer recycled --- */
    term_destroy(&T);
    term_init(&T, 4, 20, 2);
    for (int i = 1; i <= 6; i++) {
        char line[32];
        snprintf(line, sizeof(line), "A%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.rcap == 6 && T.g.scroll_off == 2, "scrollback full (max 2)");
    row_text(0, row);
    CHECK(!memcmp(row, "A04", 3), "eviction: viewport top is A04");
    CHECK(T.g.row[0][0].ch == 'A' && T.g.row[0][2].ch == '2',
          "oldest kept is A02 (A01 was evicted)");
    CHECK(T.g.row[1][0].ch == 'A' && T.g.row[1][2].ch == '3', "newest scrollback is A03");
    term_destroy(&T);
    term_init(&T, 10, 20, 100);

    /* --- scroll region --- */
    feed("\x1b[2J\x1b[H\x1b[1r");           /* full-screen region */
    feed("\x1b[2;4r");                       /* region rows 2..4 */
    feed("\x1b[2;1H");
    CHECK(T.cy == 1, "cursor at region top after H");
    feed("A\r\nB\r\nC\r\nD");                /* D should scroll within region */
    row_text(1, row);
    CHECK(row[0] == 'B', "region scroll moved A out, B to top of region");
    row_text(3, row);
    CHECK(row[0] == 'D', "D written at region bottom");
    feed("\x1b[1r");

    /* --- scrollback pull-back via scroll down (SD) --- */
    feed("\x1b[2J\x1b[H");
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "L%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.scroll_off == 3, "scroll_off 3 again");
    feed("\x1b[1T");   /* SD: the newest scrollback line returns to the viewport top */
    CHECK(T.g.scroll_off == 2, "SD pulled scrollback back");
    row_text(0, row);
    CHECK(row[0] == 'L' && row[1] == '0' && row[2] == '3', "top line is L03 after SD");
    row_text(9, row);
    CHECK(row[0] == 'L' && row[1] == '1' && row[2] == '2', "SD pushed the blank row out");

    /* --- live_off: the anchor that follows the newest output ---
     * scroll_off is an ABSOLUTE index into row[] (row[0] = oldest retained line), so the live
     * view is at scroll_off == live_off, NOT at scroll_off == 0. Navigation must clamp to
     * [0, live_off] and snap-to-live must restore live_off. These mirror main.c's
     * scrollback_nav()/snap_to_live() math exactly. Fresh grid so row[0] is the first M line. */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "M%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.live_off == 3, "live_off == 3 after 12 lines in 10 rows");
    CHECK(T.g.scroll_off == T.g.live_off, "a printing terminal stays at the live anchor");
    row_text(8, row);
    CHECK(row[0] == 'M' && row[1] == '1' && row[2] == '2', "live view shows the newest line M12");

    /* nav UP into history (main.c scrollback_nav(+5)) must move toward row[0] */
    T.g.scroll_off = iclamp_(T.g.scroll_off - 5, 0, T.g.live_off);
    CHECK(T.g.scroll_off == 0, "nav up 5 from live_off 3 clamps at 0 (oldest kept)");
    row_text(0, row);
    CHECK(row[0] == 'M' && row[1] == '0' && row[2] == '1', "scrolled back: viewport top is M01");

    /* snap to live must land on live_off, not on 0 (the old bug: it showed the oldest lines) */
    T.g.scroll_off = T.g.live_off;
    row_text(8, row);
    CHECK(row[0] == 'M' && row[1] == '1' && row[2] == '2', "snap-to-live shows the newest line M12");

    /* a whole-viewport scroll while scrolled back snaps the view to live first, so the slots
       below the viewport (which hold the newest lines) are never blanked */
    T.g.scroll_off = 0;
    feed("\x1b[10;1H");            /* cursor to the viewport bottom row */
    feed("\r\n");                  /* forces scroll_up over the whole viewport */
    CHECK(T.g.live_off == 4, "live_off advanced with the scroll");
    CHECK(T.g.scroll_off == T.g.live_off, "scroll_up snapped the scrolled-back view to live");
    row_text(7, row);
    CHECK(row[0] == 'M' && row[1] == '1' && row[2] == '2', "newest line still visible after snap");

    /* a fresh terminal has nothing above the live view: live_off == 0 -> no navigation */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("fresh\r\n");
    CHECK(T.g.live_off == 0 && T.g.scroll_off == 0, "no scrollback accumulated yet");
    CHECK(iclamp_(T.g.scroll_off - 5, 0, T.g.live_off) == 0, "cannot scroll into unwritten rows");

    /* resize must keep the live anchor consistent and keep the cursor on the same content line */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    for (int i = 1; i <= 14; i++) {
        char line[32];
        snprintf(line, sizeof(line), "K%02d\r\n", i);
        feed(line);
    }
    CHECK(T.g.scroll_off == 5 && T.g.live_off == 5, "14 lines -> live_off 5");
    T.g.scroll_off = 1;                       /* pretend the user is scrolled back */
    term_resize(&T, 8, 20);
    CHECK(T.g.scroll_off == T.g.live_off, "resize returns the view to the live anchor");
    CHECK(T.g.live_off == 7 && T.g.rows == 8, "shrink 2 rows advances the anchor by 2");
    CHECK(T.cy == 7, "cursor stays on the same content line after shrink");
    row_text(6, row);
    CHECK(row[0] == 'K' && row[1] == '1' && row[2] == '4', "newest line K14 kept after shrink");
    term_resize(&T, 10, 20);
    CHECK(T.g.live_off == 5 && T.g.scroll_off == 5, "grow 2 rows slides the anchor back up by 2");
    CHECK(T.cy == 9, "cursor stays on the same content line after grow");
    row_text(8, row);
    CHECK(row[0] == 'K' && row[1] == '1' && row[2] == '4', "newest line K14 kept after grow");
    term_destroy(&T);
    term_init(&T, 10, 20, 100);

    /* --- UTF-8 --- */
    feed("\x1b[2J\x1b[H");
    feed("aé€\U0001F600");
    CHECK(cellch(0, 0) == 'a', "ascii a");
    CHECK(cellch(0, 1) == 0xE9, "2-byte e-acute");
    CHECK(cellch(0, 2) == 0x20AC, "3-byte euro");
    CHECK(cellch(0, 3) == 0x1F600, "4-byte emoji");
    CHECK(cellattr(0, 4) & ATTR_WIDE, "emoji reserves a second cell (Emoji_Presentation)");

    /* --- wide char --- */
    feed("\x1b[2J\x1b[H");
    feed("\xE4\xB8\xADX");   /* U+4E2D then X */
    CHECK(cellch(0, 0) == 0x4E2D, "CJK char");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "wide continuation cell");
    CHECK(cellch(0, 2) == 'X', "X after wide char");

    /* --- tabs --- */
    feed("\x1b[2J\x1b[H");
    feed("a\tb");
    CHECK(cellch(0, 8) == 'b', "tab to column 8");

    /* --- ICH / DCH (parameter 1 is the count, cursor must be set first) --- */
    feed("\x1b[2J\x1b[H");
    feed("abcd");
    feed("\x1b[1;2H\x1b[2@");                       /* insert 2 blanks at col 2 */
    row_text(0, row);
    CHECK(row[0] == 'a' && row[1] == ' ' && row[2] == ' ' && row[3] == 'b', "ICH");
    feed("\x1b[2J\x1b[Habcd\x1b[1;2H\x1b[2P");      /* delete 2 chars at col 2 */
    row_text(0, row);
    CHECK(row[0] == 'a' && row[1] == 'd', "DCH");

    /* --- IL / DL (parameter 1 is the count) --- */
    feed("\x1b[2J\x1b[H");
    feed("r1\r\nr2\r\nr3");
    feed("\x1b[2;1H\x1b[1L");                       /* insert 1 blank line at row 2 */
    row_text(0, row);
    CHECK(row[0] == 'r' && row[1] == '1', "IL leaves row 1 alone");
    row_text(1, row);
    CHECK(row[0] == ' ', "IL inserts a blank line at the cursor row");
    row_text(2, row);
    CHECK(row[0] == 'r' && row[1] == '2', "IL moved r2 down one row");
    row_text(3, row);
    CHECK(row[0] == 'r' && row[1] == '3', "IL moved r3 down one row");
    feed("\x1b[2J\x1b[Hr1\r\nr2\r\nr3\x1b[1;1H\x1b[1M");   /* delete the line at row 1 */
    row_text(0, row);
    CHECK(row[0] == 'r' && row[1] == '2', "DL removes the first line");
    row_text(1, row);
    CHECK(row[0] == 'r' && row[1] == '3', "DL moved r3 up one row");

    /* --- cursor save/restore --- */
    feed("\x1b[2J\x1b[H");
    feed("\x1b[5;6H\x1b" "7z\x1b" "8Y");
    CHECK(cellch(4, 5) == 'Y', "ESC 7/8 restore rewrote same cell");
    CHECK(cellch(4, 6) == 0, "nothing written past restored cell");

    /* --- wrap --- */
    feed("\x1b[2J\x1b[H");
    for (int i = 0; i < 25; i++) feed("abcdefghij");   /* 250 chars, 20 cols */
    row_text(0, row);
    CHECK(!memcmp(row, "abcdefghijabcdefghij", 20), "autowrap fills row 1");
    row_text(1, row);
    CHECK(row[0] == 'a' && row[1] == 'b', "autowrap continues at the next line");

    /* --- resize keeps the viewport bottom anchored --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    for (int i = 1; i <= 12; i++) {
        char line[32];
        snprintf(line, sizeof(line), "L%02d\r\n", i);
        feed(line);
    }
    feed("tail");                       /* 'tail' sits on the live bottom row */
    term_resize(&T, 5, 10);
    CHECK(T.g.rows == 5 && T.g.cols == 10, "resize dims");
    CHECK(T.g.scroll_off >= 1, "resize pushed content into scrollback");
    row_text(4, row);
    CHECK(row[0] == 't' && row[1] == 'a', "bottom line preserved after shrink");
    term_resize(&T, 10, 20);
    CHECK(T.g.rows == 10 && T.g.cols == 20, "resize back");
    row_text(9, row);
    CHECK(row[0] == 't' && row[1] == 'a', "bottom line still at the bottom after grow");

    /* --- resize with a full scrollback must keep the invariant --- */
    term_destroy(&T);
    term_init(&T, 4, 10, 2);
    feed("1\r\n2\r\n3\r\n4\r\n5\r\n6");
    CHECK(T.g.scroll_off == 2 && T.g.rcap == 6, "small scrollback full");
    term_resize(&T, 6, 10);
    CHECK(T.g.rows == 6 && T.g.scroll_off + T.g.rows <= T.g.rcap,
          "grow: scrollback clamped to rcap");
    CHECK(T.g.scroll_off >= 0 && T.g.row[T.g.scroll_off + 5], "grow: bottom row exists");
    term_destroy(&T);
    term_init(&T, 4, 10, 0);
    feed("1\r\n2\r\n3\r\n4\r\n5\r\n6");
    CHECK(T.g.scroll_off == 0 && T.g.rcap == 4, "no scrollback configured");
    row_text(3, row);
    CHECK(row[0] == '6', "no-scrollback viewport bottom is newest line");
    term_resize(&T, 2, 10);
    CHECK(T.g.scroll_off == 0 && T.g.rows == 2 && T.g.rcap == 4, "shrink without scrollback");
    row_text(1, row);
    CHECK(row[0] == '6', "shrink keeps bottom line (no scrollback)");

    /* --- a height SHRINK must never move the cursor off its own line ---------------------------
       The zoom bug: zooming changes cols AND rows at once. The old shrink always gave
       up the viewport's TOP row, so a shrink larger than the number of rows above the cursor pushed
       the cursor's own line into scrollback; term_resize then clamped cy to 0, the shell repainted
       its prompt at that clamped cursor, and every zoom step left one stale prompt and a blank gap.
       The rule that fixes it: give up the rows BELOW the cursor first. */
    term_destroy(&T);
    term_init(&T, 20, 20, 100);
    feed("\r\nPROMPT-ONE\r\nPROMPT-TWO > ");     /* a two-line prompt, cursor on viewport row 2 */
    CHECK(T.cy == 2, "prompt: the cursor sits on row 2");
    int pslot = T.g.live_off + T.cy;            /* the cursor's absolute row[] slot */
    term_resize(&T, 8, 20);                     /* shrink by 12: far more than the 2 rows above it */
    CHECK(T.g.live_off + T.cy == pslot, "shrink keeps the cursor on its own row[] slot");
    CHECK(T.cy == 2, "shrink keeps the cursor's viewport row");
    CHECK(T.g.live_off == 0, "shrink with blank rows below the cursor pushes nothing into scrollback");
    CHECK(T.g.scroll_off + T.g.rows == 8, "shrink: used == rows, the freed slots stay below");
    row_text(1, row);
    CHECK(!memcmp(row, "PROMPT-ONE", 10), "shrink keeps the prompt's first line visible");
    row_text(2, row);
    CHECK(!memcmp(row, "PROMPT-TWO", 10), "shrink keeps the line the cursor is on");
    term_resize(&T, 20, 20);                    /* zoom back out */
    CHECK(T.g.live_off + T.cy == pslot && T.cy == 2, "grow back keeps the cursor where it was");
    /* three whole zoom cycles (cols AND rows, like zoom_apply) must not duplicate the prompt */
    for (int cyc = 0; cyc < 3; cyc++) {
        term_resize(&T, 6, 16);
        term_resize(&T, 20, 20);
    }
    int copies = 0;
    for (int y = 0; y < T.g.rows; y++) {
        row_text(y, row);
        if (!memcmp(row, "PROMPT", 6))
            copies++;
    }
    CHECK(copies == 2, "three zoom cycles leave exactly the two prompt lines (no leftovers)");
    CHECK(T.g.live_off + T.cy == pslot, "zoom cycles keep the cursor slot");
    CHECK(T.g.scroll_off <= T.g.live_off &&
          T.g.live_off <= imin_(T.g.scrollback_max, T.g.rcap - T.g.rows) &&
          T.g.scroll_off + T.g.rows <= T.g.rcap, "zoom cycles keep the grid invariant");

    /* Rows below the cursor are only spare while they are blank. When the viewport is full of text
       (cursor on the bottom row) the OLD rule still applies: the top line goes to scrollback, the
       newest content stays visible, and the cursor travels with its own line. */
    term_destroy(&T);
    term_init(&T, 10, 10, 100);
    for (int i = 0; i < 6; i++) { char l[8]; snprintf(l, sizeof l, "L%d\r\n", i); feed(l); }
    CHECK(T.cy == 6, "six lines: cursor on row 6, rows 7..9 blank");
    int fslot = T.g.live_off + T.cy;
    term_resize(&T, 3, 10);
    CHECK(T.g.live_off + T.cy == fslot, "full-viewport shrink keeps the cursor slot");
    CHECK(T.g.scroll_off == 4, "full-viewport shrink pushes the top lines into scrollback");
    row_text(0, row);
    CHECK(row[0] == 'L' && row[1] == '4', "full-viewport shrink keeps the NEWEST lines visible");

    /* And when the cursor sits in the middle of a block of text, the rows given up are the ones under
       it — the same trade-off kitty makes, and what keeps the application's cursor anchored. */
    term_destroy(&T);
    term_init(&T, 10, 10, 100);
    for (int i = 0; i < 8; i++) { char l[8]; snprintf(l, sizeof l, "M%d\r\n", i); feed(l); }
    feed("\x1b[3H");                            /* cursor up to row 2, text below it */
    int mslot = T.g.live_off + T.cy;
    term_resize(&T, 5, 10);
    CHECK(T.g.live_off + T.cy == mslot, "mid-text shrink keeps the cursor slot");
    CHECK(T.cy == 2, "mid-text shrink keeps the cursor's viewport row");
    row_text(2, row);
    CHECK(row[0] == 'M' && row[1] == '2', "mid-text shrink keeps the cursor's line");
    row_text(4, row);
    CHECK(row[0] == 'M' && row[1] == '4', "mid-text shrink keeps the rows down to the new bottom");

    /* The alternate screen follows the same rule (grid_resize_plain gets the cursor slot), so a
       full-screen application keeps its cursor anchored across a zoom too. */
    term_destroy(&T);
    term_init(&T, 10, 10, 100);
    feed("\x1b[?1049h");
    feed("V1\r\nV2\r\nV3");
    CHECK(T.alt_screen == 1, "alt screen active");
    int aslot = T.g.live_off + T.cy;
    term_resize(&T, 4, 10);
    CHECK(T.g.live_off + T.cy == aslot, "alt screen shrink keeps the cursor slot");
    CHECK(T.cy == 2, "alt screen shrink keeps the cursor's viewport row");
    row_text(2, row);
    CHECK(row[0] == 'V' && row[1] == '3', "alt screen keeps the cursor's line visible");
    feed("\x1b[?1049l");

    /* --- window padding: one helper decides the grid size AND its pixel origin -----------------
       main.c uses the returned offset for the pointer -> cell mapping and hands it to render.c, so
       these checks are what keep a click, the highlight and the drawn glyph on the same cell. */
    int cols, rows, xoff, yoff;
    term_grid_geometry(936, 1002, 8, 17, 0, &cols, &rows, &xoff, &yoff);
    CHECK(cols == 117 && rows == 58 && xoff == 0 && yoff == 0,
          "padding 0 is the old edge-to-edge layout (window/cell, remainder at bottom/right)");
    term_grid_geometry(936, 1002, 8, 17, 8, &cols, &rows, &xoff, &yoff);
    CHECK(xoff == 8 && yoff == 8, "the margin is applied on the left AND the top");
    CHECK(cols == 115, "16 px of margin costs exactly 2 columns at an 8 px cell");
    CHECK(rows == 58, "the same 16 px costs no row at a 17 px cell — the remainder absorbed it");
    /* The grid must fill the space between the margins, and one more cell must not fit there — that
       is what makes the pointer mapping exact rather than approximate. */
    CHECK(xoff + cols * 8 <= 936 && (cols + 1) * 8 > 936 - 2 * xoff,
          "the grid fills the space between the two margins; one more column would not fit");
    CHECK(yoff + rows * 17 <= 1002 && (rows + 1) * 17 > 1002 - 2 * yoff,
          "and the same vertically, so the leftover pixels stay at the bottom");
    /* The remainder belongs at the bottom/right: the bottom margin is the configured one PLUS the
       leftover pixels, the top margin is exactly what was configured. */
    CHECK(1002 - yoff - rows * 17 >= yoff, "the bottom margin is never smaller than the top one");
    CHECK(936 - xoff - cols * 8 >= xoff, "the right margin is never smaller than the left one");
    /* Every pixel of the GRID area must map to the cell drawn under it: the input side is main.c's
       (p - off)/cell clamped, the output side is render.c's off + cell*cellw span. A mismatch is a
       dead zone (a click that highlights a different glyph than the one under the pointer), which is
       exactly what a padding change could introduce. Pixels in the margin are deliberately clamped to
       the first/last cell, so the sweep covers the grid area only. Counted per geometry, so one check
       per window size rather than one per pixel. */
    struct { int w, h, cw, ch, pad; } geos[] = {
        { 936, 1002, 8, 17, 0 }, { 936, 1002, 8, 17, 8 }, { 300, 200, 9, 19, 4 },
        { 120, 90, 8, 17, 20 }, { 40, 40, 8, 17, 30 }, { 17, 17, 8, 17, 8 },
    };
    for (size_t gi = 0; gi < sizeof geos / sizeof geos[0]; gi++) {
        int gw = geos[gi].w, gh = geos[gi].h, cw = geos[gi].cw, ch = geos[gi].ch;
        term_grid_geometry(gw, gh, cw, ch, geos[gi].pad, &cols, &rows, &xoff, &yoff);
        int bad = 0;
        if (cols < 1 || rows < 1 || xoff < 0 || yoff < 0)
            bad = 1;
        /* the grid must lie inside the window, and it must be MAXIMAL: one more cell may not fit in
           the area left between the two margins */
        if (xoff + cols * cw > gw || yoff + rows * ch > gh)
            bad = 2;
        if ((cols + 1) * cw <= gw - 2 * xoff || (rows + 1) * ch <= gh - 2 * yoff)
            bad = 3;   /* a cell still fits between the margins -> cols/rows are not maximal */
        for (int p = xoff; p < xoff + cols * cw && !bad; p++) {
            int c = iclamp_((p - xoff) / cw, 0, cols - 1);
            if (c < 0 || c >= cols || p < xoff + c * cw || p >= xoff + (c + 1) * cw)
                bad = 4;
        }
        for (int p = yoff; p < yoff + rows * ch && !bad; p++) {
            int r = iclamp_((p - yoff) / ch, 0, rows - 1);
            if (r < 0 || r >= rows || p < yoff + r * ch || p >= yoff + (r + 1) * ch)
                bad = 5;
        }
        CHECK(bad == 0, "every grid pixel maps to the cell drawn under it (padding geometry sweep)");
    }
    /* A margin may never eat the last cell: a tiny window still gets a 1x1 grid and an origin that
       keeps that cell inside the window. */
    term_grid_geometry(40, 40, 8, 17, 30, &cols, &rows, &xoff, &yoff);
    CHECK(cols == 1 && rows == 1, "a window smaller than two margins still gets a 1x1 grid");
    CHECK(xoff + 8 <= 40 && yoff + 17 <= 40,
          "the clamped margin keeps the single cell inside the window");
    term_grid_geometry(1, 1, 0, 0, 0, &cols, &rows, &xoff, &yoff);
    CHECK(cols >= 1 && rows >= 1 && xoff >= 0 && yoff >= 0,
          "a zero cell size / 1x1 window cannot divide by zero or produce a negative origin");
    term_grid_geometry(936, 1002, 8, 17, -5, &cols, &rows, &xoff, &yoff);
    CHECK(xoff == 0 && yoff == 0 && cols == 117 && rows == 58, "a negative margin means no margin");

    /* --- reflow: a width change re-wraps, it must never truncate ---------------------- */
    static char lbuf[32][512];
#define LONG56 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrst"   /* 56 chars */

    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed(LONG56 "\r\n");
    CHECK(T.g.wrap[0] == 1 && T.g.wrap[1] == 1 && T.g.wrap[2] == 0,
          "soft-wrapped rows are marked, the hard-broken one is not");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], LONG56), "one logical line at 20 cols");
    CHECK(content_rows() == 3, "56 chars at 20 cols take 3 physical rows");

    term_resize(&T, 10, 10);
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], LONG56),
          "narrowing keeps every character (re-broken, not truncated)");
    CHECK(content_rows() == 6, "the same line takes 6 rows at 10 cols");

    term_resize(&T, 10, 20);
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], LONG56), "widening restores the logical line");
    CHECK(content_rows() == 3, "widening restores the original physical wrapping");

    /* the cursor keeps its logical cell, and typing continues the same line */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("0123456789ABCDE");
    CHECK(T.cx == 5 && T.cy == 1, "cursor on the wrapped row before the resize");
    term_resize(&T, 6, 20);
    CHECK(T.cx == 15 && T.cy == 0, "cursor keeps its logical column when the line fits again");
    feed("F");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], "0123456789ABCDEF"),
          "typing after a reflow appends to the same logical line");
    term_resize(&T, 6, 5);
    feed("X");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], "0123456789ABCDEFX"),
          "after narrowing the cursor still writes at the end of the wrapped line");

    /* a wide glyph is never split from its continuation cell, and no padding hole is left */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("abc" "\xe4\xb8\xad" "def");
    CHECK(wide_cells() == 1, "the CJK glyph reserves one continuation cell");
    term_resize(&T, 6, 4);            /* a naive cut at column 4 would split the pair */
    CHECK(wide_orphans() == 0, "no wide continuation cell is orphaned by the re-wrap");
    CHECK(wide_cells() == 1, "the wide pair still occupies exactly one continuation cell");
    term_resize(&T, 6, 10);
    CHECK(wide_cells() == 1 && wide_orphans() == 0, "the round trip keeps the wide pair intact");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], "abc? def"),
          "the 10 -> 4 -> 10 round trip leaves no padding hole inside the text");

    /* narrowing past the buffer evicts the OLDEST rows and keeps the invariants */
    term_destroy(&T);
    term_init(&T, 4, 20, 4);
    for (int i = 0; i < 6; i++)
        feed("01234567890123456789\r\n");
    feed("NEWEST");
    term_resize(&T, 4, 5);
    int nl = logical_lines(lbuf);
    CHECK(T.g.scroll_off + T.g.rows <= T.g.rcap, "reflow keeps used <= rcap");
    CHECK(T.g.live_off <= T.g.scrollback_max, "reflow keeps live_off <= scrollback_max");
    CHECK(nl > 0 && strstr(lbuf[nl - 1], "NEWEST") != NULL, "the newest line survives eviction");

    /* hard line breaks are never joined, and an erased line stops joining */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("ab\r\ncd\r\n");
    term_resize(&T, 6, 20);
    CHECK(logical_lines(lbuf) == 2 && !strcmp(lbuf[0], "ab") && !strcmp(lbuf[1], "cd"),
          "hard-broken lines stay separate lines");
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("0123456789abcd");           /* wrapped onto row 1 */
    feed("\x1b[1;1H\x1b[0K");         /* cursor to row 0, erase to end of line */
    CHECK(T.g.wrap[0] == 0, "erase-line clears the soft-wrap mark");
    term_resize(&T, 6, 20);
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], "abcd"),
          "an erased wrapped line is not joined back into one line");

    /* a coloured background run is not "trailing whitespace" and must survive */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("\x1b[41mXXXXXXXXXX\x1b[0m\r\n");
    uint32_t redb = cellbg(0, 0);
    term_resize(&T, 6, 20);
    int red = 0;
    for (int x = 0; x < T.g.cols; x++)
        if (cellbg(0, x) == redb)
            red++;
    CHECK(red == 10, "a coloured background run survives reflow");

    /* short lines must not multiply when the window narrows */
    term_destroy(&T);
    term_init(&T, 6, 40, 50);
    feed("one\r\ntwo\r\nthree\r\n");
    term_resize(&T, 6, 10);
    CHECK(logical_lines(lbuf) == 3 && !strcmp(lbuf[0], "one") && !strcmp(lbuf[2], "three"),
          "short lines re-wrap to the same three lines");
    CHECK(content_rows() == 3, "short lines do not consume extra rows when narrowing");

    /* scrollback reflows too, not only the viewport */
    term_destroy(&T);
    term_init(&T, 4, 20, 100);
    feed(LONG56 "\r\n");
    feed("second\r\n");
    CHECK(T.g.scroll_off > 0, "the long line pushed content into scrollback");
    term_resize(&T, 4, 10);
    CHECK(logical_lines(lbuf) == 2 && !strcmp(lbuf[0], LONG56) && !strcmp(lbuf[1], "second"),
          "scrollback lines reflow as well");

    /* the ALTERNATE screen is not re-wrapped: full-screen apps repaint themselves */
    term_destroy(&T);
    term_init(&T, 6, 10, 50);
    feed("\x1b[?1049h");
    CHECK(T.alt_screen == 1, "alternate screen active");
    feed("0123456789ABCDE");
    term_resize(&T, 6, 20);
    row_text(0, row);
    CHECK(!memcmp(row, "0123456789", 10), "alt screen row 0 keeps its 10 columns (no re-wrap)");
    row_text(1, row);
    CHECK(row[0] == 'A' && row[5] == ' ', "alt screen row 1 keeps the overflow in place");

    /* ... but the PARKED main screen does reflow, so it is correct when the app exits */
    term_destroy(&T);
    term_init(&T, 6, 20, 50);
    feed(LONG56 "\r\n");
    feed("\x1b[?1049h");
    feed("alt screen");
    term_resize(&T, 6, 10);
    feed("\x1b[?1049l");
    CHECK(T.alt_screen == 0, "left the alternate screen");
    CHECK(logical_lines(lbuf) == 1 && !strcmp(lbuf[0], LONG56),
          "the parked main screen reflowed while the alt screen was active");
#undef LONG56
    feed("\x1b[?1049l");
    term_destroy(&T);
    term_init(&T, 10, 20, 100);

    /* --- OSC title --- */
    feed("\x1b]0;my title\x07");
    CHECK(!strcmp(T.title, "my title"), "OSC title set");

    /* --- DECTCEM --- */
    feed("\x1b[?25l");
    CHECK(T.show_cursor == 0, "hide cursor");
    feed("\x1b[?25h");
    CHECK(T.show_cursor == 1, "show cursor");

    /* --- mouse mode bits (?1000/?1002/?1006) --- */
    feed("\x1b[?1000;1006h");
    CHECK((T.mouse_mode & MOUSE_PRESS) && (T.mouse_mode & MOUSE_SGR) &&
          !(T.mouse_mode & MOUSE_DRAG), "mouse press+sgr set, drag off");
    feed("\x1b[?1002h");
    CHECK(T.mouse_mode & MOUSE_DRAG, "drag tracking set");
    feed("\x1b[?1000;1002;1006l");
    CHECK(T.mouse_mode == 0, "all mouse modes cleared");

    /* --- evdev BTN_* -> SGR button number (session 61: the local mapping had right and middle
       swapped, which made a middle click on the tab bar do nothing and a RIGHT click close a session.
       The kernel order is LEFT 0x110, RIGHT 0x111, MIDDLE 0x112; SGR/X10 want 0/1/2 in that name order.
       The literals are written out here as well as through the enum, so changing the enum without the
       real protocol order fails a check. --- */
    CHECK(mouse_sgr_button(EVDEV_BTN_LEFT) == 0, "BTN_LEFT -> SGR 0");
    CHECK(mouse_sgr_button(EVDEV_BTN_MIDDLE) == 1, "BTN_MIDDLE -> SGR 1");
    CHECK(mouse_sgr_button(EVDEV_BTN_RIGHT) == 2, "BTN_RIGHT -> SGR 2");
    CHECK(EVDEV_BTN_RIGHT == 0x111 && EVDEV_BTN_MIDDLE == 0x112, "BTN_RIGHT is 0x111, BTN_MIDDLE 0x112");
    CHECK(mouse_sgr_button(0x111) == 2 && mouse_sgr_button(0x112) == 1,
          "raw 0x111 is right, raw 0x112 is middle");
    CHECK(mouse_sgr_button(0x113) == 3 && mouse_sgr_button(0) == 3, "side/extra/unknown -> SGR 3");

    /* --- empty/omitted params --- */
    feed("\x1b[2J\x1b[H");
    feed("\x1b[H\x1b[2;H");
    CHECK(T.cx == 0 && T.cy == 1, "CSI 2;H defaults col to 1");

    /* --- alternate screen buffer (?1049): main content + scrollback preserved --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("\x1b[2J\x1b[HMAIN");            /* main content on row 0 */
    feed("\x1b[4;3H");                    /* park main cursor at row4 col3 (0-based 3,2) */
    int so_before = T.g.scroll_off;
    CHECK(T.cx == 2 && T.cy == 3, "main cursor parked before alt");
    feed("\x1b[?1049h");                  /* enter alt (saves cursor) */
    CHECK(T.alt_screen == 1, "alt screen active");
    CHECK(T.g.scrollback_max == 0, "alt grid has no scrollback");
    CHECK(T.cx == 0 && T.cy == 0, "alt screen starts at home");
    row_text(0, row);
    CHECK(row[0] == ' ', "alt screen is cleared on entry");
    feed("ALT");
    CHECK(cellch(0, 0) == 'A' && cellch(0, 1) == 'L' && cellch(0, 2) == 'T', "alt content written");
    feed("\x1b[?1049l");                  /* leave alt (restores cursor + main) */
    CHECK(T.alt_screen == 0, "alt screen left");
    row_text(0, row);
    CHECK(!memcmp(row, "MAIN", 4), "main content restored after alt");
    CHECK(T.cx == 2 && T.cy == 3, "main cursor restored after alt");
    CHECK(T.g.scroll_off == so_before, "alt did not pollute main scrollback");

    /* --- alt screen ?47 (switch without saving cursor) --- */
    feed("\x1b[2J\x1b[H");
    feed("\x1b[?47h");
    CHECK(T.alt_screen == 1, "?47 enters alt");
    CHECK(T.cx == 0 && T.cy == 0, "?47 alt starts home");
    feed("\x1b[?47l");
    CHECK(T.alt_screen == 0, "?47 leaves alt");
    CHECK(T.cx == 0 && T.cy == 0, "?47 leaves cursor at 0,0 (no save)");

    /* --- ?1048 cursor save/restore only (no grid switch) --- */
    feed("\x1b[2J\x1b[H\x1b[6;7H");
    CHECK(T.cx == 6 && T.cy == 5, "cursor parked for 1048");
    feed("\x1b[?1048h");
    CHECK(T.alt_screen == 0, "?1048 does not switch grid");
    feed("\x1b[1;1H");
    CHECK(T.cx == 0 && T.cy == 0, "cursor moved after save");
    feed("\x1b[?1048l");
    CHECK(T.cx == 6 && T.cy == 5, "?1048l restores cursor");

    /* --- text selection (term_selected + term_selection_text) --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("\x1b[2J\x1b[H");
    feed("hello world\r\nsecond line");   /* row0 "hello world", row1 "second line" */
    char sel[256];
    int n;

    /* Whole-cell selection is expressed with the end frac at 1000 (pointer at the right
       edge of the end cell); a frac of 0 means the pointer is at the cell's left edge. */

    /* single-row selection cols 0..4 on row 0 */
    T.sel_active = 1;
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 4; T.sel_by = 0; T.sel_bf = 1000;
    CHECK(term_selected(&T, 0, 0) && term_selected(&T, 4, 0), "single-row endpoints selected");
    CHECK(!term_selected(&T, 5, 0), "single-row past end not selected");
    CHECK(!term_selected(&T, 0, 1), "other row not selected");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 5 && !memcmp(sel, "hello", 5), "selection text 'hello'");

    /* the covered sub-range (what the highlight band spans) is exact */
    double a, b;
    CHECK(term_selection_cover(&T, 0, 0, &a, &b) && a == 0.0 && b == 1.0, "cover cell0 = [0,1]");
    CHECK(term_selection_cover(&T, 4, 0, &a, &b) && a == 4.0 && b == 5.0, "cover cell4 = [4,5]");
    CHECK(!term_selection_cover(&T, 5, 0, &a, &b), "cover cell5 = none");
    CHECK(!term_selection_cover(&T, 0, 1, &a, &b), "cover other row = none");

    /* multi-row selection: (6,0)..(5,1) -> "world" + "second" */
    T.sel_ax = 6; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 5; T.sel_by = 1; T.sel_bf = 1000;
    CHECK(term_selected(&T, 6, 0) && term_selected(&T, 10, 0), "row0 from anchor to end");
    CHECK(!term_selected(&T, 5, 0), "row0 before anchor not selected");
    CHECK(term_selected(&T, 0, 1) && term_selected(&T, 5, 1), "row1 up to current col");
    CHECK(!term_selected(&T, 6, 1), "row1 past current col not selected");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 12 && !memcmp(sel, "world\nsecond", 12), "multi-row selection text");
    /* middle rows are fully covered; the end row stops at the sub-cell edge */
    CHECK(term_selection_cover(&T, 19, 0, &a, &b) && a == 19.0 && b == 20.0, "row0 last cell covered");
    CHECK(term_selection_cover(&T, 5, 1, &a, &b) && a == 5.0 && b == 6.0, "row1 end cell covered");
    CHECK(!term_selection_cover(&T, 6, 1, &a, &b), "row1 past end not covered");

    /* normalized: anchor after current (4,0)..(0,0) selects cols 0..4 */
    T.sel_ax = 4; T.sel_ay = 0; T.sel_af = 1000;
    T.sel_bx = 0; T.sel_by = 0; T.sel_bf = 0;
    CHECK(term_selected(&T, 0, 0) && term_selected(&T, 4, 0) && !term_selected(&T, 5, 0),
          "anchor-after-current normalized");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 5 && !memcmp(sel, "hello", 5), "normalized selection text");

    /* trailing spaces trimmed: select a wide region on a short line */
    feed("\x1b[2J\x1b[H");
    feed("ab");
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 10; T.sel_by = 0; T.sel_bf = 1000;
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 2 && !memcmp(sel, "ab", 2), "trailing spaces trimmed");

    /* --- sub-cell (per-glyph) selection edges --- */
    feed("\x1b[2J\x1b[H");
    feed("hello world");   /* row0: h e l l o ... at cols 0..4 */

    /* span entirely inside col2 (anchor at its left edge, end 30% in): only col2 touched */
    T.sel_active = 1;
    T.sel_ax = 2; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 2; T.sel_by = 0; T.sel_bf = 300;
    CHECK(term_selected(&T, 2, 0), "sub-cell: col2 selected when span ends inside it");
    CHECK(!term_selected(&T, 3, 0), "sub-cell: col3 not selected (span ends before it)");
    CHECK(!term_selected(&T, 1, 0), "sub-cell: col1 not selected (span starts at col2)");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 1 && sel[0] == 'l', "sub-cell text = the single touched glyph 'l'");

    /* partial edges across three cells: the band is [2,3],[3,4],[4,4.5] */
    T.sel_ax = 2; T.sel_af = 0; T.sel_ay = 0;
    T.sel_bx = 4; T.sel_bf = 500; T.sel_by = 0;
    CHECK(term_selection_cover(&T, 2, 0, &a, &b) && a == 2.0 && b == 3.0, "partial: cell2 full");
    CHECK(term_selection_cover(&T, 3, 0, &a, &b) && a == 3.0 && b == 4.0, "partial: cell3 full");
    CHECK(term_selection_cover(&T, 4, 0, &a, &b) && a == 4.0 && b == 4.5, "partial: cell4 half");
    CHECK(!term_selection_cover(&T, 5, 0, &a, &b), "partial: cell5 untouched");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 3 && !memcmp(sel, "llo", 3), "partial text 'llo' (whole glyphs)");

    /* ending mid-col2 (bf=500) still copies the whole glyph col2, not col3 */
    T.sel_ax = 0; T.sel_af = 0; T.sel_ay = 0;
    T.sel_bx = 2; T.sel_bf = 500; T.sel_by = 0;
    CHECK(term_selected(&T, 2, 0) && !term_selected(&T, 3, 0),
          "mid-cell end includes its glyph, excludes the next");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 3 && !memcmp(sel, "hel", 3), "mid-cell end text 'hel'");

    /* sub-cell anchor 50% into col1: col1 is included, col0 excluded, through col4 */
    T.sel_ax = 1; T.sel_af = 500; T.sel_ay = 0;
    T.sel_bx = 4; T.sel_bf = 1000; T.sel_by = 0;
    CHECK(term_selected(&T, 1, 0) && term_selected(&T, 4, 0), "sub-cell anchor includes its cell");
    CHECK(!term_selected(&T, 0, 0), "sub-cell anchor excludes col0");
    CHECK(term_selection_cover(&T, 1, 0, &a, &b) && a == 1.5 && b == 2.0, "anchor cell band [1.5,2]");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 4 && !memcmp(sel, "ello", 4), "sub-cell anchor text 'ello'");

    /* a click with no movement selects the whole cell under the pointer */
    T.sel_ax = 3; T.sel_ay = 0; T.sel_af = 400;
    T.sel_bx = 3; T.sel_by = 0; T.sel_bf = 400;
    CHECK(term_selection_cover(&T, 3, 0, &a, &b) && a == 3.0 && b == 4.0, "click covers whole cell");
    CHECK(!term_selection_cover(&T, 2, 0, &a, &b) && !term_selection_cover(&T, 4, 0, &a, &b),
          "click covers only its cell");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 1 && sel[0] == 'l', "click copies one glyph");

    /* sel_band_pixels: ABSOLUTE pixel band (renderer must not add the cell origin again).
       cw=10, span 2.3..6.7 on row 0. */
    SelSpan bs = { 0, 2, 300, 0, 6, 700 };
    float bx, bw;
    sel_band_pixels(&bs, 20, 2, 0, 10.0f, &bx, &bw);   /* cell2 covered [2.3,3] */
    CHECK(bx == 23.0f && bw == 7.0f, "band cell2 absolute x=23 w=7");
    sel_band_pixels(&bs, 20, 3, 0, 10.0f, &bx, &bw);   /* full [3,4] */
    CHECK(bx == 30.0f && bw == 10.0f, "band cell3 full x=30 w=10");
    sel_band_pixels(&bs, 20, 6, 0, 10.0f, &bx, &bw);   /* [6,6.7] */
    CHECK(bx == 60.0f && bw == 7.0f, "band cell6 absolute x=60 w=7");
    sel_band_pixels(&bs, 20, 7, 0, 10.0f, &bx, &bw);   /* past end */
    CHECK(bx == 0.0f && bw == 0.0f, "band cell7 none");
    sel_band_pixels(&bs, 20, 1, 0, 10.0f, &bx, &bw);   /* before start */
    CHECK(bx == 0.0f && bw == 0.0f, "band cell1 none");

    /* term_selection_span normalizes by (row,col,frac) */
    T.sel_ax = 4; T.sel_af = 200; T.sel_ay = 1;
    T.sel_bx = 1; T.sel_bf = 700; T.sel_by = 0;
    SelSpan s;
    term_selection_span(&T, &s);
    CHECK(s.r0 == 0 && s.c0 == 1 && s.f0 == 700 && s.r1 == 1 && s.c1 == 4 && s.f1 == 200,
          "span normalized to reading order");

    /* clear */
    term_select_clear(&T);
    CHECK(T.sel_active == 0, "select_clear clears active");
    CHECK(!term_selected(&T, 0, 0), "no cell selected after clear");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 0 && sel[0] == 0, "empty selection text after clear");

    /* --- per-glyph (cell-granular) endpoints — ------------------------------------------------
       main.c's sel_set_endpoints() only ever produces frac 0 (left edge of the endpoint that comes
       first in reading order) and frac 1000 (right edge of the later one), so a range can never end
       inside a glyph. These checks pin what that means for the user: whole cells only, and the cell
       under the pointer stays selected whichever way the drag goes. */
    T.sel_active = 1;
    T.sel_ax = 1; T.sel_ay = 0; T.sel_af = 0;      /* forward drag: anchor col1 .. pointer col3 */
    T.sel_bx = 3; T.sel_by = 0; T.sel_bf = 1000;
    CHECK(term_selected(&T, 1, 0) && term_selected(&T, 2, 0) && term_selected(&T, 3, 0),
          "per-glyph: every cell between the snapped edges is selected");
    CHECK(!term_selected(&T, 0, 0) && !term_selected(&T, 4, 0),
          "per-glyph: no half glyph at either end");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 3 && !memcmp(sel, "ell", 3), "per-glyph text 'ell'");
    SelSpan gs;
    term_selection_span(&T, &gs);
    float gx, gw;
    sel_band_pixels(&gs, 20, 3, 0, 10.0f, &gx, &gw);
    CHECK(gx == 30.0f && gw == 10.0f, "per-glyph: the end cell's band is the whole cell");
    sel_band_pixels(&gs, 20, 4, 0, 10.0f, &gx, &gw);
    CHECK(gx == 0.0f && gw == 0.0f, "per-glyph: the cell after the end gets no band");
    /* backwards drag: the two fracs swap (anchor right edge 1000, end left edge 0). Without the swap
       term_selection_span's reordering would cut the anchor cell out of a leftward selection. */
    T.sel_ax = 3; T.sel_ay = 0; T.sel_af = 1000;
    T.sel_bx = 1; T.sel_by = 0; T.sel_bf = 0;
    CHECK(term_selected(&T, 1, 0) && term_selected(&T, 3, 0) && !term_selected(&T, 0, 0),
          "per-glyph: dragging left keeps the anchor cell selected");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 3 && !memcmp(sel, "ell", 3), "per-glyph: a leftward drag copies the same 3 glyphs");
    /* press without a drag: exactly one whole cell */
    T.sel_ax = 2; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 2; T.sel_by = 0; T.sel_bf = 1000;
    CHECK(term_selected(&T, 2, 0) && !term_selected(&T, 1, 0) && !term_selected(&T, 3, 0),
          "per-glyph: a press with no drag selects exactly one cell");
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 1 && sel[0] == 'l', "per-glyph: a press with no drag copies one glyph");

    /* --- no highlight over empty space — ------------------------------------------------------
       term_cell_highlightable() decides whether the band is PAINTED over a covered cell; the copied
       text is deliberately unaffected (blank cells are still spaces, trailing ones trimmed). */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    feed("\x1b[2J\x1b[H");
    feed("ab cd\r\ne");                     /* row0: a b ' ' c d then blanks; row1: e then blanks */
    CHECK(term_cell_highlightable(&T, 0, 0) && term_cell_highlightable(&T, 4, 0),
          "empty: cells holding a glyph are highlighted");
    CHECK(term_cell_highlightable(&T, 2, 0), "empty: a real space IS highlighted");
    CHECK(!term_cell_highlightable(&T, 5, 0), "empty: a never-written cell gets no band");
    CHECK(!term_cell_highlightable(&T, 1, 1), "empty: past the end of the next line = no band");
    CHECK(!term_cell_highlightable(&T, -1, 0) && !term_cell_highlightable(&T, 20, 0) &&
          !term_cell_highlightable(&T, 0, 10), "empty: out-of-range cells are never highlighted");
    feed("\x1b[H\x1b[J");
    feed("\xe4\xb8\xadX");                  /* wide CJK at col0, continuation col1, 'X' col2 */
    CHECK(cellch(0, 0) == 0x4E2D && (cellattr(0, 1) & ATTR_WIDE), "wide char reserves its pair");
    CHECK(term_cell_highlightable(&T, 1, 0),
          "empty: a wide glyph's continuation cell keeps its band (else half the glyph loses it)");
    T.sel_active = 1;
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 19; T.sel_by = 0; T.sel_bf = 1000;   /* drag far past the end of the line */
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 4 && !memcmp(sel, "\xe4\xb8\xadX", 4), "empty: the copied text stops at the last glyph");

    /* --- emoji grapheme clusters + wide emoji (color emoji support) --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);

    CHECK(cp_is_wide(0x4E2D) && cp_is_wide(0x1F600) && cp_is_wide(0x1F468) &&
          cp_is_wide(0x1F1E6) && cp_is_wide(0xFF21),
          "cp_is_wide: CJK, emoji, regional indicator, fullwidth");
    CHECK(!cp_is_wide('A') && !cp_is_wide(0x20AC) && !cp_is_wide(0x2764) &&
          !cp_is_wide(0x1F3F3) && !cp_is_wide(0xFE0F),
          "cp_is_wide: text, text-presentation symbols and selectors stay narrow");
    CHECK(cp_is_emoji(0x1F600) && cp_is_emoji(0x2764) && cp_is_emoji(0x1F468), "cp_is_emoji");
    CHECK(!cp_is_emoji('A') && !cp_is_emoji(0x20AC), "plain text is not an emoji base");
    CHECK(cp_is_cluster_extend(0x200D) && cp_is_cluster_extend(0xFE0F) &&
          cp_is_cluster_extend(0x1F3FB) && cp_is_cluster_extend(0x20E3) &&
          cp_is_cluster_extend(0xE0061), "cp_is_cluster_extend: ZWJ/VS/skin tone/keycap/tag");
    CHECK(!cp_is_cluster_extend(0x1F600) && !cp_is_cluster_extend('A'), "a base is not an extend");

    /* cluster_is_wide: the MODIFIER decides the width, not just the base codepoint */
    {
        const uint32_t h1[] = { 0x2764 };
        const uint32_t h2[] = { 0x2764, 0xFE0F };
        const uint32_t k1[] = { '1', 0x20E3 };
        const uint32_t k2[] = { '1', 0xFE0F, 0x20E3 };
        const uint32_t t1[] = { 'a', 0xFE0F };
        const uint32_t r1[] = { 0x1F1FA, 0x1F1F8 };
        const uint32_t e1[] = { 0x1F600 };
        CHECK(!cluster_is_wide(h1, 1) && cluster_is_wide(h2, 2), "VS16 widens a text heart");
        CHECK(cluster_is_wide(k1, 2) && cluster_is_wide(k2, 3), "keycaps are wide");
        CHECK(!cluster_is_wide(t1, 2), "a non-emoji base + VS16 stays narrow");
        CHECK(cluster_is_wide(r1, 2) && cluster_is_wide(e1, 1), "RI pair / Emoji_Presentation base");
        CHECK(!cluster_is_wide(NULL, 0), "empty cluster is narrow");
    }

    /* thumbs up + skin tone: ONE anchor cell, one continuation cell, cursor advances 2 */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F44D\U0001F3FD");
    CHECK(cellch(0, 0) == 0x1F44D, "thumbs-up anchor cell");
    CHECK(cellnextra(0, 0) == 1 && cellextra(0, 0, 0) == 0x1F3FD, "skin tone folds into the anchor");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "wide emoji reserves a second cell");
    CHECK(cellch(0, 1) == 0, "continuation cell holds no codepoint");
    CHECK(T.cx == 2, "cursor advances 2 for a wide emoji cluster");

    /* ZWJ family: 7 codepoints -> one anchor cell with 6 continuations, 2 cells total */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466X");
    CHECK(cellch(0, 0) == 0x1F468, "family anchor is the first codepoint");
    CHECK(cellnextra(0, 0) == 6, "family cluster keeps 6 continuation codepoints");
    CHECK(cellextra(0, 0, 0) == 0x200D && cellextra(0, 0, 1) == 0x1F469 &&
          cellextra(0, 0, 5) == 0x1F466, "family continuation order");
    CHECK(cellch(0, 2) == 'X', "family occupies exactly two cells");

    /* rainbow flag: base + VS16 + ZWJ + emoji. U+1F3F3 is text-presentation on its own, but the
       VS16 inside the cluster switches it to emoji presentation, so the cluster is WIDE. */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F3F3\uFE0F\u200D\U0001F308Y");
    CHECK(cellch(0, 0) == 0x1F3F3 && cellnextra(0, 0) == 3, "rainbow-flag cluster");
    CHECK(cellextra(0, 0, 0) == 0xFE0F && cellextra(0, 0, 1) == 0x200D &&
          cellextra(0, 0, 2) == 0x1F308, "flag continuation order");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "VS16 widens the flag cluster");
    CHECK(cellch(0, 2) == 'Y', "a VS16 flag takes two cells");

    /* keycap: digit + VS16 + combining enclosing keycap -> emoji presentation, 2 cells */
    feed("\x1b[2J\x1b[H");
    feed("1\uFE0F\u20E3Z");
    CHECK(cellch(0, 0) == '1' && cellnextra(0, 0) == 2, "keycap folds onto a text base");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "keycap is emoji presentation -> wide");
    CHECK(cellch(0, 1) == 0 && cellch(0, 2) == 'Z', "keycap takes two cells");

    /* regional indicator PAIR = ONE flag cluster. The second RI folds into the first so the whole
       pair reaches HarfBuzz: the color font keys its flag glyph on the pair (one RI alone shapes
       to the RI LETTER glyph), and the pair must reserve 2 cells, not 4. */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F1FA\U0001F1F8Q");
    CHECK(cellch(0, 0) == 0x1F1FA, "flag anchor is the first regional indicator");
    CHECK(cellnextra(0, 0) == 1 && cellextra(0, 0, 0) == 0x1F1F8, "second RI folds into the anchor");
    CHECK(cellattr(0, 1) & ATTR_WIDE, "a flag reserves two cells");
    CHECK(cellch(0, 2) == 'Q', "the RI pair consumed exactly two cells");
    CHECK(T.cx == 3, "cursor after flag + Q");

    /* a THIRD regional indicator starts a new cluster (Unicode pairs RIs up) */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F1FA\U0001F1F8\U0001F1EF");
    CHECK(cellch(0, 2) == 0x1F1EF && cellnextra(0, 2) == 0, "third RI starts its own cluster");

    /* U+2764 alone is a text dingbat (1 cell); U+2764 U+FE0F is the red emoji (2 cells). The
       anchor is written narrow and UPGRADED to a wide pair when the VS16 arrives. */
    feed("\x1b[2J\x1b[H");
    feed("\u2764\uFE0FW");
    CHECK(cellch(0, 0) == 0x2764 && cellnextra(0, 0) == 1, "heart + VS16 cluster");
    CHECK((cellattr(0, 1) & ATTR_WIDE) && cellch(0, 1) == 0, "VS16 widens the anchor in place");
    CHECK(cellch(0, 2) == 'W', "text after a widened emoji lands two cells on");
    CHECK(T.cx == 3, "cursor steps over the continuation cell it just created");

    /* the upgrade must NOT happen for a base that is not emoji-capable */
    feed("\x1b[2J\x1b[H");
    feed("a\uFE0Fb");
    CHECK(cellch(0, 0) == 'a' && cellnextra(0, 0) == 1, "VS16 still folds onto a text base");
    CHECK(!(cellattr(0, 1) & ATTR_WIDE) && cellch(0, 1) == 'b', "a text base stays one cell");

    /* at the right margin there is no second cell to reserve: the emoji keeps its single cell */
    feed("\x1b[2J\x1b[H\x1b[1;20H");
    feed("\u2764\uFE0F");
    CHECK(cellch(0, 19) == 0x2764 && cellnextra(0, 19) == 1, "heart at the last column");
    CHECK(!(cellattr(0, 19) & ATTR_WIDE), "no room to widen at the right margin");

    /* more continuations than CLUSTER_MAX: the surplus starts its own cell */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F44D");
    for (int i = 0; i < CLUSTER_MAX + 2; i++)
        feed("\U0001F3FB");
    CHECK(cellnextra(0, 0) == CLUSTER_MAX, "cluster capped at CLUSTER_MAX");
    CHECK(cellch(0, 2) == 0x1F3FB, "overflowing modifier gets its own cell (after the wide pair)");

    /* the cluster anchor is dropped by a cursor jump or a newline (cl_cy is a viewport coord) */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F600");                       /* wide emoji at col 0 */
    feed("\x1b[1;3H");                        /* CUP to row 1, col 3 */
    feed("\u200D");
    CHECK(cellch(0, 2) == 0x200D, "ZWJ after CUP gets its own cell");
    feed("\x1b[2J\x1b[H");
    feed("\U0001F600\r\n\u200D");
    CHECK(cellch(1, 0) == 0x200D, "ZWJ after CRLF gets its own cell");
    CHECK(cellnextra(0, 0) == 0, "newline leaves no cluster anchor");

    /* erase clears the cluster too (a stale nextra would resurrect a deleted emoji) */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F44D\U0001F3FD");
    feed("\x1b[1;1H\x1b[2X");
    CHECK(cellch(0, 0) == 0 && cellnextra(0, 0) == 0, "erase chars clears the cluster");

    /* selection copies the WHOLE cluster, and a wide continuation cell adds no stray space */
    feed("\x1b[2J\x1b[H");
    feed("\U0001F44D\U0001F3FDx");
    T.sel_active = 1;
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 19; T.sel_by = 0; T.sel_bf = 1000;
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 9 && !memcmp(sel, "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBDx", 9),
          "selection copies the whole emoji cluster");

    feed("\x1b[2J\x1b[H");
    feed("\xE4\xB8\xADX");                     /* U+4E2D then X */
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 19; T.sel_by = 0; T.sel_bf = 1000;
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 4 && !memcmp(sel, "\xE4\xB8\xADX", 4), "wide continuation cell adds no space");

    /* a cluster widened by VS16 copies as its full sequence, continuation cell included */
    feed("\x1b[2J\x1b[H");
    feed("\u2764\uFE0Fx");                     /* U+2764 U+FE0F then x */
    T.sel_ax = 0; T.sel_ay = 0; T.sel_af = 0;
    T.sel_bx = 19; T.sel_by = 0; T.sel_bf = 1000;
    n = term_selection_text(&T, sel, sizeof(sel));
    CHECK(n == 7 && !memcmp(sel, "\xE2\x9D\xA4\xEF\xB8\x8Fx", 7),
          "selection copies a VS16-widened cluster");

    /* --- CSI 3 J: erase saved lines (scrollback), gated by term_set_clear_scrollback --- */
    term_destroy(&T);
    term_init(&T, 10, 20, 100);
    char ln[32];
    for (int i = 0; i < 30; i++) {
        int m = snprintf(ln, sizeof(ln), "L%02d\r\n", i);
        term_parse(&T, (const uint8_t *)ln, (size_t)m);
    }
    CHECK(T.g.live_off == 21 && T.g.scroll_off == 21, "30 lines in a 10-row grid -> live_off 21");
    uint32_t topline = cellch(0, 0), lastline = cellch(8, 0);
    CHECK(topline == 'L' && cellch(0, 1) == '2', "viewport top is line 20");
    CHECK(lastline == 'L' && cellch(9, 0) == 0, "the trailing newline left a blank bottom row");
    feed("\x1b[3J");
    CHECK(T.g.scroll_off == 0 && T.g.live_off == 0, "ED 3 resets both anchors");
    CHECK(cellch(0, 0) == topline && cellch(8, 0) == lastline && cellch(9, 0) == 0,
          "ED 3 leaves the viewport alone");
    CHECK(T.g.row[T.g.rows][0].ch == 0, "the slots below the viewport are blanked");
    {
        int bad = 0;
        for (int s1 = 0; s1 < T.g.rcap; s1++) {
            if (!T.g.row[s1])
                bad++;
            for (int s2 = s1 + 1; s2 < T.g.rcap; s2++)
                if (T.g.row[s1] == T.g.row[s2])
                    bad++;
        }
        CHECK(bad == 0, "ED 3 keeps row[] a valid permutation");
    }
    /* the history is really gone: nothing is retained above the viewport any more */
    for (int i = 0; i < 5; i++) {
        int m = snprintf(ln, sizeof(ln), "M%02d\r\n", i);
        term_parse(&T, (const uint8_t *)ln, (size_t)m);
    }
    CHECK(T.g.live_off == 5, "scrollback rebuilds from zero after ED 3");
    /* the setting off = keep the history */
    term_set_clear_scrollback(0);
    feed("\x1b[3J");
    CHECK(T.g.live_off == 5 && T.g.scroll_off == 5, "clear_scrollback=0 keeps the history");
    term_set_clear_scrollback(1);
    feed("\x1b[3J");
    CHECK(T.g.live_off == 0 && T.g.scroll_off == 0, "re-enabled ED 3 clears again");
    /* `clear` sends ESC[2J then ESC[3J: screen AND history go away */
    feed("\x1b[2J\x1b[3J");
    CHECK(cellch(0, 0) == 0 && T.g.live_off == 0, "2J+3J (clear) empties screen and history");

    /* --- bracketed paste mode (DEC ?2004) + the bytes a paste is sent as --- */
    CHECK(T.bracketed_paste == 0, "bracketed paste off by default");
    feed("\x1b[?2004h");
    CHECK(T.bracketed_paste == 1, "?2004h enables bracketed paste");
    feed("\x1b[?2004l");
    CHECK(T.bracketed_paste == 0, "?2004l disables it again");
    /* the mode must not disturb the neighbours it sits next to in csi_dispatch */
    feed("\x1b[?2004;1000h");
    CHECK(T.bracketed_paste == 1 && (T.mouse_mode & MOUSE_PRESS), "?2004;1000h sets both");
    feed("\x1b[?2004;1000l");
    CHECK(T.bracketed_paste == 0 && !(T.mouse_mode & MOUSE_PRESS), "?2004;1000l clears both");
    /* ESC c rebuilds the Term: an application that has not re-enabled the mode gets raw paste */
    feed("\x1b[?2004h");
    feed("\x1b" "c");   /* the split string keeps the hex escape from swallowing the 'c' */
    CHECK(T.bracketed_paste == 0, "RIS resets the mode to off");
    {
        char pb[64];
        const char *txt = "echo \"foo\"\r\necho \"bar\"\r\n";
        size_t tlen = strlen(txt);
        int wlen = paste_wrap(0, txt, tlen, pb, sizeof(pb));
        CHECK(wlen == (int)tlen && !memcmp(pb, txt, tlen), "paste_wrap off = raw bytes");
        wlen = paste_wrap(1, txt, tlen, pb, sizeof(pb));
        CHECK(wlen == (int)tlen + 12, "paste_wrap on adds the two 6-byte delimiters");
        CHECK(!memcmp(pb, "\x1b[200~", 6), "paste starts with ESC[200~");
        CHECK(!memcmp(pb + 6, txt, tlen), "the whole block stays inside the brackets");
        CHECK(!memcmp(pb + 6 + tlen, "\x1b[201~", 6), "paste ends with ESC[201~");
        /* exact fit is accepted, one byte short is refused (never a truncated paste) */
        CHECK(paste_wrap(1, txt, tlen, pb, tlen + 12) == (int)tlen + 12, "exact fit ok");
        CHECK(paste_wrap(1, txt, tlen, pb, tlen + 11) == -1, "too small = -1");
        CHECK(paste_wrap(0, txt, tlen, pb, tlen) == (int)tlen, "raw paste needs no headroom");
        CHECK(paste_wrap(1, "", 0, pb, sizeof(pb)) == 12 &&
              !memcmp(pb, "\x1b[200~\x1b[201~", 12), "empty paste still gets brackets");
        CHECK(paste_wrap(1, "x", 1, pb, 11) == -1, "brackets alone do not fit in 11 bytes");
    }

    /* term_init frees the grid it rebuilds (the RIS leak fix), but it must not free a grid that never
       existed: on a stack Term holding garbage that was a free() of a bogus pointer. */
    {
        Term lt;
        dirty_stack();
        term_init(&lt, 6, 12, 20);
        CHECK(lt.g.valid == GRID_VALID && lt.g.rows == 6 && lt.g.cols == 12, "term_init on a dirty stack Term");
        term_parse(&lt, (const uint8_t *)"ab\r\ncd", 5);
        CHECK(grid_vprow(&lt.g, 1)[0].ch == 'c', "the fresh term works");
        term_init(&lt, 6, 12, 20);          /* RIS path: the old buffers ARE freed here */
        CHECK(lt.g.valid == GRID_VALID && lt.g.row[0] && lt.g.row[0][0].ch == 0, "re-init clears and stays valid");
        term_destroy(&lt);
        CHECK(lt.g.valid == 0, "term_destroy clears the valid flag");
        term_init(&lt, 4, 8, 4);        /* destroy left it zeroed: re-init must not free anything */
        CHECK(lt.g.valid == GRID_VALID && lt.g.rows == 4, "init after destroy is safe");
        term_destroy(&lt);
    }

    /* --- config: the app_id is configurable ---------------------------------------------------
       config.c is linked into this binary, so the file format itself is testable here. The expected
       default is DEF_APP_ID (kai-term.h), the same macro config_default() and main.c use. */
    {
        Config c;
        config_default(&c);
        CHECK(!strcmp(c.app_id, DEF_APP_ID) && !strcmp(c.app_id, "Kai-Term"),
              "config: app_id defaults to Kai-Term");
        const char *path = "/tmp/kterm_test_config.conf";
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable");
        if (f) {
            fputs("app_id = com.example.myterm   # trailing comment\n", f);
            fclose(f);
            config_load(&c, path);
            CHECK(!strcmp(c.app_id, "com.example.myterm"), "config: app_id read from the file");
            f = fopen(path, "w");
            fputs("app_id =\n", f);
            fclose(f);
            config_load(&c, path);
            CHECK(!strcmp(c.app_id, DEF_APP_ID) && !strcmp(c.app_id, "Kai-Term"),
                  "config: an empty app_id falls back to Kai-Term");
            f = fopen(path, "w");
            fputs("app_id = io.github.kaida.kterm\n", f);
            fclose(f);
            config_load(&c, path);
            CHECK(!strcmp(c.app_id, "io.github.kaida.kterm"), "config: dotted app_id survives intact");
            remove(path);
        }
    }

    /* --- config: the copy_on_release toggle ---------------------------------------------------
       Default OFF: releasing a selection drag must not touch the clipboard (Ctrl+Shift+C does).
       One cfg_bool() parser now backs both this and clear_scrollback, so both are checked. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config2.conf";
        config_default(&c);
        CHECK(c.copy_on_release == 0, "config: copy_on_release defaults to off");
        CHECK(c.clear_scrollback == 1, "config: clear_scrollback defaults to on");
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (toggles)");
        if (f) {
            fclose(f);
            f = fopen(path, "w");
            fputs("copy_on_release = 1\nclear_scrollback = 0\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.copy_on_release == 1, "config: copy_on_release = 1 turns it on");
            CHECK(c.clear_scrollback == 0, "config: clear_scrollback = 0 turns it off");
            f = fopen(path, "w");
            fputs("copy_on_release = FALSE\nclear_scrollback = NO\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.copy_on_release == 0, "config: copy_on_release = FALSE is off (case-insensitive)");
            CHECK(c.clear_scrollback == 0, "config: clear_scrollback = NO is off (case-insensitive)");
            f = fopen(path, "w");
            fputs("copy_on_release = off\nclear_scrollback = true\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.copy_on_release == 0, "config: copy_on_release = off is off");
            CHECK(c.clear_scrollback == 1, "config: clear_scrollback = true is on");
            remove(path);
        }
    }

    /* --- config: the key-repeat settings ------------------------------------------------------
       A Wayland compositor never sends repeat events, so Kai-Term generates them itself and both
       numbers are its own: repeat_delay (ms before the first repeat, 0 = off) and repeat_rate
       (repeats per second afterwards). Both are clamped so a typo cannot stall or flood the PTY. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config3.conf";
        config_default(&c);
        CHECK(c.repeat_delay == 300, "config: repeat_delay defaults to 300 ms");
        CHECK(c.repeat_rate == 30, "config: repeat_rate defaults to 30/s");
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (key repeat)");
        if (f) {
            fclose(f);
            f = fopen(path, "w");
            fputs("repeat_delay = 500\nrepeat_rate = 12\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.repeat_delay == 500, "config: repeat_delay read from the file");
            CHECK(c.repeat_rate == 12, "config: repeat_rate read from the file");
            f = fopen(path, "w");
            fputs("repeat_delay = 0\n", f);   /* the escape hatch: no Kai-Term-generated repeat */
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.repeat_delay == 0, "config: repeat_delay = 0 turns the repeat off");
            CHECK(c.repeat_rate == 30, "config: repeat_rate is untouched by another key's line");
            f = fopen(path, "w");
            fputs("repeat_delay = 99999\nrepeat_rate = 0\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.repeat_delay == 5000, "config: repeat_delay is clamped to 5000 ms");
            CHECK(c.repeat_rate == 1, "config: repeat_rate is clamped to at least 1/s");
            f = fopen(path, "w");
            fputs("repeat_delay = -5\nrepeat_rate = 5000\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.repeat_delay == 0, "config: a negative repeat_delay clamps to 0 (off)");
            CHECK(c.repeat_rate == 200, "config: repeat_rate is clamped to at most 200/s");
            remove(path);
        }
    }

    /* ---------- a viewport grown past half of rcap (the row[]-overrun segfault) ----------
       rcap is fixed at (rows at init + scrollback), so growing the viewport shrinks the scrollback
       that fits: the bound on scroll_off/live_off is rcap - rows, NOT scrollback_max. Feeding more
       lines than that used to make scroll_up blank row[rcap] — a heap-buffer-overflow ASan found in
       a real window (`seq 1 5000`: 30 configured rows, a 62-row window, 2000 scrollback). */
    {
        term_destroy(&T);
        term_init(&T, 4, 20, 20);            /* rcap = 24, scrollback_max = 20 */
        term_resize(&T, 12, 20);             /* usable scrollback is now 24 - 12 = 12 */
        CHECK(T.g.rcap == 24 && T.g.rows == 12, "grow: rcap stays fixed while the viewport grows");
        CHECK(T.g.scrollback_max == 20 && T.g.rcap - T.g.rows == 12,
              "grow: the usable scrollback is smaller than scrollback_max");
        for (int i = 1; i <= 60; i++) {
            char l[32];
            snprintf(l, sizeof l, "G%02d\r\n", i);
            feed(l);
        }
        feed("NEWEST");
        CHECK(T.g.scroll_off + T.g.rows <= T.g.rcap, "grow: scrolling keeps used <= rcap");
        CHECK(T.g.live_off <= T.g.rcap - T.g.rows, "grow: live_off stays inside the grid");
        CHECK(T.g.live_off == T.g.scroll_off, "grow: printing keeps the view at the live anchor");
        char linebuf[64];
        row_text(T.g.rows - 1, linebuf);
        CHECK(strstr(linebuf, "NEWEST") != NULL, "grow: the newest line is on screen");
        CHECK(content_rows() == T.g.rcap, "grow: every slot holds content once the scrollback fills");
        feed("\x1b[1T");                     /* scroll down: must stay inside the grid */
        CHECK(T.g.scroll_off + T.g.rows <= T.g.rcap, "grow: scroll-down stays inside the grid");
        term_resize(&T, 4, 20);              /* shrink again, then overflow once more */
        for (int i = 1; i <= 40; i++) {
            char l[32];
            snprintf(l, sizeof l, "H%02d\r\n", i);
            feed(l);
        }
        feed("LAST");
        CHECK(T.g.scroll_off + T.g.rows <= T.g.rcap, "shrink after grow: used <= rcap");
        CHECK(T.g.live_off <= T.g.scrollback_max && T.g.live_off <= T.g.rcap - T.g.rows,
              "shrink after grow: live_off within both bounds");
        row_text(T.g.rows - 1, linebuf);
        CHECK(strstr(linebuf, "LAST") != NULL, "shrink after grow: the newest line is on screen");
    }

    /* --- multiplexing: the tab bookkeeping (src/mux.c) ----------------------------------------
       One window hosts several sessions. Only the bookkeeping is testable here (the PTYs and the
       window belong to main.c), and that is exactly where the bugs live: an active index pointing
       past the end, a grid left behind or freed twice, a shifted Tab keeping a pointer into its old
       home, or a limit that can be exceeded. */
    {
        Mux MX;
        char ttl[128];

        mux_init(&MX, 8);
        CHECK(MX.max == 8, "mux: max_tabs kept");
        mux_init(&MX, 0);
        CHECK(MX.max == 1, "mux: max_tabs 0 clamps to 1 (a terminal must keep its first tab)");
        mux_init(&MX, TAB_MAX + 500);
        CHECK(MX.max == TAB_MAX, "mux: max_tabs clamps to TAB_MAX");
        CHECK(mux_count(&MX) == 0 && mux_active(&MX) == -1 && mux_cur(&MX) == NULL,
              "mux: an empty mux has no tab");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, DEF_TITLE), "mux: an empty mux still titles the window Kai-Term");
        CHECK(mux_next(&MX) == -1 && mux_prev(&MX) == -1 && mux_goto(&MX, 0) == -1,
              "mux: next/prev/goto on an empty mux do nothing");

        mux_init(&MX, 3);
        int t0 = mux_open(&MX, 6, 20, 50);
        CHECK(t0 == 0 && mux_count(&MX) == 1 && mux_active(&MX) == 0,
              "mux: the first tab is index 0 and the visible one");
        CHECK(mux_cur(&MX) == &MX.tab[0], "mux: mux_cur names the visible tab");
        CHECK(MX.tab[0].term.g.rows == 6 && MX.tab[0].term.g.cols == 20,
              "mux: a new tab gets the requested grid");
        CHECK(MX.tab[0].pty.mfd == -1 && MX.tab[0].pty.child == 0,
              "mux: a new tab has no PTY yet (main.c owns spawning it)");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, DEF_TITLE), "mux: one tab keeps the pre-multiplexing title exactly");

        int t1 = mux_open(&MX, 6, 20, 50);
        int t2 = mux_open(&MX, 6, 20, 50);
        CHECK(t1 == 1 && t2 == 2 && mux_count(&MX) == 3, "mux: tabs stay dense 0..n-1");
        CHECK(mux_active(&MX) == 2, "mux: opening a tab makes it the visible one");
        CHECK(mux_open(&MX, 6, 20, 50) == -1 && mux_count(&MX) == 3,
              "mux: max_tabs stops a fourth tab");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, "Kai-Term [3/3]"), "mux: the title says which tab of how many");

        /* Mark every tab: a distinct glyph in cell (0,0) and a distinct OSC title. The title also
           arms Term.ps.t (osc_end() reads the parser's back-pointer), which is what the shift on
           close must repair. One tab is put on the alt screen so mux_close() has to free BOTH of
           its grids (ASan checks that nothing leaks or is freed twice). */
        for (int i = 0; i < 3; i++) {
            char mark = (char)('A' + i);
            term_parse(&MX.tab[i].term, (const uint8_t *)&mark, 1);
            char seq[32];
            snprintf(seq, sizeof seq, "\x1b]0;T%d\x07", i);
            term_parse(&MX.tab[i].term, (const uint8_t *)seq, strlen(seq));
        }
        const char *alt = "\x1b[?1049h";
        term_parse(&MX.tab[2].term, (const uint8_t *)alt, strlen(alt));
        CHECK(MX.tab[2].term.alt_screen == 1, "mux: tab 2 is on the alternate screen");
        CHECK(grid_vprow(&MX.tab[2].term.g, 0)[0].ch == 0, "mux: the alt screen starts blank");

        CHECK(mux_goto(&MX, 0) == 0, "mux: goto 0");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, "Kai-Term [1/3]"), "mux: the title follows the visible tab");
        CHECK(mux_goto(&MX, 99) == 2 && mux_goto(&MX, -7) == 0,
              "mux: goto clamps instead of rejecting an out-of-range index");
        CHECK(mux_next(&MX) == 1 && mux_next(&MX) == 2 && mux_next(&MX) == 0,
              "mux: next wraps around");
        CHECK(mux_prev(&MX) == 2, "mux: prev wraps around backwards");

        CHECK(mux_touch(&MX, 1) == 1 && mux_touch(&MX, 1) == 0,
              "mux: touch marks a hidden tab once, then reports nothing new");
        CHECK(mux_touch(&MX, mux_active(&MX)) == 0, "mux: the visible tab never marks itself");
        mux_touch(&MX, 0);          /* the other hidden tab */
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, "Kai-Term [3/3] !2"), "mux: unread output on hidden tabs is reported");
        CHECK(mux_goto(&MX, 1) == 1, "mux: goto 1");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, "Kai-Term [2/3] !1"), "mux: looking at a tab clears its unread mark");

        /* Close the MIDDLE tab while it is visible: the later one must slide down with its content,
           its parser back-pointer must point at its new home, and the vacated slot must be dead. */
        CHECK(mux_close(&MX, 1) == 1 && mux_count(&MX) == 2,
              "mux: closing the visible middle tab keeps two tabs and stays in bounds");
        CHECK(grid_vprow(&MX.tab[1].term.g, 0)[0].ch == 0 &&
              !strcmp(MX.tab[1].term.title, "T2"),
              "mux: the tab after a closed one slides down with its content intact");
        CHECK(MX.tab[1].term.ps.t == &MX.tab[1].term,
              "mux: a moved tab's parser points at its new home (ps.t re-armed)");
        CHECK(MX.tab[2].term.g.valid != GRID_VALID && MX.tab[2].term.g.row == NULL,
              "mux: the vacated slot is zeroed, so its grids cannot be freed twice");
        CHECK(mux_close(&MX, 1) == 0 && mux_count(&MX) == 1 && mux_active(&MX) == 0,
              "mux: closing the last-indexed visible tab steps back to the one before it");
        mux_title(&MX, ttl, sizeof ttl);
        CHECK(!strcmp(ttl, DEF_TITLE), "mux: back to one tab, back to the plain title");
        CHECK(mux_close(&MX, 7) == mux_active(&MX) && mux_count(&MX) == 1,
              "mux: closing a tab that does not exist changes nothing");
        CHECK(mux_close(&MX, 0) == -1 && mux_count(&MX) == 0 && mux_active(&MX) == -1 &&
              mux_cur(&MX) == NULL, "mux: closing the last tab reports -1 so main.c can quit");

        CHECK(mux_open(&MX, 4, 10, 20) == 0, "mux: a slot is reusable after every tab closed");
        CHECK(grid_vprow(&MX.tab[0].term.g, 0)[0].ch == 0 && MX.tab[0].term.title[0] == 0,
              "mux: a reused slot starts empty, not as the old tab");
        mux_close(&MX, 0);
    }

    /* --- config: the key-binding specs ("Ctrl+Shift+T") ----------------------------------------
       Every tab action is rebindable from the config file, so the spec parser is user-facing. The
       two rules that protect the user from themselves: a binding with NO modifier is refused (it
       would take a plain typing key away from the shell), and a spec that does not parse keeps the
       binding already in place rather than silently disabling the action. */
    {
        KeyBind KB;
        CHECK(keybind_parse("Ctrl+Shift+T", &KB) && KB.set == 1 &&
              KB.mods == (KB_CTRL | KB_SHIFT) && KB.sym == 't',
              "keybind: Ctrl+Shift+T parses to ctrl+shift+t");
        CHECK(keybind_parse("ctrl+shift+t", &KB) && KB.sym == 't',
              "keybind: the spec is case-insensitive (it names the key, not the Shift state)");
        CHECK(keybind_parse(" Ctrl + Shift + T ", &KB) && KB.mods == (KB_CTRL | KB_SHIFT) &&
              KB.sym == 't', "keybind: spaces around the tokens are ignored");
        CHECK(keybind_parse("Control+Shift+Right", &KB) && KB.sym == KB_KEY_Right &&
              KB.sym == 0xff53, "keybind: a named key uses the X keysym number xkb returns");
        CHECK(keybind_parse("Ctrl+Shift+F12", &KB) && KB.sym == KB_KEY_F12, "keybind: F12");
        CHECK(keybind_parse("Alt+1", &KB) && KB.sym == '1' && KB.mods == KB_ALT, "keybind: a digit key");
        CHECK(keybind_parse("Mod1+Mod4+Up", &KB) && KB.mods == (KB_ALT | KB_SUPER) &&
              KB.sym == KB_KEY_Up, "keybind: xkb modifier names are accepted");
        CHECK(!keybind_parse("T", &KB) && KB.set == 0,
              "keybind: a binding with no modifier is refused (it would swallow typing)");
        CHECK(!keybind_parse("Ctrl+Bogus", &KB) && KB.set == 0, "keybind: an unknown key name is refused");
        CHECK(!keybind_parse("Ctrl+Shift+T+U", &KB), "keybind: two keys in one spec is refused");
        CHECK(!keybind_parse("Ctrl", &KB), "keybind: modifiers with no key is refused");
        CHECK(!keybind_parse("", &KB), "keybind: an empty spec is refused");

        unsigned MM = 7;
        CHECK(keymods_parse("Ctrl+Shift", &MM) && MM == (KB_CTRL | KB_SHIFT), "keymods: Ctrl+Shift");
        MM = 7;
        CHECK(keymods_parse("mod1+mod4", &MM) && MM == (KB_ALT | KB_SUPER), "keymods: xkb names work");
        MM = 7;
        CHECK(!keymods_parse("Ctrl+T", &MM) && MM == 0, "keymods: a key in a modifier-only spec is refused");
        MM = 7;
        CHECK(!keymods_parse("", &MM) && MM == 0, "keymods: an empty spec is refused");
    }

    /* --- config: the multiplexing settings ---------------------------------------------------
       max_tabs plus the five bindings. A binding the file does not mention keeps its default, and a
       bad spec must not disable the action the user was trying to change. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config4.conf";
        config_default(&c);
        CHECK(c.max_tabs == 8, "config: max_tabs defaults to 8");
        CHECK(c.key_new_tab.set && c.key_new_tab.sym == 't' &&
              c.key_new_tab.mods == (KB_CTRL | KB_SHIFT), "config: the default new-tab binding");
        CHECK(c.key_close_tab.set && c.key_close_tab.sym == 'w' &&
              c.key_close_tab.mods == (KB_CTRL | KB_SHIFT), "config: the default close-tab binding");
        CHECK(c.key_next_tab.set && c.key_next_tab.sym == KB_KEY_Right,
              "config: the default next-tab binding");
        CHECK(c.key_prev_tab.set && c.key_prev_tab.sym == KB_KEY_Left,
              "config: the default prev-tab binding");
        CHECK(c.tab_prefix_mods == (KB_CTRL | KB_SHIFT), "config: the default tab prefix is Ctrl+Shift");
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (key bindings)");
        if (f) {
            fclose(f);
            f = fopen(path, "w");
            fputs("key_new_tab = Alt+N\nkey_close_tab = Ctrl+Alt+W\n"
                  "key_next_tab = Super+Right\nkey_tab_prefix = Alt\nmax_tabs = 3\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.key_new_tab.set && c.key_new_tab.sym == 'n' && c.key_new_tab.mods == KB_ALT,
                  "config: key_new_tab = Alt+N rebinds it");
            CHECK(c.key_close_tab.mods == (KB_CTRL | KB_ALT), "config: key_close_tab = Ctrl+Alt+W");
            CHECK(c.key_next_tab.mods == KB_SUPER, "config: key_next_tab = Super+Right");
            CHECK(c.tab_prefix_mods == KB_ALT, "config: key_tab_prefix = Alt");
            CHECK(c.max_tabs == 3, "config: max_tabs read from the file");
            CHECK(c.key_prev_tab.set && c.key_prev_tab.sym == KB_KEY_Left,
                  "config: a binding the file does not mention keeps its default");

            f = fopen(path, "w");
            fputs("key_new_tab = none\nkey_tab_prefix = off\nmax_tabs = 0\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.key_new_tab.set == 0, "config: key_new_tab = none unbinds the action");
            CHECK(c.tab_prefix_mods == 0, "config: key_tab_prefix = off disables the digit shortcut");
            CHECK(c.max_tabs == 1, "config: max_tabs = 0 clamps to 1");

            f = fopen(path, "w");
            fputs("key_new_tab =\nkey_close_tab = Ctrl+Shift+Nope\nmax_tabs = 99999\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.key_new_tab.set == 0, "config: an empty value unbinds too");
            CHECK(c.key_close_tab.set && c.key_close_tab.sym == 'w',
                  "config: an unparseable binding keeps the default instead of disabling the action");
            CHECK(c.max_tabs == TAB_MAX, "config: max_tabs clamps to TAB_MAX");
            remove(path);
        }
    }

    /* --- config: how the sessions are shown (tab_bar / split_max / key_toggle_layout) ---------
       These three decide what the user SEES of the multiplexing, so each value has to be readable and a
       bad one must never turn the bar off by accident. */
    {
        Config c;
        const char *path = "/tmp/kterm_test_config5.conf";
        config_default(&c);
        CHECK(c.tab_bar == BAR_AUTO, "config: tab_bar defaults to auto (bar from the second session)");
        CHECK(c.split_max == 4, "config: split_max defaults to 4");
        CHECK(c.key_toggle_layout.set && c.key_toggle_layout.sym == 's' &&
              c.key_toggle_layout.mods == (KB_CTRL | KB_SHIFT),
              "config: the default layout toggle is Ctrl+Shift+S");
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "config: temp config file writable (layout settings)");
        if (f) {
            fputs("tab_bar = always\nsplit_max = 2\nkey_toggle_layout = Alt+Return\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.tab_bar == BAR_ALWAYS, "config: tab_bar = always");
            CHECK(c.split_max == 2, "config: split_max = 2");
            CHECK(c.key_toggle_layout.set && c.key_toggle_layout.sym == KB_KEY_Return &&
                  c.key_toggle_layout.mods == KB_ALT, "config: key_toggle_layout = Alt+Return");

            f = fopen(path, "w");
            fputs("tab_bar = off\nsplit_max = 0\nkey_toggle_layout = none\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.tab_bar == BAR_OFF, "config: tab_bar = off hides the bar completely");
            CHECK(c.split_max == 1, "config: split_max = 0 clamps to 1 (a split view needs a pane)");
            CHECK(c.key_toggle_layout.set == 0, "config: key_toggle_layout = none unbinds the toggle");

            f = fopen(path, "w");
            fputs("tab_bar = sometimes\nsplit_max = 99999\n", f);
            fclose(f);
            config_default(&c);
            config_load(&c, path);
            CHECK(c.tab_bar == BAR_AUTO, "config: an unknown tab_bar value keeps the current one");
            CHECK(c.split_max == TAB_MAX, "config: split_max clamps to TAB_MAX");
            remove(path);
        }
    }

    /* --- layout: the tab bar and the side-by-side panes ----------------------------------------
       The bar, the click targets and the panes are all decided by one pure function, so every rule that
       the user can see (or trip over) is checkable here: the single-session window must not change shape,
       a click must name the tab it is drawn on, a pane's grid must fit its rect, and the pointer mapping
       (col = (ptr_x - pane.x)/cell_w) must stay exact because the leftover pixels stay at the right. */
    {
        LayoutReq rq;
        Layout L;

        /* One session, bar off: exactly the geometry the terminal had before multiplexing. */
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 1, 0, 4, BAR_OFF);
        layout_compute(&rq, &L);
        int gcols, grows, gxoff, gyoff;
        term_grid_geometry(936, 1002, 8, 17, 8, &gcols, &grows, &gxoff, &gyoff);
        CHECK(!L.bar_on && L.nitem == 0 && L.npane == 1,
              "layout: one session with tab_bar off draws one pane and no bar");
        CHECK(L.pane[0].x == gxoff && L.pane[0].y == gyoff &&
              L.pane[0].cols == gcols && L.pane[0].rows == grows,
              "layout: one pane with the bar off is exactly the old single-grid geometry");
        CHECK(L.pane[0].w == 936 - 16 && L.pane[0].h == 1002 - 16,
              "layout: the pane is the window minus the margin on both sides");
        CHECK(L.pane[0].cols * 8 <= L.pane[0].w && L.pane[0].rows * 17 <= L.pane[0].h,
              "layout: a pane's grid never grows past its rect");

        /* The bar appears by itself at the second session (BAR_AUTO). */
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 2, 1, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.bar_on && L.nitem == 2 && L.npane == 1,
              "layout: BAR_AUTO shows the bar from the second session on");
        CHECK(L.bar_y == 8 && L.bar_h == 17 && L.bar_x == 8 && L.bar_w == 920,
              "layout: the bar is one cell row inside the top margin");
        CHECK(L.pane[0].y == 8 + 17 + 8 && L.pane[0].h == 1002 - (8 + 17 + 8) - 8,
              "layout: the grid starts below the bar + gap and keeps its bottom margin");
        CHECK(L.pane[0].rows == (1002 - 33 - 8) / 17, "layout: the bar costs exactly one text row");
        CHECK(L.item[0].x == 8 && L.item[1].x == 8 + 460 && L.item[0].w == 460,
              "layout: bar items are equal width, leftover px at the right");
        CHECK(L.item[0].tab == 0 && L.item[1].tab == 1, "layout: an item names its session by index");
        CHECK(L.item[1].active == 1 && L.item[0].active == 0,
              "layout: the item of the visible session is the highlighted one");
        CHECK(layout_hit_bar(&L, 100, 12) == 0 && layout_hit_bar(&L, 500, 12) == 1,
              "layout: a click in the bar names the item it lands on (what makes tabs clickable)");
        CHECK(layout_hit_bar(&L, 100, 100) == -1 && layout_hit_bar(&L, 4, 12) == -1,
              "layout: a click below the bar or in the margin is not in the bar");
        CHECK(layout_hit_pane(&L, 100, 100) == 0 && layout_hit_pane(&L, 100, 12) == -1,
              "layout: the bar is not part of any pane");

        /* Labels: number, unread mark, OSC title, and a cut that fits the item. */
        CHECK(!strcmp(L.item[0].label, "1 shell"),
              "layout: an unnamed session is labelled by its number + shell");
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 2, 1, 4, BAR_AUTO);
        rq.tab[0].attention = 1;
        rq.tab[1].title = "vim";
        layout_compute(&rq, &L);
        CHECK(!strcmp(L.item[0].label, "1 *shell"),
              "layout: unread output on a hidden session marks its item");
        CHECK(!strcmp(L.item[1].label, "2 vim"), "layout: a session's OSC title is its label");

        lreq(&rq, LAYOUT_TABS, 80, 400, 8, 17, 8, 8, 2, 0, 4, BAR_AUTO);   /* items 32 px = 4 columns */
        rq.tab[0].title = "a\xc3\xa9" "b";                                  /* "aéb" (\x.. is greedy) */
        layout_compute(&rq, &L);
        CHECK(!strcmp(L.item[0].label, "1 a\xc3\xa9"),
              "layout: a label is cut to the item width at a UTF-8 codepoint edge, never mid-sequence");
        for (int i = 0; i < L.nitem; i++)
            if (utf8_len(L.item[i].label) * 8 > L.item[i].w) {
                CHECK(0, "layout: a label never needs more columns than its item has");
                break;
            }

        /* Side-by-side: equal panes, the gap between them, each grid fitted to its own rect. */
        lreq(&rq, LAYOUT_SPLIT, 936, 1002, 8, 17, 8, 8, 3, 0, 3, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.npane == 3 && L.nitem == 3, "layout: split mode draws one pane per session");
        CHECK(L.pane[0].x == 8 && L.pane[0].w == (920 - 2 * 8) / 3,
              "layout: split panes share the width equally");
        CHECK(L.pane[1].x == 8 + 301 + 8 && L.pane[2].x == 8 + 2 * (301 + 8),
              "layout: the gap separates the panes");
        CHECK(L.pane[0].cols == 301 / 8 && L.pane[0].rows == (1002 - 33 - 8) / 17,
              "layout: every pane gets the grid that fits its own rect");
        int overlap = 0;
        for (int i = 0; i + 1 < L.npane; i++)
            if (L.pane[i].x + L.pane[i].w > L.pane[i + 1].x ||
                L.pane[i].y != L.pane[i + 1].y || L.pane[i].h != L.pane[i + 1].h)
                overlap = 1;
        CHECK(!overlap, "layout: panes never overlap and share one top edge and height");
        CHECK(layout_hit_pane(&L, L.pane[1].x + 5, 200) == 1,
              "layout: a click in a pane names that pane (click to focus)");
        CHECK(layout_hit_pane(&L, L.pane[0].x + L.pane[0].w + 2, 200) == -1,
              "layout: a click in the gap between panes belongs to no pane");
        CHECK(layout_pane_of_tab(&L, 2) == 2 && layout_pane_of_tab(&L, 5) == -1,
              "layout: pane_of_tab finds a drawn session and rejects one that is not drawn");

        /* More sessions than split_max: the shown window slides to contain the active one. */
        lreq(&rq, LAYOUT_SPLIT, 936, 1002, 8, 17, 8, 8, 6, 0, 3, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.npane == 3 && L.pane[0].tab == 0 && L.pane[2].tab == 2,
              "layout: split_max caps the panes, starting at the first session");
        CHECK(layout_pane_of_tab(&L, 5) == -1 && layout_pane_of_tab(&L, 0) == 0,
              "layout: a session outside the visible pane window has no pane");
        rq.active = 5;
        layout_compute(&rq, &L);
        CHECK(L.pane[0].tab == 3 && L.pane[2].tab == 5,
              "layout: the pane window slides to the end so the last session is reachable");
        rq.active = 3;
        layout_compute(&rq, &L);
        CHECK(L.pane[0].tab == 2 && L.pane[2].tab == 4, "layout: the pane window centres on the active one");
        CHECK(L.nitem == 6, "layout: the bar still lists every session, drawn in a pane or not");

        /* Tab mode: the single pane shows the visible session, and only it has a pane. */
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 3, 2, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.npane == 1 && L.pane[0].tab == 2, "layout: tab mode draws the visible session only");
        CHECK(layout_pane_of_tab(&L, 2) == 0 && layout_pane_of_tab(&L, 0) == -1,
              "layout: the hidden sessions of tab mode have no pane (their output marks the bar instead)");

        /* Degenerate windows: the terminal wins over the chrome, and a grid is never empty. */
        lreq(&rq, LAYOUT_TABS, 30, 30, 8, 17, 20, 8, 2, 0, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(!L.bar_on, "layout: a window too short for bar + one row drops the bar, not the text");
        CHECK(L.npane == 1 && L.pane[0].cols >= 1 && L.pane[0].rows >= 1,
              "layout: even a tiny window keeps a usable grid (the margin is capped)");
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 0, -1, 4, BAR_ALWAYS);
        layout_compute(&rq, &L);
        CHECK(L.npane == 0 && L.nitem == 0 && !L.bar_on,
              "layout: no sessions means no pane and no bar, even with tab_bar = always");
        lreq(&rq, LAYOUT_SPLIT, 100, 100, 8, 17, 0, 0, 2, 0, 2, BAR_AUTO);
        layout_compute(&rq, &L);
        CHECK(L.pane[0].x == 0 && L.pane[1].x == 50 && L.pane[0].cols == 6 && L.pane[0].rows == 4,
              "layout: padding 0 keeps the old edge-to-edge layout with adjacent panes");
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, -5, -5, 2, 0, 4, BAR_ALWAYS);
        layout_compute(&rq, &L);
        CHECK(L.bar_on && L.pane[0].x == 0 && L.pane[0].y == 17,
              "layout: a negative padding or gap is treated as 0");

        /* layout_grid_size(): the grid size EVERY session of a layout gets, drawn or not. src/main.c
           resizes all of its sessions to this, which is what makes a tab switch cost nothing (no resize,
           no SIGWINCH, no reflow on every switch) — so it has to be one size for the whole layout. */
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 3, 2, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        int lcols, lrows;
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == L.pane[0].cols && lrows == L.pane[0].rows,
              "layout: grid_size names the grid of this layout's panes");
        int unequal = 0;
        for (int i = 0; i < L.npane; i++)
            if (L.pane[i].cols != lcols || L.pane[i].rows != lrows)
                unequal = 1;
        CHECK(!unequal, "layout: every pane of one layout has the same grid, so switching never resizes");
        lreq(&rq, LAYOUT_SPLIT, 936, 1002, 8, 17, 8, 8, 6, 5, 3, BAR_AUTO);
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(L.npane == 3 && lcols == L.pane[2].cols && lrows == L.pane[2].rows,
              "layout: a session outside the visible pane window still gets the size its pane will have");
        lreq(&rq, LAYOUT_TABS, 936, 1002, 8, 17, 8, 8, 0, -1, 4, BAR_ALWAYS);
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == 0 && lrows == 0,
              "layout: no pane means no grid size, so a caller must not resize to it");

        /* layout_initial_size(): the window size src/main.c asks the compositor for at startup has to be
           big enough that layout_compute() gives back EXACTLY the configured cols x rows. The bar costs one
           cell ROW plus the gap — cell_h, not cell_w — and the round trip below is the only thing that can
           catch that typo, because the two halves live in one module the tests link while main.c is in none
           of them. A non-square cell (8x17) is what makes the difference visible. */
        int iw, ih;
        layout_initial_size(80, 30, 8, 17, 8, 8, BAR_ALWAYS, &iw, &ih);
        CHECK(iw == 80 * 8 + 16 && ih == 30 * 17 + 16 + 17 + 8,
              "layout: the startup window is the grid + the margin + one bar ROW and its gap");
        lreq(&rq, LAYOUT_TABS, iw, ih, 8, 17, 8, 8, 1, 0, 4, BAR_ALWAYS);
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == 80 && lrows == 30,
              "layout: a tab_bar = always startup really gets the configured cols x rows back");
        layout_initial_size(80, 30, 8, 17, 8, 8, BAR_AUTO, &iw, &ih);
        CHECK(iw == 80 * 8 + 16 && ih == 30 * 17 + 16,
              "layout: with tab_bar = auto the single starting session needs no bar row");
        lreq(&rq, LAYOUT_TABS, iw, ih, 8, 17, 8, 8, 1, 0, 4, BAR_AUTO);
        layout_compute(&rq, &L);
        layout_grid_size(&L, &lcols, &lrows);
        CHECK(lcols == 80 && lrows == 30,
              "layout: a tab_bar = auto startup gets the configured cols x rows back too");
        layout_initial_size(80, 30, 8, 8, 0, 0, BAR_ALWAYS, &iw, &ih);
        CHECK(iw == 640 && ih == 248,
              "layout: padding 0 with a square cell gives the edge-to-edge startup size");
    }

    term_destroy(&T);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
