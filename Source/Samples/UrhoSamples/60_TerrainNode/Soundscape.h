// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/Audio/Sound.h>
#include <Urho3D/Audio/SoundSource.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Scene/LogicComponent.h>

using namespace Urho3D;

/// Single ambient layer with driven volume.
struct AmbientLayer
{
    String name;
    SharedPtr<Sound> sound;
    SharedPtr<SoundSource> source;
    float targetGain{0.0f};     // driven by game state
    float currentGain{0.0f};    // smoothed toward target
    float maxGain{0.5f};        // layer's maximum volume
    float fadeSpeed{2.0f};      // gain units per second for crossfade
};

/// Ambient soundscape manager — crossfades ambient layers based on game state.
class Soundscape : public LogicComponent
{
    URHO3D_OBJECT(Soundscape, LogicComponent);

public:
    explicit Soundscape(Context* context);
    static void RegisterObject(Context* context);

    void Start() override;
    void Update(float timeStep) override;

    /// Set sun altitude (0.0 = horizon, 1.0 = zenith, negative = below horizon).
    /// Drives day/night layer crossfade.
    void SetSunAltitude(float altitude) { sunAltitude_ = altitude; }
    float GetSunAltitude() const { return sunAltitude_; }

    /// Set precipitation intensity (0.0 = none, 1.0 = heavy). Drives rain audio layers.
    void SetPrecipitation(float intensity) { precipitation_ = Clamp(intensity, 0.0f, 1.0f); }
    float GetPrecipitation() const { return precipitation_; }

    /// Set wind speed (0.0 = calm, 1.0 = gale). Drives wind/storm audio.
    void SetWindSpeed(float speed) { windSpeed_ = Clamp(speed, 0.0f, 1.0f); }
    float GetWindSpeed() const { return windSpeed_; }

    /// Set cloud cover (0.0 = clear, 1.0 = overcast). Muffles birds, amplifies wind.
    void SetCloudCover(float cover) { cloudCover_ = Clamp(cover, 0.0f, 1.0f); }
    float GetCloudCover() const { return cloudCover_; }

    /// Master volume multiplier (0-1). Allows player to mute ambient without touching individual layers.
    void SetMasterGain(float gain) { masterGain_ = Clamp(gain, 0.0f, 1.0f); }
    float GetMasterGain() const { return masterGain_; }

private:
    void CreateLayer(AmbientLayer& layer, const String& name,
                     const String& soundPath, float maxGain, float fadeSpeed);
    void SmoothGain(AmbientLayer& layer, float timeStep);

    // Day/night layers
    AmbientLayer windLayer_;
    AmbientLayer birdsLayer_;
    AmbientLayer cricketsLayer_;

    // Weather layers
    AmbientLayer rainLightLayer_;
    AmbientLayer rainHeavyLayer_;
    AmbientLayer windStormLayer_;

    // Night music
    Vector<String> nightMusicPaths_;
    SharedPtr<Sound> nightMusic_;
    SharedPtr<SoundSource> nightMusicSource_;
    int currentNightTrack_{-1};
    bool nightMusicPlaying_{false};

    void ScanNightMusic();
    void StartNightMusic();
    void StopNightMusic();

    float sunAltitude_{0.5f};
    float precipitation_{0.0f};
    float windSpeed_{0.0f};
    float cloudCover_{0.0f};
    float masterGain_{1.0f};
};
