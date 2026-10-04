#!/bin/bash
# Muzzle — PreToolUse hook for Bash commands.
# Checks the command against the rules table in workboard.db.
# Whines in the terminal, logs to /tmp/urho_claude/muzzle.log.
# Returns non-zero to block (action=block), zero to allow (action=warn/log).

# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
WORKBOARD_DB="$WBDB"
MUZZLE_LOG="$IPC_DIR/muzzle.log"
SQLITE="$(cd "$(dirname "$0")/../.." && pwd)/build/bin/sqlite3"
[ -x "$SQLITE" ] || SQLITE="sqlite3"

# The command is passed via CLAUDE_TOOL_INPUT as JSON
COMMAND=$(echo "$CLAUDE_TOOL_INPUT" | grep -oP '"command"\s*:\s*"\K([^"\\]|\\.)*')

[ -z "$COMMAND" ] && exit 0
[ ! -f "$WORKBOARD_DB" ] && exit 0

# Get role from cached role file
SID="pid_$(ps -o ppid= -p $$ 2>/dev/null | tr -d ' ')"
ROLEFILE="$IPC_DIR/role_${SID}"
ROLE=$(head -1 "$ROLEFILE" 2>/dev/null)
[ -z "$ROLE" ] && ROLE="unknown"

# Query rules — match command against active hook rules with regex patterns
MATCH=$($SQLITE "$WORKBOARD_DB" "
    SELECT name, action, message FROM rules
    WHERE active = 1
      AND enforcement = 'hook'
      AND pattern IS NOT NULL
      AND pattern != ''
    ORDER BY id;
" 2>/dev/null)

[ -z "$MATCH" ] && exit 0

# Check each rule
echo "$MATCH" | while IFS='|' read -r name action message; do
    # Match pattern against command using grep
    pattern=$($SQLITE "$WORKBOARD_DB" "SELECT pattern FROM rules WHERE name='$name' AND active=1;" 2>/dev/null)
    [ -z "$pattern" ] && continue

    if echo "$COMMAND" | grep -qE "$pattern" 2>/dev/null; then
        TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

        # Log it
        echo "[$TIMESTAMP] $ROLE: $name ($action) — $COMMAND" >> "$MUZZLE_LOG"

        # Whine in the terminal
        echo "MUZZLE [$name]: $message" >&2

        # Log violation to DB
        RULE_ID=$($SQLITE "$WORKBOARD_DB" "SELECT id FROM rules WHERE name='$name';" 2>/dev/null)
        [ -n "$RULE_ID" ] && $SQLITE "$WORKBOARD_DB" "
            INSERT INTO violations (rule_id, role, context, outcome)
            VALUES ($RULE_ID, '$ROLE', '$(echo "$COMMAND" | sed "s/'/''/g")', '$action');
        " 2>/dev/null

        if [ "$action" = "block" ]; then
            exit 1
        fi
    fi
done
