/* Kai-Term — the file manager: the model, and nothing else.
 *
 * What this file is: a directory listing, a selection, a context-sensitive preview, and two grids of
 * ordinary Cells. What this file is NOT: no Wayland, no EGL, no threads, no rendering, no file
 * descriptors handed to anyone. That split is what makes the feature both testable and responsive —
 * tests/test_fm.c drives a real directory tree with no window, and src/main.c can call
 * fm_scan_build()/fm_preview_build() on a worker thread because those two functions touch nothing but
 * malloc, the filesystem and their own argument.
 *
 * The panes are Cell grids on purpose. The renderer already knows how to draw a Cell grid with the
 * configured font, colours and passes, so the file manager reads as part of the terminal instead of as
 * a picture of one. Nothing here invents a palette: the base ink and paper come from Config, and only
 * the accents (directory blue, string green, comment grey, ...) are fixed, the way ls colours are.
 *
 * Resource caps, all of them in kai-term.h so a test can name them: FM_ENTRIES_MAX entries per
 * listing, FM_PREVIEW_BYTES of text, FM_PREVIEW_DIR_MAX entries in a directory preview,
 * FM_PREVIEW_FILE_MAX bytes of image file, and the image decoders' own IMG_DIM_MAX/IMG_PIX_MAX.
 *
 * Failure policy: every filesystem call may fail (permission denied, a file that vanished between the
 * listing and the preview, a dangling symlink, an unreadable directory) and every failure ends up as a
 * line of text in a pane plus a log line. Nothing aborts a scan, nothing leaves the model half-updated,
 * and nothing here ever blocks longer than the caps above allow.
 */
#include "kai-term.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

/* ---------- colours ---------- */

/* The accents live in kai-term.h so tests/test_fm.c can pin the colour a token got; the BASE colours
   (fg/bg) are passed to fm_paint() from Config, so the file manager sits inside the terminal's own
   palette instead of next to it. */

/* whole value in unit u, computed with one decimal without floating point: pick the unit, then split
   the value into an integer part and a rounded tenth. */
void fm_human_size(uint64_t n, char *out, size_t cap)
{
    static const char *unit[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB" };
    int u = 0;
    uint64_t v = n;                    /* only decides the unit; n itself stays exact below */
    while (v >= 1024 && u + 1 < (int)(sizeof(unit) / sizeof(unit[0]))) {
        v >>= 10;
        u++;
    }
    if (u == 0) {
        snprintf(out, cap, "%llu B", (unsigned long long)n);
        return;
    }
    uint64_t base = (uint64_t)1 << (10 * u);   /* what one unit of u is worth, in bytes */
    uint64_t whole = n / base;
    uint64_t frac = ((n % base) * 10 + base / 2) / base;
    if (frac == 10) { whole++; frac = 0; }
    if (whole < 100)
        snprintf(out, cap, "%llu.%llu %s", (unsigned long long)whole,
                 (unsigned long long)frac, unit[u]);
    else
        snprintf(out, cap, "%llu %s", (unsigned long long)whole, unit[u]);
}

void fm_human_time(int64_t t, char *out, size_t cap)
{
    time_t tt = (time_t)t;
    struct tm tmv;
    if (!localtime_r(&tt, &tmv)) {
        snprintf(out, cap, "%lld", (long long)t);
        return;
    }
    char buf[64];
    if (!strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tmv))
        snprintf(buf, sizeof buf, "%lld", (long long)t);
    snprintf(out, cap, "%s", buf);
}

/* The ls-style type+permission string: 'd'/'l'/'-'/'p'/'s'/'b'/'c' then the nine permission bits. */
void fm_mode_string(mode_t m, char out[11])
{
    out[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISFIFO(m) ? 'p'
             : S_ISSOCK(m) ? 's' : S_ISBLK(m) ? 'b' : S_ISCHR(m) ? 'c' : '-';
    static const char bits[10] = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        out[1 + i] = (m & (mode_t)(1u << (8 - i))) ? bits[i] : '-';
    /* setuid/setgid/sticky replace the execute slot the ls way: lower case when the execute bit is
       set, upper case when it is not. */
    if (m & S_ISUID) out[3] = (m & S_IXUSR) ? 's' : 'S';
    if (m & S_ISGID) out[6] = (m & S_IXGRP) ? 's' : 'S';
    if (m & S_ISVTX) out[9] = (m & S_IXOTH) ? 't' : 'T';
    out[10] = 0;
}

static const char *kind_name(int kind)
{
    switch (kind) {
    case FMK_DIR:     return "directory";
    case FMK_FILE:    return "regular file";
    case FMK_SYMLINK: return "symlink";
    case FMK_FIFO:    return "FIFO";
    case FMK_SOCKET:  return "socket";
    case FMK_DEVICE:  return "device";
    default:          return "unknown";
    }
}

/* Mixing two configured colours is how chrome gets derived colours (dim text, separators) without a
   second palette the user cannot configure. render.c has the same helper for the same reason. */
static uint32_t mix_c(uint32_t a, uint32_t b, int pct)
{
    unsigned ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
    unsigned br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
    unsigned r = ar + (br > ar ? (br - ar) : 0) * (unsigned)pct / 100u;
    unsigned g = ag + (bg > ag ? (bg - ag) : 0) * (unsigned)pct / 100u;
    unsigned l = ab + (bb > ab ? (bb - ab) : 0) * (unsigned)pct / 100u;
    /* The alpha always comes from `a`: chrome is opaque even when Config.opacity makes the window
       see-through (the opacity is applied by the renderer, never baked into a colour). */
    return (a & 0xff000000u) | (r << 16) | (g << 8) | l;
}

/* ---------- paths ---------- */

/* Absolute, with "//", "/." and "/a/../" collapsed, so what the pane shows is what the shell would
   cd to. A relative path is made absolute against the process's own directory (the file manager's
   start dir is the shell's start dir, which main.c already resolved). */
static void norm_path(const char *in, char *out, size_t cap)
{
    char tmp[FM_PATH_MAX * 2];
    if (!in || !in[0]) {
        snprintf(tmp, sizeof tmp, "/");
    } else if (in[0] == '/') {
        snprintf(tmp, sizeof tmp, "%s", in);
    } else {
        char cwd[FM_PATH_MAX];
        if (getcwd(cwd, sizeof cwd))
            snprintf(tmp, sizeof tmp, "%s/%s", cwd, in);
        else
            snprintf(tmp, sizeof tmp, "/%s", in);
    }

    /* Walk the components, keeping the ones that are not "." and popping for "..". */
    char res[FM_PATH_MAX];
    size_t rl = 0;
    res[0] = 0;
    const char *p = tmp;
    int root = (p[0] == '/');
    while (*p == '/')
        p++;

    char comp[FM_NAME_MAX];
    while (*p) {
        size_t cl = 0;
        while (*p && *p != '/') {
            if (cl + 1 < sizeof comp)
                comp[cl++] = *p;
            p++;
        }
        comp[cl] = 0;
        while (*p == '/')
            p++;
        if (!cl || !strcmp(comp, "."))
            continue;
        if (!strcmp(comp, "..")) {
            /* pop the last component; at the root ".." stays at the root */
            if (rl == 0)
                continue;
            while (rl > 0 && res[rl - 1] != '/')
                rl--;
            while (rl > 0 && res[rl - 1] == '/')
                rl--;
            res[rl] = 0;
            continue;
        }
        if (rl + cl + 2 >= sizeof res)
            break;                    /* the path does not fit; keep what we have */
        res[rl++] = '/';
        memcpy(res + rl, comp, cl);
        rl += cl;
        res[rl] = 0;
    }
    if (rl == 0)
        snprintf(res, sizeof res, "%s", root ? "/" : "");
    snprintf(out, cap, "%s", res);
}

/* The directory above dir. "/" stays "/". */
static void parent_path(const char *dir, char *out, size_t cap)
{
    const char *slash = strrchr(dir, '/');
    if (!slash) {
        snprintf(out, cap, "/");
        return;
    }
    if (slash == dir) {
        snprintf(out, cap, "/");
        return;
    }
    char tmp[FM_PATH_MAX];
    size_t n = (size_t)(slash - dir);
    if (n >= sizeof tmp)
        n = sizeof tmp - 1;
    memcpy(tmp, dir, n);
    tmp[n] = 0;
    snprintf(out, cap, "%s", tmp);
}

/* dir + "/" + name, without ever producing "//" for the root. */
static void join_path(char *out, size_t cap, const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    if (dl && dir[dl - 1] == '/')
        snprintf(out, cap, "%s%s", dir, name);
    else
        snprintf(out, cap, "%s/%s", dir, name);
}

/* ---------- listing: build (any thread) ---------- */

static int entry_kind(mode_t m)
{
    if (S_ISDIR(m))
        return FMK_DIR;
    if (S_ISLNK(m))
        return FMK_SYMLINK;
    if (S_ISFIFO(m))
        return FMK_FIFO;
    if (S_ISSOCK(m))
        return FMK_SOCKET;
    if (S_ISBLK(m) || S_ISCHR(m))
        return FMK_DEVICE;
    return FMK_FILE;
}

/* Directories first, then case-insensitive by name, then by bytes so the order is total (qsort is not
   stable, so a comparator that ties leaves the order up to libc). */
static int entry_cmp(const void *va, const void *vb)
{
    const FmEntry *a = va, *b = vb;
    int ad = a->kind == FMK_DIR, bd = b->kind == FMK_DIR;
    if (ad != bd)
        return bd - ad;
    int c = strcasecmp(a->name, b->name);
    if (c)
        return c;
    return strcmp(a->name, b->name);
}

FmScan *fm_scan_build_n(const char *dir, unsigned long seq, int max_entries)
{
    FmScan *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->seq = seq;
    norm_path(dir, s->dir, sizeof s->dir);
    if (max_entries < 1)
        max_entries = 1;

    DIR *d = opendir(s->dir);
    if (!d) {
        /* The path gets a precision because the error line is short: a path longer than the line is
           cut, which is what the compiler is telling us about, and it is the honest behaviour. */
        snprintf(s->err, sizeof s->err, "cannot open %.180s: %s", s->dir, strerror(errno));
        klog(L_WARN, "fm: %s", s->err);
        return s;
    }

    int cap = 64;
    s->ent = malloc((size_t)cap * sizeof *s->ent);
    if (!s->ent) {
        closedir(d);
        snprintf(s->err, sizeof s->err, "out of memory listing %.180s", s->dir);
        return s;
    }

    errno = 0;
    struct dirent *de;
    char path[FM_PATH_MAX];
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        if (s->n >= max_entries) {
            s->truncated = 1;
            break;
        }
        if (s->n >= cap) {
            int ncap = imin_(cap * 2, max_entries);
            FmEntry *grown = realloc(s->ent, (size_t)ncap * sizeof *s->ent);
            if (!grown) {
                s->truncated = 1;
                break;
            }
            s->ent = grown;
            cap = ncap;
        }
        FmEntry *e = &s->ent[s->n++];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", de->d_name);
        e->hidden = (de->d_name[0] == '.') ? 1 : 0;

        join_path(path, sizeof path, s->dir, de->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) {
            /* The entry exists in the listing but cannot be described (it vanished, or the path is a
               dangling symlink through a directory we may not search). It stays listed: hiding it
               would be worse than showing it grey. */
            e->kind = FMK_MISSING;
            snprintf(e->name, sizeof e->name, "%s", de->d_name);
            continue;
        }
        e->kind = entry_kind(st.st_mode);
        e->mode = st.st_mode;
        e->size = (uint64_t)st.st_size;
        e->mtime = (int64_t)st.st_mtime;
        e->exec = (S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR)) ? 1 : 0;
    }
    closedir(d);

    qsort(s->ent, (size_t)s->n, sizeof *s->ent, entry_cmp);
    return s;
}

