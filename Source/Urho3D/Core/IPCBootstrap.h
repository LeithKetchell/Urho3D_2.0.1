// IPC directory bootstrap — ensures /tmp/urho_claude/ structure exists.
// Header-only, idempotent, Linux-only. Safe to call from any process.

#pragma once

#include <sys/stat.h>
#include <cerrno>
#include <cstring>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace Urho3D
{

/// Ensure the IPC directory structure and FIFOs exist.
/// Safe to call from any process — all operations are idempotent.
inline bool EnsureIPCDirectory()
{
#ifdef _WIN32
    return false;  // IPC not supported on Windows
#else
    const char* ipcDir = "/tmp/urho_claude";
    const char* instDir = "/tmp/urho_claude/instances";
    const char* fifos[] = {
        "/tmp/urho_claude/from_coder",
        "/tmp/urho_claude/from_unassigned"
    };

    // Create directories (0777 — umask will restrict)
    mkdir(ipcDir, 0777);
    mkdir(instDir, 0777);

    // Create FIFOs if they don't exist
    for (int i = 0; i < 3; ++i)
    {
        struct stat st;
        if (stat(fifos[i], &st) != 0)
        {
            if (mkfifo(fifos[i], 0666) != 0 && errno != EEXIST)
                return false;  // real error
        }
    }
    return true;
#endif
}

}  // namespace Urho3D
