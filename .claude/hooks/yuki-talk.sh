#!/bin/bash
# yuki-talk.sh — Talk to Yuki, see her reply
# Usage: yuki-talk.sh "your message here"

MSG="$*"
if [ -z "$MSG" ]; then
    echo "Usage: yuki-talk.sh <message>" >&2
    exit 1
fi

# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
SOCK="$IPC_DIR/tty/yuki.sock"
if [ ! -S "$SOCK" ]; then
    echo "Yuki is not running (no socket)" >&2
    exit 1
fi

printf 'prompt:%s' "$MSG" | nc -U -w120 "$SOCK" 2>/dev/null
