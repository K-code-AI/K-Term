#!/bin/sh
# Kai-Term command-line regression suite (make test-cli).
#
# WHY A SCRIPT AND NOT A C TEST: the option parser lives in main() in src/main.c, and src/main.c is
# deliberately not linked into bin/test_vt (it pulls in Wayland, EGL and a PTY). So argv can only be
# checked by RUNNING the real binary and asserting on its output and exit status.
#
# Usage: tests/cli_test.sh [path/to/kai-term]     (default ./bin/kai-term)
# Prints "CLI: <n> checks, <m> failures" and exits non-zero if anything failed.
# Checks that would open a window are skipped when there is no Wayland session; the skip is reported.

BIN=${1:-./bin/kai-term}
if [ ! -x "$BIN" ]; then
    echo "cli_test: no executable at '$BIN' (run make first)" >&2
    exit 1
fi

checks=0
fails=0
skips=0
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

ck() {  # ck <got> <want> <name>
    checks=$((checks + 1))
    if [ "$1" = "$2" ]; then
        return 0
    fi
    fails=$((fails + 1))
    echo "FAIL: $3"
    echo "   got:  [$1]"
    echo "   want: [$2]"
}

skip() {  # skip <name> <why>
    skips=$((skips + 1))
    echo "SKIP: $1 ($2)"
}

# A hang must not wedge the suite: wrap every launch in `timeout` when it is available.
if command -v timeout > /dev/null 2>&1; then
    TMO="timeout -k 2 15"
else
    TMO=""
fi

# run <args...> -> sets out (stdout+stderr), rc
run() {
    outf="$tmp/o"
    : > "$outf"
    $TMO "$BIN" "$@" > "$outf" 2>&1 < /dev/null
    rc=$?
    out=$(cat "$outf")
}

# run_split <args...> -> sets stdout_only, stderr_only, rc
run_split() {
    sofile="$tmp/so"
    sefile="$tmp/se"
    : > "$sofile"; : > "$sefile"
    $TMO "$BIN" "$@" > "$sofile" 2> "$sefile" < /dev/null
    rc=$?
    stdout_only=$(cat "$sofile")
    stderr_only=$(cat "$sefile")
}

# every launch that opens a window is wrapped in a timeout so a hang cannot wedge the suite
if [ -n "$WAYLAND_DISPLAY" ]; then
    have_display=1
else
    have_display=0
fi

# ---------------- help / version (no display needed) ----------------
run_split -h
ck "$rc" 0 "-h exit status"
ck "$(printf '%s\n' "$stdout_only" | sed -n '1p')" "usage: $BIN [OPTION]..." "-h usage line"
run_split -h
ck "$stderr_only" "" "-h writes only to stdout"
for opt in "-e, --exec COMMAND" "-w, --workingdir DIR" "-c, --config FILE" \
           "-d, --debug" "-v, --version" "-h, --help"; do
    run -h
    ck "$(printf '%s\n' "$out" | grep -c -- "$opt")" 1 "-h lists $opt"
done
run -h
ck "$(printf '%s\n' "$out" | grep -c -- '-C,')" 0 "-h does not list the old -C"
run --help
ck "$rc" 0 "--help exit status"
run --help; help_long="$out"
run -h; help_short="$out"
ck "$help_long" "$help_short" "--help output equals -h"
run -v
ck "$rc" 0 "-v exit status"
ck "$(printf '%s\n' "$out" | grep -c '^kai-term ')" 1 "-v prints a version line"
run --version
ck "$rc" 0 "--version exit status"

# ---------------- unknown / removed options: one generic message ----------------
# -C is the OLD spelling of the config flag; -f -s -r -o were retired in session 43. None of them may
# get a special explanation (the user asked for one generic message), and none may be accepted.
for flag in -C -f -s -r -o -x --bogus; do
    run_split "$flag" 42
    ck "$rc" 1 "$flag rejected with exit 1"
    ck "$(printf '%s\n' "$stderr_only" | sed -n '1p')" "kai-term: unknown option '$flag'" \
       "$flag generic first line"
    ck "$(printf '%s\n' "$stderr_only" | grep -c '^usage:')" 1 "$flag prints the option list"
    ck "$(printf '%s\n' "$stderr_only" | grep -c 'any more')" 0 "$flag mentions no retired flag"
    ck "$stdout_only" "" "$flag writes nothing to stdout"
done

# ---------------- options that need an argument ----------------
for flag in -e -w -c --exec --workingdir --config; do
    run_split "$flag"
    ck "$rc" 1 "$flag with no argument exits 1"
    # the message echoes the flag exactly as the user typed it
    ck "$(printf '%s\n' "$stderr_only" | sed -n '1p')" "kai-term: $flag needs an argument" \
       "$flag with no argument message"
