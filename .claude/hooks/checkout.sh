#!/bin/bash
# Archive Lock Phase 1: checkout.sh — claim exclusive ownership of a file
# Usage: checkout.sh <filepath> [coder-role]
# Creates <filepath>.locked.zip as the lock + rollback point.

set -euo pipefail

FILEPATH="${1:?Usage: checkout.sh <filepath> [role]}"
# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
ROLE="${2:-$(head -1 "$IPC_DIR"/role_pid_* 2>/dev/null | head -1 || echo unknown)}"
LOCKZIP="${FILEPATH}.locked.zip"
META="${FILEPATH}.locked.meta"
PID=$$
TIMESTAMP=$(date -Iseconds)

# Resolve to absolute path
FILEPATH=$(realpath "$FILEPATH" 2>/dev/null || echo "$FILEPATH")
LOCKZIP="${FILEPATH}.locked.zip"
META="${FILEPATH}.locked.meta"

if [ ! -f "$FILEPATH" ]; then
    echo "[Checkout] ERROR: file does not exist: $FILEPATH" >&2
    exit 1
fi

if [ -f "$LOCKZIP" ]; then
    if [ -f "$META" ]; then
        OWNER=$(grep "^locked_by=" "$META" | cut -d= -f2)
        SINCE=$(grep "^locked_at=" "$META" | cut -d= -f2)
        OWNER_PID=$(grep "^pid=" "$META" | cut -d= -f2)
        # Check if owner is still alive
        if [ -d "/proc/$OWNER_PID" ]; then
            echo "[Checkout] LOCKED by $OWNER since $SINCE (PID $OWNER_PID alive)" >&2
            exit 1
        else
            echo "[Checkout] Stale lock by $OWNER (PID $OWNER_PID dead) — auto-recovering"
            rm -f "$LOCKZIP" "$META"
        fi
    else
        echo "[Checkout] LOCKED (no metadata — manual cleanup needed)" >&2
        exit 1
    fi
fi

# Archive current state as the rollback point
DIR=$(dirname "$FILEPATH")
BASE=$(basename "$FILEPATH")
(cd "$DIR" && zip -j "$LOCKZIP" "$BASE") > /dev/null 2>&1

# Write metadata sidecar
cat > "$META" <<EOF
locked_by=$ROLE
locked_at=$TIMESTAMP
pid=$PID
filepath=$FILEPATH
EOF

echo "[Checkout] $ROLE claimed $FILEPATH"
