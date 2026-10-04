// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Audio/Audio.h"
#include "../Audio/Sound.h"
#include "../Audio/SoundListener.h"
#include "../Audio/SoundSource3D.h"
#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/ProcessUtils.h"
#include "../Core/Profiler.h"
#include "../IO/File.h"
#include "../IO/Log.h"

#include <SDL/SDL.h>

#include "../DebugNew.h"

namespace Urho3D
{

const char* AUDIO_CATEGORY = "Audio";

static const i32 MIN_BUFFERLENGTH = 20;
static const i32 MIN_MIXRATE = 11025;
static const i32 MAX_MIXRATE = 48000;
static const StringHash SOUND_MASTER_HASH("Master");

static void SDLAudioCallback(void* userdata, Uint8* stream, i32 len);

Audio::Audio(Context* context) :
    Object(context)
{
    context_->RequireSDL(SDL_INIT_AUDIO);

    // Set the master to the default value
    masterGain_[SOUND_MASTER_HASH] = 1.0f;

    // Register Audio library object factories
    RegisterAudioLibrary(context_);

    SubscribeToEvent(E_RENDERUPDATE, URHO3D_HANDLER(Audio, HandleRenderUpdate));
}

Audio::~Audio()
{
    StopCapture();
    Release();
    context_->ReleaseSDL();
}

bool Audio::SetMode(i32 bufferLengthMSec, i32 mixRate, bool stereo, bool interpolation)
{
    Release();

    bufferLengthMSec = Max(bufferLengthMSec, MIN_BUFFERLENGTH);
    mixRate = Clamp(mixRate, MIN_MIXRATE, MAX_MIXRATE);

    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;

    desired.freq = mixRate;

    desired.format = AUDIO_S16;
    desired.callback = SDLAudioCallback;
    desired.userdata = this;

    // SDL uses power of two audio fragments. Determine the closest match
    i32 bufferSamples = mixRate * bufferLengthMSec / 1000;
    desired.samples = (Uint16)NextPowerOfTwo((u32)bufferSamples);
    if (Abs((i32)desired.samples / 2 - bufferSamples) < Abs((i32)desired.samples - bufferSamples))
        desired.samples /= 2;

    // Intentionally disallow format change so that the obtained format will always be the desired format, even though that format
    // is not matching the device format, however in doing it will enable the SDL's internal audio stream with audio conversion.
    // Also disallow channels change to avoid issues on multichannel audio device (5.1, 7.1, etc)
    i32 allowedChanges = SDL_AUDIO_ALLOW_ANY_CHANGE & ~SDL_AUDIO_ALLOW_FORMAT_CHANGE & ~SDL_AUDIO_ALLOW_CHANNELS_CHANGE;

    if (stereo)
    {
        desired.channels = 2;
        deviceID_ = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &desired, &obtained, allowedChanges);
    }

    // If stereo requested but not available then fall back into mono
    if (!stereo || !deviceID_)
    {
        desired.channels = 1;
        deviceID_ = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &desired, &obtained, allowedChanges);

        if (!deviceID_)
        {
            URHO3D_LOGERROR("Could not initialize audio output");
            return false;
        }
    }

    if (obtained.format != AUDIO_S16)
    {
        URHO3D_LOGERROR("Could not initialize audio output, 16-bit buffer format not supported");
        SDL_CloseAudioDevice(deviceID_);
        deviceID_ = 0;
        return false;
    }

    stereo_ = obtained.channels == 2;
    sampleSize_ = (u32)(stereo_ ? sizeof(i32) : sizeof(i16));
    // Guarantee a fragment size that is low enough so that Vorbis decoding buffers do not wrap
    fragmentSize_ = Min(NextPowerOfTwo((u32)mixRate >> 6u), (u32)obtained.samples);
    mixRate_ = obtained.freq;
    interpolation_ = interpolation;
    clipBuffer_ = new i32[stereo ? fragmentSize_ << 1u : fragmentSize_];

    URHO3D_LOGINFO("Set audio mode " + String(mixRate_) + " Hz " + (stereo_ ? "stereo" : "mono") + (interpolation_ ? " interpolated" : ""));

    return Play();
}

void Audio::Update(float timeStep)
{
    if (!playing_)
        return;

    UpdateInternal(timeStep);
}

bool Audio::Play()
{
    if (playing_)
        return true;

    if (!deviceID_)
    {
        URHO3D_LOGERROR("No audio mode set, can not start playback");
        return false;
    }

    SDL_PauseAudioDevice(deviceID_, 0);

    // Update sound sources before resuming playback to make sure 3D positions are up to date
    UpdateInternal(0.0f);

    playing_ = true;
    return true;
}

