#!/bin/bash
# Archive Lock Phase 2: claim.sh — claim a line range in a source file
# Usage: claim.sh <filepath> <start-line> <end-line> [coder-role]
# Claims file: <filepath>.claims (one claim per line, tab-separated)
# Format: role<TAB>start<TAB>end<TAB>pid<TAB>timestamp

set -euo pipefail

FILEPATH="${1:?Usage: claim.sh <filepath> <start> <end> [role]}"
START="${2:?Usage: claim.sh <filepath> <start> <end> [role]}"
END="${3:?Usage: claim.sh <filepath> <start> <end> [role]}"
ROLE="${4:-coder}"
FILEPATH=$(realpath "$FILEPATH" 2>/dev/null || echo "$FILEPATH")
CLAIMS="${FILEPATH}.claims"
PID=$$
TIMESTAMP=$(date -Iseconds)

if [ ! -f "$FILEPATH" ]; then
    echo "[Claim] ERROR: file does not exist: $FILEPATH" >&2
    exit 1
fi

if [ "$START" -ge "$END" ] 2>/dev/null; then
    echo "[Claim] ERROR: start ($START) must be less than end ($END)" >&2
    exit 1
fi

# Check for overlaps with existing claims
if [ -f "$CLAIMS" ]; then
    while IFS=$'\t' read -r crole cstart cend cpid ctime; do
        [ -z "$crole" ] && continue
        # Check if owner PID is dead — skip stale claims
        if [ -n "$cpid" ] && ! [ -d "/proc/$cpid" ]; then
            continue
        fi
        # Overlap: ranges intersect if start < existing_end AND end > existing_start
        if [ "$START" -lt "$cend" ] && [ "$END" -gt "$cstart" ]; then
            echo "[Claim] OVERLAP with $crole (lines $cstart-$cend, PID $cpid)" >&2
            exit 1
        fi
    done < "$CLAIMS"
fi

# Append claim
printf '%s\t%s\t%s\t%s\t%s\n' "$ROLE" "$START" "$END" "$PID" "$TIMESTAMP" >> "$CLAIMS"
echo "[Claim] $ROLE claimed $FILEPATH lines $START-$END"
