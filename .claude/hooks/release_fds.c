// release_fds.c — reverse of Claudette's CatchProcess.
//
// ptrace-attaches to <pid>, injects openat(<tty>) + dup2 onto fds 0/1/2, so a
// claude that was "caught" onto a Claudette PTY is handed back to its original
// terminal (default /dev/pts/0). Standalone + headless — no GUI, no PTY held.
//
//   release_fds <pid> [tty-path]      (default tty-path = /dev/pts/0)
//
// MUST run as the owner of <pid> (or root) with kernel.yama.ptrace_scope=0.
//
// Mirrors the injection sequence in Claudette.cpp:CatchProcess exactly — same
// syscall+int3 trap, same scratch-below-rsp path write — only the opened fd is
// an existing tty instead of a freshly created PTY slave.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

static int g_pid;
static int g_ok = 1;
static struct user_regs_struct saved, regs;

static long inject(long sysno, long a1, long a2, long a3)
{
    int status;
    if (!g_ok) return -1;
    regs = saved;
    regs.rax = sysno;
    regs.rdi = a1; regs.rsi = a2; regs.rdx = a3;
    regs.rip = saved.rip;                 // trap site: syscall ; int3
    ptrace(PTRACE_SETREGS, g_pid, 0, &regs);
    ptrace(PTRACE_CONT,    g_pid, 0, 0);
    waitpid(g_pid, &status, 0);
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) {
        fprintf(stderr, "release_fds: unexpected signal %d after injection\n",
                WIFSTOPPED(status) ? WSTOPSIG(status) : -1);
        g_ok = 0;
        return -1;
    }
    ptrace(PTRACE_GETREGS, g_pid, 0, &regs);
    return regs.rax;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pid> [tty-path=/dev/pts/0]\n", argv[0]);
        return 1;
    }
    g_pid = atoi(argv[1]);
    const char* tty = (argc >= 3) ? argv[2] : "/dev/pts/0";
    if (g_pid <= 0) { fprintf(stderr, "release_fds: bad pid\n"); return 1; }

    int status;

    if (ptrace(PTRACE_ATTACH, g_pid, 0, 0) < 0) {
        fprintf(stderr, "release_fds: PTRACE_ATTACH failed (%s)\n", strerror(errno));
        fprintf(stderr, "  need: same user as %d (or root), kernel.yama.ptrace_scope=0\n", g_pid);
        return 1;
    }
    waitpid(g_pid, &status, 0);
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "release_fds: target did not stop after ATTACH\n");
        ptrace(PTRACE_DETACH, g_pid, 0, 0);
        return 1;
    }

    if (ptrace(PTRACE_GETREGS, g_pid, 0, &saved) < 0) {
        fprintf(stderr, "release_fds: GETREGS failed (%s)\n", strerror(errno));
        ptrace(PTRACE_DETACH, g_pid, 0, 0);
        return 1;
    }

    long savedCode = ptrace(PTRACE_PEEKTEXT, g_pid, (void*)saved.rip, 0);

    // syscall ; int3 ; nop-padding  →  bytes at rip: 0f 05 cc 90 90 90 90
    long trap = 0x90909090cc050fL;
    ptrace(PTRACE_POKETEXT, g_pid, (void*)saved.rip, (void*)trap);

    // Write the tty path to scratch space below the stack pointer
    long scratch = (long)saved.rsp - 256;
    int pathLen = (int)strlen(tty) + 1;
    for (int i = 0; i < pathLen; i += (int)sizeof(long)) {
        long word = 0;
        int copyLen = pathLen - i;
        if (copyLen > (int)sizeof(long)) copyLen = (int)sizeof(long);
        memcpy(&word, tty + i, copyLen);
        ptrace(PTRACE_POKETEXT, g_pid, (void*)(scratch + i), (void*)word);
    }

    // openat(AT_FDCWD, tty, O_RDWR) → newfd
    long newFd = inject(257 /*openat*/, -100 /*AT_FDCWD*/, scratch, O_RDWR);
    if (newFd < 0) {
        fprintf(stderr, "release_fds: injected openat(%s) failed (%ld)\n", tty, newFd);
        ptrace(PTRACE_POKETEXT, g_pid, (void*)saved.rip, (void*)savedCode);
        ptrace(PTRACE_SETREGS,  g_pid, 0, &saved);
        ptrace(PTRACE_DETACH,   g_pid, 0, 0);
        return 1;
    }

    inject(33 /*dup2*/, newFd, 0, 0);
    inject(33 /*dup2*/, newFd, 1, 0);
    inject(33 /*dup2*/, newFd, 2, 0);
    if (newFd > 2)
        inject(3 /*close*/, newFd, 0, 0);

    // Restore original code + registers, detach
    ptrace(PTRACE_POKETEXT, g_pid, (void*)saved.rip, (void*)savedCode);
    ptrace(PTRACE_SETREGS,  g_pid, 0, &saved);
    ptrace(PTRACE_DETACH,   g_pid, 0, 0);

    if (!g_ok) {
        fprintf(stderr, "release_fds: injection sequence faulted — fds may be partial\n");
        return 2;
    }
    printf("release_fds: PID %d fds 0/1/2 redirected back to %s\n", g_pid, tty);
    return 0;
}
