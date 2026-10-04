// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "Sample.h"

namespace Urho3D
{
class Text;
}

/// Async scene loading crash test dummy.
/// Tests LoadAsyncXML on the Vulkan backend.
class AsyncLoading : public Sample
{
    URHO3D_OBJECT(AsyncLoading, Sample);

public:
    explicit AsyncLoading(Context* context);
    void Start() override;

private:
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandleAsyncLoadProgress(StringHash eventType, VariantMap& eventData);
    void HandleAsyncLoadFinished(StringHash eventType, VariantMap& eventData);

    void StartAsyncLoad();
    void SetStatusText(const String& text);

    SharedPtr<Text> statusText_;
    SharedPtr<Scene> loadScene_;
    bool loadStarted_{false};
    bool loadFinished_{false};
    float waitTimer_{0.0f};
};
