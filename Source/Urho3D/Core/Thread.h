// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#ifdef URHO3D_IS_BUILDING
#include "Urho3D.h"
#else
#include <Urho3D/Urho3D.h>
#endif

#ifndef _WIN32
#include <pthread.h>
using ThreadID = pthread_t;
#else
using ThreadID = unsigned;
#endif

namespace Urho3D
{

/// Operating system thread.
class URHO3D_API Thread
{
public:
    /// Construct. Does not start the thread yet.
    Thread();
    /// Destruct. If running, stop and wait for thread to finish.
    virtual ~Thread();

    /// The function to run in the thread.
    virtual void ThreadFunction() = 0;

    /// Start running the thread. Return true if successful, or false if already running or if can not create the thread.
    bool Run();
    /// Set the running flag to false and wait for the thread to finish.
    void Stop();
    /// Set thread priority. The thread must have been started first.
    void SetPriority(int priority);

    /// Set CPU affinity for this thread (Linux only via sched_setaffinity).
    /// cpuIndex: CPU core number to pin to (0-based)
    /// Returns true if successful
    bool SetAffinity(int cpuIndex);

    /// Return whether thread exists.
    bool IsStarted() const { return handle_ != nullptr; }

    /// Set the current thread as the main thread.
    static void SetMainThread();
    /// Return the current thread's ID.
    /// @nobind
    static ThreadID GetCurrentThreadID();
    /// Return whether is executing in the main thread.
    static bool IsMainThread();

    /// Get the number of logical CPU cores available
    static int GetNumCores();

    /// Get the number of NUMA nodes (1 if NUMA not available)
    static int GetNumNumaNodes();

    /// Get CPU core to NUMA node mapping (returns -1 if not available)
    static int GetCoreNumaNode(int coreIndex);

protected:
    /// Thread handle.
    void* handle_;
    /// Running flag.
    volatile bool shouldRun_;

    /// Main thread's thread ID.
    static ThreadID mainThreadID;
};

}
