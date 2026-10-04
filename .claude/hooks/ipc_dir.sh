#!/bin/bash
# ipc_dir.sh — Derive a project-specific IPC directory.
# Source this from any hook to get IPC_DIR set.
# Uses CLAUDE_PROJECT_DIR if available, falls back to walking up from CWD.
#
# Hash algorithm: Urho3D SDBM (matches StringHash in C++).
# Both sides MUST use the same algorithm or IPC paths diverge.

_resolve_project_root() {
    if [ -n "$CLAUDE_PROJECT_DIR" ]; then
        echo "$CLAUDE_PROJECT_DIR"
        return
    fi
    local walk="$(pwd)"
    while [ "$walk" != "/" ]; do
        if [ -d "$walk/.claude/hooks" ]; then
            echo "$walk"
            return
        fi
        walk="$(dirname "$walk")"
    done
    echo "/tmp"
}

# SDBM hash — identical to Urho3D's SDBMHash / StringHash::Calculate
_sdbm_hash() {
    local str="$1"
    local hash=0
    local i c
    for (( i=0; i<${#str}; i++ )); do
        c=$(printf '%d' "'${str:$i:1}")
        hash=$(( (c + (hash << 6) + (hash << 16) - hash) & 0xFFFFFFFF ))
    done
    printf '%08x' "$hash"
}

_PROJECT_ROOT="$(_resolve_project_root)"
# Ensure trailing slash to match C++ GetProjectRoot() output
[[ "$_PROJECT_ROOT" != */ ]] && _PROJECT_ROOT="${_PROJECT_ROOT}/"
_HASH="$(_sdbm_hash "$_PROJECT_ROOT")"
IPC_DIR="/tmp/claude_${_HASH}"
INST_DIR="$IPC_DIR/instances"
RELAY_SOCK="$IPC_DIR/tty/manager_relay.sock"
WBDB="${_PROJECT_ROOT}.claude/workboard.db"
mkdir -p "$INST_DIR" "$IPC_DIR/tty"
# Ensure IPC dirs are group-writable so claude user can participate
chmod 2775 "$IPC_DIR" "$INST_DIR" "$IPC_DIR/tty" 2>/dev/null
chgrp users "$IPC_DIR" "$INST_DIR" "$IPC_DIR/tty" 2>/dev/null
