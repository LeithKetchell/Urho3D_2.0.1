#!/bin/bash
# Archive Lock Phase 4: Sequenced single build
# Checks for locked/claimed files before building. If any exist, queues intent and exits.
# If clear, runs safe_build.sh. Notifies coder on success or failure.
# Usage: build_when_clear.sh <target> [extra make args...]

set -euo pipefail

HOOKS_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_ROOT="$(cd "$HOOKS_DIR/../.." && pwd)/Source"   # script-relative, run-user-independent
# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
QUEUE_DIR="$IPC_DIR/build_queue"
mkdir -p "$QUEUE_DIR"

TARGET="${1:?Usage: build_when_clear.sh <target> [extra make args...]}"
shift

# Check for any locked or claimed files in the source tree
LOCKED_FILES=$(find "$SOURCE_ROOT" \( -name "*.locked.zip" -o -name "*.claims" \) 2>/dev/null || true)

if [ -n "$LOCKED_FILES" ]; then
    # Files still checked out — queue build intent, notify coder
    echo "$TARGET" > "$QUEUE_DIR/${TARGET}.queued"
    echo "[BuildQueue] $TARGET queued — files still checked out:"
    echo "$LOCKED_FILES"

    # Notify coder via socket if available
    if [ -x "$HOOKS_DIR/claude_ipc.sh" ]; then
        "$HOOKS_DIR/claude_ipc.sh" send coder "Build $TARGET queued — waiting for file releases: $LOCKED_FILES" 2>/dev/null || true
    fi
    exit 0
fi

# Clear — remove any stale queue entry and build
rm -f "$QUEUE_DIR/${TARGET}.queued"

echo "[BuildWhenClear] All files released — building $TARGET"
"$HOOKS_DIR/safe_build.sh" "$TARGET" "$@"
EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ]; then
    echo "[BuildWhenClear] $TARGET succeeded"
    # Notify all coders via socket
    if [ -x "$HOOKS_DIR/claude_ipc.sh" ]; then
        "$HOOKS_DIR/claude_ipc.sh" send coder "Build $TARGET complete — OK" 2>/dev/null || true
    fi
else
    echo "[BuildWhenClear] $TARGET FAILED (exit $EXIT_CODE)"
    # Notify coder of failure
    if [ -x "$HOOKS_DIR/claude_ipc.sh" ]; then
        "$HOOKS_DIR/claude_ipc.sh" send coder "Build $TARGET FAILED (exit $EXIT_CODE)" 2>/dev/null || true
    fi
fi

exit $EXIT_CODE
