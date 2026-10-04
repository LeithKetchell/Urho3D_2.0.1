#!/bin/bash
# PostToolUse hook — reports context window usage to WorkboardManager.
# Reads Claude Code hook JSON from stdin, extracts context metrics,
# sends __CONTEXT__ message via relay socket.

source "$(dirname "$0")/ipc_dir.sh"

get_claude_pid() {
    local walk="$PPID"
    local claude_pid=""
    local claudette_pid=""
    while [ "$walk" -gt 1 ] 2>/dev/null; do
        local comm
        comm=$(ps -o comm= -p "$walk" 2>/dev/null)
        if [ "$comm" = "Claudette" ]; then
            claudette_pid="$walk"
            break
        elif [ "$comm" = "claude" ] && [ -z "$claude_pid" ]; then
            claude_pid="$walk"
        fi
        walk=$(ps -o ppid= -p "$walk" 2>/dev/null | tr -d ' ')
        [ -z "$walk" ] && break
    done
    echo "${claudette_pid:-${claude_pid:-$PPID}}"
}

get_session_id() {
    echo "pid_$(get_claude_pid)"
}

get_role() {
    local sid
    sid=$(get_session_id)
    if [ -f "$INST_DIR/${sid}.role" ]; then
        head -1 "$INST_DIR/${sid}.role"
        return
    fi
    local rolefile="$IPC_DIR/role_${sid}"
    if [ -f "$rolefile" ]; then
        head -1 "$rolefile"
        return
    fi
    echo "${CLAUDE_ROLE:-unassigned}"
}

INPUT=$(cat)

# Extract context metrics from hook JSON
PCT=$(echo "$INPUT" | jq -r '.session.context_window.used_percentage // empty' 2>/dev/null)

if [ -z "$PCT" ]; then
    exit 0
fi

TOKENS=$(echo "$INPUT" | jq -r '.session.context_window.total_input_tokens // 0' 2>/dev/null)
MAX=$(echo "$INPUT" | jq -r '.session.context_window.context_window_size // 200000' 2>/dev/null)

PCT=$(echo "$PCT" | cut -d. -f1)

ROLE=$(get_role)

if [ -S "$RELAY_SOCK" ]; then
    printf 'manager:%s:__CONTEXT__:%s:%s:%s\n' "$ROLE" "$PCT" "$TOKENS" "$MAX" \
        | nc -U -w1 "$RELAY_SOCK" 2>/dev/null
fi

exit 0
