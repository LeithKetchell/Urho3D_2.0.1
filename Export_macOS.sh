#!/bin/bash
# Export_macOS.sh — Build a self-contained Claudette distribution for macOS
#
# Same pattern as Export_Linux.sh. Pulls from the live project tree,
# packs assets, sanitizes identity, generates the installer.
#
# Usage: ./Export_macOS.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
EXPORT_DIR="$PROJECT_ROOT/Export_macOS"
BIN_SRC="$PROJECT_ROOT/bin"
BUILD_BIN="$PROJECT_ROOT/build/bin"
HOOKS_SRC="$PROJECT_ROOT/.claude/hooks"
PACKAGE_TOOL="$BUILD_BIN/tool/PackageTool"

DEV_USER="$(whoami)"
DEV_HOME="$HOME"
DEV_HOST="$(hostname)"
DEV_FULLNAME="$(id -F 2>/dev/null || echo "")"

echo "=== Claudette Export (macOS) ==="
echo ""

# --- Verify prerequisites ---
FAIL=0
[ -x "$PACKAGE_TOOL" ] && echo "[OK] PackageTool"       || { echo "[FAIL] PackageTool not found"; FAIL=1; }
[ -x "$BUILD_BIN/Claudette" ] && echo "[OK] Claudette" || { echo "[FAIL] Claudette not found"; FAIL=1; }
[ -x "$BUILD_BIN/WorkboardManager" ] && echo "[OK] WorkboardManager" || { echo "[FAIL] WorkboardManager not found"; FAIL=1; }
[ "$FAIL" -gt 0 ] && exit 1

# --- Create output structure ---
echo ""
mkdir -p "$EXPORT_DIR/bin"
mkdir -p "$EXPORT_DIR/.claude/hooks"
mkdir -p "$EXPORT_DIR/Claude"

# --- Pack CoreData.pak ---
echo ""
echo "Packing CoreData.pak..."
STAGING="$(mktemp -d)"
trap "rm -rf '$STAGING'" EXIT

mkdir -p "$STAGING/CoreData/Textures" "$STAGING/CoreData/Techniques" \
         "$STAGING/CoreData/RenderPaths" "$STAGING/CoreData/Shaders/GLSL"

for f in Textures/Ramp.png Textures/Spot.png Techniques/NoTexture.xml \
         RenderPaths/Forward.xml Shaders/GLSL/Basic.glsl; do
    [ -f "$BIN_SRC/CoreData/$f" ] || { echo "[FAIL] Missing CoreData/$f"; exit 1; }
    cp "$BIN_SRC/CoreData/$f" "$STAGING/CoreData/$f"
done

"$PACKAGE_TOOL" -pqc "$STAGING/CoreData" "$EXPORT_DIR/bin/CoreData.pak"
echo "[OK] CoreData.pak"

# --- Pack Data.pak ---
echo "Packing Data.pak..."
mkdir -p "$STAGING/Data/Textures" "$STAGING/Data/UI" "$STAGING/Data/Fonts"

for f in Textures/UI.png UI/DefaultStyle.xml Fonts/UbuntuMono-R.ttf "Fonts/Anonymous Pro.ttf"; do
    [ -f "$BIN_SRC/Data/$f" ] || { echo "[FAIL] Missing Data/$f"; exit 1; }
    cp "$BIN_SRC/Data/$f" "$STAGING/Data/$f"
done

"$PACKAGE_TOOL" -pqc "$STAGING/Data" "$EXPORT_DIR/bin/Data.pak"
echo "[OK] Data.pak"

# --- Binaries ---
cp "$BUILD_BIN/Claudette" "$EXPORT_DIR/bin/Claudette"
chmod +x "$EXPORT_DIR/bin/Claudette"
echo "[OK] bin/Claudette"

cp "$BUILD_BIN/WorkboardManager" "$EXPORT_DIR/bin/WorkboardManager"
chmod +x "$EXPORT_DIR/bin/WorkboardManager"
echo "[OK] bin/WorkboardManager"

