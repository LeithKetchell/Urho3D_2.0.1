#!/bin/bash
# Safe build wrapper for multi-coder environments
# Acquires a per-target flock before running make.
# Usage: safe_build.sh <target> [extra make args...]
#
# If another coder is building the same target:
#   - If the lock holder's Claude ancestor is alive and registered → stand down
#   - If the lock holder is an orphan (dead Claude) → wait for it to finish, then build
# This prevents dead coders from permanently blocking builds.

BUILD_DIR="$(cd "$(dirname "$0")/../.." && pwd)/build"
# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
LOCK_DIR="$IPC_DIR/locks/builds"
INSTANCE_DIR="$IPC_DIR/instances"
PENDING_DIR="$IPC_DIR/builds_pending"
mkdir -p "$LOCK_DIR" "$PENDING_DIR"

HOOK_DIR="$(cd "$(dirname "$0")" && pwd)"   # absolute: run_make runs after a cd into build/
# Route make through Claudette (running as the local user) so build artifacts are
# owned by the local user, not claude-owned. --no-fallback => fail closed if no broker is
# reachable (never silently build as claude). The flock/sprint orchestration
# below stays claude-side; only the compiler/linker runs as the local user.
run_make() {
    "$HOOK_DIR/broker_exec.sh" --no-fallback make -j4 "$@"
}

TARGET="$1"
if [ -z "$TARGET" ]; then
    echo "Usage: safe_build.sh <target> [extra make args...]" >&2
    exit 1
fi
shift

# Sprint detection: count live Claudette processes.
#
# OLD behavior (removed): >= SPRINT_THRESHOLD coders caused every build to DEFER
# (touch a pending flag and exit 0). Deferred builds only flushed when a build ran
# while the count was BELOW the threshold — which never happens during an active
# sprint, so all build progress starved. Sprinting (more coders) halted all builds.
#
# NEW behavior: builds ALWAYS run. During a sprint they SERIALIZE under a global
# build lock so we get one heavy `make -j4` at a time (no N-coder compiler storm)
# instead of deferring forever. The per-target flock below still prevents two
# coders building the SAME target at once (the real anti-corruption guard).
CLAUDETTE_COUNT=$(pgrep -c Claudette 2>/dev/null || echo 0)
# Sprint threshold = the user's rubber coder cap, queried live from Manager
# (single source of truth; default 4 if the relay is unavailable). Builds are
# batched only when the live fleet EXCEEDS the sanctioned cap — a fleet AT the
# cap is normal operation, not a sprint.
SPRINT_THRESHOLD=$("$(dirname "$0")/claude_ipc.sh" coder-cap 2>/dev/null || echo 4)
[ -n "$SPRINT_THRESHOLD" ] || SPRINT_THRESHOLD=4
GLOBAL_LOCK="$LOCK_DIR/global_build.lock"

# Vestigial from the old defer-and-skip scheme — clear stale markers, no longer used.
rm -f "$PENDING_DIR"/* 2>/dev/null

MANAGER_BIN="$BUILD_DIR/bin/WorkboardManager"
SPRINT_MODE=false
if [ "$CLAUDETTE_COUNT" -gt "$SPRINT_THRESHOLD" ]; then
    SPRINT_MODE=true
    # Infra exception: if WorkboardManager is missing/broken/down, build it now and
    # skip the global serialize wait so Manager recovery isn't blocked behind a big build.
    if [ "$TARGET" = "WorkboardManager" ]; then
        MANAGER_ALIVE=$(pgrep -xc WorkboardManage 2>/dev/null | head -1)
        MANAGER_ALIVE="${MANAGER_ALIVE:-0}"
        if [ ! -f "$MANAGER_BIN" ] || [ ! -x "$MANAGER_BIN" ] || [ "$MANAGER_ALIVE" -eq 0 ]; then
            echo "=== Sprint exception: WorkboardManager missing/down — building immediately (bypass serialize) ==="
            SPRINT_MODE=false
        fi
    fi
    [ "$SPRINT_MODE" = true ] && echo "=== Sprint ($CLAUDETTE_COUNT coders): $TARGET serialized under global build lock ==="
fi

# Run make, serialized machine-wide during a sprint so many coders don't storm the box.
build_target() {
    if [ "$SPRINT_MODE" = true ]; then
        exec 201>"$GLOBAL_LOCK"
        echo "=== Waiting for global build slot (sprint serialize)... ==="
        flock 201
        echo "=== Global build slot acquired ==="
    fi
    cd "$BUILD_DIR" && run_make "$@"
    local ec=$?
    [ "$SPRINT_MODE" = true ] && flock -u 201
    return $ec
}

LOCK_FILE="$LOCK_DIR/${TARGET}.lock"

# Try non-blocking lock first
exec 200>"$LOCK_FILE"
if flock -n 200; then
    # Got it immediately
    echo "=== Build lock acquired for $TARGET ==="
    build_target "$TARGET" "$@"
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 0 ]; then
        echo "=== Build complete: $TARGET ==="
    else
        echo "=== Build FAILED: $TARGET (exit $EXIT_CODE) ==="
    fi
    exit $EXIT_CODE
fi

# Lock is held — find out who holds it
HOLDER_PID=$(fuser "$LOCK_FILE" 2>/dev/null | tr -d ' ' | cut -d' ' -f1 | head -1)

# Check if the lock holder descends from a registered Claude instance
is_holder_alive() {
    local pid="$1"
    while [ -n "$pid" ] && [ "$pid" -gt 1 ] 2>/dev/null; do
        # Check if this PID matches any cached role file (role_pid_NNNN)
        if [ -f "$IPC_DIR/role_pid_${pid}" ]; then
            if ps -p "$pid" > /dev/null 2>&1; then
                return 0  # alive and registered
            else
                return 1  # registered but dead
            fi
        fi
        pid=$(ps -p "$pid" -o ppid= 2>/dev/null | tr -d ' ')
    done
    return 1  # no registered ancestor found
}

if [ -n "$HOLDER_PID" ] && is_holder_alive "$HOLDER_PID"; then
    echo "=== Target $TARGET already building by another coder, standing down ==="
    exit 0
fi

# Holder is an orphan (dead Claude) — wait for its build to finish, then take the lock
echo "=== Target $TARGET locked by orphan build (dead coder) — waiting for it to finish ==="
flock 200
echo "=== Build lock acquired for $TARGET (after orphan release) ==="
build_target "$TARGET" "$@"
EXIT_CODE=$?
if [ $EXIT_CODE -eq 0 ]; then
    echo "=== Build complete: $TARGET ==="
else
    echo "=== Build FAILED: $TARGET (exit $EXIT_CODE) ==="
fi
exit $EXIT_CODE
