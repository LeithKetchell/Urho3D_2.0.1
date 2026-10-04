// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

namespace Urho3D
{

class Node;
class Scene;

}

/// Deferred Rendering Example
/// This sample demonstrates:
///     - Setting up a 3D scene with multiple dynamic lights
///     - Configuring the deferred rendering pipeline via render paths
///     - Creating and managing light volumes for deferred lighting
///     - Comparing forward vs deferred rendering performance with many lights
///     - Toggling between different render paths at runtime
class DeferredRendering : public Sample
{
    URHO3D_OBJECT(DeferredRendering, Sample);

public:
    /// Construct.
    explicit DeferredRendering(Context* context);

    /// Setup after engine initialization and before running the main loop.
    void Start() override;

private:
    /// Construct the scene content.
    void CreateScene();
    /// Construct an instruction text to the UI.
    void CreateInstructions();
    /// Set up a viewport for displaying the scene.
    void SetupViewport();
    /// Create multiple point lights in a grid pattern.
    void CreateLights();
    /// Read input and move the camera.
    void MoveCamera(float timeStep);
    /// Handle toggling between render paths.
    void HandleRenderPathToggle();
    /// Subscribe to application-wide logic update events.
    void SubscribeToEvents();
    /// Handle the logic update event.
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    /// Handle key down events.
    void HandleKeyDown(StringHash eventType, VariantMap& eventData);

    /// Current render path type (0 = Forward, 1 = Deferred, 2 = PBR Deferred).
    int currentRenderPath_;
    /// UI text showing current render path.
    SharedPtr<Text> renderPathText_;
    /// Number of lights in the scene.
    static const int NUM_LIGHTS = 16;
    SharedPtr<ProfilerUI> profilerUI_;
};
