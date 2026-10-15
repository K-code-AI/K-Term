/* K-Term — configuration: simple "key = value" file. */
#include "k-term.h"

#include <ctype.h>
#include <errno.h>
#include <strings.h>     /* strcasecmp for the boolean config values */
#include <unistd.h>

static uint32_t parse_color(const char *s, uint32_t fallback)
{
    while (isspace((unsigned char)*s))
        s++;
    if (s[0] == '#')
        s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    char *end;
    unsigned long long v = strtoull(s, &end, 16);
    if (end == s)
        return fallback;
    while (isspace((unsigned char)*end))
        end++;
    if (*end)
        return fallback;   /* trailing garbage */
    if (strlen(s) <= 6)
        v |= 0xff000000ull;   /* no alpha given: opaque */
    return (uint32_t)v;
}

void config_default(Config *c)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->font, sizeof(c->font), "monospace");
    c->font_size = 14;
    c->cols = 80;
    c->rows = 24;
    c->scrollback = 2000;
    c->fg = DEF_FG;
    c->bg = DEF_BG;
    c->cursor_color = CURSOR_COL;
    c->opacity = 1.0f;
    c->shell[0] = 0;           /* empty => $SHELL */
    c->working_dir[0] = 0;     /* empty => $HOME */
    /* xdg-shell app_id: window rules and taskbar grouping key off it, so the default
       reads like the display name. DEF_APP_ID in k-term.h is the single source (see below). */
    snprintf(c->app_id, sizeof(c->app_id), DEF_APP_ID);
    c->clear_scrollback = 1;   /* CSI 3 J drops the scrollback (on by default) */
    c->copy_on_release = 0;    /* selecting does not copy unless the user asks (Ctrl+Shift+C does) */
    c->repeat_delay = 300;     /* ms before a held key repeats (0 = K-Term does not repeat) */
    c->repeat_rate = 30;       /* repeats per second once repeating */
    /* A few pixels of margin on every side. Compositors round the window corners (niri does), and with
       the grid flush against the edge the corner glyphs get clipped. 0 restores the old layout. */
    c->padding = 8;
    c->loglevel = L_WARN;
}

void expand_tilde(const char *in, char *out, size_t cap)
{
    if (in && in[0] == '~' && (in[1] == 0 || in[1] == '/')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(out, cap, "%s%s", home, in + 1);
            return;
        }
    }
    snprintf(out, cap, "%s", in && in ? in : "");
}

static void trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        e--;
    *e = 0;
    char *b = s;
    while (*b && isspace((unsigned char)*b))
        b++;
    if (b != s)
        memmove(s, b, strlen(b) + 1);
}

/* Boolean config values: 0 / false / no / off (case-insensitive) mean OFF, anything else means ON.
   Shared by clear_scrollback and copy_on_release so the two cannot drift apart. */
static int cfg_bool(const char *val)
{
    if (!strcmp(val, "0") || !strcasecmp(val, "false") ||
        !strcasecmp(val, "no") || !strcasecmp(val, "off"))
        return 0;
    return 1;
}

static void apply(Config *c, const char *key, char *val)
{
    if (!strcmp(key, "font")) {
        snprintf(c->font, sizeof(c->font), "%s", val);
    } else if (!strcmp(key, "font_size")) {
        c->font_size = iclamp_(atoi(val), 6, 256);
    } else if (!strcmp(key, "cols")) {
        c->cols = iclamp_(atoi(val), 20, 4096);
    } else if (!strcmp(key, "rows")) {
        c->rows = iclamp_(atoi(val), 4, 4096);
    } else if (!strcmp(key, "scrollback")) {
        c->scrollback = iclamp_(atoi(val), 0, 1000000);
    } else if (!strcmp(key, "foreground")) {
        c->fg = parse_color(val, c->fg);
    } else if (!strcmp(key, "background")) {
        c->bg = parse_color(val, c->bg);
    } else if (!strcmp(key, "cursor_color")) {
        c->cursor_color = parse_color(val, c->cursor_color);
    } else if (!strcmp(key, "opacity")) {
        float f = (float)atof(val);
        c->opacity = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    } else if (!strcmp(key, "shell")) {
        snprintf(c->shell, sizeof(c->shell), "%s", val);
    } else if (!strcmp(key, "working_dir")) {
        expand_tilde(val, c->working_dir, sizeof(c->working_dir));
    } else if (!strcmp(key, "app_id")) {
        /* The compositor's handle for the window: niri/Sway window rules, the taskbar entry and the
         "open terminal here" matches all key off it. An empty value is allowed and means "use the
         built-in default" (DEF_APP_ID), so `app_id =` never produces an empty id. */
        snprintf(c->app_id, sizeof(c->app_id), "%s", val);
        if (!c->app_id[0])
            snprintf(c->app_id, sizeof(c->app_id), DEF_APP_ID);
    } else if (!strcmp(key, "clear_scrollback")) {
        /* 1 (default) = CSI 3 J also drops the scrollback; 0 = keep the history */
        c->clear_scrollback = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "copy_on_release")) {
        /* 0 (default) = releasing the drag only selects; 1 = also copy to the clipboard.
           Ctrl+Shift+C copies either way. */
        c->copy_on_release = cfg_bool(val) ? 1 : 0;
    } else if (!strcmp(key, "repeat_delay")) {
        /* 0 turns K-Term's own repeat off and leaves it to the compositor. */
        c->repeat_delay = iclamp_(atoi(val), 0, 5000);
    } else if (!strcmp(key, "repeat_rate")) {
        c->repeat_rate = iclamp_(atoi(val), 1, 200);
    } else if (!strcmp(key, "padding")) {
        /* px of margin around the whole grid, applied on all four sides (0 = flush with the window
           edge). The clamp keeps a huge value from shrinking the terminal away: term_grid_geometry()
           additionally caps the margin so at least one whole cell always fits. */
        c->padding = iclamp_(atoi(val), 0, 128);
    } else if (!strcmp(key, "log")) {
        c->loglevel = iclamp_(atoi(val), L_ERROR, L_DEBUG);
    } else {
        klog(L_WARN, "config: unknown key '%s'", key);
    }
}

void config_load(Config *c, const char *path)
{
    char defpath[4096];
    if (!path) {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg)
            snprintf(defpath, sizeof(defpath), "%s/k-term/k-term.conf", xdg);
        else
            snprintf(defpath, sizeof(defpath), "%s/.config/k-term/k-term.conf",
                     getenv("HOME") ? getenv("HOME") : ".");
        path = defpath;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        klog(L_INFO, "no config file at %s (using defaults)", path);
        return;
    }

    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        /* '#' starts a comment only at the start of the line or after whitespace, so a value such
           as working_dir = /home/me/my#project survives; "value # note" is still stripped. */
        for (char *hash = line; *hash; hash++) {
            if (*hash != '#')
                continue;
            if (hash == line || hash[-1] == ' ' || hash[-1] == '\t') {
                *hash = 0;
                break;
            }
        }
        trim(line);
        if (!line[0])
            continue;
        char *eq = strchr(line, '=');
        if (!eq) {
            klog(L_WARN, "config:%d: malformed line '%s'", lineno, line);
            continue;
        }
        *eq = 0;
        char *key = line, *val = eq + 1;
        trim(key);
        trim(val);
        if (!key[0]) {
            klog(L_WARN, "config:%d: empty key", lineno);
            continue;
        }
        apply(c, key, val);
    }
    fclose(f);
    klog(L_INFO, "config loaded from %s", path);
}
