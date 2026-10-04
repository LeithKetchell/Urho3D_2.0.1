// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Audio/AudioDefs.h"
#include "../Container/ArrayPtr.h"
#include "../Container/HashSet.h"
#include "../Core/Mutex.h"
#include "../Core/Object.h"
#include "../Core/Timer.h"

namespace Urho3D
{

class AudioImpl;
class Sound;
class SoundListener;
class SoundSource;

/// %Audio subsystem.
class URHO3D_API Audio : public Object
{
    URHO3D_OBJECT(Audio, Object);

public:
    /// Construct.
    explicit Audio(Context* context);
    /// Destruct. Terminate the audio thread and free the audio buffer.
    ~Audio() override;

    /// Initialize sound output with specified buffer length and output mode.
    bool SetMode(i32 bufferLengthMSec, i32 mixRate, bool stereo, bool interpolation = true);
    /// Run update on sound sources. Not required for continued playback, but frees unused sound sources & sounds and updates 3D positions.
    void Update(float timeStep);
    /// Restart sound output.
    bool Play();
    /// Suspend sound output.
    void Stop();
    /// Set master gain on a specific sound type such as sound effects, music or voice.
    /// @property
    void SetMasterGain(const String& type, float gain);
    /// Pause playback of specific sound type. This allows to suspend e.g. sound effects or voice when the game is paused. By default all sound types are unpaused.
    void PauseSoundType(const String& type);
    /// Resume playback of specific sound type.
    void ResumeSoundType(const String& type);
    /// Resume playback of all sound types.
    void ResumeAll();
    /// Set active sound listener for 3D sounds.
    /// @property
    void SetListener(SoundListener* listener);
    /// Stop any sound source playing a certain sound clip.
    void StopSound(Sound* sound);

    /// Return the number of available audio input (capture) devices.
    unsigned GetNumCaptureDevices() const;
    /// Return the name of an audio input device by index.
    String GetCaptureDeviceName(unsigned index) const;
    /// Open and warm up a capture device. Device starts UNPAUSED with hardware codec hot;
    /// callback discards samples until StartCapture flips recording on. Eliminates the
    /// SDL-unpause -> codec-wake latency (~1s on consumer audio chipsets).
    bool PreOpenCapture(const String& deviceName = String::EMPTY, i32 sampleRate = 44100);
    /// Begin recording on a pre-opened device (no SDL state change, just flag flip — first
    /// non-zero callback delivers real mic data). Or open+start if not pre-opened (will
    /// incur cold-start latency).
    bool StartCapture(const String& deviceName = String::EMPTY, i32 sampleRate = 44100);
    /// Pause SDL device and stop accumulating samples. Use only when done with the recording
    /// session — re-recording incurs warm-up latency (PreOpen+StartCapture is the hot path).
    void PauseCapture();
    /// Stop capturing and close the input device (captured data is kept).
    void StopCapture();
    /// Write the captured audio to a WAV file.
    bool SaveCapture(const String& path);
    /// Return whether audio input is active (device open, recording or paused).
    bool IsCapturing() const { return captureDeviceID_ != 0; }
    /// Return whether actively recording (not just pre-opened/paused).
    bool IsRecording() const { return captureDeviceID_ != 0 && captureRecording_; }
    /// Discard captured audio data.
    void ClearCapture();
    /// Return the number of captured samples.
    unsigned GetCaptureSampleCount() const;
    /// Return capture duration in seconds.
    float GetCaptureDuration() const;
    /// Drain up to maxSamples from the capture buffer into dest. Returns the number of samples actually copied. Thread-safe.
    unsigned DrainCaptureSamples(i16* dest, unsigned maxSamples);
    /// Return the capture sample rate (actual rate obtained from the device).
    i32 GetCaptureSampleRate() const { return captureSampleRate_; }
    /// Return the name of the active capture device (empty if not capturing).
    const String& GetCaptureDeviceName() const { return captureDeviceName_; }