# --- Hook scripts ---
echo ""
for script in claude_ipc.sh safe_build.sh file_lock.sh tty-inject.sh; do
    if [ -f "$HOOKS_SRC/$script" ]; then
        cp "$HOOKS_SRC/$script" "$EXPORT_DIR/.claude/hooks/$script"
        chmod +x "$EXPORT_DIR/.claude/hooks/$script"
        echo "[OK] .claude/hooks/$script"
    fi
done

# --- C sources (macOS uses pty-proxy, no tty-listen-win) ---
for src in tty-listen.c pty-proxy.c; do
    if [ -f "$HOOKS_SRC/$src" ]; then
        cp "$HOOKS_SRC/$src" "$EXPORT_DIR/$src"
        echo "[OK] $src"
    fi
done

# --- settings.json (hooks) ---
if [ -f "$PROJECT_ROOT/.claude/settings.template.json" ]; then
    cp "$PROJECT_ROOT/.claude/settings.template.json" "$EXPORT_DIR/.claude/settings.json"
    echo "[OK] .claude/settings.json"
fi

# --- settings.local.json (minimal permissions) ---
cat > "$EXPORT_DIR/.claude/settings.local.json" <<'PERMS'
{
  "permissions": {
    "allow": [
      "Bash($CLAUDE_PROJECT_DIR/.claude/hooks/claude_ipc.sh:*)",
      "Bash($CLAUDE_PROJECT_DIR/.claude/hooks/safe_build.sh:*)"
    ],
    "deny": [],
    "ask": []
  }
}
PERMS
echo "[OK] .claude/settings.local.json"

# --- CLAUDE.md ---
MEMORY_DIR="$(find "$HOME/.claude/projects" -maxdepth 1 -type d -name "*$(basename "$PROJECT_ROOT")*" 2>/dev/null | head -1)/memory"
if [ -f "$MEMORY_DIR/claudette_rules.md" ]; then
    cp "$MEMORY_DIR/claudette_rules.md" "$EXPORT_DIR/CLAUDE.md"
    echo "[OK] CLAUDE.md"
elif [ -f "$PROJECT_ROOT/Claude/CLAUDETTE_RULES.md" ]; then
    cp "$PROJECT_ROOT/Claude/CLAUDETTE_RULES.md" "$EXPORT_DIR/CLAUDE.md"
    echo "[OK] CLAUDE.md (from CLAUDETTE_RULES.md)"
fi

# --- Workboard template ---
cat > "$EXPORT_DIR/Claude/WORKBOARD.md" <<'WORKBOARD'
# Workboard
## Team
| Name | Role | Focus |
|------|------|-------|
| **User** | Boss | Final authority |
| **Coder** | Implementation | Code, shaders, builds, plans |
## Planned (proposals — pending verification)
| Pri | Plan | File | Review | Summary |
|-----|------|------|--------|---------|
## Ready (verified — available for any Coder to claim)
| Pri | Plan | File | Summary |
|-----|------|------|---------|
## In Progress (owned — Under Construction)
| Task | Owner | Started | Review | Notes |
|------|-------|---------|--------|-------|
## Done (complete — pending review)
| Task | Owner | Completed | Review | Notes |
|------|-------|-----------|--------|-------|
## Archive (accepted — reference only)
| Task | Owner | Completed | Notes |
|------|-------|-----------|-------|
WORKBOARD
echo "[OK] Claude/WORKBOARD.md"

# --- Reference docs ---
for doc in CLAUDETTE_RULES.md IPC_PROTOCOL.md; do
    if [ -f "$PROJECT_ROOT/Claude/$doc" ]; then
        cp "$PROJECT_ROOT/Claude/$doc" "$EXPORT_DIR/Claude/$doc"
        echo "[OK] Claude/$doc"
    fi
done

# --- Sanitize ---
echo ""
echo "Sanitizing..."