void Audio::Stop()
{
    playing_ = false;
}

void Audio::SetMasterGain(const String& type, float gain)
{
    masterGain_[type] = Clamp(gain, 0.0f, 1.0f);

    for (Vector<SoundSource*>::Iterator i = soundSources_.Begin(); i != soundSources_.End(); ++i)
        (*i)->UpdateMasterGain();
}

void Audio::PauseSoundType(const String& type)
{
    MutexLock lock(audioMutex_);
    pausedSoundTypes_.Insert(type);
}

void Audio::ResumeSoundType(const String& type)
{
    MutexLock lock(audioMutex_);
    pausedSoundTypes_.Erase(type);
    // Update sound sources before resuming playback to make sure 3D positions are up to date
    // Done under mutex to ensure no mixing happens before we are ready
    UpdateInternal(0.0f);
}

void Audio::ResumeAll()
{
    MutexLock lock(audioMutex_);
    pausedSoundTypes_.Clear();
    UpdateInternal(0.0f);
}

void Audio::SetListener(SoundListener* listener)
{
    listener_ = listener;
}

void Audio::StopSound(Sound* sound)
{
    for (Vector<SoundSource*>::Iterator i = soundSources_.Begin(); i != soundSources_.End(); ++i)
    {
        if ((*i)->GetSound() == sound)
            (*i)->Stop();
    }
}

float Audio::GetMasterGain(const String& type) const
{
    // By definition previously unknown types return full volume
    HashMap<StringHash, Variant>::ConstIterator findIt = masterGain_.Find(type);
    if (findIt == masterGain_.End())
        return 1.0f;

    return findIt->second_.GetFloat();
}

bool Audio::IsSoundTypePaused(const String& type) const
{
    return pausedSoundTypes_.Contains(type);
}

SoundListener* Audio::GetListener() const
{
    return listener_;
}

void Audio::AddSoundSource(SoundSource* soundSource)
{
    MutexLock lock(audioMutex_);
    soundSources_.Push(soundSource);
}

void Audio::RemoveSoundSource(SoundSource* soundSource)
{
    Vector<SoundSource*>::Iterator i = soundSources_.Find(soundSource);
    if (i != soundSources_.End())
    {
        MutexLock lock(audioMutex_);
        soundSources_.Erase(i);
    }
}

float Audio::GetSoundSourceMasterGain(StringHash typeHash) const
{
    HashMap<StringHash, Variant>::ConstIterator masterIt = masterGain_.Find(SOUND_MASTER_HASH);

    if (!typeHash)
        return masterIt->second_.GetFloat();

    HashMap<StringHash, Variant>::ConstIterator typeIt = masterGain_.Find(typeHash);

    if (typeIt == masterGain_.End() || typeIt == masterIt)
        return masterIt->second_.GetFloat();

    return masterIt->second_.GetFloat() * typeIt->second_.GetFloat();
}

void SDLAudioCallback(void* userdata, Uint8* stream, i32 len)
{
    auto* audio = static_cast<Audio*>(userdata);
    {
        MutexLock Lock(audio->GetMutex());
        audio->MixOutput(stream, len / audio->GetSampleSize());
    }
}

void Audio::MixOutput(void* dest, u32 samples)
{
    if (!playing_ || !clipBuffer_)
    {
        memset(dest, 0, samples * (size_t)sampleSize_);
        return;
    }

    while (samples)
    {
        // If sample count exceeds the fragment (clip buffer) size, split the work
        u32 workSamples = Min(samples, fragmentSize_);
        u32 clipSamples = workSamples;
        if (stereo_)
            clipSamples <<= 1;

        // Clear clip buffer
        i32* clipPtr = clipBuffer_.Get();
        memset(clipPtr, 0, clipSamples * sizeof(i32));

        // Mix samples to clip buffer
        for (Vector<SoundSource*>::Iterator i = soundSources_.Begin(); i != soundSources_.End(); ++i)
        {
            SoundSource* source = *i;

            // Check for pause if necessary
            if (!pausedSoundTypes_.Empty())
            {
                if (pausedSoundTypes_.Contains(source->GetSoundType()))
                    continue;
            }

            source->Mix(clipPtr, workSamples, mixRate_, stereo_, interpolation_);
        }
        // Copy output from clip buffer to destination
        auto* destPtr = (short*)dest;
        while (clipSamples--)
            *destPtr++ = (short)Clamp(*clipPtr++, -32768, 32767);
        samples -= workSamples;
        ((u8*&)dest) += sampleSize_ * workSamples;
    }
}

void Audio::HandleRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace RenderUpdate;

    Update(eventData[P_TIMESTEP].GetFloat());
}

