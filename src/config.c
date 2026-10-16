/* Kai-Term — configuration: simple "key = value" file. */
#include "kai-term.h"

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
       reads like the display name. DEF_APP_ID in kai-term.h is the single source (see below). */
    snprintf(c->app_id, sizeof(c->app_id), DEF_APP_ID);
    c->clear_scrollback = 1;   /* CSI 3 J drops the scrollback (on by default) */
    c->copy_on_release = 0;    /* selecting does not copy unless the user asks (Ctrl+Shift+C does) */
    c->repeat_delay = 300;     /* ms before a held key repeats (0 = Kai-Term does not repeat) */
    c->repeat_rate = 30;       /* repeats per second once repeating */
    /* A few pixels of margin on every side. Compositors round the window corners (niri does), and with
       the grid flush against the edge the corner glyphs get clipped. 0 restores the old layout. */
    c->padding = 8;
    /* Multiplexing defaults: the GNOME-terminal-style tab keys. Every one of them is a config string,
       so a user can rebind or unbind (`key_new_tab = none`) any action. */
    c->max_tabs = 8;
    keybind_parse("Ctrl+Shift+T", &c->key_new_tab);
    keybind_parse("Ctrl+Shift+W", &c->key_close_tab);
    keybind_parse("Ctrl+Shift+Right", &c->key_next_tab);
    keybind_parse("Ctrl+Shift+Left", &c->key_prev_tab);
    keymods_parse("Ctrl+Shift", &c->tab_prefix_mods);
    /* How the sessions are shown: the bar appears by itself at the second session (so a single-session
       window looks exactly like it did before multiplexing), and side-by-side view shows at most 4
       sessions at a time — below that a pane is still wide enough to read. */
    c->tab_bar = BAR_AUTO;
    c->split_max = 4;
    keybind_parse("Ctrl+Shift+S", &c->key_toggle_layout);
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

/* ---------- key binding specs ("Ctrl+Shift+T") ---------- */

/* Modifier names a binding may use. xkbcommon's own names (Mod1/Mod4) are included because
   `setxkbmap`-style documentation quotes them; main.c maps them back onto the real masks. */
static const struct { const char *name; unsigned bit; } mod_names[] = {
    { "ctrl",    KB_CTRL  }, { "control", KB_CTRL  },
    { "shift",   KB_SHIFT },
    { "alt",     KB_ALT   }, { "mod1",    KB_ALT   },
    { "super",   KB_SUPER }, { "mod4",    KB_SUPER }, { "win", KB_SUPER }, { "gui", KB_SUPER },
};

/* Named non-printable keys. Printable keys need no table: a one-character token IS its keysym. */
static const struct { const char *name; uint32_t sym; } key_names[] = {
    { "BackSpace", KB_KEY_BackSpace },
    { "Tab",       KB_KEY_Tab       },
    { "Return",    KB_KEY_Return    }, { "Enter", KB_KEY_Return },
    { "Escape",    KB_KEY_Escape    }, { "Esc",   KB_KEY_Escape },
    { "space",     KB_KEY_Space     }, { "Space", KB_KEY_Space  },
    { "Left",      KB_KEY_Left      }, { "Right", KB_KEY_Right },
    { "Up",        KB_KEY_Up        }, { "Down",  KB_KEY_Down  },
    { "Home",      KB_KEY_Home      }, { "End",   KB_KEY_End   },
    { "Page_Up",   KB_KEY_Prior     }, { "PageUp", KB_KEY_Prior }, { "Prior", KB_KEY_Prior },
    { "Page_Down", KB_KEY_Next      }, { "PageDown", KB_KEY_Next }, { "Next", KB_KEY_Next },
    { "Insert",    KB_KEY_Insert    }, { "Delete", KB_KEY_Delete },
    { "F1",  KB_KEY_F1  }, { "F2",  KB_KEY_F2  }, { "F3",  KB_KEY_F3  },
    { "F4",  KB_KEY_F4  }, { "F5",  KB_KEY_F5  }, { "F6",  KB_KEY_F6  },
    { "F7",  KB_KEY_F7  }, { "F8",  KB_KEY_F8  }, { "F9",  KB_KEY_F9  },
    { "F10", KB_KEY_F10 }, { "F11", KB_KEY_F11 }, { "F12", KB_KEY_F12 },
};

