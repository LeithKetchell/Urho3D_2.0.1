#!/bin/bash
# broker_lib.sh — shared helpers for the Claudette write/exec broker.
# Source this (it sources ipc_dir.sh for IPC_DIR). See
# Claude/CLAUDETTE_WRITE_EXEC_BROKER_PLAN.md.
#
# The child cannot rely on an env var for the socket path: the spawn chain is
# Claudette -> sudo -> node(claude), and sudo's env_reset strips CLAUDE_BROKER_SOCK.
# Instead the child DERIVES it: same IPC_DIR (SDBM hash, via ipc_dir.sh) plus its
# own /proc ancestry walk up to the parent Claudette pid. The broker's SO_PEERCRED
# descendant check makes this self-correcting — a child can only ever reach its own
# parent's socket.

source "$(dirname "${BASH_SOURCE[0]}")/ipc_dir.sh"

# Project root (no trailing slash), resolved from this script's location.
BROKER_PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# find_broker_sock — sets BROKER_SOCK + BROKER_CLAUDETTE_PID, returns 0 if a live
# broker socket for THIS process's parent Claudette exists, else 1.
find_broker_sock() {
    BROKER_SOCK=""
    BROKER_CLAUDETTE_PID=""
    local p=$$ comm ppid
    local hops=0
    while [ "$hops" -lt 40 ]; do
        hops=$((hops + 1))
        # ppid = field 4 of /proc/<p>/stat, read past the last ')' so a comm
        # containing spaces/parens can't shift the columns.
        ppid=$(awk '{x=$0; sub(/^.*\) /,"",x); split(x,f," "); print f[2]}' "/proc/$p/stat" 2>/dev/null)
        [ -z "$ppid" ] && return 1
        [ "$ppid" -le 1 ] && return 1
        p="$ppid"
        comm=$(cat "/proc/$p/comm" 2>/dev/null)
        if [ "$comm" = "Claudette" ]; then
            BROKER_CLAUDETTE_PID="$p"
            BROKER_SOCK="$IPC_DIR/broker/claude-$p.sock"
            [ -S "$BROKER_SOCK" ] && return 0
            return 1
        fi
    done
    return 1
}

# broker_handles_path <absolute-path> — returns 0 if the broker would take this
# write (inside the tree, not part of the cage), else 1. Mirrors the C++
# BrokerPolicyCheckPath scope so file_lock.sh and broker_write.sh agree.
broker_handles_path() {
    local abs="$1"
    case "$abs" in
        "$BROKER_PROJECT_ROOT"/.claude/*) return 1 ;;
        "$BROKER_PROJECT_ROOT"/.git/*)    return 1 ;;
        "$BROKER_PROJECT_ROOT"/CLAUDE.md) return 1 ;;
        "$BROKER_PROJECT_ROOT"/*)         return 0 ;;
        *) return 1 ;;
    esac
}
