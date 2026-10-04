// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "Soundscape.h"

#include <Urho3D/Audio/AudioDefs.h>
#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Math/Random.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>

Soundscape::Soundscape(Context* context) :
    LogicComponent(context)
{
    SetUpdateEventMask(LogicComponentEvents::Update);
}

void Soundscape::RegisterObject(Context* context)
{
    context->RegisterFactory<Soundscape>();
}

void Soundscape::Start()
{
    // Day/night ambient layers
    CreateLayer(windLayer_, "Wind",
        "Sounds/Ambient/Wind.ogg", 0.4f, 2.0f);
    CreateLayer(birdsLayer_, "BirdsDay",
        "Sounds/Ambient/BirdsDay.ogg", 0.3f, 3.0f);
    CreateLayer(cricketsLayer_, "Crickets",
        "Sounds/Ambient/Crickets.ogg", 0.3f, 3.0f);

    // Weather layers
    CreateLayer(rainLightLayer_, "RainLight",
        "Sounds/Ambient/RainLight.ogg", 0.5f, 2.0f);
    CreateLayer(rainHeavyLayer_, "RainHeavy",
        "Sounds/Ambient/RainHeavy.ogg", 0.6f, 2.0f);
    CreateLayer(windStormLayer_, "WindStorm",
        "Sounds/Ambient/WindStorm.ogg", 0.7f, 2.0f);

    URHO3D_LOGINFO("Soundscape: 6 ambient layers initialized (3 day/night + 3 weather)");

    ScanNightMusic();
}

void Soundscape::CreateLayer(AmbientLayer& layer, const String& name,
                             const String& soundPath, float maxGain, float fadeSpeed)
{
    auto* cache = GetSubsystem<ResourceCache>();
    layer.name = name;
    layer.maxGain = maxGain;
    layer.fadeSpeed = fadeSpeed;
    layer.sound = cache->GetResource<Sound>(soundPath);

    if (layer.sound)
    {
        layer.sound->SetLooped(true);
        layer.source = node_->CreateComponent<SoundSource>();
        layer.source->SetSoundType(SOUND_AMBIENT);
        layer.source->SetGain(0.0f);
        layer.source->Play(layer.sound);
    }
    else
        URHO3D_LOGWARNINGF("Soundscape: could not load '%s'", soundPath.CString());
}

void Soundscape::Update(float timeStep)
{
    // --- Day/night layers ---

    // Wind: always present, louder in storms
    float windBase = windLayer_.maxGain;
    float windStormBoost = Clamp(windSpeed_ * 0.3f, 0.0f, 0.3f);
    windLayer_.targetGain = windBase + windStormBoost;

    // Birds: peak at noon (sun > 0.2), silent at night. Muffled by rain/cloud.
    float cloudMuffle = 1.0f - Clamp(cloudCover_ * 0.5f, 0.0f, 0.5f);
    float rainMuffle = 1.0f - Clamp(precipitation_ * 0.8f, 0.0f, 0.8f);
    if (sunAltitude_ > 0.2f)
        birdsLayer_.targetGain = birdsLayer_.maxGain *
            Clamp((sunAltitude_ - 0.2f) / 0.6f, 0.0f, 1.0f) * cloudMuffle * rainMuffle;
    else
        birdsLayer_.targetGain = 0.0f;

    // Crickets: inverse of birds — peak at night, muffled by rain
    if (sunAltitude_ < 0.1f)
        cricketsLayer_.targetGain = cricketsLayer_.maxGain * rainMuffle;
    else if (sunAltitude_ < 0.3f)
        cricketsLayer_.targetGain = cricketsLayer_.maxGain *
            (1.0f - (sunAltitude_ - 0.1f) / 0.2f) * rainMuffle;
    else
        cricketsLayer_.targetGain = 0.0f;

    // --- Weather layers ---

    // --- Rain layers: plateau-and-overlap crossfade ---
    // Light rain: 0.0..0.5 ramp, 0.5..0.6 plateau, 0.6..1.0 fade
    {
        float t = precipitation_;
        float light;
        if (t <= 0.0f)         light = 0.0f;
        else if (t <= 0.5f)    light = t / 0.5f;              // 0..1
        else if (t <= 0.6f)    light = 1.0f;                   // plateau
        else                   light = (1.0f - t) / 0.4f;      // 1..0
        rainLightLayer_.targetGain = rainLightLayer_.maxGain * Clamp(light, 0.0f, 1.0f);
    }

    // Heavy rain: silent 0..0.4, ramp 0.4..1.0 (overlaps light's plateau)
    {
        float t = precipitation_;
        float heavy = (t < 0.4f) ? 0.0f : (t - 0.4f) / 0.6f;
        rainHeavyLayer_.targetGain = rainHeavyLayer_.maxGain * Clamp(heavy, 0.0f, 1.0f);
    }

    // Storm wind: kicks in at high wind speed
    if (windSpeed_ > 0.4f)
        windStormLayer_.targetGain = windStormLayer_.maxGain *
            Clamp((windSpeed_ - 0.4f) / 0.6f, 0.0f, 1.0f);
    else
        windStormLayer_.targetGain = 0.0f;

    // --- Smooth all layers toward target ---
    SmoothGain(windLayer_, timeStep);
    SmoothGain(birdsLayer_, timeStep);
    SmoothGain(cricketsLayer_, timeStep);
    SmoothGain(rainLightLayer_, timeStep);
    SmoothGain(rainHeavyLayer_, timeStep);
    SmoothGain(windStormLayer_, timeStep);

    // --- Night music ---
    if (!nightMusicPaths_.Empty())
    {
        bool isNight = sunAltitude_ < 0.0f;
        if (isNight && !nightMusicPlaying_)
            StartNightMusic();
        else if (!isNight && nightMusicPlaying_)
            StopNightMusic();

        // When current track finishes, play next
        if (nightMusicPlaying_ && nightMusicSource_ && !nightMusicSource_->IsPlaying())
        {
            currentNightTrack_ = (currentNightTrack_ + 1) % nightMusicPaths_.Size();
            auto* cache = GetSubsystem<ResourceCache>();
            nightMusic_ = cache->GetResource<Sound>(nightMusicPaths_[currentNightTrack_]);
            if (nightMusic_)
            {
                nightMusic_->SetLooped(false);
                nightMusicSource_->Play(nightMusic_);
            }
        }
    }
}