/* Split a binding spec on '+', trimming spaces. Returns the token count, 0 when the spec is
   malformed or absurdly long (a config line is 512 bytes, so 8 tokens of 31 is generous). */
static int split_spec(const char *spec, char tok[8][32])
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", spec);
    int n = 0;
    char *save = NULL;
    for (char *p = strtok_r(buf, "+", &save); p; p = strtok_r(NULL, "+", &save)) {
        trim(p);
        if (!p[0])
            continue;                       /* "Ctrl++" style empties are ignored */
        if (n >= 8)
            return 0;
        snprintf(tok[n++], 32, "%s", p);
    }
    return n;
}

static unsigned mod_bit_of(const char *tok)
{
    for (size_t i = 0; i < sizeof(mod_names) / sizeof(mod_names[0]); i++)
        if (!strcasecmp(tok, mod_names[i].name))
            return mod_names[i].bit;
    return 0;
}

/* A key token -> its keysym. A single printable character IS its own keysym (X keysyms for ASCII are
   the codepoints), and letters are folded to LOWERCASE: a binding names the KEY, and the Shift state
   is a modifier of its own, so "Ctrl+Shift+T" and "Ctrl+Shift+t" mean the same thing and main.c
   folds the pressed keysym the same way before comparing. */
static int sym_of(const char *tok, uint32_t *sym)
{
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++)
        if (!strcasecmp(tok, key_names[i].name)) {
            *sym = key_names[i].sym;
            return 1;
        }
    if (!tok[1]) {
        unsigned char c = (unsigned char)tok[0];
        if (c >= '!' && c <= '~') {           /* printable, not space */
            if (c >= 'A' && c <= 'Z')
                c = (unsigned char)(c - 'A' + 'a');
            *sym = c;
            return 1;
        }
    }
    return 0;
}

int keybind_parse(const char *spec, KeyBind *out)
{
    out->sym = 0;
    out->mods = 0;
    out->set = 0;
    if (!spec)
        return 0;
    char tok[8][32];
    int n = split_spec(spec, tok);
    if (n <= 0)
        return 0;
    unsigned mods = 0;
    uint32_t sym = 0;
    int have_key = 0;
    for (int i = 0; i < n; i++) {
        unsigned b = mod_bit_of(tok[i]);
        if (b) {
            mods |= b;
            continue;
        }
        uint32_t s;
        if (!sym_of(tok[i], &s)) {
            klog(L_WARN, "config: binding '%s' names an unknown key '%s' (known: a single "
                         "character, or BackSpace Tab Return Escape space Left Right Up Down Home End "
                         "Page_Up Page_Down Insert Delete F1-F12)", spec, tok[i]);
            return 0;
        }
        if (have_key) {
            klog(L_WARN, "config: binding '%s' names more than one key", spec);
            return 0;
        }
        sym = s;
        have_key = 1;
    }
    if (!have_key) {
        klog(L_WARN, "config: binding '%s' names no key", spec);
        return 0;
    }
    if (!mods) {
        /* Refused on purpose: an unmodified binding would take a plain typing key away from the
           shell, which is never what the user meant to switch a tab with. */
        klog(L_WARN, "config: binding '%s' has no modifier — refusing it (an unmodified binding "
                     "would swallow normal typing)", spec);
        return 0;
    }
    out->sym = sym;
    out->mods = mods;
    out->set = 1;
    return 1;
}

/* A modifier-only spec, for the `key_tab_prefix` shortcut (prefix + a digit 1-9). */
int keymods_parse(const char *spec, unsigned *mods)
{
    *mods = 0;
    if (!spec)
        return 0;
    char tok[8][32];
    int n = split_spec(spec, tok);
    if (n <= 0)
        return 0;
    unsigned m = 0;
    for (int i = 0; i < n; i++) {
        unsigned b = mod_bit_of(tok[i]);
        if (!b) {
            klog(L_WARN, "config: '%s' is not a modifier list: '%s' is not a modifier", spec, tok[i]);
            return 0;
        }
        m |= b;
    }
    if (!m)
        return 0;
    *mods = m;
    return 1;
}

/* `key_xxx =` (empty) or none / off / no / 0 unbinds the action. A spec that does not parse keeps the
   binding that is already set (the default), so one typo cannot silently disable tab switching. */
