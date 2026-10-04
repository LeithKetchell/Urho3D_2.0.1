#!/usr/bin/env bash
# screenshot_trap.sh — Background monitor that watches for screenshot processes.
# Logs PID, PPID, cmdline, and owning TTY of any capture binary that spawns.
# Usage: .claude/hooks/screenshot_trap.sh &
# Kill with: kill $(cat /tmp/urho_claude/screenshot_trap.pid)

# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
TRAP_PID_FILE="$IPC_DIR/screenshot_trap.pid"
TRAP_LOG="$IPC_DIR/screenshot_trap.log"
# WBDB set by ipc_dir.sh

# Known screenshot binaries
TARGETS="scrot|maim|flameshot|grim|gnome-screenshot|spectacle|xfce4-screenshooter|xwd|fbgrab"
# ImageMagick import needs separate check (bare 'import' collides with python)
IMPORT_PATTERN="(^|/)import\s+(-window|-screen|-display)"

echo $$ > "$TRAP_PID_FILE"
echo "[$(date '+%Y-%m-%d %H:%M:%S')] Screenshot trap started (PID $$)" >> "$TRAP_LOG"

cleanup() {
    rm -f "$TRAP_PID_FILE"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Screenshot trap stopped" >> "$TRAP_LOG"
    exit 0
}
trap cleanup TERM INT

while true; do
    # Scan for any matching process — write to temp file to avoid pipeline races
    _scan="$IPC_DIR/.screenshot_scan.$$"
    ps -eo pid,ppid,tty,args 2>/dev/null | grep -iE "\b($TARGETS)\b" | grep -v grep | grep -v screenshot_trap > "$_scan" 2>/dev/null
    # ImageMagick import: only match the actual binary, not python import statements
    ps -eo pid,ppid,tty,args 2>/dev/null | grep -E "$IMPORT_PATTERN" | grep -v grep | grep -v screenshot_trap | grep -v 'python' >> "$_scan" 2>/dev/null

    while IFS= read -r line; do
        [ -z "$line" ] && continue

        PID=$(echo "$line" | awk '{print $1}')
        PPID_VAL=$(echo "$line" | awk '{print $2}')
        TTY=$(echo "$line" | awk '{print $3}')
        CMD=$(echo "$line" | awk '{$1=$2=$3=""; print $0}' | sed 's/^ *//')

        TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

        # Walk FULL parent chain — dump every ancestor PID + command
        CHAIN="$PID"
        WALK="$PPID_VAL"
        while [ "$WALK" -gt 1 ] 2>/dev/null; do
            WALK_CMD=$(ps -o args= -p "$WALK" 2>/dev/null | head -c 80)
            CHAIN="$CHAIN <- $WALK($WALK_CMD)"
            WALK=$(ps -o ppid= -p "$WALK" 2>/dev/null | tr -d ' ')
            [ -z "$WALK" ] && break
        done

        # Attribute to a coder role via cached role files
        CODER_ROLE="unknown"
        for rolefile in "$IPC_DIR"/role_*; do
            [ -f "$rolefile" ] || continue
            ROLE_NAME=$(head -1 "$rolefile" 2>/dev/null)
            [ -z "$ROLE_NAME" ] && continue
            # Extract the PID from the role filename (role_pid_NNNN)
            ROLE_PID=$(echo "$rolefile" | grep -oP 'pid_\K[0-9]+')
            [ -z "$ROLE_PID" ] && continue
            CHECK="$PPID_VAL"
            while [ "$CHECK" -gt 1 ] 2>/dev/null; do
                if [ "$CHECK" = "$ROLE_PID" ]; then
                    CODER_ROLE="$ROLE_NAME"
                    break 2
                fi
                CHECK=$(ps -o ppid= -p "$CHECK" 2>/dev/null | tr -d ' ')
                [ -z "$CHECK" ] && break
            done
        done

        ENTRY="[$TIMESTAMP] CAUGHT: pid=$PID ppid=$PPID_VAL tty=$TTY role=$CODER_ROLE cmd=$CMD"
        echo "$ENTRY" >> "$TRAP_LOG"
        echo "  CHAIN: $CHAIN" >> "$TRAP_LOG"

        # Log to workboard violations
        CONTEXT="pid=$PID ppid=$PPID_VAL tty=$TTY chain=$CHAIN"
        SQLITE_BIN="$(cd "$(dirname "$0")/../.." && pwd)/build/bin/sqlite3"
        [ -x "$SQLITE_BIN" ] || SQLITE_BIN="sqlite3"
        $SQLITE_BIN "$WBDB" "
            INSERT OR IGNORE INTO rules (id, category, name, description, enforcement, action, message, active)
            VALUES (22, 'security', 'no_screenshots', 'No screen capture without permission.', 'hook', 'block',
                    'BLOCKED: Screenshot attempt logged and denied.', 1);
            INSERT INTO violations (rule_id, role, timestamp, context, outcome)
            VALUES (22, '${CODER_ROLE}', '${TIMESTAMP}', '$(echo "$CONTEXT" | sed "s/'/''/g")', 'caught');
        " 2>/dev/null

        # Alert manager
        if [ -S "$RELAY_SOCK" ]; then
            printf '[SECURITY] Screenshot CAUGHT: %s pid=%s cmd=%s\n' \
                "$CODER_ROLE" "$PID" "${CMD:0:100}" \
                | nc -U -w2 "$RELAY_SOCK" 2>/dev/null
        fi

    done < "$_scan"
    rm -f "$_scan"

    sleep 2
done
