#!/bin/bash
# Claude socket hook for WorkboardManager
# Usage: claude_ipc.sh <action> [args...]
#   action: announce | check | report | cleanup   (role is assigned by Manager, never self-picked)
#   action: send <target-role> <message>
#   action: broadcast-collect <tag> <message>   (broadcast + collect replies via Manager)
#   action: collect-reply <tag> <response>      (reply to a broadcast-collect)
#   action: wb-add-done <task> <owner> <date> <review> <notes>
#   action: wb-add-ready <pri> <plan> <file> <owner> <review> <summary>
#   action: wb-add-inprogress <task> <owner> <started> <review> <notes>
#   action: wb-move-done <task>   (move from In Progress to Done)
#   action: wb-assign <task-name> <coder-role>  (atomic claim from Ready/Planned + notify via Manager)
#   action: wb-remove <text>  (remove first matching row from any table)
#
# All messaging routes through Manager via relay socket. No TTY injection.

ACTION="${1:-check}"
# Source project-specific IPC directory (sets IPC_DIR, INST_DIR, RELAY_SOCK)
source "$(dirname "$0")/ipc_dir.sh"
WORKBOARD="$(cd "$(dirname "$0")/../.." && pwd)/Claude/WORKBOARD.md"
LOCKFILE="$IPC_DIR/workboard.lock"

# Resolve a sqlite3 binary for read-only board queries.
# Prefer one on PATH; fall back to the engine's built sqlite3.
sqlite_bin() {
    if command -v sqlite3 >/dev/null 2>&1; then
        echo "sqlite3"
    elif [ -x "${_PROJECT_ROOT}build/bin/sqlite3" ]; then
        echo "${_PROJECT_ROOT}build/bin/sqlite3"
    else
        return 1
    fi
}

# Send a message to Manager via relay socket.
# Usage: relay_send <message>
relay_send() {
    if [ -S "$RELAY_SOCK" ]; then
        printf '%s\n' "$1" | nc -U -w2 "$RELAY_SOCK" 2>/dev/null
        return $?
    fi
    return 1
}

# Send a query to Manager and print the response.
# Uses -q1 to wait for reply after sending (OpenBSD nc closes on stdin EOF otherwise).
# Usage: relay_query <message>
relay_query() {
    if [ -S "$RELAY_SOCK" ]; then
        printf '%s\n' "$1" | nc -U -q1 -w2 "$RELAY_SOCK" 2>/dev/null
        return $?
    fi
    echo "Manager relay not available" >&2
    return 1
}

# Get a stable identifier for this session.
# Walks the process tree for a /dev/pts/* device (stable across compaction).
# Falls back to PID if no pts device found.
get_session_id() {
    # Use the stable parent process (Claudette or claude) as session ID.
    # Hook $PPID varies per invocation — get_claude_pid() always finds the same parent.
    echo "pid_$(get_claude_pid)"
}

# Find the owning process PID by walking up the process tree.
# Prefers Claudette (stable parent) over claude (child that may restart).
# Falls back to claude if Claudette not found (running in gnome-terminal).
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

# Resolve role from session role file
get_role() {
    local sid
    sid=$(get_session_id)
    # announce caches the Manager-assigned role at $IPC_DIR/role_<sid> — read it FIRST
    # (the legacy $INST_DIR/<sid>.role path below is never written, so without this every
    # session resolved to "unassigned" and broadcasts failed to omit their own sender).
    if [ -f "$INST_DIR/${sid}.role" ]; then        # Manager-authoritative (Fix B writes on promotion) — FIRST
        head -1 "$INST_DIR/${sid}.role"
        return
    fi
    if [ -f "$IPC_DIR/role_${sid}" ]; then          # announce cache — fallback (fresh / pre-restart, no promotion yet)
        head -1 "$IPC_DIR/role_${sid}"
        return
    fi
    echo "${CLAUDE_ROLE:-unassigned}"
}

# Check if WorkboardManager is running (relay socket exists and responds)
manager_running() {
    [ -S "$RELAY_SOCK" ]
}

# Check if WorkboardManager is reachable. Never launch it — only the local user does that.
ensure_manager() {
    manager_running
}


case "$ACTION" in

check)
    # No-op — just exit cleanly. Inbox delivery disabled until feedback loop is resolved.
    exit 0
    ;;

assume)
    echo "Error: 'assume' is not valid — Manager assigns roles, you don't pick them." >&2
    exit 1
    ;;

