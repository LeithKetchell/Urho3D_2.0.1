// PlatformUtils.h — Cross-platform helpers with no Urho3D equivalent
// Used by WorkboardManager and WorkboardClient

#pragma once

#include <Urho3D/Container/Str.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#endif

/// Get current process ID.
static inline int GetCurrentPID()
{
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

/// Atomic single-level mkdir (for lock mechanism).
/// Returns true if directory was created (lock acquired), false if it already exists.
static inline bool AtomicMkdir(const Urho3D::String& path)
{
#ifdef _WIN32
    return CreateDirectoryA(path.CString(), nullptr) != 0;
#else
    return mkdir(path.CString(), 0777) == 0;
#endif
}

/// Ignore SIGPIPE — no-op on Windows.
static inline void IgnoreSigPipe()
{
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
}
