// YukiEars — Speech-to-text bridge.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Captures audio from the mic via Urho3D Audio subsystem,
// feeds PCM to a speech recognition model, delivers text
// to Yuki's brain. The microphone driver, not the brain.
//
// Currently uses whisper.cpp via llama.cpp's ggml backend.
// Replaceable — the interface is: audio in, text out.

#pragma once

#include "../Core/Object.h"
#include "../Core/Timer.h"

namespace Urho3D
{

/// Speech-to-text interface. Listens to the mic, produces text.
class URHO3D_API YukiEars : public Object
{
    URHO3D_OBJECT(YukiEars, Object);

public:
    explicit YukiEars(Context* context);
    ~YukiEars() override;

    /// Start listening. Pre-opens the mic for warm start.
    bool Start(i32 sampleRate = 16000);
    /// Stop listening.
    void Stop();

    /// Begin recording (call when user starts speaking).
    void BeginListening();
    /// Stop recording and transcribe. Returns the transcribed text.
    /// Blocking — waits for transcription to complete.
    String EndListeningAndTranscribe();

    /// Poll: check if the mic is active.
    bool IsListening() const { return listening_; }
    /// Poll: check if ears are ready (model loaded, mic available).
    bool IsReady() const { return ready_; }

    /// Get the last transcription result.
    const String& GetLastTranscription() const { return lastText_; }

private:
    /// Drain captured audio into the accumulation buffer.
    void DrainMic();

    bool ready_{};
    bool listening_{};
    i32 sampleRate_{16000};

    /// Accumulated PCM samples while listening.
    Vector<i16> audioBuffer_;
    /// Drain scratch buffer.
    Vector<i16> drainBuffer_;

    /// Last transcription result.
    String lastText_;
};

}