done

# ---------------- the config file flag ----------------
printf 'app_id = cli-suite\nfont_size = 30\n' > "$tmp/conf"
if [ "$have_display" = 1 ]; then
    run -c "$tmp/conf" -d -e true
    ck "$rc" 0 "-c FILE runs"
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-suite')" 1 "-c FILE loads that file"
    ck "$(printf '%s\n' "$out" | grep -c 'cell 18x34')" 1 "-c FILE applies font_size"
    run --config "$tmp/conf" -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-suite')" 1 "--config FILE loads that file"
    run --config="$tmp/conf" -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-suite')" 1 "--config=FILE loads that file"

    # -d must beat `log =` in the file (command line wins; past bug, session 43)
    printf 'app_id = cli-log\nlog = 0\n' > "$tmp/conf0"
    run -c "$tmp/conf0" -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-log')" 1 "-d overrides log = 0"
    run -c "$tmp/conf0" -e true
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-log')" 0 "without -d the config log level is kept"

    # -w DIR and -e COMMAND
    run -d -w "$tmp" -e true
    ck "$rc" 0 "-w DIR runs"
    ck "$(printf '%s\n' "$out" | grep -c "shell starts in $tmp")" 1 "-w DIR sets the startup directory"
    run -d -e "echo cli-marker"
    ck "$rc" 0 "-e COMMAND runs"
    # the shell exits, so either the pty reaches EOF first or SIGCHLD is reaped first; either way the
    # run must end by itself rather than timing out. With multiplexing the message names the tab.
    ck "$(printf '%s\n' "$out" | grep -cE 'mux: the pty of tab [0-9]+/[0-9]+ closed|mux: the shell of tab [0-9]+/[0-9]+ exited')" 1 "-e COMMAND ends on its own"
    run -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'kai-term exiting')" 1 "a plain run shuts down cleanly"

    # multiplexing settings: config-only keys, and the first session must be reported
    printf 'max_tabs = 4\nkey_new_tab = Alt+N\nkey_close_tab = none\nkey_tab_prefix = Alt\n' > "$tmp/muxconf"
    run -c "$tmp/muxconf" -d -e true
    ck "$rc" 0 "a config with the multiplexing keys runs"
    ck "$(printf '%s\n' "$out" | grep -c 'unknown key')" 0 "the multiplexing config keys are known"
    ck "$(printf '%s\n' "$out" | grep -c 'config: key_close_tab unbound')" 1 "key_close_tab = none unbinds it"
    ck "$(printf '%s\n' "$out" | grep -c 'mux: session 1/1 started')" 1 "the first session is logged"

    # how the sessions are SHOWN (session 57): the tab bar, the split cap and the toggle binding.
    # The `layout:` line is what layout_apply() logs once per layout-shape change, so it is the
    # client-side evidence that the bar really is on/off and how many panes the window has.
    printf 'tab_bar = always\nsplit_max = 2\nkey_toggle_layout = none\n' > "$tmp/barconf"
    run -c "$tmp/barconf" -d -e true
    ck "$rc" 0 "a config with the display keys runs"
    ck "$(printf '%s\n' "$out" | grep -c 'unknown key')" 0 "the display config keys are known"
    ck "$(printf '%s\n' "$out" | grep -c 'config: key_toggle_layout unbound')" 1 "key_toggle_layout = none unbinds the toggle"
    ck "$(printf '%s\n' "$out" | grep -c 'layout: tab view, 1 pane, tab bar on, grid')" 1 "tab_bar = always shows the bar with one session"
    run -d -e true
    ck "$(printf '%s\n' "$out" | grep -c 'layout: tab view, 1 pane, tab bar off, grid')" 1 "tab_bar = auto (the default) shows no bar with one session"

    # everything at once
    run -c "$tmp/conf" -d -w "$tmp" -e true
    ck "$rc" 0 "combined -c -d -w -e runs"
    ck "$(printf '%s\n' "$out" | grep -c 'app_id: cli-suite')" 1 "combined: config file applied"
    ck "$(printf '%s\n' "$out" | grep -c "shell starts in $tmp")" 1 "combined: -w applied"
else
    for name in "-c FILE" "--config FILE" "--config=FILE" "-d overrides log" "-w DIR" "-e COMMAND" \
                "multiplexing keys" "display keys (tab_bar/split_max)" "combined flags"; do
        skip "$name" "no WAYLAND_DISPLAY"
    done
fi

echo "CLI: $checks checks, $fails failures${skips:+, $skips skipped}"
[ "$fails" -eq 0 ] || exit 1
exit 0
