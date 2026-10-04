// YukiEars — Speech-to-text bridge.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../ML/YukiEars.h"
#include "../Audio/Audio.h"
#include "../IO/Log.h"
#include "../Core/Context.h"

#include "../DebugNew.h"

namespace Urho3D
{

static const unsigned DRAIN_CHUNK = 1600;  // 100ms at 16kHz

YukiEars::YukiEars(Context* context) :
    Object(context)
{
}

YukiEars::~YukiEars()
{
    Stop();
}

bool YukiEars::Start(i32 sampleRate)
{
    sampleRate_ = sampleRate;

    auto* audio = GetSubsystem<Audio>();
    if (!audio)
    {
        URHO3D_LOGERROR("YukiEars: Audio subsystem not available");
        return false;
    }

    unsigned numDevices = audio->GetNumCaptureDevices();
    if (numDevices == 0)
    {
        URHO3D_LOGWARNING("YukiEars: No capture devices found");
        return false;
    }

    String devName = audio->GetCaptureDeviceName(0);
    if (!audio->PreOpenCapture(devName, sampleRate))
    {
        URHO3D_LOGERROR("YukiEars: Failed to pre-open mic: " + devName);
        return false;
    }

    drainBuffer_.Resize(DRAIN_CHUNK);
    ready_ = true;

    URHO3D_LOGINFO("YukiEars: Ready — mic: " + devName + " @ " +
        String(audio->GetCaptureSampleRate()) + " Hz");
    return true;
}

void YukiEars::Stop()
{
    if (listening_)
    {
        auto* audio = GetSubsystem<Audio>();
        if (audio)
            audio->StopCapture();
        listening_ = false;
    }
    ready_ = false;
}

void YukiEars::BeginListening()
{
    if (!ready_ || listening_)
        return;

    audioBuffer_.Clear();

    auto* audio = GetSubsystem<Audio>();
    if (audio && audio->StartCapture(String::EMPTY, sampleRate_))
    {
        listening_ = true;
        URHO3D_LOGINFO("YukiEars: Listening...");
    }
}

String YukiEars::EndListeningAndTranscribe()
{
    if (!listening_)
        return String::EMPTY;

    // Drain any remaining audio
    DrainMic();

    auto* audio = GetSubsystem<Audio>();
    if (audio)
    {
        audio->PauseCapture();
        audio->ClearCapture();
    }
    listening_ = false;

    if (audioBuffer_.Empty())
    {
        URHO3D_LOGINFO("YukiEars: No audio captured");
        return String::EMPTY;
    }

    URHO3D_LOGINFOF("YukiEars: Captured %u samples (%.1fs)",
        audioBuffer_.Size(),
        (float)audioBuffer_.Size() / (float)sampleRate_);

    // TODO: Feed audioBuffer_ to whisper model for transcription.
    // For now, this is the interface point — audio in, text out.
    // The whisper model integration goes here when the model file is available.
    //
    // Pipeline:
    //   1. Convert i16 PCM to float32 normalized [-1, 1]
    //   2. Resample to 16kHz if needed (whisper expects 16kHz)
    //   3. Compute mel spectrogram (mtmd_audio_preprocessor_whisper)
    //   4. Run whisper encoder → decoder
    //   5. Decode tokens to text
    //
    // The audio data is ready. The model is the missing piece.

    lastText_ = "[audio: " + String(audioBuffer_.Size()) + " samples, awaiting whisper model]";

    URHO3D_LOGINFO("YukiEars: " + lastText_);
    return lastText_;
}

void YukiEars::DrainMic()
{
    auto* audio = GetSubsystem<Audio>();
    if (!audio)
        return;

    for (;;)
    {
        unsigned drained = audio->DrainCaptureSamples(drainBuffer_.Buffer(), DRAIN_CHUNK);
        if (drained == 0)
            break;

        unsigned oldSize = audioBuffer_.Size();
        audioBuffer_.Resize(oldSize + drained);
        memcpy(audioBuffer_.Buffer() + oldSize, drainBuffer_.Buffer(), drained * sizeof(i16));
    }
}

}
