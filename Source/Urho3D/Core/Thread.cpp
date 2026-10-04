// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Core/Thread.h"

#ifdef _WIN32
#include "../Engine/WinWrapped.h"
#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#include "../DebugNew.h"

namespace Urho3D
{

#ifdef URHO3D_THREADING
#ifdef _WIN32

static DWORD WINAPI ThreadFunctionStatic(void* data)
{
    Thread* thread = static_cast<Thread*>(data);
    thread->ThreadFunction();
    return 0;
}

#else

static void* ThreadFunctionStatic(void* data)
{
    auto* thread = static_cast<Thread*>(data);
    thread->ThreadFunction();
    pthread_exit((void*)nullptr);
    return nullptr;
}

#endif
#endif // URHO3D_THREADING

ThreadID Thread::mainThreadID;

Thread::Thread() :
    handle_(nullptr),
    shouldRun_(false)
{
}

Thread::~Thread()
{
    Stop();
}

bool Thread::Run()
{
#ifdef URHO3D_THREADING
    // Check if already running
    if (handle_)
        return false;

    shouldRun_ = true;
#ifdef _WIN32
    handle_ = CreateThread(nullptr, 0, ThreadFunctionStatic, this, 0, nullptr);
#else
    handle_ = new pthread_t;
    pthread_attr_t type;
    pthread_attr_init(&type);
    pthread_attr_setdetachstate(&type, PTHREAD_CREATE_JOINABLE);
    pthread_create((pthread_t*)handle_, &type, ThreadFunctionStatic, this);
#endif
    return handle_ != nullptr;
#else
    return false;
#endif // URHO3D_THREADING
}

void Thread::Stop()
{
#ifdef URHO3D_THREADING
    // Check if already stopped
    if (!handle_)
        return;

    shouldRun_ = false;
#ifdef _WIN32
    WaitForSingleObject((HANDLE)handle_, INFINITE);
    CloseHandle((HANDLE)handle_);
#else
    auto* thread = (pthread_t*)handle_;
    if (thread)
        pthread_join(*thread, nullptr);
    delete thread;
#endif
    handle_ = nullptr;
#endif // URHO3D_THREADING
}

void Thread::SetPriority(int priority)
{
#ifdef URHO3D_THREADING
#ifdef _WIN32
    if (handle_)
        SetThreadPriority((HANDLE)handle_, priority);
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    auto* thread = (pthread_t*)handle_;
    if (thread)
        pthread_setschedprio(*thread, priority);
#endif
#endif // URHO3D_THREADING
}

void Thread::SetMainThread()
{
    mainThreadID = GetCurrentThreadID();
}

ThreadID Thread::GetCurrentThreadID()
{
#ifdef URHO3D_THREADING
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif
#else
    return ThreadID();
#endif // URHO3D_THREADING
}

bool Thread::IsMainThread()
{
#ifdef URHO3D_THREADING
    return GetCurrentThreadID() == mainThreadID;
#else
    return true;
#endif // URHO3D_THREADING
}

bool Thread::SetAffinity(int cpuIndex)
{
#ifdef URHO3D_THREADING
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    if (!handle_)
        return false;

    // Phase 1.2: Set CPU affinity using sched_setaffinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuIndex, &cpuset);

    auto* thread = (pthread_t*)handle_;
    if (pthread_setaffinity_np(*thread, sizeof(cpu_set_t), &cpuset) != 0)
    {
        return false;
    }
    return true;
#else
    // CPU affinity not supported on this platform
    return false;
#endif // __linux__
#else
    return false;
#endif // URHO3D_THREADING
}

int Thread::GetNumCores()
{
#ifdef URHO3D_THREADING
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
    long numCores = sysconf(_SC_NPROCESSORS_ONLN);
    return (numCores > 0) ? static_cast<int>(numCores) : 1;
#else
    return 1;
#endif
#else
    return 1;
#endif // URHO3D_THREADING
}

int Thread::GetNumNumaNodes()
{
#ifdef URHO3D_THREADING
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // Try to get NUMA nodes using libnuma if available
    // For now, return 1 (single node) as fallback
    // Full NUMA support would require linking against libnuma
    return 1;
#else
    return 1;
#endif
#else
    return 1;
#endif // URHO3D_THREADING
}

int Thread::GetCoreNumaNode(int coreIndex)
{
#ifdef URHO3D_THREADING
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    // Would require libnuma for actual implementation
    // For now, return -1 (unknown)
    return -1;
#else
    return -1;
#endif
#else
    return -1;
#endif // URHO3D_THREADING
}

}
