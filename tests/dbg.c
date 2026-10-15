/* K-Term — grid model sanity checker (no display needed).
 * Verifies the row[] layout invariant after each stage and prints a compact
 * view of the scrollback + viewport row buffers. */
#include "k-term.h"

static Term T;
static int problems;

static void feed(const char *s){ term_parse(&T,(const uint8_t*)s,strlen(s)); }

static void chk(const char *stage){
    Grid *g = &T.g;
    int used = g->scroll_off + g->rows;
    /* the scrollback a viewport of the CURRENT height can hold: growing the viewport eats into it, so
       scrollback_max alone is not a bound (that is what let scroll_up touch row[rcap]). */
    int sbmax = g->scrollback_max < g->rcap - g->rows ? g->scrollback_max : g->rcap - g->rows;
    if (g->scroll_off < 0 || g->scroll_off > sbmax) {
        printf("BAD scroll_off=%d (max %d) after %s\n", g->scroll_off, sbmax, stage);
        problems++;
    }
    if (g->live_off < g->scroll_off || g->live_off > sbmax) {
        printf("BAD live_off=%d (scroll_off=%d max %d) after %s\n",
               g->live_off, g->scroll_off, sbmax, stage);
        problems++;
    }
    if (g->live_off + g->rows > g->rcap) {
        printf("BAD live_off+rows=%d > rcap=%d after %s\n",
               g->live_off + g->rows, g->rcap, stage);
        problems++;
    }
    if (used > g->rcap) {
        printf("BAD used=%d > rcap=%d after %s\n", used, g->rcap, stage);
        problems++;
    }
    for (int i = 0; i < g->rcap; i++) {
        if (!g->row[i]) {
            printf("NULL row[%d] after %s\n", i, stage);
            problems++;
            continue;
        }
        for (int j = i + 1; j < g->rcap; j++)
            if (g->row[j] == g->row[i]) {
                printf("DUP row[%d]==row[%d] after %s\n", i, j, stage);
                problems++;
            }
    }
    printf("%-24s scroll_off=%d live_off=%d rows=%d rcap=%d used=%d |", stage, g->scroll_off,
           g->live_off, g->rows, g->rcap, used);
    for (int i = 0; i < used; i++) {
        Cell *r = g->row[i];
        uint32_t c = r ? r[0].ch : 0;
        char b[8];
        int n = c ? utf8_encode(c, b) : 0;
        printf(" %c%c%s", i < g->scroll_off ? 's' : 'v', c ? b[0] : '.',
               n > 1 ? "?" : "");
    }
    printf("\n");
}

