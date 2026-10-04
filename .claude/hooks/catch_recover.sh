#!/bin/bash
# catch_recover.sh [pid] [--kill-claudette <cpid>]
#
# Diagnose (and begin safely recovering) a claude left in a bad terminal state
# after a Claudette `catch`. Figures out where the process's stdio actually
# points, fires a harmless redraw, classifies the failure, and prints the one
# safe next move. Destructive steps are opt-in only.
#
# Run as the OWNER of <pid> (e.g. the local user) — reading /proc/<pid>/fd needs it.

set -uo pipefail   # NOT -e: probes may fail; we want to report, not abort

PID="${1:-}"
if [ -z "$PID" ]; then
    echo "Usage: catch_recover.sh <pid-of-caught-claude> [--kill-claudette <cpid>]" >&2
    exit 1
fi
KILL_CPID=""
[ "${2:-}" = "--kill-claudette" ] && KILL_CPID="${3:-}"

# Tee everything to a world-readable log so Claude can read the verdict directly
# (no copy-paste). Path is stable per target pid.
LOG="/tmp/catch_recover_${PID}.log"
: > "$LOG" 2>/dev/null && chmod 644 "$LOG" 2>/dev/null
exec > >(tee "$LOG") 2>&1

say()  { printf '%s\n' "$*"; }
hr()   { printf -- '────────────────────────────────────────\n'; }

hr; say "catch_recover: target claude PID $PID"; hr

# 1． Alive?
if ! kill -0 "$PID" 2>/dev/null; then
    say "✗ PID $PID is not alive. Nothing to recover (process is gone)."
    exit 1
fi
say "✓ alive"

# 2． Controlling terminal
CTTY="$(ps -o tty= -p "$PID" | tr -d ' ')"
say "  controlling tty : ${CTTY:-none}"
say "  ps state        : $(ps -o stat= -p "$PID" | tr -d ' ')   uptime $(ps -o etime= -p "$PID" | tr -d ' ')"

# 3． Where do stdin/out/err actually point?
declare -a TGT
say "  stdio fds:"
for n in 0 1 2; do
    t="$(readlink "/proc/$PID/fd/$n" 2>/dev/null || echo '??(no perm — run as owner)')"
    TGT[$n]="$t"
    case "$n" in 0) lbl=stdin;; 1) lbl=stdout;; 2) lbl=stderr;; esac
    printf '    fd%d %-6s → %s\n' "$n" "$lbl" "$t"
done

# 4． Classify
CT_DEV="/dev/$CTTY"
on_ctty=0 ; on_other=0
for n in 0 1 2; do
    case "${TGT[$n]}" in
        "$CT_DEV")        on_ctty=$((on_ctty+1)) ;;
        /dev/pts/*)       on_other=$((on_other+1)) ;;
    esac
done

hr
if   [ "$on_ctty" -eq 3 ]; then
    VERDICT="NOT_STUCK"
    say "VERDICT: catch did NOT stick — all 3 fds still on $CT_DEV."
    say "         The garble is just corrupted terminal state, not a moved session."
elif [ "$on_other" -eq 3 ] && [ "$on_ctty" -eq 0 ]; then
    VERDICT="FULLY_MOVED"
    say "VERDICT: catch took FULLY — all 3 fds moved to Claudette's PTY (${TGT[1]})."
    say "         Garble = Claudette's emulator choking on the Ink TUI."
else
    VERDICT="SPLIT"
    say "VERDICT: SPLIT-BRAIN — fds are split between $CT_DEV and Claudette's PTY."
    say "         (${on_ctty} on ctty, ${on_other} on other pts) — the worst case."
fi
hr

# 5． Harmless redraw nudge (safe in every case)
say "→ sending SIGWINCH to force a TUI repaint (harmless)…"
kill -WINCH "$PID" 2>/dev/null && say "  sent. Now drag-resize the $CTTY window once to trigger a redraw." \
                               || say "  could not signal (perm?)"

# 6． Show candidate Claudettes (the catcher has NO spawned claude child)
hr; say "Claudette processes (catcher = the one with no claude child):"
while read -r cpid cppid; do
    [ -z "$cpid" ] && continue
    kids="$(pgrep -P "$cpid" 2>/dev/null | tr '\n' ' ')"
    kidcomm=""
    for k in $kids; do kidcomm+="$(ps -o comm= -p "$k" 2>/dev/null) "; done
    if printf '%s' "$kidcomm" | grep -qiE 'claude|node|sudo'; then
        role="spawned-claude (LEAVE ALONE)"
    else
        role="*** likely CATCHER ***"
    fi
    printf '  Claudette pid %-7s (ppid %-7s) children:[%s] → %s\n' "$cpid" "$cppid" "${kidcomm:-none}" "$role"
done < <(ps -eo pid=,ppid=,comm= | awk '$3=="Claudette"{print $1, $2}')

# 7． Recommended next move
hr; say "NEXT MOVE:"
case "$VERDICT" in
  NOT_STUCK)
    say "  Safe path. After the redraw above, kill the catcher to remove interference:"
    say "    $0 $PID --kill-claudette <catcher-pid>"
    say "  $PID stays alive (its fds never left $CT_DEV)." ;;
  FULLY_MOVED)
    say "  Do NOT kill the catcher — it holds the only PTY $PID talks through;"
    say "  killing it SIGHUPs $PID. Recovery = reverse the migration back to"
    say "  $CT_DEV. Tell Claude to build the 'release' tool (reverse ptrace dup2)." ;;
  SPLIT)
    say "  Do NOT kill anything yet. Paste this whole output back to Claude —"
    say "  split state needs a tailored reverse, not a blunt kill." ;;
esac
hr

# 8． Opt-in kill (only meaningful for the NOT_STUCK case)
if [ -n "$KILL_CPID" ]; then
    say ""
    if [ "$VERDICT" != "NOT_STUCK" ]; then
        say "⚠ REFUSING --kill-claudette: verdict is $VERDICT, not NOT_STUCK."
        say "  Killing the catcher here could take $PID down. Aborting."
        exit 2
    fi
    say "→ verdict NOT_STUCK: gracefully terminating catcher Claudette $KILL_CPID (SIGTERM)…"
    kill -TERM "$KILL_CPID" 2>/dev/null && say "  sent SIGTERM." || say "  could not signal $KILL_CPID."
    sleep 1
    kill -0 "$PID" 2>/dev/null && say "✓ $PID still alive after killing catcher — good." \
                               || say "✗ $PID died — unexpected; check immediately."
fi
