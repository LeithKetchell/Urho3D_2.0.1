#!/bin/bash
# Link serializer for multi-coder environments
# Acquires a per-target flock before running the actual linker.
# Usage: link_serialize.sh <target> <linker command...>
#
# CMake link.txt calls this as: link_serialize.sh TargetName /usr/bin/c++ [args...]
# This prevents concurrent link steps from corrupting shared output binaries.

# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
LOCK_DIR="$IPC_DIR/locks/links"
mkdir -p "$LOCK_DIR"

TARGET="$1"
shift

if [ -z "$TARGET" ]; then
    echo "link_serialize.sh: missing target name" >&2
    exit 1
fi

LOCK_FILE="$LOCK_DIR/${TARGET}.lock"

exec 200>"$LOCK_FILE"
flock 200

"$@"
EXIT_CODE=$?

exit $EXIT_CODE