int main(void){
    term_init(&T,10,20,100);
    feed("hello\r\nworld");
    feed("\x1b[3;5HX\x1b[2J\x1b[Habcdef\r\x1b[2K");
    feed("\x1b[2J\x1b[H\x1b[31mR\x1b[0m\x1b[38;2;10;20;30mT\x1b[0m\x1b[48;5;196mU\x1b[0m\x1b[1mB\x1b[0m");
    chk("sgr");
    feed("\x1b[2J\x1b[H");
    for(int i=1;i<=12;i++){char l[32];snprintf(l,sizeof l,"line%02d\r\n",i);feed(l);}
    chk("scroll1");
    feed("\x1b[2J\x1b[H\x1b[1r\x1b[2;4r\x1b[2;1HA\r\nB\r\nC\r\nD");
    chk("region");
    feed("\x1b[1r");
    feed("\x1b[2J\x1b[H");
    for(int i=1;i<=12;i++){char l[32];snprintf(l,sizeof l,"L%02d\r\n",i);feed(l);}
    chk("scroll2");
    feed("\x1b[1T");
    chk("SD");
    feed("\x1b[2M");
    chk("DL-scrollback");
    feed("\x1b[1;1H\x1b[3L");
    chk("IL-scrollback");
    feed("\x1b[2J\x1b[Ha\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    feed("\x1b[2J\x1b[H\xE4\xB8\xADX");
    feed("\x1b[2J\x1b[Htab");
    feed("\x1b[2J\x1b[Habcd\x1b[1;2@\x1b[1;1H");
    feed("\x1b[2J\x1b[Habcd\x1b[1;2P\x1b[1;1H");
    chk("chrop");
    feed("\x1b[2J\x1b[Hr1\r\nr2\r\nr3\x1b[3;1L\x1b[1;1H");
    chk("IL");
    feed("\x1b[2J\x1b[Hr1\r\nr2\r\nr3\x1b[1;1M\x1b[1;1H");
    chk("DL");
    feed("\x1b[2J\x1b[H\x1b[5;6H\x1b""7z\x1b""8Y");
    chk("save");
    /* CSI 3 J (erase saved lines): the viewport rows move to the top slots and the rest are
       blanked, so row[] must stay a NULL-free, duplicate-free permutation. */
    feed("\x1b[2J\x1b[H");
    for(int i=1;i<=12;i++){char l[32];snprintf(l,sizeof l,"E%02d\r\n",i);feed(l);}
    chk("ed3-before");
    feed("\x1b[3J");
    chk("ed3");
    feed("\x1b[3J");                      /* a second ED 3 with nothing to drop */
    chk("ed3-again");
    for(int i=1;i<=30;i++){char l[32];snprintf(l,sizeof l,"f%02d\r\n",i);feed(l);}
    feed("\x1b[3J");
    chk("ed3-deep");
    feed("\x1b[2J\x1b[H");
    for(int i=0;i<25;i++) feed("abcdefghij");
    chk("wrap");
    /* small scrollback: exercise the eviction/recycle path */
    term_destroy(&T);
    term_init(&T,4,20,2);
    for(int i=1;i<=10;i++){char l[32];snprintf(l,sizeof l,"s%02d\r\n",i);feed(l);}
    chk("small-scrollback");
    feed("\x1b[1T\x1b[1T");
    chk("small-SD");
    feed("\x1b[2J\x1b[Htail");
    chk("pre-resize");
    term_resize(&T,2,10);
    chk("resize-shrink");
    term_resize(&T,8,20);
    chk("resize-grow");
    feed("\x1b[3J");                      /* ED 3 with a tiny scrollback (rcap = rows + 2) */
    chk("ed3-small");
    /* REFLOW: a width change joins the soft-wrapped rows and re-breaks them, allocating a fresh row
       buffer per produced line. row[] must still be a NULL-free, duplicate-free permutation of
       exactly rcap buffers after every step, and the scroll anchors must stay inside the grid. */
    term_destroy(&T);
    term_init(&T, 6, 20, 6);
    for (int i = 0; i < 3; i++) { for (int k = 0; k < 5; k++) feed("abcdefghij"); feed("\r\n"); }
    chk("reflow-before");
    term_resize(&T, 6, 7);   chk("reflow-narrow");
    term_resize(&T, 6, 33);  chk("reflow-wide");
    term_resize(&T, 3, 3);   chk("reflow-narrow-both");
    term_resize(&T, 9, 41);  chk("reflow-wide-both");
    feed("after");
    chk("reflow-after");
    /* reflow that overflows a small buffer: the oldest rows are evicted, never duplicated */
    term_destroy(&T);
    term_init(&T, 4, 40, 3);
    for (int i = 0; i < 4; i++) { for (int k = 0; k < 4; k++) feed("abcdefghij"); feed("\r\n"); }
    chk("reflow-overflow-before");
    term_resize(&T, 4, 4);   chk("reflow-overflow");
    term_resize(&T, 2, 40);  chk("reflow-overflow-back");
    /* GROW the viewport past half of rcap and then keep printing: rcap is fixed, so the usable
       scrollback becomes rcap - rows (< scrollback_max). Scrolling past that must evict from the
       oldest end, never walk off the end of row[] (the segfault this stage covers). */
    term_destroy(&T);
    term_init(&T, 4, 20, 20);            /* rcap = 24, scrollback_max = 20 */
    term_resize(&T, 12, 20);             /* usable scrollback is now 24 - 12 = 12 */
    chk("growthen-scroll-before");
    for (int i = 1; i <= 60; i++) { char l[32]; snprintf(l, sizeof l, "G%02d\r\n", i); feed(l); }
    chk("growthen-scroll");
    feed("\x1b[1T\x1b[1T");
    chk("growthen-SD");
    term_resize(&T, 4, 20);
    chk("growthen-shrink");
    for (int i = 1; i <= 40; i++) { char l[32]; snprintf(l, sizeof l, "H%02d\r\n", i); feed(l); }
    chk("growthen-scroll2");
    /* SHRINK with a cursor to preserve (the zoom leftover-lines bug, ISSUES.md #4). The viewport now
       gives up its BOTTOM rows while the cursor has rows below it, which blanks a slot IN PLACE
       instead of sliding the window down one slot — a different code path than the old top-row-into-
       scrollback one, so row[] must be re-checked as a NULL-free, duplicate-free permutation for
       both, plus the alternate screen (grid_resize_plain gets the cursor slot) and a whole zoom sweep
       where cols AND rows change together. */
    term_destroy(&T);
    term_init(&T, 6, 20, 6);                 /* rcap = 12 */
    for (int i = 1; i <= 4; i++) { char l[32]; snprintf(l, sizeof l, "Z%02d\r\n", i); feed(l); }
    feed("\x1b[3;1H");                       /* cursor on viewport row 2, text BELOW it */
    chk("zoom-before");
    term_resize(&T, 3, 9);   chk("zoom-bottom");        /* 3 rows given up from the bottom */
    term_resize(&T, 8, 26);  chk("zoom-back");
    term_resize(&T, 2, 5);   chk("zoom-past-cursor");   /* now the TOP rows go to scrollback */
    term_resize(&T, 6, 20);  chk("zoom-back2");
    for (int cyc = 0; cyc < 3; cyc++) {                 /* a whole zoom sweep */
        term_resize(&T, 4, 11);
        term_resize(&T, 9, 31);
    }
    chk("zoom-sweep");
    feed("after");
    chk("zoom-after");
    feed("\x1b[?1049h");                                 /* alternate screen: no reflow, same rule */
    for (int i = 1; i <= 5; i++) { char l[32]; snprintf(l, sizeof l, "A%02d\r\n", i); feed(l); }
    term_resize(&T, 3, 10);  chk("alt-shrink");
    term_resize(&T, 9, 30);  chk("alt-grow");
    feed("\x1b[?1049l");
    chk("alt-exit");
    term_destroy(&T);
    printf("%s (%d problems)\n", problems ? "MODEL FAIL" : "MODEL OK", problems);
    return problems ? 1 : 0;
}
