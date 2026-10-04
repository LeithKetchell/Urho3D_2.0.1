#!/bin/bash
# broker_write.sh — PreToolUse(Write|Edit): route the write through this instance's
# Claudette broker so the file is created as leith, not claude.
# See Claude/CLAUDETTE_WRITE_EXEC_BROKER_PLAN.md (Layer 2 / phase 2).
#
# Flow: parse the tool input, compute the FULL new file content (Write -> content;
# Edit -> read current file and apply old_string->new_string), send a WRITE frame
# to the broker, and on OK BLOCK the original tool call (exit 2) so claude's own
# Write/Edit does NOT also run — the file is already written, by Claudette, as leith.
#
# Transition safety (phase 2, before the kernel floor): if no broker is reachable
# (e.g. raw claude, or a Claudette without the broker), we FALL OPEN (exit 0) and
# let claude's own Write/Edit proceed. Phase 3's ACL floor is what finally makes the
# broker non-bypassable; until then this hook must not brick brokerless instances.

source "$(dirname "$0")/broker_lib.sh"

INPUT=$(cat)

# No broker reachable -> fall open. Pre-floor this lets claude's own Write/Edit run
# (transition-safe). Post-floor the direct write hits the ACL and hard-fails, so the
# fall-open never results in a silent claude-owned write — fail-closed by construction.
if ! find_broker_sock; then
    exit 0
fi

TMP=$(mktemp "${TMPDIR:-/tmp}/broker_in.XXXXXX")
printf '%s' "$INPUT" > "$TMP"

RESULT=$(SOCK="$BROKER_SOCK" PROJECT_ROOT="$BROKER_PROJECT_ROOT" INPUT_FILE="$TMP" \
    python3 "$(dirname "$0")/broker_write.py")
RC=$?
rm -f "$TMP"

if [ "$RC" -eq 2 ]; then
    printf '%s\n' "$RESULT" >&2
    exit 2
fi
exit 0