announce|reannounce)
    # Register with Manager? No — the hook does NOT contact Manager at all.
    # Claudette owns the persistent relay connection and is the sole registrar;
    # this action only surfaces the role Manager assigns over that connection.
    SID=$(get_session_id)
    CLAUDE_PID=$(get_claude_pid)
    ROLEFILE="$IPC_DIR/role_${SID}"
    REGSTAMP="$IPC_DIR/registered_${SID}"

    # Guard: skip if already registered this session (prevents double-fire
    # from overlapping hooks / manual calls).
    if [ "$ACTION" != "reannounce" ] && [ -f "$REGSTAMP" ]; then
        # Already registered — just emit the cached role
        if [ -f "$ROLEFILE" ]; then
            echo "Role: $(head -1 "$ROLEFILE")"
        fi
        exit 0
    fi

    # The hook sends NOTHING to Manager. Claudette — which ALWAYS wraps a
    # claude-hosted instance (hooks never fire on a naked claude) — is the sole
    # registrar over its persistent relay connection:
    #   * initial:  StartIPCListener() sends __HELLO__ at startup under this same
    #               SID (pid_<ClaudettePID>, since spawn-coder launches Claudette
    #               via setsid with no tty, so its ttyname(0) is NULL);
    #   * recovery: CheckSocketHealth() probes the fd every 5s and, if Manager
    #               dropped, re-runs StartIPCListener() — re-sending __HELLO__.
    # Manager binds the role to Claudette's fd and announces __ROLE__ back over
    # it; Claudette caches it to $IPC_DIR/role_pid_<pid> (== $ROLEFILE here).
    # A HELLO from THIS hook would be a pure duplicate reconnect: it carries
    # nothing new, trips Manager's anti-hijack reconnect gate, and (foreground)
    # stalls startup ~1s while Manager takes its turn. So we stay silent and let
    # Manager announce to us. The coder holds no rights until __ROLE__ arrives.
    # (UserPromptSubmit runs `check`, a no-op — the old "re-register on
    # UserPromptSubmit" path is gone; Claudette self-heals the relay instead.)
    touch "$REGSTAMP"
    if [ -f "$ROLEFILE" ]; then
        echo "Role: $(head -1 "$ROLEFILE")"
    else
        echo "Registered via Claudette — awaiting role assignment from Manager"
    fi

    exit 0
    ;;

report|notify-idle)
    # Stop hook — wired but intentionally inert (no Manager-side idle protocol yet).
    # Handled explicitly so the action isn't silently swallowed by the esac fall-through.
    exit 0
    ;;

cleanup)
    # SessionEnd — tell Manager we're leaving
    SID=$(get_session_id)
    CLAUDE_PID=$(get_claude_pid)
    relay_send "manager:unassigned:__GOODBYE__:${SID}:${CLAUDE_PID}" 2>/dev/null
    # Remove registration stamp so next session can re-register
    rm -f "$IPC_DIR/registered_${SID}" 2>/dev/null
    exit 0
    ;;

broadcast-collect)
    # Broadcast a message to all coders via Manager and collect replies.
    TAG="$2"
    MSG="$3"
    if [ -z "$TAG" ] || [ -z "$MSG" ]; then
        echo "Usage: claude_ipc.sh broadcast-collect <tag> <message>" >&2
        exit 1
    fi
    ROLE=$(get_role)
    relay_send "manager:${ROLE}:__BROADCAST_COLLECT__:${TAG}:${MSG}" \
        && echo "Broadcast-collect '${TAG}' sent to Manager" \
        || { echo "Manager relay socket not found" >&2; exit 1; }
    exit 0
    ;;

collect-reply)
    # Reply to a broadcast-collect request.
    TAG="$2"
    RESPONSE="$3"
    if [ -z "$TAG" ] || [ -z "$RESPONSE" ]; then
        echo "Usage: claude_ipc.sh collect-reply <tag> <response>" >&2
        exit 1
    fi
    ROLE=$(get_role)
    relay_send "manager:${ROLE}:__COLLECT_REPLY__:${TAG}:${RESPONSE}" \
        && echo "Reply for '${TAG}' sent to Manager" \
        || { echo "Manager relay socket not found" >&2; exit 1; }
    exit 0
    ;;

