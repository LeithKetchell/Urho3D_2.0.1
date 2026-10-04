#!/bin/bash
# Archive Lock Phase 1: rollback.sh — restore file to pre-checkout state
# Usage: rollback.sh <filepath>
# Extracts original from <filepath>.locked.zip, deletes lock files.

set -euo pipefail

FILEPATH="${1:?Usage: rollback.sh <filepath>}"
FILEPATH=$(realpath "$FILEPATH" 2>/dev/null || echo "$FILEPATH")
LOCKZIP="${FILEPATH}.locked.zip"
META="${FILEPATH}.locked.meta"

if [ ! -f "$LOCKZIP" ]; then
    echo "[Rollback] ERROR: no archive found for $FILEPATH — nothing to rollback" >&2
    exit 1
fi

DIR=$(dirname "$FILEPATH")
unzip -o "$LOCKZIP" -d "$DIR" > /dev/null 2>&1

rm -f "$LOCKZIP" "$META"
echo "[Rollback] $FILEPATH restored to pre-checkout state"