void Soundscape::ScanNightMusic()
{
    auto* cache = GetSubsystem<ResourceCache>();
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (unsigned d = 0; d < dirs.Size(); ++d)
    {
        String musicDir = dirs[d] + "Music/Night/";
        auto* fs = GetSubsystem<FileSystem>();
        if (!fs->DirExists(musicDir))
            continue;
        Vector<String> files;
        fs->ScanDir(files, musicDir, "*.ogg", SCAN_FILES, false);
        for (unsigned i = 0; i < files.Size(); ++i)
            nightMusicPaths_.Push("Music/Night/" + files[i]);
    }
    if (!nightMusicPaths_.Empty())
        URHO3D_LOGINFOF("Soundscape: %u night music tracks found", nightMusicPaths_.Size());
}

void Soundscape::StartNightMusic()
{
    if (nightMusicPaths_.Empty())
    {
        URHO3D_LOGWARNING("Soundscape: no night music tracks found — cannot start");
        return;
    }
    currentNightTrack_ = Rand() % nightMusicPaths_.Size();
    auto* cache = GetSubsystem<ResourceCache>();
    nightMusic_ = cache->GetResource<Sound>(nightMusicPaths_[currentNightTrack_]);
    if (nightMusic_)
    {
        nightMusic_->SetLooped(false);
        nightMusicSource_ = node_->CreateComponent<SoundSource>();
        nightMusicSource_->SetSoundType(SOUND_MUSIC);
        nightMusicSource_->SetGain(0.3f * masterGain_);
        nightMusicSource_->Play(nightMusic_);
        nightMusicPlaying_ = true;
        URHO3D_LOGINFOF("Soundscape: night music started — %s (gain %.2f)",
            nightMusicPaths_[currentNightTrack_].CString(), 0.3f * masterGain_);
    }
    else
        URHO3D_LOGWARNINGF("Soundscape: failed to load night track: %s",
            nightMusicPaths_[currentNightTrack_].CString());
}

void Soundscape::StopNightMusic()
{
    if (nightMusicSource_)
    {
        nightMusicSource_->Stop();
        nightMusicSource_->Remove();
        nightMusicSource_.Reset();
    }
    nightMusic_.Reset();
    nightMusicPlaying_ = false;
}

void Soundscape::SmoothGain(AmbientLayer& layer, float timeStep)
{
    if (layer.currentGain < layer.targetGain)
        layer.currentGain = Min(layer.currentGain + layer.fadeSpeed * timeStep,
                                layer.targetGain);
    else
        layer.currentGain = Max(layer.currentGain - layer.fadeSpeed * timeStep,
                                layer.targetGain);

    if (layer.source)
        layer.source->SetGain(layer.currentGain * masterGain_);
}
