#!/bin/bash
# claudette-init.sh — Set up a new project directory for Claudette/Manager use.
# Usage: claudette-init.sh [target-dir]
#   target-dir: project root to initialize (default: current directory)
#
# Creates:
#   .claude/                     — project root marker
#   .claude/hooks/               — symlinks to shared security/IPC hooks
#   .claude/settings.local.json  — Claude Code project settings with hook wiring
#   CLAUDE.md                    — template project instructions

set -euo pipefail

# Resolve where shared hooks live (same directory as this script)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SHARED_HOOKS="$SCRIPT_DIR"

# Target project directory
TARGET="${1:-.}"
TARGET="$(cd "$TARGET" && pwd)"

if [ "$TARGET" = "$(cd "$SCRIPT_DIR/../.." && pwd)" ]; then
    echo "ERROR: Target is the Claudette source project — nothing to do." >&2
    exit 1
fi

echo "Initializing Claudette project: $TARGET"

# 1. Create .claude directory structure
mkdir -p "$TARGET/.claude/hooks"

# 2. Symlink shared hooks
HOOKS=(
    ipc_dir.sh
    claude_ipc.sh
    muzzle.sh
    screenshot_guard.sh
    screenshot_trap.sh
    hooks_protect.sh
    file_lock.sh
    safe_build.sh
)

for hook in "${HOOKS[@]}"; do
    if [ -f "$SHARED_HOOKS/$hook" ]; then
        ln -sf "$SHARED_HOOKS/$hook" "$TARGET/.claude/hooks/$hook"
        echo "  Linked: $hook"
    else
        echo "  SKIP:   $hook (not found in $SHARED_HOOKS)"
    fi
done

# 3. Create settings.local.json (only if it doesn't exist)
SETTINGS="$TARGET/.claude/settings.local.json"
if [ ! -f "$SETTINGS" ]; then
    cat > "$SETTINGS" << 'SETTINGSEOF'
{
  "permissions": {
    "allow": [],
    "deny": [],
    "ask": []
  },
  "hooks": {
    "SessionStart": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "$CLAUDE_PROJECT_DIR/.claude/hooks/claude_ipc.sh announce"
          }
        ]
      }
    ],
    "UserPromptSubmit": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "$CLAUDE_PROJECT_DIR/.claude/hooks/claude_ipc.sh check"
          }
        ]
      }
    ],
    "Stop": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "$CLAUDE_PROJECT_DIR/.claude/hooks/claude_ipc.sh notify-idle",
            "async": true
          }
        ]
      }
    ],
    "Notification": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "$CLAUDE_PROJECT_DIR/.claude/hooks/claude_ipc.sh check"
          }
        ]
      }
    ],
    "PreToolUse": [
      {
        "matcher": "Read|Write|Edit",
        "hooks": [
          {
            "type": "command",
            "command": "$CLAUDE_PROJECT_DIR/.claude/hooks/file_lock.sh"
          }
        ]
      },
      {
        "matcher": "Bash",
        "hooks": [
          {
            "type": "command",
            "command": "$CLAUDE_PROJECT_DIR/.claude/hooks/muzzle.sh"
          }
        ]
      },
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "$CLAUDE_PROJECT_DIR/.claude/hooks/screenshot_guard.sh"
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "Read|Write|Edit",
        "hooks": [
          {
            "type": "command",
            "command": "$CLAUDE_PROJECT_DIR/.claude/hooks/file_lock.sh"
          }
        ]
      }
    ],
    "SessionEnd": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "$CLAUDE_PROJECT_DIR/.claude/hooks/claude_ipc.sh cleanup",
            "async": true
          }
        ]
      }
    ]
  }
}
SETTINGSEOF
    echo "  Created: .claude/settings.local.json"
else
    echo "  EXISTS:  .claude/settings.local.json (not overwritten)"
fi

# 4. Create CLAUDE.md template (only if it doesn't exist)
CLAUDEMD="$TARGET/CLAUDE.md"
if [ ! -f "$CLAUDEMD" ]; then
    PROJECT_NAME="$(basename "$TARGET")"
    cat > "$CLAUDEMD" << MDEOF
# CLAUDE.md

## Project Overview

**${PROJECT_NAME}** — [describe your project here]

## Session Startup Protocol

On session start:

1. Run \`.claude/hooks/claude_ipc.sh announce\` — registers with WorkboardManager (Manager assigns your role)
2. Check in with the local user for your assignment

## Build Commands

\`\`\`bash
# Add your build commands here
\`\`\`

## Repository Structure

\`\`\`
# Describe your project layout here
\`\`\`
MDEOF
    echo "  Created: CLAUDE.md"
else
    echo "  EXISTS:  CLAUDE.md (not overwritten)"
fi

echo ""
echo "Done. To launch:"
echo "  cd $TARGET"
echo "  $(cd "$SCRIPT_DIR/../.." && pwd)/build/bin/WorkboardManager &"
echo "  $(cd "$SCRIPT_DIR/../.." && pwd)/build/bin/claudette"
