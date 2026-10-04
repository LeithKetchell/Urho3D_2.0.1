#!/bin/bash
# apply_hook_edits.sh — apply the two cage-protected hook edits for the
# "sprint threshold follows the Manager rubber cap" change, on coder5's behalf.
#
# Edits:
#   1. .claude/hooks/claude_ipc.sh  — add a `coder-cap` subcommand that queries
#      Manager (__CODER_CAP__) for the live maxLocalCoders_ value.
#   2. .claude/hooks/safe_build.sh  — source the threshold from that query
#      (default 4) and defer builds only when the fleet EXCEEDS the cap (-gt).
#
# Safe to re-run: each edit is guarded and skipped if already applied.
# Backs up both files to *.bak-<epoch> before modifying.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
IPC="$ROOT/.claude/hooks/claude_ipc.sh"
SB="$ROOT/.claude/hooks/safe_build.sh"
STAMP="$(date +%s)"

fail() { echo "ERROR: $*" >&2; exit 1; }

[ -f "$IPC" ] || fail "not found: $IPC"
[ -f "$SB" ]  || fail "not found: $SB"

# ── Pre-flight: verify the anchors we depend on still exist ───────────────────
grep -q '^wb-add-done)' "$IPC" || fail "anchor 'wb-add-done)' missing in claude_ipc.sh — file changed, aborting."
grep -q '^SPRINT_THRESHOLD=4$' "$SB" || grep -q 'coder-cap' "$SB" \
    || fail "anchor 'SPRINT_THRESHOLD=4' missing in safe_build.sh — file changed, aborting."

# ─────────────────────────────────────────────────────────────────────────────
# Edit 1: claude_ipc.sh — insert the coder-cap case before `wb-add-done)`
# ─────────────────────────────────────────────────────────────────────────────
if grep -q '^coder-cap)' "$IPC"; then
    echo "[skip] claude_ipc.sh already has a coder-cap) case."
else
    cp -p "$IPC" "$IPC.bak-$STAMP"
    BLOCK="$(mktemp)"
    cat > "$BLOCK" <<'EOF'
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

EOF
    # Insert the block immediately before the first `wb-add-done)` line.
    awk -v blockfile="$BLOCK" '
        /^wb-add-done\)/ && !done {
            while ((getline line < blockfile) > 0) print line
            close(blockfile)
            done = 1
        }
        { print }
    ' "$IPC" > "$IPC.new"
    rm -f "$BLOCK"
    # Sanity: the new file must contain the case and still parse as bash.
    grep -q '^coder-cap)' "$IPC.new" || { rm -f "$IPC.new"; fail "insertion failed (coder-cap not present)"; }
    bash -n "$IPC.new" || { rm -f "$IPC.new"; fail "edited claude_ipc.sh fails syntax check — reverted (backup at $IPC.bak-$STAMP)"; }
    mv "$IPC.new" "$IPC"
    chmod --reference="$IPC.bak-$STAMP" "$IPC" 2>/dev/null || chmod +x "$IPC"
    echo "[ok]  claude_ipc.sh: coder-cap) case added (backup: $IPC.bak-$STAMP)"
fi

# ─────────────────────────────────────────────────────────────────────────────
# Edit 2: safe_build.sh — threshold from Manager cap + defer only when EXCEEDED
# ─────────────────────────────────────────────────────────────────────────────
if grep -q 'coder-cap' "$SB" && ! grep -q '^SPRINT_THRESHOLD=4$' "$SB"; then
    echo "[skip] safe_build.sh already sources the threshold from Manager."
else
    cp -p "$SB" "$SB.bak-$STAMP"

    # 2a: replace the hard-coded `SPRINT_THRESHOLD=4` line with a live query.
    python3 - "$SB" <<'PY'
import sys, io
path = sys.argv[1]
src = io.open(path, encoding='utf-8').read()

old_line = 'SPRINT_THRESHOLD=4\n'
new_block = (
    '# Sprint threshold = the user\'s rubber coder cap, queried live from Manager\n'
    '# (single source of truth; default 4 if the relay is unavailable). Builds are\n'
    '# batched only when the live fleet EXCEEDS the sanctioned cap — a fleet AT the\n'
    '# cap is normal operation, not a sprint.\n'
    'SPRINT_THRESHOLD=$("$(dirname "$0")/claude_ipc.sh" coder-cap 2>/dev/null || echo 4)\n'
    '[ -n "$SPRINT_THRESHOLD" ] || SPRINT_THRESHOLD=4\n'
)
if old_line in src:
    src = src.replace(old_line, new_block, 1)

# 2b: defer only when coders EXCEED the cap (was -ge, now -gt).
src = src.replace(
    'if [ "$CLAUDETTE_COUNT" -ge "$SPRINT_THRESHOLD" ]; then',
    'if [ "$CLAUDETTE_COUNT" -gt "$SPRINT_THRESHOLD" ]; then',
    1,
)

io.open(path + '.new', 'w', encoding='utf-8').write(src)
PY

    grep -q 'claude_ipc.sh" coder-cap' "$SB.new" || { rm -f "$SB.new"; fail "safe_build.sh: threshold-query insertion failed"; }
    grep -q 'CLAUDETTE_COUNT" -gt "$SPRINT_THRESHOLD"' "$SB.new" || { rm -f "$SB.new"; fail "safe_build.sh: -gt comparison not applied"; }
    bash -n "$SB.new" || { rm -f "$SB.new"; fail "edited safe_build.sh fails syntax check — reverted (backup at $SB.bak-$STAMP)"; }
    mv "$SB.new" "$SB"
    chmod --reference="$SB.bak-$STAMP" "$SB" 2>/dev/null || chmod +x "$SB"
    echo "[ok]  safe_build.sh: threshold now from Manager cap, defer on -gt (backup: $SB.bak-$STAMP)"
fi

echo
echo "Done. Quick self-test (queries Manager; needs the NEW WorkboardManager running):"
echo "  \$ $IPC coder-cap"
echo "Expected: the current cap (e.g. 5). Prints 4 if the relay/handler isn't live yet."
