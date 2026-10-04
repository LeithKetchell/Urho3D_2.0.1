#!/bin/bash
# handback.sh [pid] [--kill-catcher <cpid>]
#
# Hand a "caught" claude back to its original terminal: reverse the fd migration
# (release_fds) so <pid>'s stdio points at its controlling tty again, verify it
# took, nudge a redraw, and (optionally) shut down the now-decoupled catcher.
#
# Run as the OWNER of <pid> (e.g. the local user), kernel.yama.ptrace_scope=0.

set -uo pipefail

PID="${1:-}"
if [ -z "$PID" ]; then
    echo "Usage: handback.sh <pid-of-caught-claude> [--kill-catcher <cpid>]" >&2
    exit 1
fi
KILL_CPID=""
[ "${2:-}" = "--kill-catcher" ] && KILL_CPID="${3:-}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REL="$ROOT/build/bin/release_fds"

LOG="/tmp/handback_${PID}.log"
: > "$LOG" 2>/dev/null && chmod 644 "$LOG" 2>/dev/null
exec > >(tee "$LOG") 2>&1

say(){ printf '%s\n' "$*"; }
hr(){ printf -- '────────────────────────────────────────\n'; }
fds(){ for n in 0 1 2; do printf '    fd%d → %s\n' "$n" "$(readlink /proc/$1/fd/$n 2>/dev/null || echo '??')"; done; }

hr; say "handback: returning PID $PID to its controlling terminal"; hr

kill -0 "$PID" 2>/dev/null || { say "✗ PID $PID not alive"; exit 1; }
[ -x "$REL" ] || { say "✗ release_fds not found at $REL"; exit 1; }

CTTY="$(ps -o tty= -p "$PID" | tr -d ' ')"
TTYPATH="/dev/$CTTY"
say "  controlling tty : $TTYPATH"
say "  fds BEFORE:"; fds "$PID"

if [ ! -e "$TTYPATH" ]; then
    say "✗ $TTYPATH no longer exists — original terminal was closed."
    say "  Open a fresh terminal, get its /dev/pts/N (run: tty), then:"
    say "    $REL $PID /dev/pts/N"
    exit 1
fi

hr; say "→ reversing fd migration to $TTYPATH …"
"$REL" "$PID" "$TTYPATH"
rc=$?
hr

say "  fds AFTER:"; fds "$PID"
ok=0; for n in 0 1 2; do [ "$(readlink /proc/$PID/fd/$n 2>/dev/null)" = "$TTYPATH" ] && ok=$((ok+1)); done

if [ "$ok" -eq 3 ]; then
    say "✓ all 3 fds now on $TTYPATH — handed back."
elif [ "$ok" -gt 0 ]; then
    say "⚠ only $ok/3 fds moved back (rc=$rc). Partial — do NOT kill the catcher; re-run handback.sh."
    exit 2
else
    say "✗ fds did not move (rc=$rc). Catcher still owns them — do NOT kill it. Check ptrace_scope/owner."
    exit 2
fi

# Clean the terminal state claude left behind, then repaint
say "→ resetting terminal state on $TTYPATH and repainting…"
stty sane < "$TTYPATH" 2>/dev/null || true
kill -WINCH "$PID" 2>/dev/null || true
say "  In the $CTTY terminal: press Enter; if still messy, type 'reset' + Enter."

hr; say "NEXT:"
say "  $PID is back on $CTTY, context intact. The catcher no longer holds its"
say "  stdio, so it is now SAFE to shut down. Find it:"
say "    ps -eo pid,comm | grep Claudette   # the one with no claude child"
say "  then:  handback.sh $PID --kill-catcher <cpid>"

if [ -n "$KILL_CPID" ]; then
    hr
    if [ "$ok" -ne 3 ]; then
        say "⚠ REFUSING --kill-catcher: fds not fully restored. Aborting kill."
        exit 2
    fi
    # MUST be SIGKILL: a catcher Claudette set childPid_=<caught pid>, so its
    # graceful Stop()/bury does kill(childPid_, SIGTERM) — which would signal the
    # claude we just rescued. SIGKILL skips all cleanup, so it can't reach it.
    say "→ fds confirmed off the catcher — killing Claudette $KILL_CPID (SIGKILL, skips its cleanup)…"
    kill -9 "$KILL_CPID" 2>/dev/null && say "  sent." || say "  could not signal $KILL_CPID."
    sleep 1
    kill -0 "$PID" 2>/dev/null && say "✓ $PID still alive after catcher shutdown — done." \
                               || say "✗ $PID died — investigate."
fi
hr
