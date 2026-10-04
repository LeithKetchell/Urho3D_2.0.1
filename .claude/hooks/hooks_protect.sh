#!/bin/bash
# hooks_protect.sh — PreToolUse hook that blocks permission changes and writes
# to .claude/hooks/ security scripts. Prevents Claude from circumventing
# read-only protections on security hooks.

[ "$CLAUDE_TOOL_NAME" = "Bash" ] || exit 0

# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"

COMMAND=$(echo "$CLAUDE_TOOL_INPUT" | grep -oP '"command"\s*:\s*"\K([^"\\]|\\.)*')
[ -z "$COMMAND" ] && exit 0

HOOKS_DIR=".claude/hooks"

# Block chmod on hooks directory or any file within it
if echo "$COMMAND" | grep -qE "(chmod|chown|chattr|setfacl)\b.*($HOOKS_DIR|screenshot_trap|screenshot_guard|hooks_protect|muzzle)"; then
    echo "BLOCKED: Permission changes to security hooks are not allowed." >&2
    if [ -S "$RELAY_SOCK" ]; then
        printf '[SECURITY] Permission change on hooks BLOCKED: %s\n' "${COMMAND:0:200}" \
            | nc -U -w2 "$RELAY_SOCK" 2>/dev/null
    fi
    exit 1
fi

# Block direct writes/overwrites to protected hook files
if echo "$COMMAND" | grep -qE "(>\s*|tee\s+|cp\s+|mv\s+|dd\s+.*of=|install\s+).*($HOOKS_DIR/(screenshot_trap|screenshot_guard|hooks_protect|muzzle))"; then
    echo "BLOCKED: Overwriting security hooks is not allowed." >&2
    if [ -S "$RELAY_SOCK" ]; then
        printf '[SECURITY] Hook overwrite BLOCKED: %s\n' "${COMMAND:0:200}" \
            | nc -U -w2 "$RELAY_SOCK" 2>/dev/null
    fi
    exit 1
fi

exit 0