    /// Return byte size of one sample.
    /// @property
    u32 GetSampleSize() const { return sampleSize_; }

    /// Return mixing rate.
    /// @property
    i32 GetMixRate() const { return mixRate_; }

    /// Return whether output is interpolated.
    /// @property
    bool GetInterpolation() const { return interpolation_; }

    /// Return whether output is stereo.
    /// @property
    bool IsStereo() const { return stereo_; }

    /// Return whether audio is being output.
    /// @property
    bool IsPlaying() const { return playing_; }

    /// Return whether an audio stream has been reserved.
    /// @property
    bool IsInitialized() const { return deviceID_ != 0; }

    /// Return master gain for a specific sound source type. Unknown sound types will return full gain (1).
    /// @property
    float GetMasterGain(const String& type) const;

    /// Return whether specific sound type has been paused.
    bool IsSoundTypePaused(const String& type) const;

    /// Return active sound listener.
    /// @property
    SoundListener* GetListener() const;

    /// Return all sound sources.
    const Vector<SoundSource*>& GetSoundSources() const { return soundSources_; }

    /// Return whether the specified master gain has been defined.
    bool HasMasterGain(const String& type) const { return masterGain_.Contains(type); }

    /// Add a sound source to keep track of. Called by SoundSource.
    void AddSoundSource(SoundSource* soundSource);
    /// Remove a sound source. Called by SoundSource.
    void RemoveSoundSource(SoundSource* soundSource);

    /// Return audio thread mutex.
    Mutex& GetMutex() { return audioMutex_; }

    /// Return sound type specific gain multiplied by master gain.
    float GetSoundSourceMasterGain(StringHash typeHash) const;

    /// Mix sound sources into the buffer.
    void MixOutput(void* dest, u32 samples);

private:
    /// Handle render update event.
    void HandleRenderUpdate(StringHash eventType, VariantMap& eventData);
    /// Stop sound output and release the sound buffer.
    void Release();
    /// Actually update sound sources with the specific timestep. Called internally.
    void UpdateInternal(float timeStep);
    /// SDL audio capture callback.
    static void SDLCaptureCallback(void* userdata, unsigned char* stream, int len);

    /// Clipping buffer for mixing.
    SharedArrayPtr<i32> clipBuffer_;
    /// Audio thread mutex.
    Mutex audioMutex_;
    /// SDL audio device ID.
    u32 deviceID_{};
    /// Sample size.
    u32 sampleSize_{};
    /// Clip buffer size in samples.
    u32 fragmentSize_{};
    /// Mixing rate.
    i32 mixRate_{};
    /// Mixing interpolation flag.
    bool interpolation_{};
    /// Stereo flag.
    bool stereo_{};
    /// Playing flag.
    bool playing_{};
    /// Master gain by sound source type.
    HashMap<StringHash, Variant> masterGain_;
    /// Paused sound types.
    HashSet<StringHash> pausedSoundTypes_;
    /// Sound sources.
    Vector<SoundSource*> soundSources_;
    /// Sound listener.
    WeakPtr<SoundListener> listener_;
    /// SDL capture device ID.
    u32 captureDeviceID_{};
    /// Capture sample rate.
    i32 captureSampleRate_{};
    /// Name of the active capture device.
    String captureDeviceName_;
    /// Accumulated capture buffer (mono 16-bit PCM).
    Vector<i16> captureBuffer_;
    /// Mutex for capture buffer access.
    Mutex captureMutex_;
    /// True when actively recording (not just pre-opened/paused).
    bool captureRecording_{};
    /// Timestamp when recording started (microseconds, hires timer).
    HiresTimer captureStartTimer_;
    /// Elapsed microseconds at recording stop.
    long long captureStopUSec_{};
};

/// Register Audio library objects.
/// @nobind
void URHO3D_API RegisterAudioLibrary(Context* context);

}
