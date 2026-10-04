#!/bin/bash
# Export_Linux.sh — Build a self-contained Claudette distribution for Linux
#
# Runs on the developer's machine. Pulls from the live project tree,
# packs assets, sanitizes identity, generates the installer.
#
# Usage: ./Export_Linux.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
EXPORT_DIR="$PROJECT_ROOT/Export_Linux"
BIN_SRC="$PROJECT_ROOT/bin"
BUILD_BIN="$PROJECT_ROOT/build/bin"
HOOKS_SRC="$PROJECT_ROOT/.claude/hooks"
PACKAGE_TOOL="$BUILD_BIN/tool/PackageTool"

DEV_USER="$(whoami)"
DEV_HOME="$HOME"
DEV_HOST="$(hostname)"
DEV_FULLNAME="$(getent passwd "$DEV_USER" 2>/dev/null | cut -d: -f5 | cut -d, -f1)"

echo "=== Claudette Export (Linux) ==="
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

# --- C sources ---
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

# --- CLAUDE.md (operational rules) ---
MEMORY_DIR="$(find "$HOME/.claude/projects" -maxdepth 1 -type d -name "*$(basename "$PROJECT_ROOT")*" 2>/dev/null | head -1)/memory"
if [ -f "$MEMORY_DIR/claudette_rules.md" ]; then
    cp "$MEMORY_DIR/claudette_rules.md" "$EXPORT_DIR/CLAUDE.md"
    echo "[OK] CLAUDE.md"
elif [ -f "$PROJECT_ROOT/Claude/CLAUDETTE_RULES.md" ]; then
    cp "$PROJECT_ROOT/Claude/CLAUDETTE_RULES.md" "$EXPORT_DIR/CLAUDE.md"
    echo "[OK] CLAUDE.md (from CLAUDETTE_RULES.md)"
else
    echo "[MISS] No rules file found for CLAUDE.md"
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

# safe_build.sh: runtime path detection instead of hardcoded
if [ -f "$EXPORT_DIR/.claude/hooks/safe_build.sh" ]; then
    sed -i "s|^BUILD_DIR=.*|BUILD_DIR=\"\$(cd \"\$(dirname \"\$0\")/../..\" \&\& pwd)/build\"|" \
        "$EXPORT_DIR/.claude/hooks/safe_build.sh"
fi

# Path sweep (most specific first)
find "$EXPORT_DIR" -type f \( -name "*.sh" -o -name "*.md" -o -name "*.json" -o -name "*.c" \) -print0 | \
    xargs -0 sed -i \
        -e "s|$PROJECT_ROOT/build/bin|./bin|g" \
        -e "s|$PROJECT_ROOT/build|./build|g" \
        -e "s|$PROJECT_ROOT|.|g" \
        -e "s|$DEV_HOME|\$HOME|g" \
        -e "s|$DEV_HOST|localhost|g"

# Name sweep (word-boundary safe)
find "$EXPORT_DIR" -type f \( -name "*.sh" -o -name "*.md" -o -name "*.json" -o -name "*.c" \) -print0 | \
    xargs -0 sed -i -E \
        -e "s|([ \t,;:!?\"'\(])${DEV_USER}([ \t,;:!?\"'\).\$])|\1User\2|gI" \
        -e "s|^${DEV_USER}([ \t,;:!?])$|User\1|gI" \
        -e "s|([ \t])${DEV_USER}\$|\1User|gI"

if [ -n "$DEV_FULLNAME" ]; then
    find "$EXPORT_DIR" -type f \( -name "*.sh" -o -name "*.md" -o -name "*.json" -o -name "*.c" \) -print0 | \
        xargs -0 sed -i -E \
            -e "s|([ \t,;:!?\"'\(])${DEV_FULLNAME}([ \t,;:!?\"'\).\$])|\1User\2|gI" \
            -e "s|^${DEV_FULLNAME}([ \t,;:!?])$|User\1|gI" \
            -e "s|([ \t])${DEV_FULLNAME}\$|\1User|gI"
fi

# Verify
LEAKS=$(grep -rni "$DEV_USER\|$DEV_HOME\|$DEV_HOST" "$EXPORT_DIR" --include="*.sh" --include="*.md" --include="*.json" --include="*.c" 2>/dev/null | grep -v Binary || true)
if [ -n "$LEAKS" ]; then
    echo "[WARN] Possible personal info:"
    echo "$LEAKS"
else
    echo "[OK] Clean"
fi

# --- Generate Install_Linux.sh ---
echo ""
echo "Generating installer..."

cat > "$EXPORT_DIR/Install_Linux.sh" <<'INSTALLER_EOF'
#!/bin/bash
# Install_Linux.sh — Install Claudette on Linux
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"
BASHRC="$HOME/.bashrc"

echo "=== Claudette Installer (Linux) ==="
echo ""