FmScan *fm_scan_build(const char *dir, unsigned long seq)
{
    return fm_scan_build_n(dir, seq, FM_ENTRIES_MAX);
}

void fm_scan_free(FmScan *s)
{
    if (!s)
        return;
    free(s->ent);
    free(s);
}

/* ---------- the model: init / size / install ---------- */

void fm_init(FileMgr *fm, const char *dir)
{
    memset(fm, 0, sizeof *fm);
    norm_path(dir && dir[0] ? dir : "/", fm->dir, sizeof fm->dir);
    fm->focus = FM_FOCUS_LIST;
    fm->preview_on = 1;
    fm->dirty = 1;
}

void fm_destroy(FileMgr *fm)
{
    free(fm->ent);
    free(fm->vis);
    free(fm->list.cell);
    free(fm->prev.cell);
    fm_preview_free(fm->pv);
    memset(fm, 0, sizeof *fm);
}

void fm_set_size(FileMgr *fm, int lc, int lr, int pc, int pr)
{
    lc = imax_(lc, 0); lr = imax_(lr, 0); pc = imax_(pc, 0); pr = imax_(pr, 0);
    if (fm->list_cols == lc && fm->list_rows == lr && fm->prev_cols == pc && fm->prev_rows == pr)
        return;
    fm->list_cols = lc; fm->list_rows = lr;
    fm->prev_cols = pc; fm->prev_rows = pr;

    /* A pane that cannot be allocated is reported as 0x0, so the painter and the renderer both skip it
       instead of reading past a smaller buffer. The old buffer is freed on failure: an out-of-memory
       resize must not leak the grid it could not grow. */
    fm->list.cols = fm->list.rows = 0;
    fm->prev.cols = fm->prev.rows = 0;
    if (lc > 0 && lr > 0) {
        Cell *grown = realloc(fm->list.cell, (size_t)lc * (size_t)lr * sizeof(Cell));
        if (grown) {
            fm->list.cell = grown;
            fm->list.cols = lc;
            fm->list.rows = lr;
        } else {
            free(fm->list.cell);
            fm->list.cell = NULL;
        }
    }
    if (pc > 0 && pr > 0) {
        Cell *grown = realloc(fm->prev.cell, (size_t)pc * (size_t)pr * sizeof(Cell));
        if (grown) {
            fm->prev.cell = grown;
            fm->prev.cols = pc;
            fm->prev.rows = pr;
        } else {
            free(fm->prev.cell);
            fm->prev.cell = NULL;
        }
    }
    fm->dirty = 1;
}

/* The view after the hidden filter. Kept as an index array so a selection survives toggling dotfiles
   and so the listing itself never has to be re-read from disk. */
static void rebuild_vis(FileMgr *fm)
{
    if (fm->nvis < 0)
        fm->nvis = 0;
    if (fm->vcap < fm->n) {
        int *grown = realloc(fm->vis, (size_t)fm->n * sizeof *fm->vis);
        if (!grown) {
            fm->nvis = 0;
            return;
        }
        fm->vis = grown;
        fm->vcap = fm->n;
    }
    int n = 0;
    for (int i = 0; i < fm->n; i++)
        if (fm->show_hidden || !fm->ent[i].hidden)
            fm->vis[n++] = i;
    fm->nvis = n;
}

/* How many rows of the list pane hold entries, and where they start. A pane too short for the header
   and the footer loses the chrome, never the entries. */
static void list_metrics(const FileMgr *fm, int *hdr, int *first, int *avail)
{
    int h = 1, f = 1;
    if (fm->list_rows < 4)
        h = f = 0;
    *hdr = h;
    *first = h;
    *avail = imax_(fm->list_rows - h - f, 0);
}

static void ensure_visible(FileMgr *fm)
{
    int hdr, first, avail;
    list_metrics(fm, &hdr, &first, &avail);
    (void)hdr; (void)first;
    if (avail <= 0) {
        fm->top = 0;
        return;
    }
    if (fm->sel < fm->top)
        fm->top = fm->sel;
    if (fm->sel >= fm->top + avail)
        fm->top = fm->sel - avail + 1;
    fm->top = iclamp_(fm->top, 0, imax_(fm->nvis - avail, 0));
}

