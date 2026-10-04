#!/bin/bash
# File I/O locking for multi-coder safety
# Uses mkdir (atomic on Linux) via PreToolUse/PostToolUse hooks
#
# PreToolUse: acquires lock (mkdir atomic creation)
# PostToolUse: releases lock (rmdir)
#
# Receives JSON on stdin with tool_name, tool_input.file_path, tool_use_id

# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
LOCK_DIR="$IPC_DIR/locks"
mkdir -p "$LOCK_DIR"

# Read JSON from stdin
INPUT=$(cat)

# Extract fields with grep — no python needed
TOOL_NAME=$(echo "$INPUT" | grep -oP '"tool_name"\s*:\s*"\K([^"\\]|\\.)*')
FILE_PATH=$(echo "$INPUT" | grep -oP '"file_path"\s*:\s*"\K([^"\\]|\\.)*')
TOOL_USE_ID=$(echo "$INPUT" | grep -oP '"tool_use_id"\s*:\s*"\K([^"\\]|\\.)*')
HOOK_EVENT=$(echo "$INPUT" | grep -oP '"hook_event_name"\s*:\s*"\K([^"\\]|\\.)*')

# Get the Claude instance PID (stable across hook invocations, unlike $$)
CLAUDE_PID=$(awk '{print $4}' /proc/$$/stat 2>/dev/null)
[ -z "$CLAUDE_PID" ] && CLAUDE_PID=$$

# Skip if no file path
[ -z "$FILE_PATH" ] && exit 0

# Only lock files under project root — skip system/temp files
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
case "$FILE_PATH" in
    "$PROJECT_ROOT/.claude"*) exit 0 ;;  # never lock hook/config files
    "$PROJECT_ROOT"*) ;;                  # project file, proceed
    *) exit 0 ;;                          # not our file, skip
esac

# Sanitize file path for lock dir name: replace / with __
RELATIVE_PATH="${FILE_PATH#$PROJECT_ROOT/}"
SANITIZED=$(echo "$RELATIVE_PATH" | tr '/' '__')
LOCK_ENTRY="$LOCK_DIR/${SANITIZED}.lock"

case "$HOOK_EVENT" in

PreToolUse)
    # Determine lock type
    case "$TOOL_NAME" in
        Read)      LOCK_TYPE="read" ;;
        Write|Edit) LOCK_TYPE="write" ;;
        *) exit 0 ;;
    esac

    # Broker deference: if broker_write.sh will route this Write/Edit through the
    # Claudette broker, it BLOCKS the tool (exit 2). A blocked tool never runs its
    # PostToolUse, so any write lock we took here would leak and never release
    # (holder PID stays alive, so it won't be broken as stale). Skip acquisition
    # for brokered paths — the broker serializes writes itself.
    if [ "$LOCK_TYPE" = "write" ] && [ -r "$(dirname "$0")/broker_lib.sh" ]; then
        # shellcheck source=/dev/null
        source "$(dirname "$0")/broker_lib.sh"
        if find_broker_sock && broker_handles_path "$FILE_PATH"; then
            exit 0
        fi
    fi

    # For read locks: allow multiple readers, block if writer present
    # For write locks: block if any lock present
    if [ "$LOCK_TYPE" = "read" ]; then
        # Check for write lock
        if [ -d "$LOCK_ENTRY" ] && [ -f "$LOCK_ENTRY/type" ]; then
            EXISTING_TYPE=$(cat "$LOCK_ENTRY/type" 2>/dev/null)
            if [ "$EXISTING_TYPE" = "write" ]; then
                # Check if holder is alive
                EXISTING_PID=$(cat "$LOCK_ENTRY/pid" 2>/dev/null)
                if [ -n "$EXISTING_PID" ] && [ -d "/proc/$EXISTING_PID" ]; then
                    echo "File write-locked by another coder (PID $EXISTING_PID): $RELATIVE_PATH" >&2
                    exit 2
                fi
                # Holder is dead — break stale lock
                rm -rf "$LOCK_ENTRY"
            fi
        fi
        # Read lock: create dir if not exists, add our reader marker
        mkdir -p "$LOCK_ENTRY"
        echo "read" > "$LOCK_ENTRY/type"
        echo "$CLAUDE_PID" > "$LOCK_ENTRY/reader.${TOOL_USE_ID}"

    else
        # Write lock: need exclusive access
        # Try atomic mkdir — fails if dir already exists
        if ! mkdir "$LOCK_ENTRY" 2>/dev/null; then
            # Lock exists — check if holder(s) alive
            if [ -f "$LOCK_ENTRY/pid" ]; then
                EXISTING_PID=$(cat "$LOCK_ENTRY/pid" 2>/dev/null)
                if [ -n "$EXISTING_PID" ] && [ -d "/proc/$EXISTING_PID" ]; then
                    echo "File locked by another coder (PID $EXISTING_PID): $RELATIVE_PATH" >&2
                    exit 2
                fi
            fi
            # Check for active readers — remove stale ones whose PID is dead
            ACTIVE_READERS=0
            for rf in "$LOCK_ENTRY"/reader.*; do
                [ -f "$rf" ] || continue
                READER_PID=$(cat "$rf" 2>/dev/null)
                if [ -n "$READER_PID" ] && [ -d "/proc/$READER_PID" ]; then
                    ACTIVE_READERS=1
                else
                    # Reader is dead — remove stale marker
                    rm -f "$rf"
                fi
            done
            if [ "$ACTIVE_READERS" -eq 1 ]; then
                echo "File read-locked by another coder: $RELATIVE_PATH" >&2
                exit 2
            fi
            # All holders dead — break stale lock and reclaim
            rm -rf "$LOCK_ENTRY"
            mkdir "$LOCK_ENTRY" 2>/dev/null || { echo "Lock contention: $RELATIVE_PATH" >&2; exit 2; }
        fi
        echo "write" > "$LOCK_ENTRY/type"
        echo "$CLAUDE_PID" > "$LOCK_ENTRY/pid"
    fi
    exit 0
    ;;

PostToolUse)
    if [ -d "$LOCK_ENTRY" ]; then
        EXISTING_TYPE=$(cat "$LOCK_ENTRY/type" 2>/dev/null)
        if [ "$EXISTING_TYPE" = "read" ]; then
            # Remove our reader marker
            rm -f "$LOCK_ENTRY/reader.${TOOL_USE_ID}"
            # If no readers left, remove the lock dir
            REMAINING=$(ls "$LOCK_ENTRY"/reader.* 2>/dev/null | wc -l)
            if [ "$REMAINING" -eq 0 ]; then
                rm -rf "$LOCK_ENTRY"
            fi
        else
            # Write lock — remove entirely
            rm -rf "$LOCK_ENTRY"
        fi
    fi
    exit 0
    ;;

*)
    exit 0
    ;;

esac