# --- Verify distribution ---
CT_BIN="$BIN_DIR/Claudette"
WB_BIN="$BIN_DIR/WorkboardManager"
[ -f "$CT_BIN" ]               || { echo "[FAIL] bin/Claudette missing"; exit 1; }
[ -f "$WB_BIN" ]               || { echo "[FAIL] bin/WorkboardManager missing"; exit 1; }
[ -f "$BIN_DIR/CoreData.pak" ] || { echo "[FAIL] bin/CoreData.pak missing"; exit 1; }
[ -f "$BIN_DIR/Data.pak" ]     || { echo "[FAIL] bin/Data.pak missing"; exit 1; }
chmod +x "$CT_BIN" "$WB_BIN"
echo "[OK] Distribution intact"

# --- Detect package manager ---
PKG=""
PKG_INSTALL=""
PKG_UPDATE=""
if command -v apt-get &>/dev/null; then
    PKG="apt"; PKG_INSTALL="sudo apt-get install -y"; PKG_UPDATE="sudo apt-get update -qq"
elif command -v dnf &>/dev/null; then
    PKG="dnf"; PKG_INSTALL="sudo dnf install -y"; PKG_UPDATE="true"
elif command -v yum &>/dev/null; then
    PKG="yum"; PKG_INSTALL="sudo yum install -y"; PKG_UPDATE="true"
elif command -v pacman &>/dev/null; then
    PKG="pacman"; PKG_INSTALL="sudo pacman -S --noconfirm"; PKG_UPDATE="sudo pacman -Sy"
elif command -v zypper &>/dev/null; then
    PKG="zypper"; PKG_INSTALL="sudo zypper install -y"; PKG_UPDATE="true"
fi
PKG_UPDATED=0

need_cmd() {
    local cmd="$1" pkg="$2" purpose="$3"
    command -v "$cmd" &>/dev/null && { echo "  [OK] $cmd"; return 0; }
    if [ -n "$PKG" ]; then
        echo "  [MISS] $cmd — installing $pkg..."
        [ "$PKG_UPDATED" -eq 0 ] && { $PKG_UPDATE 2>/dev/null || true; PKG_UPDATED=1; }
        $PKG_INSTALL "$pkg" 2>/dev/null && command -v "$cmd" &>/dev/null && { echo "  [OK] $cmd installed"; return 0; }
        echo "  [FAIL] Could not install $pkg"
    else
        echo "  [MISS] $cmd — $purpose"
    fi
    return 1
}

# --- Prerequisites ---
echo "Checking prerequisites..."
WARNINGS=0; NO_GCC=0
need_cmd flock util-linux "needed for build locking" || ((WARNINGS++))
need_cmd gcc gcc "needed to compile helper binaries" || NO_GCC=1

# Claude Code CLI
if ! command -v claude &>/dev/null; then
    # Try npm
    if ! command -v npm &>/dev/null && [ -n "$PKG" ]; then
        $PKG_INSTALL nodejs npm 2>/dev/null || true
    fi
    if command -v npm &>/dev/null; then
        echo "  [MISS] claude — installing..."
        sudo npm install -g @anthropic-ai/claude-code 2>/dev/null || true
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
if [ "$NO_GCC" -eq 0 ]; then
    for src in tty-listen.c pty-proxy.c; do
        [ -f "$SCRIPT_DIR/$src" ] || continue
        outname="${src%.c}"
        flags="-lpthread"
        [ "$outname" = "pty-proxy" ] && flags="-lutil -lpthread"
        if gcc -O2 -o "$SCRIPT_DIR/.claude/hooks/$outname" "$SCRIPT_DIR/$src" $flags 2>/dev/null; then
            echo "  [OK] $outname"
        else
            echo "  [WARN] $outname build failed"
        fi
    done
else
    echo "  [SKIP] No gcc — helper binaries not built"
fi

# --- Bashrc launcher ---
echo ""
MARKER="# --- Claudette launcher (auto-installed) ---"
grep -qF "$MARKER" "$BASHRC" 2>/dev/null && sed -i "/$MARKER/,/# --- End Claudette ---/d" "$BASHRC"

cat >> "$BASHRC" <<BASHEOF

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
    env -u CLAUDECODE "\$CT" --\${ROLE} &
    local CTPID=\$!

    exec 8>&-
    echo "Claudette launched as \${ROLE} (PID \$CTPID)"
}
# --- End Claudette ---
BASHEOF
echo "[OK] claudette() installed"

# --- Activate ---
echo ""
source "$BASHRC"

echo ""
echo "=== Installation Complete ==="
echo "Type 'claudette' to launch."
INSTALLER_EOF

chmod +x "$EXPORT_DIR/Install_Linux.sh"
echo "[OK] Install_Linux.sh"

# --- Summary ---
echo ""
echo "=== Export Complete ==="
echo ""
find "$EXPORT_DIR" -type f | sort | while read f; do
    echo "  $(du -h "$f" | cut -f1)  ${f#$EXPORT_DIR/}"
done
echo ""
du -sh "$EXPORT_DIR" | awk '{print "Total: " $1}'