int fm_install_scan(FileMgr *fm, FmScan *s)
{
    if (!s)
        return 0;
    if (s->seq != fm->scan_req) {
        /* A newer listing request has been issued since; this answer is about a directory the user
           left. The PREVIEW counter is separate, so asking for a preview never invalidates a scan. */
        klog(L_DEBUG, "fm: dropping stale listing of %s (request %lu, newest %lu)",
             s->dir, s->seq, fm->scan_req);
        fm_scan_free(s);
        return 0;
    }
    free(fm->ent);
    fm->ent = s->ent;
    fm->n = s->n;
    fm->ecap = s->n;
    fm->truncated = s->truncated;
    snprintf(fm->err, sizeof fm->err, "%s", s->err);
    /* The listing we were asked for is the one for fm->dir; if the answer names a different directory
       the request was issued before a navigation and the seq check above already caught that. */
    fm->scan_req = s->seq;
    fm->have_listing = 1;
    rebuild_vis(fm);
    fm->sel = iclamp_(fm->sel, 0, imax_(fm->nvis - 1, 0));
    if (fm->nvis == 0)
        fm->sel = -1;
    fm->top = 0;
    ensure_visible(fm);
    fm->dirty = 1;
    free(s);          /* the shell only; ent was moved out of it */
    return 1;
}

int fm_go(FileMgr *fm, const char *dir, unsigned long *seq)
{
    char want[FM_PATH_MAX];
    norm_path(dir, want, sizeof want);
    if (!strcmp(want, fm->dir) && fm->have_listing)
        return 0;              /* already showing it */
    snprintf(fm->dir, sizeof fm->dir, "%s", want);
    fm->seq++;
    fm->scan_req = fm->seq;
    if (seq)
        *seq = fm->seq;
    fm->dirty = 1;
    klog(L_DEBUG, "fm: listing %s (request %lu)", fm->dir, fm->seq);
    return 1;
}

int fm_refresh(FileMgr *fm, unsigned long *seq)
{
    fm->seq++;
    fm->scan_req = fm->seq;
    if (seq)
        *seq = fm->seq;
    fm->dirty = 1;
    return 1;
}

int fm_parent(FileMgr *fm, unsigned long *seq)
{
    char up[FM_PATH_MAX];
    parent_path(fm->dir, up, sizeof up);
    if (!strcmp(up, fm->dir))
        return 0;              /* already at the root */
    fm->sel = 0;
    return fm_go(fm, up, seq);
}

int fm_go_sel(FileMgr *fm, unsigned long *seq)
{
    const FmEntry *e = fm_sel_entry(fm);
    if (!e)
        return 0;
    char path[FM_PATH_MAX];
    if (!fm_entry_path(fm, fm->sel, path, sizeof path))
        return 0;
    if (e->kind == FMK_DIR) {
        fm->sel = 0;
        return fm_go(fm, path, seq);
    }
    if (e->kind == FMK_SYMLINK) {
        /* lstat says "symlink"; only stat says what it points at. Following a link to a directory is
           what the user means by Enter, and a dangling link simply fails the stat and does nothing. */
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            fm->sel = 0;
            return fm_go(fm, path, seq);
        }
    }
    return 0;
}

/* ---------- selection ---------- */

int fm_count(const FileMgr *fm) { return fm->nvis; }
int fm_sel(const FileMgr *fm) { return fm->nvis ? fm->sel : -1; }

const FmEntry *fm_sel_entry(const FileMgr *fm)
{
    if (!fm->nvis || fm->sel < 0 || fm->sel >= fm->nvis)
        return NULL;
    return &fm->ent[fm->vis[fm->sel]];
}

int fm_entry_path(const FileMgr *fm, int pos, char *out, size_t cap)
{
    if (pos < 0 || pos >= fm->nvis) {
        if (cap)
            out[0] = 0;
        return 0;
    }
    join_path(out, cap, fm->dir, fm->ent[fm->vis[pos]].name);
    return 1;
}

void fm_select(FileMgr *fm, int pos)
{
    if (!fm->nvis) {
        fm->sel = -1;
        return;
    }
    fm->sel = iclamp_(pos, 0, fm->nvis - 1);
    ensure_visible(fm);
    fm->dirty = 1;
}

void fm_move(FileMgr *fm, int delta)
{
    if (!fm->nvis)
        return;
    fm_select(fm, fm->sel + delta);
}

void fm_page(FileMgr *fm, int pages)
{
    int hdr, first, avail;
    list_metrics(fm, &hdr, &first, &avail);
    if (avail <= 0)
        avail = 1;
    fm_select(fm, fm->sel + pages * avail);
}

void fm_home(FileMgr *fm) { fm_select(fm, 0); }
void fm_end(FileMgr *fm)  { fm_select(fm, fm->nvis - 1); }

/* Dotfiles on/off. The listing keeps every entry and marks the hidden ones, so this is a rebuild of the
   index array, not a second opendir(): toggling a 8000-entry directory costs no disk access. The
   selection stays a POSITION in the view (the same rule fm_install_scan applies), so it is clamped to
   what the new view holds rather than remembered by name. */
void fm_set_hidden(FileMgr *fm, int show)
{
    show = show ? 1 : 0;
    if (fm->show_hidden == show)
        return;
    fm->show_hidden = show;
    rebuild_vis(fm);
    fm->sel = fm->nvis ? iclamp_(fm->sel, 0, fm->nvis - 1) : -1;
    ensure_visible(fm);
    fm->dirty = 1;
}

/* The view position the list pane draws at row `row`, or -1 when that row is chrome or empty. Uses the
   very same list_metrics() the painter uses, so a click always selects the entry drawn under it —
   including in a pane too short for its header and footer, where the entries start at row 0. */
int fm_row_pos(const FileMgr *fm, int row)
{
    int hdr, first, avail;
    list_metrics(fm, &hdr, &first, &avail);
    (void)hdr;
    if (row < first || row >= first + avail)
        return -1;
    int pos = fm->top + (row - first);
    if (pos < 0 || pos >= fm->nvis)
        return -1;
    return pos;
}

/* ---------- preview: build (any thread) ---------- */

static int meta_add(FmPreview *p, const char *label, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* One "label\tvalue" line. The label column is fixed so the painter can align the values, and the value
   is cut to what is left rather than overflowing the line. */
#define FM_META_LABEL 15
#define FM_META_VALUE (FM_META_WIDTH - FM_META_LABEL - 2)

static int meta_add(FmPreview *p, const char *label, const char *fmt, ...)
{
    if (p->nmeta >= FM_META_LINES)
        return 0;
    char val[FM_META_WIDTH];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(val, sizeof val, fmt, ap);
    va_end(ap);
    snprintf(p->meta[p->nmeta], FM_META_WIDTH, "%.*s\t%.*s", FM_META_LABEL, label, FM_META_VALUE, val);
    p->nmeta++;
    return 1;
}

static void meta_stat(FmPreview *p, const struct stat *st)
{
    char sz[32], when[64], mode[11];
    fm_human_size((uint64_t)st->st_size, sz, sizeof sz);
    fm_human_time((int64_t)st->st_mtime, when, sizeof when);
    fm_mode_string(st->st_mode, mode);
    meta_add(p, "type", "%s", kind_name(entry_kind(st->st_mode)));
    meta_add(p, "size", "%s (%llu bytes)", sz, (unsigned long long)st->st_size);
    meta_add(p, "mode", "%s  %04o", mode, (unsigned)(st->st_mode & 07777));

    const char *user = NULL, *group = NULL;
    struct passwd *pw = getpwuid(st->st_uid);
    if (pw && pw->pw_name)
        user = pw->pw_name;
    struct group *gr = getgrgid(st->st_gid);
    if (gr && gr->gr_name)
        group = gr->gr_name;
    meta_add(p, "owner", "uid %u%s%s%s", (unsigned)st->st_uid,
             user ? " (" : "", user ? user : "", user ? ")" : "");
    meta_add(p, "group", "gid %u%s%s%s", (unsigned)st->st_gid,
             group ? " (" : "", group ? group : "", group ? ")" : "");
    meta_add(p, "modified", "%s", when);
    if (st->st_nlink > 1)
        meta_add(p, "links", "%llu", (unsigned long long)st->st_nlink);
}

/* Read the whole file (already capped by the caller) into a malloc'd buffer. */
static char *read_file(const char *path, size_t cap, size_t *out_len, int *out_trunc, char *err, size_t errcap)
{
    *out_len = 0;
    if (out_trunc)
        *out_trunc = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY);
    if (fd < 0) {
        if (err)
            snprintf(err, errcap, "cannot read %s: %s", path, strerror(errno));
        return NULL;
    }
    size_t capsz = cap ? cap : FM_PREVIEW_BYTES;
    char *buf = malloc(capsz + 1);
    if (!buf) {
        close(fd);
        if (err)
            snprintf(err, errcap, "out of memory reading %s", path);
        return NULL;
    }
    size_t got = 0;
    for (;;) {
        if (got >= capsz) {
            if (out_trunc)
                *out_trunc = 1;
            break;
        }
        ssize_t r = read(fd, buf + got, capsz - got);
        if (r == 0)
            break;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            if (err)
                snprintf(err, errcap, "reading %s: %s", path, strerror(errno));
            break;
        }
        got += (size_t)r;
    }
    close(fd);
    buf[got] = 0;
    *out_len = got;
    return buf;
}