static void set_keybind(KeyBind *kb, const char *key, const char *val)
{
    if (!val[0] || !strcmp(val, "0") || !strcasecmp(val, "none") ||
        !strcasecmp(val, "off") || !strcasecmp(val, "no")) {
        kb->sym = 0;
        kb->mods = 0;
        kb->set = 0;
        klog(L_INFO, "config: %s unbound", key);
        return;
    }
    KeyBind parsed;
    if (!keybind_parse(val, &parsed)) {
        klog(L_WARN, "config: %s = '%s' is not a usable binding (want MOD+MOD+KEY, for example "
                     "Ctrl+Shift+T); keeping the current binding", key, val);
        return;
    }
    *kb = parsed;
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
        /* 0 turns Kai-Term's own repeat off and leaves it to the compositor. */
        c->repeat_delay = iclamp_(atoi(val), 0, 5000);
    } else if (!strcmp(key, "repeat_rate")) {
        c->repeat_rate = iclamp_(atoi(val), 1, 200);
    } else if (!strcmp(key, "padding")) {
        /* px of margin around the whole grid, applied on all four sides (0 = flush with the window
           edge). The clamp keeps a huge value from shrinking the terminal away: term_grid_geometry()
           additionally caps the margin so at least one whole cell always fits. */
        c->padding = iclamp_(atoi(val), 0, 128);
    } else if (!strcmp(key, "max_tabs")) {
        /* How many sessions one window may hold. Clamped to 1..TAB_MAX: 0 would mean a terminal that
           cannot open its own first tab. */
        c->max_tabs = iclamp_(atoi(val), 1, TAB_MAX);
    } else if (!strcmp(key, "key_new_tab")) {
        set_keybind(&c->key_new_tab, key, val);
    } else if (!strcmp(key, "key_close_tab")) {
        set_keybind(&c->key_close_tab, key, val);
    } else if (!strcmp(key, "key_next_tab")) {
        set_keybind(&c->key_next_tab, key, val);
    } else if (!strcmp(key, "key_prev_tab")) {
        set_keybind(&c->key_prev_tab, key, val);
    } else if (!strcmp(key, "key_tab_prefix")) {
        /* prefix + a digit 1-9 jumps to that tab; empty / none / off disables the shortcut */
        if (!val[0] || !strcasecmp(val, "none") || !strcasecmp(val, "off") ||
            !strcasecmp(val, "no") || !strcmp(val, "0")) {
            c->tab_prefix_mods = 0;
            klog(L_INFO, "config: key_tab_prefix disabled");
        } else if (!keymods_parse(val, &c->tab_prefix_mods)) {
            klog(L_WARN, "config: key_tab_prefix = '%s' is not a modifier list (for example "
                         "Ctrl+Shift); keeping the current one", val);
        }
    } else if (!strcmp(key, "tab_bar")) {
        /* off = never draw the bar (the window title and the keyboard are the only way to see the other
           sessions), auto (default) = draw it from the second session on, always = also with one session. */
        if (!strcmp(val, "0") || !strcasecmp(val, "off") || !strcasecmp(val, "no") ||
            !strcasecmp(val, "false") || !strcasecmp(val, "never")) {
            c->tab_bar = BAR_OFF;
        } else if (!strcmp(val, "1") || !strcasecmp(val, "always") || !strcasecmp(val, "on") ||
                   !strcasecmp(val, "yes") || !strcasecmp(val, "true")) {
            c->tab_bar = BAR_ALWAYS;
        } else if (!strcasecmp(val, "auto") || !strcasecmp(val, "multiple")) {
            c->tab_bar = BAR_AUTO;
        } else {
            klog(L_WARN, "config: tab_bar = '%s' is not a value (off, auto or always); keeping %s",
                 val, c->tab_bar == BAR_OFF ? "off" : (c->tab_bar == BAR_ALWAYS ? "always" : "auto"));
        }
    } else if (!strcmp(key, "split_max")) {
        /* Sessions drawn side by side at most. More sessions than that are still open and still listed in
           the bar; the shown window of panes slides to contain the visible one. */
        c->split_max = iclamp_(atoi(val), 1, TAB_MAX);
    } else if (!strcmp(key, "key_toggle_layout")) {
        set_keybind(&c->key_toggle_layout, key, val);
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
            snprintf(defpath, sizeof(defpath), "%s/kai-term/kai-term.conf", xdg);
        else
            snprintf(defpath, sizeof(defpath), "%s/.config/kai-term/kai-term.conf",
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