void Audio::Release()
{
    Stop();

    if (deviceID_)
    {
        SDL_CloseAudioDevice(deviceID_);
        deviceID_ = 0;
        clipBuffer_.Reset();
    }
}

void Audio::UpdateInternal(float timeStep)
{
    URHO3D_PROFILE(UpdateAudio);

    // Update in reverse order, because sound sources might remove themselves
    for (i32 i = soundSources_.Size() - 1; i >= 0; --i)
    {
        SoundSource* source = soundSources_[i];

        // Check for pause if necessary; do not update paused sound sources
        if (!pausedSoundTypes_.Empty())
        {
            if (pausedSoundTypes_.Contains(source->GetSoundType()))
                continue;
        }

        source->Update(timeStep);
    }
}

// ============================================================================
// Audio Capture
// ============================================================================

void Audio::SDLCaptureCallback(void* userdata, unsigned char* stream, int len)
{
    auto* audio = static_cast<Audio*>(userdata);

    // Don't accumulate data when not recording
    if (!audio->captureRecording_)
        return;

    int sampleCount = len / (int)sizeof(i16);
    const i16* samples = reinterpret_cast<const i16*>(stream);

    MutexLock lock(audio->captureMutex_);
    unsigned oldSize = audio->captureBuffer_.Size();
    audio->captureBuffer_.Resize(oldSize + sampleCount);
    memcpy(&audio->captureBuffer_[oldSize], samples, sampleCount * sizeof(i16));
}

unsigned Audio::GetNumCaptureDevices() const
{
    return (unsigned)SDL_GetNumAudioDevices(SDL_TRUE);
}

String Audio::GetCaptureDeviceName(unsigned index) const
{
    const char* name = SDL_GetAudioDeviceName((int)index, SDL_TRUE);
    return name ? String(name) : String::EMPTY;
}

bool Audio::PreOpenCapture(const String& deviceName, i32 sampleRate)
{
    if (captureDeviceID_)
        return true;  // Already open

    sampleRate = Clamp(sampleRate, MIN_MIXRATE, MAX_MIXRATE);

    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    memset(&desired, 0, sizeof(desired));

    desired.freq = sampleRate;
    desired.format = AUDIO_S16;
    desired.channels = 1;
    desired.samples = 256;
    desired.callback = SDLCaptureCallback;
    desired.userdata = this;

    const char* sdlDeviceName = deviceName.Empty() ? nullptr : deviceName.CString();

    captureDeviceID_ = SDL_OpenAudioDevice(sdlDeviceName, SDL_TRUE, &desired, &obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);

    if (!captureDeviceID_)
    {
        URHO3D_LOGERRORF("Could not pre-open audio capture device '%s': %s",
            deviceName.Empty() ? "default" : deviceName.CString(), SDL_GetError());
        return false;
    }

    captureSampleRate_ = obtained.freq;
    captureDeviceName_ = deviceName.Empty()
        ? String(SDL_GetAudioDeviceName(0, SDL_TRUE))
        : deviceName;
    captureRecording_ = false;

    // Device starts UNPAUSED so the hardware codec stays hot and the OS audio
    // backend (PipeWire/Pulse/CoreAudio/WASAPI) keeps the capture stream live.
    // Opening alone is not enough — the underlying stream is only spun up on
    // unpause, and that's where the ~1s warm-up latency lives.
    // The callback discards data while !captureRecording_ (see SDLCaptureCallback
    // gate near the top of this file), so unpaused-but-not-recording is free.
    SDL_PauseAudioDevice(captureDeviceID_, 0);

    URHO3D_LOGINFOF("Audio capture pre-opened: '%s' at %d Hz mono (device hot, ready to record)",
        captureDeviceName_.CString(), captureSampleRate_);
    return true;
}

bool Audio::StartCapture(const String& deviceName, i32 sampleRate)
{
    // If not pre-opened, open now
    if (!captureDeviceID_)
    {
        if (!PreOpenCapture(deviceName, sampleRate))
            return false;
    }

    // Clear our buffer and reset state. Unpause the device in case PauseCapture
    // was called previously — PreOpen only unpauses on first open.
    {
        MutexLock lock(captureMutex_);
        captureBuffer_.Clear();
    }

    SDL_PauseAudioDevice(captureDeviceID_, 0);
    captureRecording_ = true;
    captureStartTimer_.Reset();
    captureStopUSec_ = 0;

    URHO3D_LOGINFOF("Audio capture recording started: '%s' at %d Hz mono",
        captureDeviceName_.CString(), captureSampleRate_);
    return true;
}