/* Replace what a text preview cannot draw (every control character below 0x20 except tab and newline,
   DEL, and any NUL that survived the binary probe further into the file) with a space, in place. Tab is
   kept because the painter expands it to stops. */
static void sanitize_text(char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\t' || c == '\n')
            continue;
        if (c < 0x20 || c == 0x7f)
            s[i] = ' ';
    }
}

FmPreview *fm_preview_build(const char *path, unsigned long seq)
{
    FmPreview *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->seq = seq;
    snprintf(p->path, sizeof p->path, "%s", path ? path : "");

    struct stat st;
    if (lstat(p->path, &st) != 0) {
        p->kind = FMP_META;
        snprintf(p->err, sizeof p->err, "cannot stat %.180s: %s", p->path, strerror(errno));
        meta_add(p, "path", "%s", p->path);
        meta_add(p, "error", "%s", strerror(errno));
        klog(L_DEBUG, "fm: preview: %s", p->err);
        return p;
    }
    meta_add(p, "path", "%s", p->path);
    meta_stat(p, &st);

    if (S_ISDIR(st.st_mode)) {
        p->kind = FMP_DIR;
        FmScan *s = fm_scan_build_n(p->path, seq, FM_PREVIEW_DIR_MAX);
        if (s) {
            p->ent = s->ent;
            p->n = s->n;
            p->truncated = s->truncated;
            snprintf(p->err, sizeof p->err, "%s", s->err);
            free(s);          /* the shell; ent moved out */
        }
        meta_add(p, "entries", "%d%s", p->n, p->truncated ? " (first shown)" : "");
        return p;
    }

    if (!S_ISREG(st.st_mode)) {
        p->kind = FMP_META;
        snprintf(p->err, sizeof p->err, "%s is not a regular file - no preview",
                 kind_name(entry_kind(st.st_mode)));
        return p;
    }

    /* Sniff the first bytes: an image is decided by its own magic, never by the file name. */
    size_t hn = 0;
    char *head = read_file(p->path, 32, &hn, NULL, p->err, sizeof p->err);
    char fmt[8] = "";
    if (head)
        img_sniff((const uint8_t *)head, hn, fmt);
    free(head);

    if (fmt[0]) {
        if ((uint64_t)st.st_size > FM_PREVIEW_FILE_MAX) {
            p->kind = FMP_META;
            p->truncated = 1;
            snprintf(p->err, sizeof p->err, "%s image is %llu bytes, over the %u MiB preview limit",
                     fmt, (unsigned long long)st.st_size, FM_PREVIEW_FILE_MAX / (1024u * 1024u));
            meta_add(p, "note", "%s", p->err);
            return p;
        }
        size_t fn = 0;
        char *mem = read_file(p->path, (size_t)st.st_size, &fn, &p->truncated, p->err, sizeof p->err);
        if (mem) {
            int w = 0, h = 0;
            p->rgba = img_decode_mem((const uint8_t *)mem, fn, &w, &h, p->fmt);
            free(mem);
            if (p->rgba) {
                p->w = w;
                p->h = h;
                p->kind = FMP_IMAGE;
                meta_add(p, "format", "%s image, %dx%d px", p->fmt, w, h);
                return p;
            }
            snprintf(p->err, sizeof p->err, "%s file could not be decoded", fmt);
        }
        /* Not decodable: fall through to the metadata view, which says why. */
        p->kind = FMP_META;
        meta_add(p, "note", "%s", p->err);
        return p;
    }

    /* Text, unless a NUL in the head says otherwise (the same rule git uses to call a file binary). */
    size_t tn = 0;
    int trunc = 0;
    char *txt = read_file(p->path, FM_PREVIEW_BYTES, &tn, &trunc, p->err, sizeof p->err);
    if (!txt) {
        p->kind = FMP_META;
        meta_add(p, "note", "%s", p->err);
        return p;
    }
    size_t probe = imin_(tn, 512u);
    int binary = 0;
    for (size_t i = 0; i < probe; i++)
        if (!txt[i]) { binary = 1; break; }
    if (binary) {
        free(txt);
        p->kind = FMP_META;
        snprintf(p->err, sizeof p->err, "binary file - no text preview");
        meta_add(p, "note", "%s", p->err);
        char hex[80];
        size_t hn2 = 0;
        char *hb = read_file(p->path, 16, &hn2, NULL, NULL, 0);
        if (hb) {
            int at = 0;
            for (size_t i = 0; i < hn2 && at + 3 < (int)sizeof hex; i++)
                at += snprintf(hex + at, sizeof hex - (size_t)at, "%02x%s",
                               (unsigned char)hb[i], i + 1 < hn2 ? " " : "");
            free(hb);
            meta_add(p, "first bytes", "%s", hex);
        }
        return p;
    }
    sanitize_text(txt, tn);
    p->kind = FMP_TEXT;
    p->text = txt;
    p->len = tn;
    p->truncated = trunc;
    p->lang = fm_lang_for_path(p->path);
    if (p->truncated)
        meta_add(p, "note", "text preview truncated at %u KiB", FM_PREVIEW_BYTES / 1024u);
    return p;
}

void fm_preview_free(FmPreview *p)
{
    if (!p)
        return;
    free(p->ent);
    free(p->text);
    free(p->rgba);
    free(p);
}

int fm_install_preview(FileMgr *fm, FmPreview *pv)
{
    if (!pv)
        return 0;
    if (pv->seq != fm->pv_req) {
        klog(L_DEBUG, "fm: dropping stale preview of %s (request %lu, newest %lu)",
             pv->path, pv->seq, fm->pv_req);
        fm_preview_free(pv);
        return 0;
    }
    /* Every preview that becomes the installed one gets a fresh uid. The renderer keys the preview's GL
       texture on it, so a recycled malloc address can never make it reuse the previous picture's texels.
       Assigned HERE, on the main thread, not in fm_preview_build(): that one runs on the worker thread
       and the counter must not be a race. */
    static unsigned long uid_next;
    pv->uid = ++uid_next;
    fm_preview_free(fm->pv);
    fm->pv = pv;
    fm->dirty = 1;
    return 1;
}

int fm_preview_request(FileMgr *fm, unsigned long *seq, char *path, size_t cap)
{
    char want[FM_PATH_MAX];
    if (fm_sel_entry(fm))
        fm_entry_path(fm, fm->sel, want, sizeof want);
    else
        snprintf(want, sizeof want, "%s", fm->dir);

    /* Re-preview when the selection moved to a different entry, and after every listing request: a
       refresh means the content may have changed, and the newest listing id is then newer than the
       newest preview id. */
    if (fm->pv && !strcmp(fm->pv->path, want) && fm->pv_req > fm->scan_req)
        return 0;
    fm->seq++;
    fm->pv_req = fm->seq;
    if (seq)
        *seq = fm->pv_req;
    snprintf(path, cap, "%s", want);
    return 1;
}