if [ -f "$EXPORT_DIR/.claude/hooks/safe_build.sh" ]; then
    sed -i '' "s|^BUILD_DIR=.*|BUILD_DIR=\"\$(cd \"\$(dirname \"\$0\")/../..\" \&\& pwd)/build\"|" \
        "$EXPORT_DIR/.claude/hooks/safe_build.sh"
fi

find "$EXPORT_DIR" -type f \( -name "*.sh" -o -name "*.md" -o -name "*.json" -o -name "*.c" \) -print0 | \
    xargs -0 sed -i '' \
        -e "s|$PROJECT_ROOT/build/bin|./bin|g" \
        -e "s|$PROJECT_ROOT/build|./build|g" \
        -e "s|$PROJECT_ROOT|.|g" \
        -e "s|$DEV_HOME|\$HOME|g" \
        -e "s|$DEV_HOST|localhost|g"

find "$EXPORT_DIR" -type f \( -name "*.sh" -o -name "*.md" -o -name "*.json" -o -name "*.c" \) -print0 | \
    xargs -0 sed -i '' -E \
        -e "s|([ \t,;:!?\"'\(])${DEV_USER}([ \t,;:!?\"'\).\$])|\1User\2|gI" \
        -e "s|^${DEV_USER}([ \t,;:!?])$|User\1|gI" \
        -e "s|([ \t])${DEV_USER}\$|\1User|gI"

if [ -n "$DEV_FULLNAME" ]; then
    find "$EXPORT_DIR" -type f \( -name "*.sh" -o -name "*.md" -o -name "*.json" -o -name "*.c" \) -print0 | \
        xargs -0 sed -i '' -E \
            -e "s|([ \t,;:!?\"'\(])${DEV_FULLNAME}([ \t,;:!?\"'\).\$])|\1User\2|gI"
fi

LEAKS=$(grep -rni "$DEV_USER\|$DEV_HOME\|$DEV_HOST" "$EXPORT_DIR" --include="*.sh" --include="*.md" --include="*.json" --include="*.c" 2>/dev/null | grep -v Binary || true)
if [ -n "$LEAKS" ]; then
    echo "[WARN] Possible personal info:"
    echo "$LEAKS"
else
    echo "[OK] Clean"
fi

# --- Generate Install_macOS.sh ---
echo ""
echo "Generating installer..."

cat > "$EXPORT_DIR/Install_macOS.sh" <<'INSTALLER_EOF'
#!/bin/bash
# Install_macOS.sh — Install Claudette on macOS
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"
SHELL_RC="$HOME/.zshrc"
[ -f "$SHELL_RC" ] || SHELL_RC="$HOME/.bashrc"

echo "=== Claudette Installer (macOS) ==="
echo ""

# --- Verify distribution ---
CT_BIN="$BIN_DIR/Claudette"
[ -f "$CT_BIN" ]               || { echo "[FAIL] bin/Claudette missing"; exit 1; }
[ -f "$BIN_DIR/CoreData.pak" ] || { echo "[FAIL] bin/CoreData.pak missing"; exit 1; }
[ -f "$BIN_DIR/Data.pak" ]     || { echo "[FAIL] bin/Data.pak missing"; exit 1; }
chmod +x "$CT_BIN"
echo "[OK] Distribution intact"

# --- Prerequisites ---
echo ""
echo "Checking prerequisites..."
WARNINGS=0; NO_CC=0

# Xcode command line tools (provides clang/gcc)
if ! command -v cc &>/dev/null; then
    echo "  [MISS] cc — installing Xcode command line tools..."
    xcode-select --install 2>/dev/null || true
    echo "  [NOTE] Accept the Xcode dialog, then re-run this script."
    NO_CC=1
else
    echo "  [OK] cc"
fi

# Homebrew
if ! command -v brew &>/dev/null; then
    echo "  [MISS] brew — needed for flock. Install from https://brew.sh/"
    ((WARNINGS++))