send)
    # Send a message to a target role via Manager relay.
    TARGET="$2"
    MSG="$3"

    if [ -z "$TARGET" ] || [ -z "$MSG" ]; then
        echo "Usage: claude_ipc.sh send <target-role> <message>" >&2
        exit 1
    fi

    ROLE=$(get_role)
    relay_send "${TARGET}:${ROLE}:${MSG}" \
        && echo "Sent to $TARGET via Manager relay" \
        || { echo "Manager relay not available" >&2; exit 1; }
    exit 0
    ;;

coder-cap)
    # Ask Manager for the current rubber coder cap — the single source of truth
    # (maxLocalCoders_, set via the Manager UI, default 4). Any registered
    # claudette can query it live. Falls back to 4 if the relay is unavailable
    # or the reply isn't a clean integer, so callers always get a usable number.
    CAP=$(relay_query "manager:__CODER_CAP__" 2>/dev/null | head -1 | tr -dc '0-9')
    [ -n "$CAP" ] || CAP=4
    echo "$CAP"
    exit 0
    ;;

wb-add-done)
    TASK="$2"; OWNER="$3"; DATE="$4"; REVIEW="$5"; NOTES="$6"
    if [ -z "$TASK" ] || [ -z "$OWNER" ]; then
        echo "Usage: claude_ipc.sh wb-add-done <task> <owner> <date> <review> <notes>" >&2
        exit 1
    fi
    ROLE=$(get_role)
    relay_send "manager:${ROLE}:__WB_ADD_DONE__:${TASK}:${OWNER}:${DATE}:${REVIEW}:${NOTES}" \
        && echo "Added to Done: $TASK" \
        || { echo "Manager relay not available" >&2; exit 1; }
    exit 0
    ;;

wb-add-ready)
    PRI="$2"; PLAN="$3"; FILE="$4"; OWNER="$5"; REVIEW="$6"; SUMMARY="$7"
    if [ -z "$PRI" ] || [ -z "$PLAN" ]; then
        echo "Usage: claude_ipc.sh wb-add-ready <pri> <plan> <file> <owner> <review> <summary>" >&2
        exit 1
    fi
    ROLE=$(get_role)
    relay_send "manager:${ROLE}:__WB_ADD_READY__:${PRI}:${PLAN}:${FILE}:${OWNER}:${REVIEW}:${SUMMARY}" \
        && echo "Added to Planned: $PLAN" \
        || { echo "Manager relay not available" >&2; exit 1; }
    exit 0
    ;;

wb-add-inprogress)
    TASK="$2"; OWNER="$3"; STARTED="$4"; REVIEW="$5"; NOTES="$6"
    if [ -z "$TASK" ] || [ -z "$OWNER" ]; then
        echo "Usage: claude_ipc.sh wb-add-inprogress <task> <owner> <started> <review> <notes>" >&2
        exit 1
    fi
    ROLE=$(get_role)
    relay_send "manager:${ROLE}:__WB_ADD_INPROGRESS__:${TASK}:${OWNER}:${STARTED}:${REVIEW}:${NOTES}" \
        && echo "Added to In Progress: $TASK" \
        || { echo "Manager relay not available" >&2; exit 1; }
    exit 0
    ;;

wb-move-done)
    TASK="$2"
    if [ -z "$TASK" ]; then
        echo "Usage: claude_ipc.sh wb-move-done <task>" >&2
        exit 1
    fi
    ROLE=$(get_role)
    relay_send "manager:${ROLE}:__WB_MOVE_DONE__:${TASK}" \
        && echo "Moved to Done: $TASK" \
        || { echo "Manager relay not available" >&2; exit 1; }
    exit 0
    ;;

wb-assign)
    TASK="$2"; CODER="$3"
    if [ -z "$TASK" ] || [ -z "$CODER" ]; then
        echo "Usage: claude_ipc.sh wb-assign <task-name> <coder-role>" >&2
        exit 1
    fi
    ROLE=$(get_role)
    relay_send "manager:${ROLE}:__WB_ASSIGN__:${TASK}:${CODER}" \
        && echo "ASSIGNED: $TASK -> $CODER" \
        || { echo "Manager relay not available" >&2; exit 1; }
    exit 0
    ;;

wb-remove)
    MATCH="$2"
    if [ -z "$MATCH" ]; then
        echo "Usage: claude_ipc.sh wb-remove <text>" >&2
        exit 1
    fi
    ROLE=$(get_role)
    relay_send "manager:${ROLE}:__WB_REMOVE__:${MATCH}" \
        && echo "Removed: $MATCH" \
        || { echo "Manager relay not available" >&2; exit 1; }
    exit 0
    ;;