/* ---------- text colouring ---------- */

typedef struct {
    const char *kw;     /* space-separated keywords, NULL = none */
    const char *types;  /* type names coloured differently, NULL = none */
    char lc;            /* line comment lead character ('#', ';', '/'), 0 = none */
    char lc2;           /* second character of a two-character line comment ('/' for "//"), 0 = one */
    int block;          /* has a C-style block comment */
    int pp;             /* C preprocessor lines start with '#' */
    int strings;        /* has quoted strings */
    int numbers;        /* colour numbers */
} Lang;

/* Keyword lists, one array per language. They are ARRAYS, not `const char *` variables, because a
   static table may only be initialised with an address constant: `kw_C` names one, `kw_c[0]` does not
   (the compiler said so: "initializer element is not constant"). */
static const char kw_C[] =
    "auto break case const continue default do else enum extern for goto if inline register return "
    "sizeof static struct switch typedef union volatile while _Alignas _Atomic _Bool _Complex "
    "_Generic _Static_assert _Thread_local";
static const char kw_PY[] =
    "and as assert async await break class continue def del elif else except finally for from global "
    "if import in is lambda nonlocal not or pass raise try while with yield None True False self cls";
static const char kw_SH[] =
    "if then else elif fi case esac for while until do done in function return exit local export "
    "readonly declare typeset shift break continue source eval set unset trap select time";
static const char kw_JSON[] = "true false null";
static const char kw_MAKE[] =
    "include define undef ifdef ifndef else endif export unexport override vpath ifeq ifneq";
static const char kw_RS[] =
    "as async await break const continue crate dyn else enum extern fn for if impl in let loop match "
    "mod move mut pub ref return static struct trait type unsafe use where while true false self super";
static const char kw_JS[] =
    "async await break case catch class const continue debugger default delete do else export extends "
    "finally for from function get if import in instanceof interface let new of return set static "
    "super switch this throw try typeof var void while with yield true false null undefined";
static const char kw_GO[] =
    "break case chan const continue default defer else fallthrough for func go goto if import "
    "interface map package range return select struct switch type var true false nil iota";
static const char kw_YAML[] = "true false null yes no on off null";

static const Lang lang_tab[FM_LANG_N] = {
    /* FM_LANG_NONE */ { NULL, NULL, 0, 0, 0, 0, 0, 0 },
    /* C      */ { kw_C, "char double float int long short signed unsigned void bool size_t ssize_t "
                         "uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t",
                   '/', '/', 1, 1, 1, 1 },
    /* python */ { kw_PY, "int float str list dict tuple set bool bytes None", '#', 0, 0, 0, 1, 1 },
    /* shell */ { kw_SH, NULL, '#', 0, 0, 0, 1, 1 },
    /* json  */ { kw_JSON, NULL, 0, 0, 0, 0, 1, 1 },
    /* md    */ { NULL, NULL, 0, 0, 0, 0, 0, 0 },
    /* make  */ { kw_MAKE, NULL, '#', 0, 0, 0, 0, 0 },
    /* ini   */ { NULL, NULL, ';', 0, 0, 0, 1, 1 },
    /* html  */ { NULL, NULL, 0, 0, 0, 0, 1, 0 },
    /* rust  */ { kw_RS, "u8 u16 u32 u64 usize i8 i16 i32 i64 bool char str String Vec Option Result",
                  '/', '/', 1, 0, 1, 1 },
    /* js    */ { kw_JS, "string number boolean any unknown never object Array Record",
                  '/', '/', 1, 0, 1, 1 },
    /* go    */ { kw_GO, "bool byte error rune string int int8 int16 int32 int64 uint uint8 "
                         "uint16 uint32 uint64 uintptr float32 float64 complex128 any",
                  '/', '/', 1, 0, 1, 1 },
    /* css   */ { NULL, NULL, 0, 0, 1, 0, 1, 1 },
    /* yaml  */ { kw_YAML, NULL, '#', 0, 0, 0, 1, 1 },
};

static const Lang *lang_of(int lang)
{
    if (lang <= FM_LANG_NONE || lang >= FM_LANG_N)
        return &lang_tab[FM_LANG_NONE];
    return &lang_tab[lang];
}

/* Is word (len bytes, no NUL inside) in the space-separated list? */
static int kw_hit(const char *list, const char *word, size_t len)
{
    if (!list || !len)
        return 0;
    const char *p = list;
    while (*p) {
        while (*p == ' ')
            p++;
        const char *e = p;
        while (*e && *e != ' ')
            e++;
        if ((size_t)(e - p) == len && !memcmp(p, word, len))
            return 1;
        p = e;
    }
    return 0;
}

/* The extension decides the language; a few well-known file names decide it alone. */
int fm_lang_for_path(const char *path)
{
    if (!path)
        return FM_LANG_NONE;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    static const struct { const char *name; int lang; } named[] = {
        { "Makefile", FM_LANG_MAKE }, { "makefile", FM_LANG_MAKE },
        { "GNUmakefile", FM_LANG_MAKE }, { "CMakeLists.txt", FM_LANG_C },
        { "Dockerfile", FM_LANG_SH }, { ".gitignore", FM_LANG_SH },
        { "Rakefile", FM_LANG_MAKE }, { "Kconfig", FM_LANG_MAKE },
        { "config", FM_LANG_INI }, { ".Xresources", FM_LANG_INI },
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++)
        if (!strcmp(base, named[i].name))
            return named[i].lang;

    const char *dot = strrchr(base, '.');
    if (!dot)
        return FM_LANG_NONE;
    dot++;
    static const struct { const char *ext; int lang; } ext[] = {
        { "c", FM_LANG_C }, { "h", FM_LANG_C }, { "cc", FM_LANG_C }, { "cpp", FM_LANG_C },
        { "cxx", FM_LANG_C }, { "hpp", FM_LANG_C }, { "hh", FM_LANG_C }, { "inl", FM_LANG_C },
        { "py", FM_LANG_PY }, { "pyw", FM_LANG_PY },
        { "sh", FM_LANG_SH }, { "bash", FM_LANG_SH }, { "zsh", FM_LANG_SH }, { "ksh", FM_LANG_SH },
        { "json", FM_LANG_JSON }, { "jsonc", FM_LANG_JSON },
        { "md", FM_LANG_MD }, { "markdown", FM_LANG_MD },
        { "mk", FM_LANG_MAKE }, { "mak", FM_LANG_MAKE },
        { "ini", FM_LANG_INI }, { "cfg", FM_LANG_INI }, { "conf", FM_LANG_INI },
        { "desktop", FM_LANG_INI }, { "toml", FM_LANG_INI },
        { "html", FM_LANG_HTML }, { "htm", FM_LANG_HTML }, { "xml", FM_LANG_HTML },
        { "rs", FM_LANG_RS },
        { "js", FM_LANG_JS }, { "mjs", FM_LANG_JS }, { "jsx", FM_LANG_JS },
        { "ts", FM_LANG_JS }, { "tsx", FM_LANG_JS },
        { "go", FM_LANG_GO },
        { "css", FM_LANG_CSS },
        { "yaml", FM_LANG_YAML }, { "yml", FM_LANG_YAML },
    };
    for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++)
        if (!strcasecmp(dot, ext[i].ext))
            return ext[i].lang;
    return FM_LANG_NONE;
}

void fm_hl_init(FmHl *st, int lang)
{
    st->lang = lang;
    st->in_block = 0;
}

static int starts(const char *s, size_t n, size_t i, const char *lit)
{
    size_t l = strlen(lit);
    return i + l <= n && !memcmp(s + i, lit, l);
}

