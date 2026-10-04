#!/bin/bash
# Archive Lock Phase 2: unclaim.sh — release all claims by a role on a file
# Usage: unclaim.sh <filepath> [coder-role]
# Removes all lines matching the role from <filepath>.claims.

set -euo pipefail

FILEPATH="${1:?Usage: unclaim.sh <filepath> [role]}"
ROLE="${2:-coder}"
FILEPATH=$(realpath "$FILEPATH" 2>/dev/null || echo "$FILEPATH")
CLAIMS="${FILEPATH}.claims"

if [ ! -f "$CLAIMS" ]; then
    echo "[Unclaim] No claims file for $FILEPATH" >&2
    exit 0
fi

# Filter out claims by this role, keep the rest
TEMP=$(mktemp)
grep -v "^${ROLE}	" "$CLAIMS" > "$TEMP" 2>/dev/null || true

REMOVED=$(( $(wc -l < "$CLAIMS") - $(wc -l < "$TEMP") ))

if [ "$REMOVED" -eq 0 ]; then
    echo "[Unclaim] $ROLE has no claims on $FILEPATH"
    rm -f "$TEMP"
    exit 0
fi

if [ -s "$TEMP" ]; then
    mv "$TEMP" "$CLAIMS"
else
    rm -f "$TEMP" "$CLAIMS"
fi

echo "[Unclaim] $ROLE released $REMOVED claim(s) on $FILEPATH"
