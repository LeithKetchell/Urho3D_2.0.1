// VoiceChat — LAN Voice Broadcast
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// WHAT:  Push-to-talk voice chat over LAN. Demonstrates Urho3D 2.0.1 audio
//        capture API: PreOpenCapture (warm start), StartCapture, DrainCaptureSamples,
//        PauseCapture. Raw 16-bit mono PCM streamed over RUDP — no compression,
//        LAN bandwidth is free.
//
// HOW:   Every instance starts a server and discovers peers via broadcast.
//        Mesh topology — no hub. Voice data sent to all peers (server connection
//        + all client connections). Received audio fed into BufferedSoundStream
//        for immediate playback.
//
// PTT:   Hold the TALK button or press 1+2+3 keys simultaneously to transmit.
//        Deliberate chord prevents accidental transmission. Release to stop.
//        Mic is pre-opened on startup to eliminate cold-start latency (~1s).
//
// PROTO: MSG_USER+100: MSG_VOICE_DATA
//        Format: [U32 sampleCount][raw PCM data (sampleCount × 2 bytes)]
//        Sent unreliable unordered — dropped frames are acceptable for voice.
//        Relay: each peer relays received voice to its other clients (mesh fan-out).
//
// AUDIO: Capture rate requested at 16kHz, SDL may override (typically 48kHz).
//        Playback stream format matches actual capture rate from GetCaptureSampleRate().
//        DrainCaptureSamples pulls 100ms chunks (1600 samples at 16kHz) per frame.
//
// DEPS:  Audio (capture + playback), Network (SLikeNet RUDP), UI, Sample base class.

#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

namespace Urho3D
{

class BufferedSoundStream;
class Button;
class SoundSource;
class Text;

}

/// Voice chat — CB radio over LAN.
/// Hold TALK or 1+2+3 to transmit. Release to stop.
/// Every instance is a peer. Mesh, not hub.
class VoiceChat : public Sample
{
    URHO3D_OBJECT(VoiceChat, Sample);

public:
    explicit VoiceChat(Context* context);
    void Start() override;

private:
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandleHostDiscovered(StringHash eventType, VariantMap& eventData);
    void HandleConnectionStatus(StringHash eventType, VariantMap& eventData);
    void HandleClientConnected(StringHash eventType, VariantMap& eventData);
    void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);

    /// Status text.
    SharedPtr<Text> statusText_;
    /// The one button.
    SharedPtr<Button> talkButton_;
    /// Profiler.
    SharedPtr<ProfilerUI> profilerUI_;
    /// Playback stream for received audio.
    SharedPtr<BufferedSoundStream> playStream_;
    /// SoundSource for playback.
    SharedPtr<SoundSource> playSoundSource_;
    /// Drain buffer.
    Vector<i16> drainBuffer_;
    /// Mic pre-opened.
    bool micPreOpened_{};
    /// Currently transmitting.
    bool transmitting_{};
    /// Has at least one peer.
    bool hasPeers_{};
    /// Retry timer.
    float retryTimer_{};
};