void fm_hl_line(FmHl *st, const char *line, size_t len, uint32_t *fg, uint32_t base)
{
    for (size_t i = 0; i < len; i++)
        fg[i] = base;
    if (!st || st->lang <= FM_LANG_NONE || st->lang >= FM_LANG_N || !len)
        return;
    const Lang *L = lang_of(st->lang);

    /* Markdown and INI are line-shaped, not token-shaped: colour the whole line. */
    if (st->lang == FM_LANG_MD) {
        size_t i = 0;
        while (i < len && (line[i] == ' ' || line[i] == '\t'))
            i++;
        if (i < len && (line[i] == '#' || line[i] == '>' || line[i] == '-' || line[i] == '*')) {
            for (size_t k = i; k < len; k++)
                fg[k] = FM_KEY_COL;
            return;
        }
        if (starts(line, len, i, "```"))
            st->in_block = !st->in_block;      /* reuse: inside a fenced code block */
        if (st->in_block)
            for (size_t k = 0; k < len; k++)
                fg[k] = FM_STR_COL;
        return;
    }

    size_t i = 0;

    /* A block comment that started on an earlier line continues here. */
    if (st->in_block && L->block) {
        for (size_t k = 0; k < len; k++)
            fg[k] = FM_CMT_COL;
        for (size_t k = 0; k + 1 < len; k++)
            if (line[k] == '*' && line[k + 1] == '/') {
                st->in_block = 0;
                i = k + 2;
                break;
            }
        if (st->in_block)
            return;
    }

    /* C preprocessor: the whole directive line is one colour. */
    if (L->pp) {
        size_t k = 0;
        while (k < len && (line[k] == ' ' || line[k] == '\t'))
            k++;
        if (k < len && line[k] == '#') {
            for (; k < len; k++)
                fg[k] = FM_PP_COL;
            return;
        }
    }
    /* make: a recipe line (leading tab) is shell, not make syntax - leave it plain. */
    if (st->lang == FM_LANG_MAKE && line[0] == '\t')
        return;
    /* INI: [section] is a heading, key = value colours the key (spaces around the '=' are normal). */
    if (st->lang == FM_LANG_INI) {
        size_t k = 0;
        while (k < len && isspace((unsigned char)line[k]))
            k++;
        if (k < len && line[k] == '[') {
            for (; k < len; k++)
                fg[k] = FM_TYPE_COL;
            return;
        }
        size_t s = k;
        while (k < len && line[k] != '=' && line[k] != ';' && !isspace((unsigned char)line[k]))
            k++;
        size_t nameend = k;
        while (k < len && isspace((unsigned char)line[k]))
            k++;
        if (nameend > s && k < len && line[k] == '=') {
            for (size_t j = s; j < nameend; j++)
                fg[j] = FM_KEY_COL;
            i = k;
        }
    }
    /* YAML: a top-level "key:" colours the key. */
    if (st->lang == FM_LANG_YAML) {
        size_t k = 0;
        while (k < len && (line[k] == ' ' || line[k] == '-'))
            k++;
        size_t s = k;
        while (k < len && line[k] != ':' && line[k] != '#')
            k++;
        if (k > s && k < len && line[k] == ':') {
            for (size_t j = s; j < k; j++)
                fg[j] = FM_KEY_COL;
            i = k;
        }
    }
    /* make: "target: deps" or "VAR = value" at the start of a line. */
    if (st->lang == FM_LANG_MAKE && line[0] != '#') {
        size_t k = 0;
        while (k < len && line[k] != ':' && line[k] != '=' && line[k] != ' ' && line[k] != '\t')
            k++;
        size_t nameend = k;
        while (k < len && (line[k] == ' ' || line[k] == '\t'))
            k++;
        if (nameend > 0 && k < len && (line[k] == ':' || line[k] == '=')) {
            if (line[k] == ':' && k + 1 < len && line[k + 1] == ':')
                ;   /* "::" rule: not a target name */
            else
                for (size_t j = 0; j < nameend; j++)
                    fg[j] = FM_TYPE_COL;
        }
    }

    while (i < len) {
        unsigned char c = (unsigned char)line[i];

        /* line comments: one character ('#', ';') or two ("//") */
        int is_comment = 0;
        if (L->lc2) {
            if (i + 1 < len && line[i] == L->lc && line[i + 1] == L->lc2)
                is_comment = 1;
        } else if (L->lc && c == (unsigned char)L->lc) {
            is_comment = 1;
        }
        if (!is_comment && st->lang == FM_LANG_INI && c == '#')
            is_comment = 1;          /* ini files use both ';' and '#' */
        if (is_comment) {
            for (size_t k = i; k < len; k++)
                fg[k] = FM_CMT_COL;
            return;
        }
        if (L->block && i + 1 < len && c == '/' && line[i + 1] == '*') {
            size_t k = i;
            st->in_block = 1;
            while (k < len) {
                fg[k++] = FM_CMT_COL;
                if (k > i + 1 && k < len && line[k - 1] == '*' && line[k] == '/') {
                    fg[k++] = FM_CMT_COL;
                    st->in_block = 0;
                    break;
                }
            }
            i = k;
            continue;
        }
        /* HTML tag: <name ...> */
        if (st->lang == FM_LANG_HTML && c == '<') {
            size_t k = i;
            while (k < len && line[k] != '>')
                k++;
            for (size_t j = i; j <= k && j < len; j++)
                fg[j] = FM_PP_COL;
            i = (k < len) ? k + 1 : len;
            continue;
        }
        /* strings */
        if (L->strings && (c == '"' || c == '\'' || c == '`')) {
            char q = (char)c;
            size_t k = i + 1;
            fg[i] = FM_STR_COL;
            while (k < len) {
                fg[k] = FM_STR_COL;
                if (line[k] == '\\' && q != '\'' && k + 1 < len) {
                    k++;
                    fg[k] = FM_STR_COL;
                    k++;
                    continue;
                }
                if (line[k] == q) { k++; break; }
                k++;
            }
            /* JSON: a quoted string in front of a ':' is a KEY, and keys are quoted, so this belongs
               to the string branch and not to the identifier branch below. */
            if (st->lang == FM_LANG_JSON) {
                size_t j = k;
                while (j < len && line[j] == ' ')
                    j++;
                if (j < len && line[j] == ':')
                    for (size_t m = i; m < k; m++)
                        fg[m] = FM_TYPE_COL;
            }
            i = k;
            continue;
        }
        /* numbers */
        if (L->numbers && (isdigit(c) || (c == '.' && i + 1 < len && isdigit((unsigned char)line[i + 1])))) {
            size_t k = i;
            while (k < len && (isalnum((unsigned char)line[k]) || line[k] == '.' ||
                               ((line[k] == 'x' || line[k] == 'X') && k == i + 1) ||
                               ((line[k] == 'e' || line[k] == 'E') && k > i)))
                k++;
            for (size_t j = i; j < k; j++)
                fg[j] = FM_NUM_COL;
            i = k;
            continue;
        }
        /* identifiers */
        if (isalpha(c) || c == '_' || c >= 0x80) {
            size_t k = i;
            while (k < len && (isalnum((unsigned char)line[k]) || line[k] == '_' ||
                               (unsigned char)line[k] >= 0x80))
                k++;
            uint32_t col = 0;
            if (kw_hit(L->kw, line + i, k - i))
                col = FM_KEY_COL;
            else if (kw_hit(L->types, line + i, k - i))
                col = FM_TYPE_COL;
            if (col)
                for (size_t j = i; j < k; j++)
                    fg[j] = col;
            i = k;
            continue;
        }
        /* shell variables */
        if (st->lang == FM_LANG_SH && c == '$') {
            size_t k = i + 1;
            if (k < len && line[k] == '{')
                k++;
            while (k < len && (isalnum((unsigned char)line[k]) || line[k] == '_'))
                k++;
            if (k < len && line[k] == '}')
                k++;
            for (size_t j = i; j < k; j++)
                fg[j] = FM_NUM_COL;
            i = k;
            continue;
        }
        /* python decorator */
        if (st->lang == FM_LANG_PY && c == '@' && i == 0) {
            for (size_t k = i; k < len; k++)
                fg[k] = FM_PP_COL;
            return;
        }
        i++;
    }
}