else
    echo "  [OK] brew"
    # flock (not native on macOS)
    if ! command -v flock &>/dev/null; then
        echo "  [MISS] flock — installing via brew..."
        if brew install flock 2>/dev/null; then
            echo "  [OK] flock installed"
        else
            echo "  [WARN] Could not install flock"
            ((WARNINGS++))
        fi
    else
        echo "  [OK] flock"
    fi
fi

# Node.js + Claude Code CLI
if ! command -v claude &>/dev/null; then
    if ! command -v npm &>/dev/null; then
        if command -v brew &>/dev/null; then
            echo "  [MISS] npm — installing node via brew..."
            brew install node 2>/dev/null || true
        fi
    fi
    if command -v npm &>/dev/null; then
        echo "  [MISS] claude — installing..."
        npm install -g @anthropic-ai/claude-code 2>/dev/null || true
    fi
    if ! command -v claude &>/dev/null; then
        echo ""
        echo "[FAIL] Claude Code CLI required."
        echo "  Install Node.js (https://nodejs.org/), then: npm install -g @anthropic-ai/claude-code"
        exit 1
    fi
    echo "  [OK] claude installed"
else
    echo "  [OK] claude"
fi

[ "$WARNINGS" -gt 0 ] && echo "[WARN] $WARNINGS optional component(s) missing"
echo ""

# --- IPC directories ---
mkdir -p /tmp/urho_claude/{instances,tty,locks,locks/builds}
echo "[OK] IPC directories"

# --- Build C helpers ---
echo ""
if [ "$NO_CC" -eq 0 ]; then
    for src in tty-listen.c pty-proxy.c; do
        [ -f "$SCRIPT_DIR/$src" ] || continue
        outname="${src%.c}"
        # macOS: no -lutil needed, forkpty is in libutil or system libs
        if cc -O2 -o "$SCRIPT_DIR/.claude/hooks/$outname" "$SCRIPT_DIR/$src" -lpthread 2>/dev/null; then
            echo "  [OK] $outname"
        else
            echo "  [WARN] $outname build failed"
        fi
    done
else
    echo "  [SKIP] No compiler — helper binaries not built"
fi

# --- Shell RC launcher ---
echo ""
MARKER="# --- Claudette launcher (auto-installed) ---"
grep -qF "$MARKER" "$SHELL_RC" 2>/dev/null && sed -i '' "/$MARKER/,/# --- End Claudette ---/d" "$SHELL_RC"

cat >> "$SHELL_RC" <<BASHEOF

$MARKER
export CLAUDETTE_HOME="$SCRIPT_DIR"

unalias claudette 2>/dev/null
claudette() {
    local CT="\$CLAUDETTE_HOME/bin/Claudette"
    [ -x "\$CT" ] || { echo "Claudette not found at \$CT"; return 1; }

    mkdir -p /tmp/urho_claude/instances
    exec 8>/tmp/urho_claude/launch.lock
    flock -w 60 8 || { echo "Launch lock timeout"; exec 8>&-; return 1; }

    local ROLE="coder"

    cd "\$CLAUDETTE_HOME/bin"
    "\$CT" --\${ROLE} &
    local CTPID=\$!

    exec 8>&-
    echo "Claudette launched as \${ROLE} (PID \$CTPID)"
}
# --- End Claudette ---
BASHEOF
echo "[OK] claudette() installed in $SHELL_RC"

# --- Activate ---
echo ""
source "$SHELL_RC"

echo ""
echo "=== Installation Complete ==="
echo "Type 'claudette' to launch."
INSTALLER_EOF

chmod +x "$EXPORT_DIR/Install_macOS.sh"
echo "[OK] Install_macOS.sh"

# --- Summary ---
echo ""
echo "=== Export Complete ==="
echo ""
find "$EXPORT_DIR" -type f | sort | while read f; do
    echo "  $(du -h "$f" | cut -f1)  ${f#$EXPORT_DIR/}"
done
echo ""
du -sh "$EXPORT_DIR" | awk '{print "Total: " $1}'
