/* Kai-Term — multiplexing: several terminal sessions (tabs) in one window.

   This file is deliberately pure bookkeeping: no Wayland, no EGL, no file descriptors, no fork. That
   is what makes the tab logic testable headlessly (tests/test_vt.c links it next to vt.c). src/main.c
   owns everything that touches the outside world — spawning and reaping the shell, the PTY descriptor
   in epoll, the window title — and this file owns the Tab array, the active index, the per-tab
   attention flag and the Term lifetime.

   The array is kept DENSE (tabs occupy 0..n-1) so "tab 3" means the same thing to the user, to
   mux_goto() and to main.c's fd -> tab lookup. Closing a tab therefore shifts the later ones down;
   the one hazard that creates is Term.ps.t, a pointer a Term keeps to itself, which mux_close()
   re-arms for every moved tab (see the comment there). */
#include "kai-term.h"

void mux_init(Mux *m, int max_tabs)
{
    memset(m, 0, sizeof(*m));
    /* A max of 0 or a negative one is a config mistake, not a request for a terminal that cannot
       open a session: clamp to 1 so the window always keeps the tab it started with. */
    m->max = iclamp_(max_tabs > 0 ? max_tabs : 1, 1, TAB_MAX);
    m->active = -1;
}

int mux_count(const Mux *m) { return m->n; }
int mux_active(const Mux *m) { return m->active; }

Tab *mux_cur(Mux *m)
{
    if (m->active < 0 || m->active >= m->n)
        return NULL;
    return &m->tab[m->active];
}

int mux_touch(Mux *m, int idx)
{
    if (idx < 0 || idx >= m->n || idx == m->active)
        return 0;
    if (m->tab[idx].attention)
        return 0;          /* already marked: the caller has nothing new to show */
    m->tab[idx].attention = 1;
    return 1;
}

int mux_open(Mux *m, int rows, int cols, int scrollback)
{
    if (m->n >= m->max) {
        klog(L_WARN, "mux: not opening another tab: max_tabs = %d reached", m->max);
        return -1;
    }
    int i = m->n++;
    Tab *t = &m->tab[i];
    /* The slot may hold the leftovers of a tab that was closed and shifted away; term_init() memsets
       the whole Term, so nothing of the old one survives. The PTY fields are set to "no process":
       main.c spawns it and keeps the descriptor, mux never does. */
    term_init(&t->term, rows, cols, scrollback);
    t->pty.mfd = -1;
    t->pty.child = 0;
    t->pty.rows = rows;
    t->pty.cols = cols;
    t->attention = 0;
    m->active = i;      /* a new tab is the one the user is looking at */
    return i;
}

int mux_close(Mux *m, int idx)
{
    if (idx < 0 || idx >= m->n) {
        klog(L_WARN, "mux: close request for tab %d, which does not exist (%d open)", idx, m->n);
        return m->active;
    }
    term_destroy(&m->tab[idx].term);

    /* Keep the array dense. Tab is POD (Term holds heap pointers, but they move WITH the struct), so
       a struct copy is the whole move; the vacated slot is zeroed so its grid pointers cannot be
       freed a second time. */
    for (int i = idx; i + 1 < m->n; i++)
        m->tab[i] = m->tab[i + 1];
    m->n--;
    memset(&m->tab[m->n], 0, sizeof(Tab));

    /* Term.ps.t points at the Term itself (vt.c's parser reads it through the Parser). term_parse()
       re-arms it on every call, but nothing may read it BEFORE that first parse, so fix it here for
       every tab that just moved. */
    for (int i = idx; i < m->n; i++)
        m->tab[i].term.ps.t = &m->tab[i].term;

    if (m->n == 0) {
        m->active = -1;
        return -1;                       /* the caller quits: a window with no session is pointless */
    }
    if (idx < m->active)
        m->active--;                     /* the tabs after it slid down, so its index did too */
    else if (idx == m->active)
        m->active = imin_(idx, m->n - 1); /* the tab that took its place, or the new last one */
    m->active = iclamp_(m->active, 0, m->n - 1);
    m->tab[m->active].attention = 0;
    return m->active;
}

int mux_goto(Mux *m, int idx)
{
    if (m->n == 0) {
        m->active = -1;
        return -1;
    }
    /* Clamped rather than rejected: `key_tab_prefix` + 9 on a two-tab window should land on the last
       tab, and a compositor that repeats a stale index must not be able to lose the visible tab. */
    m->active = iclamp_(idx, 0, m->n - 1);
    m->tab[m->active].attention = 0;
    return m->active;
}

int mux_next(Mux *m)
{
    if (m->n == 0)
        return -1;
    return mux_goto(m, (m->active + 1) % m->n);
}

int mux_prev(Mux *m)
{
    if (m->n == 0)
        return -1;
    return mux_goto(m, (m->active + m->n - 1) % m->n);
}

void mux_title(const Mux *m, char *out, size_t cap)
{
    /* One session: the title stays exactly what it was before multiplexing existed, so a user who
       never opens a second tab sees no change at all. */
    if (m->n <= 1) {
        snprintf(out, cap, "%s", DEF_TITLE);
        return;
    }
    int unread = 0;
    for (int i = 0; i < m->n; i++)
        if (m->tab[i].attention)
            unread++;
    if (unread)
        snprintf(out, cap, "%s [%d/%d] !%d", DEF_TITLE, m->active + 1, m->n, unread);
    else
        snprintf(out, cap, "%s [%d/%d]", DEF_TITLE, m->active + 1, m->n);
}