/* ---------- painting ---------- */

/* Decode one UTF-8 sequence. Invalid bytes become U+FFFD and consume exactly one byte, so a byte that
   is not valid UTF-8 can never shift the column count or run past the end of the string. */
static uint32_t dec_utf8(const char *s, size_t n, size_t *used)
{
    unsigned char c0 = (unsigned char)s[0];
    if (c0 < 0x80) { *used = 1; return c0; }
    int need;
    uint32_t cp;
    if ((c0 & 0xe0) == 0xc0) { need = 1; cp = c0 & 0x1fu; }
    else if ((c0 & 0xf0) == 0xe0) { need = 2; cp = c0 & 0x0fu; }
    else if ((c0 & 0xf8) == 0xf0) { need = 3; cp = c0 & 0x07u; }
    else { *used = 1; return 0xfffd; }
    if (n < (size_t)(need + 1)) { *used = 1; return 0xfffd; }
    for (int i = 1; i <= need; i++) {
        unsigned char ci = (unsigned char)s[i];
        if ((ci & 0xc0) != 0x80) { *used = 1; return 0xfffd; }
        cp = (cp << 6) | (ci & 0x3fu);
    }
    /* reject overlong forms and surrogates, the way a strict decoder must */
    if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) ||
        (need == 3 && cp < 0x10000) || (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) {
        *used = 1;
        return 0xfffd;
    }
    *used = (size_t)need + 1;
    return cp;
}

static void put_cell(FmPane *p, int x, int y, uint32_t ch, uint32_t fg, uint32_t bg, uint32_t attr)
{
    if (!p->cell || x < 0 || y < 0 || x >= p->cols || y >= p->rows)
        return;
    Cell *c = &p->cell[y * p->cols + x];
    c->ch = ch;
    c->fg = fg;
    c->bg = bg;
    c->attr = attr;
    c->nextra = 0;
}

/* Write s (n < 0 = NUL-terminated) into row y starting at column x. A codepoint that vt.c would treat
   as WIDE takes two cells here too, with the same ATTR_WIDE continuation cell, so the renderer's
   existing wide-glyph rules apply to the file manager exactly as they do to a session. */
static int pane_puts(FmPane *p, int x, int y, const char *s, int n,
                     uint32_t fg, uint32_t bg, uint32_t attr)
{
    if (!p->cell || y < 0 || y >= p->rows)
        return x;
    size_t len = (n < 0) ? strlen(s) : (size_t)n;
    size_t i = 0;
    while (i < len && x < p->cols) {
        size_t used = 0;
        uint32_t cp = dec_utf8(s + i, len - i, &used);
        int wide = cp_is_wide(cp) && x + 1 < p->cols;
        put_cell(p, x, y, cp, fg, bg, attr | (wide ? ATTR_WIDE : 0));
        if (wide)
            put_cell(p, x + 1, y, 0, fg, bg, ATTR_WIDE);
        x += wide ? 2 : 1;
        i += used;
    }
    return x;
}

/* How many cells a string occupies, counting a wide codepoint as two. */
static int pane_cells(const char *s, size_t n)
{
    int cells = 0;
    size_t i = 0;
    while (i < n) {
        size_t used = 0;
        uint32_t cp = dec_utf8(s + i, n - i, &used);
        cells += cp_is_wide(cp) ? 2 : 1;
        i += used;
    }
    return cells;
}

/* Right-aligned write ending just before column x_end. */
static void pane_puts_right(FmPane *p, int x_end, int y, const char *s,
                            uint32_t fg, uint32_t bg, uint32_t attr)
{
    int cells = pane_cells(s, strlen(s));
    int x = imax_(x_end - cells, 0);
    pane_puts(p, x, y, s, -1, fg, bg, attr);
}

static void pane_clear(FmPane *p, uint32_t fg, uint32_t bg)
{
    if (!p->cell)
        return;
    for (int y = 0; y < p->rows; y++)
        for (int x = 0; x < p->cols; x++)
            put_cell(p, x, y, 0, fg, bg, 0);
}

/* The colour one entry's name is drawn in, and its attributes. */
static void entry_colour(const FmEntry *e, uint32_t fg, uint32_t *col, uint32_t *attr)
{
    *attr = 0;
    switch (e->kind) {
    case FMK_DIR:     *col = FM_DIR_COL;  *attr = ATTR_BOLD; break;
    case FMK_SYMLINK: *col = FM_LINK_COL; break;
    case FMK_MISSING: *col = mix_c(fg, 0xff000000u, 55); break;
    case FMK_FIFO:
    case FMK_SOCKET:
    case FMK_DEVICE:  *col = mix_c(fg, FM_DIR_COL, 40); break;
    default:          *col = e->exec ? FM_EXEC_COL : fg;
                      if (e->exec) *attr = ATTR_BOLD;
                      break;
    }
}

/* One line of the list pane. */
static void paint_entry(FileMgr *fm, const FmEntry *e, int y, uint32_t fg, uint32_t bg, int selected)
{
    FmPane *p = &fm->list;
    uint32_t rowbg = selected ? SEL_BG : bg;
    uint32_t col, attr;
    entry_colour(e, fg, &col, &attr);
    /* The selection changes the BACKGROUND only: the entry keeps its kind colour, so a selected
       directory is still recognisably a directory. Every accent here is light enough to read on
       SEL_BG, which is dark. */
    /* the whole row is highlighted, so a click anywhere on it reads as the selection */
    for (int x = 0; x < p->cols; x++)
        put_cell(p, x, y, 0, col, rowbg, 0);

    int x = pane_puts(p, 1, y, e->name, -1, col, rowbg, attr);
    if (e->kind == FMK_SYMLINK && x < p->cols)
        pane_puts(p, x, y, "@", -1, col, rowbg, 0);

    if (e->kind == FMK_FILE || e->kind == FMK_SYMLINK) {
        char sz[32];
        fm_human_size(e->size, sz, sizeof sz);
        pane_puts_right(p, p->cols - 1, y, sz, mix_c(fg, rowbg, 35), rowbg, 0);
    }
}

static void paint_list(FileMgr *fm, uint32_t fg, uint32_t bg)
{
    FmPane *p = &fm->list;
    if (!p->cell)
        return;
    uint32_t dim = mix_c(fg, bg, 45);
    pane_clear(p, fg, bg);

    int hdr, first, avail;
    list_metrics(fm, &hdr, &first, &avail);

    if (hdr) {
        char head[FM_PATH_MAX + 16];
        snprintf(head, sizeof head, "%s", fm->dir);
        pane_puts(p, 1, 0, head, -1, fg, bg, ATTR_BOLD);
    }
    for (int i = 0; i < avail; i++) {
        int pos = fm->top + i;
        if (pos >= fm->nvis)
            break;
        paint_entry(fm, &fm->ent[fm->vis[pos]], first + i, fg, bg, pos == fm->sel ? 1 : 0);
    }
    if (hdr) {
        char foot[FM_ERR_MAX];
        if (fm->err[0])
            snprintf(foot, sizeof foot, "%s", fm->err);
        else if (!fm->nvis)
            snprintf(foot, sizeof foot, "empty%s", fm->show_hidden ? "" : " (dotfiles hidden)");
        else
            snprintf(foot, sizeof foot, "%d of %d shown%s%s", fm->nvis, fm->n,
                     fm->truncated ? " (listing capped)" : "",
                     fm->show_hidden ? "  dotfiles on" : "");
        pane_puts(p, 1, fm->list_rows - 1, foot, -1,
                  fm->err[0] ? FM_ERR_COL : dim, bg, 0);
    }
}

