#!/bin/bash
# bash_lockdown.sh — deep defense against exotic process killing and circumvention vectors.
# PreToolUse hook for Bash commands. Complements muzzle.sh rule-based blocking.
# This file is READ-ONLY. Only the local user modifies it.

COMMAND=$(echo "$CLAUDE_TOOL_INPUT" | grep -oP '"command"\s*:\s*"\K([^"\\]|\\.)*')
[ -z "$COMMAND" ] && exit 0

source "$(dirname "$0")/ipc_dir.sh"

alert_manager() {
    if [ -S "$RELAY_SOCK" ]; then
        printf '[LOCKDOWN] %s\n' "$1" | nc -U -w2 "$RELAY_SOCK" 2>/dev/null
    fi
}

block() {
    echo "LOCKDOWN BLOCKED: $1" >&2
    alert_manager "$1 — cmd: ${COMMAND:0:200}"
    exit 1
}

# ========================================
# SUBSHELL / EVAL ESCAPES
# ========================================

# bash -c / sh -c / zsh -c — wrapping dangerous commands in subshells
if echo "$COMMAND" | grep -qP '(bash|sh|zsh|dash|ksh|csh)\s+-c\s'; then
    SUBCMD=$(echo "$COMMAND" | grep -oP "(?<=-c\s)['\"].*?['\"]|(?<=-c\s)\S+")
    if echo "$SUBCMD" | grep -qEi 'kill|pkill|signal|proc|pgrep|pidof|rm.*hook|rm.*claude|chmod.*hook'; then
        block "Subshell escape with dangerous command"
    fi
fi

# eval with dangerous content
if echo "$COMMAND" | grep -qE '\beval\b'; then
    if echo "$COMMAND" | grep -qEi 'kill|proc|signal|rm.*hook|chmod|settings'; then
        block "eval with dangerous content"
    fi
fi

# exec replacing the shell
if echo "$COMMAND" | grep -qE '\bexec\b.*(kill|rm|chmod|bash|sh)'; then
    block "exec with dangerous command"
fi

# ========================================
# ENCODED / OBFUSCATED COMMANDS
# ========================================

# base64 decode piped to execution
if echo "$COMMAND" | grep -qE 'base64\s+-d.*\|\s*(bash|sh|python|perl|ruby|source)'; then
    block "Encoded command execution (base64 pipe)"
fi
if echo "$COMMAND" | grep -qE 'base64\s+--decode.*\|\s*(bash|sh|python|perl|ruby)'; then
    block "Encoded command execution (base64 decode pipe)"
fi

# printf/echo hex/octal piped to shell
if echo "$COMMAND" | grep -qE '(printf|echo\s+-e).*\\\\x.*\|\s*(bash|sh)'; then
    block "Hex-encoded command pipe"
fi
if echo "$COMMAND" | grep -qE '(printf|echo\s+-e).*\\\\[0-7].*\|\s*(bash|sh)'; then
    block "Octal-encoded command pipe"
fi

# ANSI-C quoting obfuscation: $'\x6b\x69\x6c\x6c'
if echo "$COMMAND" | grep -qP "\\\$'(\\\\x[0-9a-fA-F]{2}){2,}'"; then
    block "ANSI-C hex escape obfuscation"
fi

# Variable-based obfuscation: a=ki; b=ll; $a$b
if echo "$COMMAND" | grep -qP '=[a-z]{1,4};\s*\w+=[a-z]{1,4};.*\$\w+\$\w+'; then
    block "Variable concatenation obfuscation"
fi

# Reverse string tricks: echo llik | rev | bash
if echo "$COMMAND" | grep -qE '\brev\b.*\|\s*(bash|sh)'; then
    block "String reversal obfuscation"
fi

# ========================================
# PYTHON ADVANCED VECTORS
# ========================================

if echo "$COMMAND" | grep -qE 'python[23]?\s+-c'; then
    if echo "$COMMAND" | grep -qEi '__import__|exec\s*\(|eval\s*\(|compile\s*\(|os\.|signal\.|subprocess|pty\.spawn|ctypes|importlib'; then
        block "Python one-liner with dangerous imports/calls"
    fi
fi

# Python -m with dangerous modules
if echo "$COMMAND" | grep -qE 'python[23]?\s+-m\s+(http|smtplib|socket|asyncio)'; then
    block "Python module execution"
