#!/bin/bash
# adopt.sh <pid> — hand a running ("free"/orphan) claude to a catching Claudette.
#
# Instead of spawning a fresh claude, Claudette ptrace-adopts the live process
# <pid>: it migrates that claude's stdio onto a Claudette-owned PTY, then holds
# the persistent Manager connection on its behalf (registering under the caught
# pid). The orphan terminal goes silent — the session moves into the Claudette
# window — and the instance becomes a fully registered, reachable coder.
#
# MUST be run as the same user that owns the target claude (ptrace attach and
# Claudette's adoption both require same-user, with kernel.yama.ptrace_scope=0).

set -euo pipefail

PID="${1:-}"
if [ -z "$PID" ]; then
    echo "Usage: adopt.sh <pid-of-claude>" >&2
    echo "  hint: pgrep -u \"\$(id -un)\" -x claude" >&2
    exit 1
fi

# Resolve project root from this script's location (.claude/hooks/adopt.sh)
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLAUDETTE="$ROOT/build/bin/Claudette"

# 1. Target must be alive
kill -0 "$PID" 2>/dev/null || { echo "adopt: PID $PID is not alive" >&2; exit 1; }

# 2. Target must be a claude — and NOT already a Claudette
comm="$(ps -o comm= -p "$PID" | tr -d ' ')"
[ "$comm" = "Claudette" ] && { echo "adopt: PID $PID is already a Claudette" >&2; exit 1; }
[ "$comm" = "claude" ]    || { echo "adopt: PID $PID is '$comm', not 'claude'" >&2; exit 1; }

# 3. Must own the target (ptrace_scope=0 only permits attaching to own processes)
towner="$(ps -o user= -p "$PID" | tr -d ' ')"
me="$(id -un)"
[ "$towner" = "$me" ] || {
    echo "adopt: PID $PID is owned by '$towner', but you are '$me' — run adopt.sh as '$towner'" >&2
    exit 1
}

# 4. ptrace must be permitted
scope="$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 0)"
[ "$scope" = "0" ] || {
    echo "adopt: ptrace_scope=$scope (need 0) — run: sudo sysctl kernel.yama.ptrace_scope=0" >&2
    exit 1
}

# 5. Catching Claudette binary must exist
[ -x "$CLAUDETTE" ] || { echo "adopt: Claudette not found/executable at $CLAUDETTE" >&2; exit 1; }

echo "adopt: handing claude PID $PID to a catching Claudette (its terminal will go silent)..."
exec "$CLAUDETTE" catch pid "$PID"