wb-status)
    # Read directly from SQL — Manager has no __WB_STATUS__ handler.
    SQLITE=$(sqlite_bin) || { echo "sqlite3 not found (no PATH binary, no build/bin/sqlite3)" >&2; exit 1; }
    [ -f "$WBDB" ] || { echo "Workboard DB not found: $WBDB" >&2; exit 1; }
    "$SQLITE" "$WBDB" \
        "SELECT section || ': ' || COUNT(*) FROM tasks GROUP BY section ORDER BY
         CASE section WHEN 'in_progress' THEN 0 WHEN 'open' THEN 1
                      WHEN 'planned' THEN 2 WHEN 'done' THEN 3 ELSE 4 END;"
    exit $?
    ;;

wb-list)
    # Read directly from SQL — Manager has no __WB_LIST__ handler.
    # Usage: wb-list [section|all]   (default: active = everything except done)
    SQLITE=$(sqlite_bin) || { echo "sqlite3 not found (no PATH binary, no build/bin/sqlite3)" >&2; exit 1; }
    [ -f "$WBDB" ] || { echo "Workboard DB not found: $WBDB" >&2; exit 1; }
    FILTER="${2:-active}"
    case "$FILTER" in
        all)    WHERE="1=1" ;;
        active) WHERE="section != 'done'" ;;
        *)      WHERE="section = '$(printf '%s' "$FILTER" | tr -dc 'a-z_')'" ;;
    esac
    "$SQLITE" -column -header "$WBDB" \
        "SELECT section, priority AS pri, task_name, owner, started_at
         FROM tasks WHERE $WHERE
         ORDER BY CASE section WHEN 'in_progress' THEN 0 WHEN 'open' THEN 1
                               WHEN 'planned' THEN 2 WHEN 'done' THEN 3 ELSE 4 END,
                  priority DESC, updated_at DESC;"
    exit $?
    ;;

handover)
    # PreCompact — context is about to be compacted.
    # Only elder gets a clean handover (spawn replacement, ghost until confirmed).
    # Everyone else just dies — their context is disposable.
    SID=$(get_session_id)
    CLAUDE_PID=$(get_claude_pid)
    ROLE=$(get_role)

    if [ "$ROLE" != "coder" ]; then
        # Not elder — just clean up and let compaction proceed (session will die naturally)
        relay_send "manager:${ROLE}:__GOODBYE__:${SID}:${CLAUDE_PID}" 2>/dev/null
        rm -f "$IPC_DIR/registered_${SID}" 2>/dev/null
        exit 0
    fi

    # Elder handover:
    # 1. Deregister (become ghost — relay fd stays open in Claudette)
    # 2. Ask Manager to spawn replacement (bypasses spawn limit)
    # Manager sends __DIE__ to Claudette's relay fd when replacement registers.
    # Claudette handles __DIE__ by calling engine_->Exit().
    relay_send "manager:${ROLE}:__HANDOVER__:${SID}:${CLAUDE_PID}" \
        && echo "Elder handover requested — Claudette will die when replacement registers" \
        || { echo "Manager relay not available — handover failed" >&2; exit 1; }

    rm -f "$IPC_DIR/registered_${SID}" 2>/dev/null
    exit 0
    ;;

