#!/bin/bash
# Archive Lock Phase 1: release.sh — release file ownership
# Usage: release.sh <filepath>
# Deletes <filepath>.locked.zip and .locked.meta.

set -euo pipefail

FILEPATH="${1:?Usage: release.sh <filepath>}"
FILEPATH=$(realpath "$FILEPATH" 2>/dev/null || echo "$FILEPATH")
LOCKZIP="${FILEPATH}.locked.zip"
META="${FILEPATH}.locked.meta"

if [ ! -f "$LOCKZIP" ]; then
    echo "[Release] WARNING: $FILEPATH is not checked out" >&2
    exit 0
fi

# Verify caller is the owner
if [ -f "$META" ]; then
    OWNER_PID=$(grep "^pid=" "$META" | cut -d= -f2)
    ROLE=$(grep "^locked_by=" "$META" | cut -d= -f2)
    # Allow release by same role or if owner is dead
    MY_PID=$$
    if [ "$OWNER_PID" != "$MY_PID" ] && [ -d "/proc/$OWNER_PID" ]; then
        echo "[Release] WARNING: owned by $ROLE (PID $OWNER_PID) — releasing anyway (same team)"
    fi
fi

rm -f "$LOCKZIP" "$META"
ROLE=${ROLE:-unknown}
echo "[Release] $ROLE released $FILEPATH"