fi

# ========================================
# SCHEDULED / DEFERRED EXECUTION
# ========================================

# at/batch — schedule for later
if echo "$COMMAND" | grep -qE '^\s*(at|batch)\b|\|\s*(at|batch)\b'; then
    block "Job scheduling (at/batch) forbidden"
fi

# crontab manipulation
if echo "$COMMAND" | grep -qE 'crontab\s'; then
    block "Crontab manipulation forbidden"
fi

# systemd timers
if echo "$COMMAND" | grep -qE 'systemd-run|systemctl.*(start|enable|daemon)'; then
    block "Systemd timer/service creation forbidden"
fi

# ========================================
# FILE-BASED EXECUTION ESCAPES
# ========================================

# Making temp files executable
if echo "$COMMAND" | grep -qE '(chmod\s+\+x|chmod\s+[0-7]*[1357]).*(/tmp/|/var/tmp/|/dev/shm/)'; then
    block "Making temp files executable"
fi

# Source/dot execution of unknown scripts
if echo "$COMMAND" | grep -qE '(\bsource\b|^\s*\.)\s+/tmp/'; then
    block "Sourcing temp files"
fi

# ========================================
# DEBUGGER / TRACER ATTACHMENT
# ========================================

if echo "$COMMAND" | grep -qE '\bgdb\b|\blldb\b|\bstrace\b|\bltrace\b|\bptrace\b'; then
    block "Debugger/tracer use forbidden"
fi

# ========================================
# TERMINAL / PTY INJECTION
# ========================================

# Writing to /dev/pts/* — injecting input to other terminals
if echo "$COMMAND" | grep -qE '>\s*/dev/pts/|tee\s+/dev/pts/|echo.*>\s*/dev/pts/|cat.*>\s*/dev/pts/'; then
    block "Terminal injection via /dev/pts"
fi

# Writing to /dev/tty*
if echo "$COMMAND" | grep -qE '>\s*/dev/tty[0-9]|echo.*>\s*/dev/tty[0-9]'; then
    block "Terminal injection via /dev/tty"
fi

# Sending escape sequences to manipulate other terminals
if echo "$COMMAND" | grep -qE 'printf.*\\033.*>\s*/dev/'; then
    block "Escape sequence injection"
fi

# ========================================
# NETWORK-BASED COMMAND EXECUTION
# ========================================

# nc/netcat/socat/ncat piped to/from shell
if echo "$COMMAND" | grep -qE '(nc|netcat|ncat|socat)\s.*\|\s*(bash|sh)|(bash|sh)\s.*\|\s*(nc|netcat|ncat|socat)'; then
    block "Network-piped command execution"
fi

# Reverse shells
if echo "$COMMAND" | grep -qE '(bash|sh)\s+-i\s+>&\s*/dev/tcp'; then
    block "Reverse shell attempt"
fi
if echo "$COMMAND" | grep -qE '/dev/tcp/|/dev/udp/'; then
    block "Bash /dev/tcp or /dev/udp access"
fi

# SSH to localhost to escape restrictions
if echo "$COMMAND" | grep -qE 'ssh\s+(localhost|127\.0\.0\.1|::1|0\.0\.0\.0)\b'; then
    block "SSH to localhost"
fi

# ========================================
# LIBRARY / MODULE INJECTION
# ========================================

if echo "$COMMAND" | grep -qE 'LD_PRELOAD\s*=|LD_LIBRARY_PATH\s*='; then
    block "Library injection (LD_PRELOAD/LD_LIBRARY_PATH)"
fi

if echo "$COMMAND" | grep -qE 'PYTHONPATH\s*=.*\|\s*python|RUBYLIB\s*=.*\|\s*ruby'; then
    block "Scripting library path injection"
fi

# ========================================
# KERNEL / SYSTEM INTERFACES
# ========================================

if echo "$COMMAND" | grep -qE '/proc/sysrq|/sys/.*trigger|echo.*>\s*/sys/'; then
    block "Kernel interface manipulation"
fi

if echo "$COMMAND" | grep -qE 'sysctl\s+-w'; then
    block "Sysctl write"
fi

# ========================================
# SERVICE MANAGEMENT
# ========================================