void Audio::PauseCapture()
{
    if (!captureDeviceID_)
        return;

    captureRecording_ = false;
    // Pause SDL device so its internal ring buffer doesn't fill with stale data.
    // StartCapture will unpause fresh — device stays open, no close/reopen needed.
    SDL_PauseAudioDevice(captureDeviceID_, 1);
    URHO3D_LOGINFO("Audio capture paused (device open, paused)");
}

void Audio::StopCapture()
{
    if (!captureDeviceID_)
        return;

    captureStopUSec_ = captureStartTimer_.GetUSec(false);

    SDL_PauseAudioDevice(captureDeviceID_, 1);
    SDL_CloseAudioDevice(captureDeviceID_);
    captureDeviceID_ = 0;
    captureDeviceName_.Clear();
    captureRecording_ = false;

    MutexLock lock(captureMutex_);
    URHO3D_LOGINFOF("Audio capture stopped: %u samples (%.2fs), recorded duration %.2fs",
        captureBuffer_.Size(),
        captureSampleRate_ > 0 ? (float)captureBuffer_.Size() / captureSampleRate_ : 0.0f,
        captureStopUSec_ / 1000000.0f);
}

bool Audio::SaveCapture(const String& path)
{
    MutexLock lock(captureMutex_);

    if (captureBuffer_.Empty())
    {
        URHO3D_LOGWARNING("No captured audio to save");
        return false;
    }

    File file(context_, path, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERRORF("Could not open file for writing: %s", path.CString());
        return false;
    }

    // The capture buffer may contain leading zeros from SDL's internal buffer pre-fill.
    // Use the recorded timestamps to determine the true duration, then take that many
    // samples from the END of the buffer — the end is correct, the start has dead silence.
    unsigned totalSamples = captureBuffer_.Size();
    unsigned trueSamples = totalSamples;

    if (captureStopUSec_ > 0 && captureSampleRate_ > 0)
    {
        double trueDuration = captureStopUSec_ / 1000000.0;
        trueSamples = (unsigned)(trueDuration * captureSampleRate_);
        if (trueSamples > totalSamples)
            trueSamples = totalSamples;

        unsigned trimmed = totalSamples - trueSamples;
        if (trimmed > 0)
            URHO3D_LOGINFOF("Audio capture: trimming %u leading samples (%.3fs dead silence)",
                trimmed, (float)trimmed / captureSampleRate_);
    }

    const i16* writeStart = captureBuffer_.Buffer() + (totalSamples - trueSamples);
    u32 dataSize = trueSamples * sizeof(i16);
    u32 fileSize = 36 + dataSize;
    i16 bitsPerSample = 16;
    i16 channels = 1;
    i32 byteRate = captureSampleRate_ * channels * bitsPerSample / 8;
    i16 blockAlign = channels * bitsPerSample / 8;

    // RIFF header
    file.Write("RIFF", 4);
    file.WriteU32(fileSize);
    file.Write("WAVE", 4);

    // fmt chunk
    file.Write("fmt ", 4);
    file.WriteU32(16);             // chunk size
    file.WriteU16(1);              // PCM format
    file.WriteU16(channels);
    file.WriteU32(captureSampleRate_);
    file.WriteU32(byteRate);
    file.WriteU16(blockAlign);
    file.WriteU16(bitsPerSample);

    // data chunk
    file.Write("data", 4);
    file.WriteU32(dataSize);
    file.Write(writeStart, dataSize);

    URHO3D_LOGINFOF("Saved %u samples to %s (%.2fs, buffer had %u)",
        trueSamples, path.CString(),
        (float)trueSamples / captureSampleRate_, totalSamples);
    return true;
}

void Audio::ClearCapture()
{
    MutexLock lock(captureMutex_);
    captureBuffer_.Clear();
}

unsigned Audio::GetCaptureSampleCount() const
{
    return captureBuffer_.Size();
}

float Audio::GetCaptureDuration() const
{
    if (captureSampleRate_ <= 0)
        return 0.0f;
    return (float)captureBuffer_.Size() / captureSampleRate_;
}

unsigned Audio::DrainCaptureSamples(i16* dest, unsigned maxSamples)
{
    MutexLock lock(captureMutex_);

    unsigned available = captureBuffer_.Size();
    unsigned toCopy = Min(maxSamples, available);
    if (toCopy == 0)
        return 0;

    memcpy(dest, captureBuffer_.Buffer(), toCopy * sizeof(i16));

    // Erase consumed samples from the front
    if (toCopy == available)
        captureBuffer_.Clear();
    else
        captureBuffer_.Erase(0, toCopy);

    return toCopy;
}

void RegisterAudioLibrary(Context* context)
{
    Sound::RegisterObject(context);
    SoundSource::RegisterObject(context);
    SoundSource3D::RegisterObject(context);
    SoundListener::RegisterObject(context);
}

}
