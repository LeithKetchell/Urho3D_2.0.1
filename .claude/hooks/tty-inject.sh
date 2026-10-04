#!/bin/bash
# tty-inject.sh — Send a message to a Claude Code instance via its pty-proxy socket
# Usage: tty-inject.sh <role> <message>
#
# The message is injected into the instance's TTY as if the user typed it.

ROLE="$1"
shift
MESSAGE="$*"

if [ -z "$ROLE" ] || [ -z "$MESSAGE" ]; then
    echo "Usage: tty-inject.sh <role> <message>" >&2
    exit 1
fi

# Source project-specific IPC directory
source "$(dirname "$0")/ipc_dir.sh"
SOCK="$IPC_DIR/tty/${ROLE}.sock"

if [ ! -S "$SOCK" ]; then
    echo "No socket for role '$ROLE' at $SOCK" >&2
    exit 1
fi

# Send text as bulk, then Enter after a delay.
# Claude Code's TUI uses bracketed paste detection — rapid byte bursts
# are treated as paste, and CR within a paste doesn't trigger submit.
# The delay lets paste detection expire so CR is treated as a keypress.
{ printf '%s' "$MESSAGE"; sleep 0.15; printf '\r'; } | nc -U -w1 "$SOCK" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Injection failed" >&2
    exit 1
fi
