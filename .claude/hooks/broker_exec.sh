#!/bin/bash
# broker_exec.sh <arg0> [arg1 ...] — run a whitelisted command (git / clang-format /
# clang-tidy) through this instance's Claudette broker so it executes as the local user and
# its output/file-changes land owned by the local user. cwd = the current working directory.
#
# Drop-in: `broker_exec.sh git commit -m ...`, `broker_exec.sh clang-format -i f.cpp`.
# Falls back to running the command DIRECTLY if no broker is reachable (transition-safe,
# same fail-open stance as broker_write.sh). See Claude/CLAUDETTE_WRITE_EXEC_BROKER_PLAN.md.
source "$(dirname "$0")/broker_lib.sh"

# --no-fallback: fail closed when no broker is reachable, instead of running the
# command directly as claude. The build path (safe_build.sh) passes this so a
# missing broker can never silently produce claude-owned artifacts. Other
# consumers (git / clang-format) omit it and keep the phase-4a direct fallback.
NO_FALLBACK=0
if [ "$1" = "--no-fallback" ]; then
    NO_FALLBACK=1
    shift
fi

if [ "$#" -lt 1 ]; then
    echo "usage: broker_exec.sh [--no-fallback] <command> [args...]" >&2
    exit 2
fi

if ! find_broker_sock; then
    if [ "$NO_FALLBACK" = 1 ]; then
        echo "broker_exec: no broker reachable — refusing to run '$1' as claude (fail-closed)" >&2
        exit 1
    fi
    # No broker -> run directly so brokerless instances aren't bricked (phase 4a).
    exec "$@"
fi

exec env SOCK="$BROKER_SOCK" python3 "$(dirname "$0")/broker_exec.py" "$PWD" "$@"