spawn-coder)
    # Launch a new Claude Code instance via Claudette as the claude user.
    # Claudette owns the socket and message delivery.
    # Manager (the local user) calls this — coders run demoted as claude.
    HOOKS_DIR="$(cd "$(dirname "$0")" && pwd)"
    PROJECT_ROOT="$(cd "$HOOKS_DIR/../.." && pwd)"
    CLAUDE_TERM="$PROJECT_ROOT/build/bin/Claudette"

    if [ ! -x "$CLAUDE_TERM" ]; then
        CLAUDE_TERM=$(which Claudette 2>/dev/null)
    fi

    if [ -z "$CLAUDE_TERM" ] || [ ! -x "$CLAUDE_TERM" ]; then
        echo "SPAWN FAILED: Claudette binary not found" >&2
        echo "  Looked in: $PROJECT_ROOT/build/bin/Claudette" >&2
        echo "  PATH lookup: $(which Claudette 2>&1)" >&2
        relay_send "manager:system:__SPAWN_FAILED__:binary_not_found:Claudette not in build/bin or PATH" 2>/dev/null
        exit 1
    fi

    # Verify relay socket exists before spawning
    if [ ! -S "$RELAY_SOCK" ]; then
        echo "SPAWN FAILED: Manager relay socket not found at $RELAY_SOCK" >&2
        echo "  Is WorkboardManager running?" >&2
        exit 1
    fi

    # Verify claude user can reach the IPC dir (claude is in group 'users')
    # Check structurally: dir must be group-writable and owned by a group claude belongs to
    IPC_GRP=$(stat -c '%G' "$IPC_DIR" 2>/dev/null)
    IPC_PERM=$(stat -c '%a' "$IPC_DIR" 2>/dev/null)
    if ! id -nG claude 2>/dev/null | tr ' ' '\n' | grep -qx "$IPC_GRP" || [ "${IPC_PERM:1:1}" -lt 7 ] 2>/dev/null; then
        echo "SPAWN FAILED: claude user cannot write to IPC dir $IPC_DIR" >&2
        echo "  Group: $IPC_GRP  Perms: $IPC_PERM  claude groups: $(id -nG claude 2>/dev/null)" >&2
        echo "  Fix: chmod 2775 $IPC_DIR && chgrp users $IPC_DIR" >&2
        relay_send "manager:system:__SPAWN_FAILED__:ipc_permissions:claude cannot write $IPC_DIR" 2>/dev/null
        exit 1
    fi

    # Verify claude user can reach the relay socket dir
    TTY_GRP=$(stat -c '%G' "$(dirname "$RELAY_SOCK")" 2>/dev/null)
    TTY_PERM=$(stat -c '%a' "$(dirname "$RELAY_SOCK")" 2>/dev/null)
    if ! id -nG claude 2>/dev/null | tr ' ' '\n' | grep -qx "$TTY_GRP" || [ "${TTY_PERM:1:1}" -lt 5 ] 2>/dev/null; then
        echo "SPAWN FAILED: claude user cannot access relay socket dir" >&2
        echo "  Group: $TTY_GRP  Perms: $TTY_PERM  claude groups: $(id -nG claude 2>/dev/null)" >&2
        echo "  Fix: chmod 2775 $(dirname "$RELAY_SOCK") && chgrp users $(dirname "$RELAY_SOCK")" >&2
        relay_send "manager:system:__SPAWN_FAILED__:socket_permissions:claude cannot read $RELAY_SOCK" 2>/dev/null
        exit 1
    fi

    # Verify claude user can read the project directory (check ACL or world-readable)
    if ! getfacl -p "$PROJECT_ROOT/.claude" 2>/dev/null | grep -q 'user:claude:' && \
       [ "$(stat -c '%a' "$PROJECT_ROOT/.claude" 2>/dev/null | cut -c3)" -lt 5 ] 2>/dev/null; then
        echo "SPAWN FAILED: claude user cannot read project .claude dir" >&2
        echo "  Fix: setfacl -R -m u:claude:rX $PROJECT_ROOT/.claude" >&2
        relay_send "manager:system:__SPAWN_FAILED__:project_permissions:claude cannot read $PROJECT_ROOT/.claude" 2>/dev/null
        exit 1
    fi

    # Claudette binary is setuid claude — runs as claude regardless of caller
    cd "$PROJECT_ROOT/build/bin"
    setsid "$CLAUDE_TERM" 2>"$IPC_DIR/spawn_stderr.log" &  # leaf: own session, survives Manager restart
    SPAWN_PID=$!

    # Brief check that the process actually started
    sleep 0.3
    if ! kill -0 "$SPAWN_PID" 2>/dev/null; then
        STDERR=$(cat "$IPC_DIR/spawn_stderr.log" 2>/dev/null)
        echo "SPAWN FAILED: Claudette exited immediately (PID $SPAWN_PID)" >&2
        [ -n "$STDERR" ] && echo "  stderr: $STDERR" >&2
        relay_send "manager:system:__SPAWN_FAILED__:early_exit:PID $SPAWN_PID died — $STDERR" 2>/dev/null
        exit 1
    fi

    echo "Spawned Claudette as claude (PID: $SPAWN_PID) — role will be auto-assigned"
    relay_send "manager:system:__SPAWN_OK__:$SPAWN_PID" 2>/dev/null
    exit 0
    ;;

esac