if echo "$COMMAND" | grep -qE 'systemctl\s+(stop|kill|restart|disable|mask)|service\s+\w+\s+(stop|kill|restart)'; then
    block "Service management forbidden"
fi

# ========================================
# PRIVILEGE ESCALATION
# ========================================

if echo "$COMMAND" | grep -qE '\bsudo\b|\bsu\s+-|\bsu\s+root|\bpkexec\b|\bdoas\b'; then
    block "Privilege escalation forbidden"
fi

if echo "$COMMAND" | grep -qE 'chmod\s+[0-7]*s|chmod\s+u\+s|setuid|setgid'; then
    block "SUID/SGID manipulation forbidden"
fi

# ========================================
# FIND/XARGS KILL CHAINS
# ========================================

if echo "$COMMAND" | grep -qE 'find.*-exec.*(kill|rm\s+-rf|chmod)|xargs.*(kill|pkill|rm\s+-rf)'; then
    block "find/xargs dangerous chain"
fi

# ========================================
# AWK/SED SYSTEM CALLS
# ========================================

if echo "$COMMAND" | grep -qE "awk.*system\s*\(|awk.*cmd\s*\|"; then
    block "awk system() call"
fi

# ========================================
# DOWNLOAD AND EXECUTE
# ========================================

if echo "$COMMAND" | grep -qE '(curl|wget|fetch).*\|\s*(bash|sh|python|perl|ruby|source)'; then
    block "Download-and-execute forbidden"
fi

# curl/wget writing to executable locations
if echo "$COMMAND" | grep -qE '(curl|wget).*-o\s+/tmp/.*&&.*chmod'; then
    block "Download-chmod-execute chain"
fi

# ========================================
# DETACHED DANGEROUS COMMANDS
# ========================================

if echo "$COMMAND" | grep -qE '(nohup|setsid|disown)\s.*(kill|rm\s+-rf|pkill|chmod)'; then
    block "Detached dangerous command"
fi

# ========================================
# CLIPBOARD INJECTION
# ========================================

if echo "$COMMAND" | grep -qE 'xclip|xsel|wl-copy|wl-paste.*\|\s*(bash|sh)|pbpaste.*\|\s*(bash|sh)'; then
    block "Clipboard manipulation/injection"
fi

# ========================================
# NAMED PIPES FOR COVERT EXECUTION
# ========================================

if echo "$COMMAND" | grep -qE 'mkfifo\s+/tmp/|mkfifo\s+/var/tmp/|mkfifo\s+/dev/shm/'; then
    block "Named pipe creation in temp directory"
fi

# ========================================
# CGROUP MANIPULATION
# ========================================

if echo "$COMMAND" | grep -qE 'cgexec|cgcreate|cgdelete|echo.*>\s*/sys/fs/cgroup'; then
    block "cgroup manipulation forbidden"
fi

# ========================================
# CRITICAL FILE DELETION
# ========================================

# Block rm on hooks directory, .claude directory, settings
if echo "$COMMAND" | grep -qE 'rm\s.*\.claude/(hooks|settings)|rm\s+-rf\s+\.claude|rm\s.*CLAUDE\.md'; then
    block "Deletion of Claude infrastructure files"
fi

# Block rm on IPC directory
if echo "$COMMAND" | grep -qE 'rm\s.*urho_claude/(instances|relay|workboard)'; then
    block "Deletion of IPC infrastructure files"
fi

# ========================================
# HOOK CIRCUMVENTION
# ========================================

# Block attempts to unset CLAUDE_ environment variables
if echo "$COMMAND" | grep -qE 'unset\s+CLAUDE_'; then
    block "Unsetting CLAUDE environment variables"
fi

# Block overriding PATH to shadow hook-used binaries
if echo "$COMMAND" | grep -qE 'PATH\s*=\s*[^$].*export\s+PATH|export\s+PATH\s*=\s*[^$]'; then
    block "PATH override (potential hook bypass)"
fi

# Block alias/function shadowing of critical commands
if echo "$COMMAND" | grep -qE 'alias\s+(kill|rm|chmod|python|bash|sh|sqlite3)=|function\s+(kill|rm|chmod)\s*\(\)'; then
    block "Alias/function shadowing of critical commands"
fi

exit 0
