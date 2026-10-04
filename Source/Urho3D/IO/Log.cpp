// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Container/Str.h"
#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/ProcessUtils.h"
#include "../Core/Thread.h"
#include "../Core/Timer.h"
#include "../IO/File.h"
#include "../IO/IOEvents.h"
#include "../IO/Log.h"

#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#endif
#if defined(IOS) || defined(TVOS)
extern "C" void SDL_IOS_LogMessage(const char* message);
#endif

#include "../DebugNew.h"

namespace Urho3D
{

const char* logLevelPrefixes[] =
{
    "TRACE",
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR",
    nullptr
};

static Log* logInstance = nullptr;
static bool threadErrorDisplayed = false;

Log::Log(Context* context) :
    Object(context),
#ifdef _DEBUG
    level_(LOG_DEBUG),
#else
    level_(LOG_INFO),
#endif
    timeStamp_(true),
    inWrite_(false),
    quiet_(false)
{
    logInstance = this;

    SubscribeToEvent(E_ENDFRAME, URHO3D_HANDLER(Log, HandleEndFrame));
}

Log::~Log()
{
    logInstance = nullptr;
}

void Log::Open(const String& fileName)
{
#if !defined(__ANDROID__) && !defined(IOS) && !defined(TVOS)
    if (fileName.Empty())
        return;
    if (logFile_ && logFile_->IsOpen())
    {
        if (logFile_->GetName() == fileName)
            return;
        else
            Close();
    }

    logFile_ = new File(context_);
    if (logFile_->Open(fileName, FILE_WRITE))
        Write(LOG_INFO, "Opened log file " + fileName);
    else
    {
        logFile_.Reset();
        Write(LOG_ERROR, "Failed to create log file " + fileName);
    }
#endif
}

void Log::Close()
{
#if !defined(__ANDROID__) && !defined(IOS) && !defined(TVOS)
    if (logFile_ && logFile_->IsOpen())
    {
        logFile_->Close();
        logFile_.Reset();
    }
#endif
}

void Log::SetLevel(int level)
{
    if (level < LOG_TRACE || level > LOG_NONE)
    {
        URHO3D_LOGERRORF("Attempted to set erroneous log level %d", level);
        return;
    }

    level_ = level;
}

void Log::SetTimeStamp(bool enable)
{
    timeStamp_ = enable;
}

void Log::SetQuiet(bool quiet)
{
    quiet_ = quiet;
}

void Log::WriteFormat(int level, const char* format, ...)
{
    if (!logInstance)
        return;

    if (level != LOG_RAW)
    {
        // No-op if illegal level
        if (level < LOG_TRACE || level >= LOG_NONE || logInstance->level_ > level)
            return;
    }

    // Forward to normal Write() after formatting the input
    String message;
    va_list args;
    va_start(args, format);
    message.AppendWithFormatArgs(format, args);
    va_end(args);

    Write(level, message);
}

void Log::Write(int level, const String& message)
{
    // Special case for LOG_RAW level
    if (level == LOG_RAW)
    {
        WriteRaw(message, false);
        return;
    }

    // No-op if illegal level, or the level is excluded by the current filter
    if (level < LOG_TRACE || level >= LOG_NONE)
        return;
    if (!logInstance || logInstance->level_ > level)
        return;

    // EVERY thread — including main — only enqueues here. The main-loop-tail Drain()
    // is the SOLE writer to the sinks, so output can never interleave. Capture the
    // timestamp NOW (call time) so drained lines keep their true order and time
    // instead of being stamped when the queue is later flushed.
    String timeStamp = logInstance->timeStamp_ ? Time::GetTimeStamp() : String::EMPTY;
    MutexLock lock(logInstance->logMutex_);
    logInstance->threadMessages_.Push(StoredLogMessage(message, level, false, timeStamp));
}

void Log::WriteRaw(const String& message, bool error)
{
    if (!logInstance)
        return;

    // Enqueue like everything else; emitted verbatim (no prefix/timestamp) by Drain().
    MutexLock lock(logInstance->logMutex_);
    logInstance->threadMessages_.Push(StoredLogMessage(message, LOG_RAW, error, String::EMPTY));
}

void Log::EmitStored(const StoredLogMessage& stored)
{
    // Main thread only (invoked from Drain). This is the single point that touches
    // the sinks, so writes are inherently serialized and whole-message.
    lastMessage_ = stored.message_;

    if (stored.level_ == LOG_RAW)
    {
#if defined(__ANDROID__)
        if (quiet_)
        {
            if (stored.error_)
                __android_log_print(ANDROID_LOG_ERROR, "Urho3D", "%s", stored.message_.CString());
        }
        else
            __android_log_print(stored.error_ ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "Urho3D", "%s", stored.message_.CString());
#elif defined(IOS) || defined(TVOS)
        SDL_IOS_LogMessage(stored.message_.CString());
#else
        if (quiet_)
        {
            if (stored.error_)
                PrintUnicode(stored.message_, true);
        }
        else
            PrintUnicode(stored.message_, stored.error_);
#endif
        if (logFile_)
        {
            logFile_->Write(stored.message_.CString(), stored.message_.Length());
            logFile_->Flush();
        }

        inWrite_ = true;
        using namespace LogMessage;
        VariantMap& eventData = GetEventDataMap();
        eventData[P_MESSAGE] = stored.message_;
        eventData[P_LEVEL] = stored.error_ ? LOG_ERROR : LOG_INFO;
        SendEvent(E_LOGMESSAGE, eventData);
        inWrite_ = false;
        return;
    }

    String formattedMessage = logLevelPrefixes[stored.level_];
    formattedMessage += ": " + stored.message_;

    if (!stored.timeStamp_.Empty())
        formattedMessage = "[" + stored.timeStamp_ + "] " + formattedMessage;

#if defined(__ANDROID__)
    int androidLevel = ANDROID_LOG_VERBOSE + stored.level_;
    __android_log_print(androidLevel, "Urho3D", "%s", stored.message_.CString());
#elif defined(IOS) || defined(TVOS)
    SDL_IOS_LogMessage(stored.message_.CString());
#else
    if (quiet_)
    {
        // If in quiet mode, still print the error message to the standard error stream
        if (stored.level_ == LOG_ERROR)
            PrintUnicodeLine(formattedMessage, true);
    }
    else
        PrintUnicodeLine(formattedMessage, stored.level_ == LOG_ERROR);
#endif

    if (logFile_)
    {
        logFile_->WriteLine(formattedMessage);
        logFile_->Flush();
    }

    inWrite_ = true;
    using namespace LogMessage;
    VariantMap& eventData = GetEventDataMap();
    eventData[P_MESSAGE] = formattedMessage;
    eventData[P_LEVEL] = stored.level_;
    SendEvent(E_LOGMESSAGE, eventData);
    inWrite_ = false;
}

void Log::Drain()
{
    if (!logInstance)
        return;

    // The drain MUST run on the main thread — it is the single writer. If the main
    // thread id was never set up, threaded handling can't be trusted; bail loudly once.
    if (!Thread::IsMainThread())
    {
        if (!threadErrorDisplayed)
        {
            fprintf(stderr, "Thread::mainThreadID is not setup correctly! Threaded log handling disabled\n");
            threadErrorDisplayed = true;
        }
        return;
    }

    // Guard against re-entrant drain (e.g. an E_LOGMESSAGE subscriber calling Drain()).
    if (logInstance->inWrite_)
        return;

    // Steal the queue under the lock (O(1) head/tail swap), then emit WITHOUT holding
    // the lock so producer threads never block on file I/O. Messages come out in the
    // exact order they were enqueued (call-time order) and are written whole.
    List<StoredLogMessage> batch;
    {
        MutexLock lock(logInstance->logMutex_);
        logInstance->threadMessages_.Swap(batch);
    }

    for (List<StoredLogMessage>::ConstIterator i = batch.Begin(); i != batch.End(); ++i)
        logInstance->EmitStored(*i);
}

void Log::HandleEndFrame(StringHash eventType, VariantMap& eventData)
{
    // Drain at the tail of the main loop — the single, ordered flush of everything
    // logged this frame by any thread.
    Drain();
}

}
