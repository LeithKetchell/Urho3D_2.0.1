#!/usr/bin/env bash
# screenshot_guard.sh — PreToolUse hook that blocks AND LOGS screenshot attempts.
# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
BLOCK_FLAG="$IPC_DIR/screenshots_blocked"
# WBDB set by ipc_dir.sh

# Only act if screenshots are blocked
[ ! -f "$BLOCK_FLAG" ] && exit 0

# Read the tool input from stdin (Claude Code passes JSON)
INPUT=$(cat)

# Identify caller
CALLER_ROLE="${CLAUDE_ROLE:-unknown}"
CALLER_PID="${CLAUDE_PID:-$$}"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

# Extract tool name from environment
TOOL_NAME="${CLAUDE_TOOL_NAME:-}"

SQLITE="$(cd "$(dirname "$0")/../.." && pwd)/build/bin/sqlite3"
[ -x "$SQLITE" ] || SQLITE="sqlite3"

log_violation() {
    local context="$1"
    # Log to workboard violations table (rule 22 = screenshot attempt)
    $SQLITE "$WBDB" "
        INSERT OR IGNORE INTO rules (id, category, name, description, enforcement, action, message, active)
        VALUES (22, 'security', 'no_screenshots', 'No screen capture without permission.', 'hook', 'block',
                'BLOCKED: Screenshot attempt logged and denied.', 1);
        INSERT INTO violations (rule_id, role, timestamp, context, outcome)
        VALUES (22, '${CALLER_ROLE}', '${TIMESTAMP}', '$(echo "$context" | sed "s/'/''/g")', 'blocked');
    " 2>/dev/null

    # Also alert via relay socket
    if [ -S "$RELAY_SOCK" ]; then
        printf '[SECURITY] Screenshot blocked: %s (PID %s) — %s\n' \
            "$CALLER_ROLE" "$CALLER_PID" "$context" \
            | nc -U -w2 "$RELAY_SOCK" 2>/dev/null
    fi
}

# Block any tool that involves screen capture
case "$TOOL_NAME" in
    *[Ss]creenshot*|*[Ss]creen[Cc]apture*|*[Ss]creen[Gg]rab*)
        log_violation "tool=$TOOL_NAME"
        echo '{"decision":"block","reason":"Screenshots blocked by WorkboardManager. Attempt logged."}'
        exit 0
        ;;
esac

# For Bash tool: check if the command invokes a capture binary
if [ "$TOOL_NAME" = "Bash" ]; then
    CMD=$(echo "$INPUT" | grep -oP '"command"\s*:\s*"\K([^"\\]|\\.)*')
    # Broad pattern: known Linux screenshot tools + xdotool getactivewindow + fbgrab + xclip image
    if echo "$CMD" | grep -qiE "(^|;|\||&&|sudo\s+)\s*(xwd|scrot|maim|flameshot|grim|gnome-screenshot|spectacle|xfce4-screenshooter|import\s+-window\s+root|fbgrab|xdotool\s+.*screenshot|xclip\s+.*image)\b"; then
        log_violation "bash_cmd=$(echo "$CMD" | head -c 200)"
        echo '{"decision":"block","reason":"Capture command blocked. Attempt logged."}'
        exit 0
    fi
fi

exit 0