/* The two header lines every preview starts with: what is being previewed, and what it is. */
static void paint_preview_head(FileMgr *fm, uint32_t fg, uint32_t bg, int *row_out)
{
    FmPane *p = &fm->prev;
    const FmPreview *pv = fm->pv;
    uint32_t dim = mix_c(fg, bg, 45);
    int row = 0;
    const char *base = strrchr(pv->path, '/');
    base = (base && base[1]) ? base + 1 : (pv->path[0] ? pv->path : "/");
    pane_puts(p, 1, row, base, -1, fg, bg, ATTR_BOLD);
    row++;
    char sub[FM_META_WIDTH];
    const char *what = pv->kind == FMP_DIR ? "directory"
                     : pv->kind == FMP_IMAGE ? "image"
                     : pv->kind == FMP_TEXT ? "text"
                     : pv->kind == FMP_META ? "information" : "preview";
    snprintf(sub, sizeof sub, "%.100s  -  %s", pv->path, what);
    pane_puts(p, 1, row, sub, -1, dim, bg, 0);
    row++;
    if (pv->err[0]) {
        pane_puts(p, 1, row, pv->err, -1, FM_ERR_COL, bg, 0);
        row++;
    }
    *row_out = row;
}

static void paint_preview_dir(FileMgr *fm, uint32_t fg, uint32_t bg, int row)
{
    FmPane *p = &fm->prev;
    const FmPreview *pv = fm->pv;
    uint32_t dim = mix_c(fg, bg, 45);
    for (int i = 0; i < pv->n && row < p->rows; i++, row++) {
        const FmEntry *e = &pv->ent[i];
        uint32_t col, attr;
        entry_colour(e, fg, &col, &attr);
        pane_puts(p, 2, row, e->name, -1, col, bg, attr);
        if (e->kind == FMK_FILE) {
            char sz[32];
            fm_human_size(e->size, sz, sizeof sz);
            pane_puts_right(p, p->cols - 1, row, sz, dim, bg, 0);
        } else if (e->kind == FMK_DIR) {
            pane_puts_right(p, p->cols - 1, row, "dir", dim, bg, 0);
        }
    }
    if (row < p->rows && (pv->n || pv->truncated)) {
        char more[64];
        snprintf(more, sizeof more, "%d entr%s%s", pv->n, pv->n == 1 ? "y" : "ies",
                 pv->truncated ? " - listing capped" : "");
        pane_puts(p, 2, row, more, -1, dim, bg, 0);
    }
}

static void paint_preview_text(FileMgr *fm, uint32_t fg, uint32_t bg, int row)
{
    FmPane *p = &fm->prev;
    const FmPreview *pv = fm->pv;
    FmHl st;
    fm_hl_init(&st, pv->lang);
    uint32_t hl[4096];
    const char *s = pv->text ? pv->text : "";
    size_t n = pv->len, i = 0;
    while (row < p->rows) {
        size_t eol = i;
        while (eol < n && s[eol] != '\n')
            eol++;
        size_t ln = imin_(eol - i, sizeof hl / sizeof hl[0]);
        fm_hl_line(&st, s + i, ln, hl, fg);
        int x = 2;
        size_t k = 0;
        while (k < ln && x < p->cols) {
            if (s[i + k] == '\t') {
                x = imin_(x + (4 - (x - 2) % 4), p->cols);   /* 4-column stops, aligned to the text */
                k++;
                continue;
            }
            size_t used = 0;
            uint32_t cp = dec_utf8(s + i + k, ln - k, &used);
            int wide = cp_is_wide(cp) && x + 1 < p->cols;
            put_cell(p, x, row, cp, hl[k], bg, wide ? ATTR_WIDE : 0);
            if (wide)
                put_cell(p, x + 1, row, 0, hl[k], bg, ATTR_WIDE);
            x += wide ? 2 : 1;
            k += used;
        }
        row++;
        if (eol >= n)
            break;
        i = eol + 1;
    }
    if (pv->truncated && row < p->rows) {
        char more[64];
        snprintf(more, sizeof more, "- text truncated at %u KiB -", FM_PREVIEW_BYTES / 1024u);
        pane_puts(p, 2, row, more, -1, mix_c(fg, bg, 45), bg, 0);
    }
}

static void paint_preview_meta(FileMgr *fm, uint32_t fg, uint32_t bg, int row)
{
    FmPane *p = &fm->prev;
    const FmPreview *pv = fm->pv;
    uint32_t dim = mix_c(fg, bg, 45);
    for (int i = 0; i < pv->nmeta && row < p->rows; i++, row++) {
        const char *line = pv->meta[i];
        const char *tab = strchr(line, '\t');
        if (!tab) {
            pane_puts(p, 2, row, line, -1, fg, bg, 0);
            continue;
        }
        int labelw = pane_cells(line, (size_t)(tab - line));
        pane_puts(p, 2, row, line, (int)(tab - line), dim, bg, 0);
        pane_puts(p, 2 + labelw + 1, row, tab + 1, -1, fg, bg, 0);
    }
}

static void paint_preview(FileMgr *fm, uint32_t fg, uint32_t bg)
{
    FmPane *p = &fm->prev;
    if (!p->cell)
        return;
    pane_clear(p, fg, bg);
    if (!fm->pv) {
        pane_puts(p, 1, 0, "no preview", -1, mix_c(fg, bg, 45), bg, 0);
        return;
    }
    int row = 0;
    paint_preview_head(fm, fg, bg, &row);
    switch (fm->pv->kind) {
    case FMP_DIR:  paint_preview_dir(fm, fg, bg, row); break;
    case FMP_TEXT: paint_preview_text(fm, fg, bg, row); break;
    case FMP_IMAGE:
        /* The picture itself is drawn by the renderer over the box fm_image_fit() reports; the cells
           under it stay blank so it composites against the pane background. */
        if (fm->pv->nmeta)
            paint_preview_meta(fm, fg, bg, row);
        break;
    default:       paint_preview_meta(fm, fg, bg, row); break;
    }
}

void fm_paint(FileMgr *fm, uint32_t fg, uint32_t bg)
{
    paint_list(fm, fg, bg);
    paint_preview(fm, fg, bg);
    fm->dirty = 0;
}

void fm_image_fit(const FileMgr *fm, int cell_w, int cell_h,
                  int *col, int *row, int *cols, int *rows, int *px_w, int *px_h)
{
    *col = *row = *cols = *rows = 0;
    *px_w = *px_h = 0;
    const FmPreview *pv = fm->pv;
    if (!pv || pv->kind != FMP_IMAGE || pv->w <= 0 || pv->h <= 0)
        return;
    int cw = imax_(cell_w, 1), ch = imax_(cell_h, 1);
    int avail_rows = imax_(fm->prev.rows - 3, 0);      /* the preview header owns 3 rows */
    if (avail_rows <= 0 || fm->prev.cols <= 0)
        return;
    int avail_w = fm->prev.cols * cw, avail_h = avail_rows * ch;

    /* Fit, never enlarge: a 32x32 icon in a 60x30 pane stays 32x32 px instead of becoming a blurry
       square. The ratio is kept in PIXELS, not cells, which is what stops a picture from looking
       stretched in a terminal whose cells are not square. */
    long long num = 1024, den = 1024;
    if ((long long)pv->w * avail_h < (long long)pv->h * avail_w) {
        num = avail_h; den = pv->h;                    /* height-limited */
    } else {
        num = avail_w; den = pv->w;                    /* width-limited */
    }
    if (num > den)
        num = den;                                     /* never upscale */
    long long scale = num * 1024 / imax_(den, 1);      /* 1024 == 1:1 */
    int w = imax_((int)((long long)pv->w * scale / 1024), 1);
    int h = imax_((int)((long long)pv->h * scale / 1024), 1);
    if (w > avail_w) w = avail_w;
    if (h > avail_h) h = avail_h;

    *cols = (w + cw - 1) / cw;
    *rows = (h + ch - 1) / ch;
    *col = (fm->prev.cols - *cols) / 2;
    *row = 3 + (avail_rows - *rows) / 2;
    if (*col < 0) *col = 0;
    if (*row < 3) *row = 3;
    /* The box in PIXELS as well as in cells: ceil-dividing into cells can add up to one cell of slack,
       and a picture drawn into the CELL box would be stretched by that slack. The renderer draws these
       numbers into the cell box and centres them in it, so the aspect the fit computed is the aspect the
       user sees. */
    *px_w = w;
    *px_h = h;
}
