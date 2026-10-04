#!/bin/bash
# file_protect.sh — PreToolUse hook for Edit and Write tools.
# Blocks modification of critical infrastructure files that Claude must never change.
# This is the last line of defense — if Claude removes its own hooks, game over.
# This file is READ-ONLY. Only the local user modifies it.

TOOL="$CLAUDE_TOOL_NAME"

# Only intercept Edit and Write tools
[ "$TOOL" != "Edit" ] && [ "$TOOL" != "Write" ] && exit 0

# Extract file_path from JSON input
FILE_PATH=$(echo "$CLAUDE_TOOL_INPUT" | grep -oP '"file_path"\s*:\s*"\K([^"\\]|\\.)*')
[ -z "$FILE_PATH" ] && exit 0

source "$(dirname "$0")/ipc_dir.sh"

alert_manager() {
    if [ -S "$RELAY_SOCK" ]; then
        printf '[FILE-PROTECT] %s\n' "$1" | nc -U -w2 "$RELAY_SOCK" 2>/dev/null
    fi
}

# Protected files — absolute block, no exceptions
PROTECTED=(
    ".claude/settings.json"
    ".claude/settings.local.json"
    ".claude/hooks/hooks_protect.sh"
    ".claude/hooks/muzzle.sh"
    ".claude/hooks/screenshot_guard.sh"
    ".claude/hooks/screenshot_trap.sh"
    ".claude/hooks/bash_lockdown.sh"
    ".claude/hooks/file_protect.sh"
    ".claude/hooks/ipc_dir.sh"
    "CLAUDE.md"
)

for pattern in "${PROTECTED[@]}"; do
    if [[ "$FILE_PATH" == *"$pattern" ]]; then
        echo "FILE-PROTECT BLOCKED: Modification of $pattern is forbidden. Only the local user changes infrastructure." >&2
        alert_manager "BLOCKED modification of $FILE_PATH"
        exit 1
    fi
done

# Also block creating new settings files that could override hooks
if [[ "$FILE_PATH" == *".claude/settings"* ]]; then
    echo "FILE-PROTECT BLOCKED: Creating/modifying Claude settings files is forbidden." >&2
    alert_manager "BLOCKED settings file: $FILE_PATH"
    exit 1
fi

# Block creating new hook scripts (could be used to shadow existing ones)
if [[ "$FILE_PATH" == *".claude/hooks/"* ]]; then
    # Allow only known non-security hooks to be created/modified
    BASENAME=$(basename "$FILE_PATH")
    case "$BASENAME" in
        claude_ipc.sh|safe_build.sh|build_when_clear.sh|checkout.sh|claim.sh|release.sh|rollback.sh|unclaim.sh|yuki-talk.sh|link_serialize.sh|ensure_manager.sh|tty-inject.sh)
            # These are operational hooks, allow modification
            exit 0
            ;;
        *)
            echo "FILE-PROTECT BLOCKED: Creating/modifying hook scripts requires the local user's approval." >&2
            alert_manager "BLOCKED hook modification: $FILE_PATH"
            exit 1
            ;;
    esac
fi

exit 0
