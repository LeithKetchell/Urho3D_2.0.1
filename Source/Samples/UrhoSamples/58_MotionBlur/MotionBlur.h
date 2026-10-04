// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

namespace Urho3D
{

class Node;
class Scene;
class Text;

}

/// Post-processing showcase with motion blur, bloom, and FXAA.
/// This sample demonstrates:
///     - Camera-based motion blur via post-process
///     - Bloom, FXAA post-process effects
///     - Three light types (directional, point, spot) all moving with shadows
///     - Moving objects in the scene
///     - Toggling post-process effects at runtime
class MotionBlur : public Sample
{
    URHO3D_OBJECT(MotionBlur, Sample);

public:
    /// Construct.
    explicit MotionBlur(Context* context);

    /// Setup after engine initialization and before running the main loop.
    void Start() override;

private:
    /// Construct the scene content.
    void CreateScene();
    /// Construct an instruction text to the UI.
    void CreateInstructions();
    /// Set up a viewport with post-processing effects.
    void SetupViewport();
    /// Read input and moves the camera.
    void MoveCamera(float timeStep);
    /// Animate scene objects and lights.
    void AnimateScene(float timeStep);
    /// Update the instruction text with current effect state.
    void UpdateInstructions();
    /// Subscribe to application-wide logic update events.
    void SubscribeToEvents();
    /// Handle the logic update event.
    void HandleUpdate(StringHash eventType, VariantMap& eventData);

    /// Motion blur enabled flag.
    bool motionBlurEnabled_{true};
    /// Bloom enabled flag.
    bool bloomEnabled_{true};
    /// FXAA enabled flag.
    bool fxaaEnabled_{false};
    /// Motion blur strength.
    float motionBlurStrength_{1.0f};
    /// Instruction text element.
    SharedPtr<Text> instructionText_;
    /// Profiler UI overlay.
    SharedPtr<ProfilerUI> profilerUI_;
    /// Directional light node.
    SharedPtr<Node> dirLightNode_;
    /// Point light nodes.
    Vector<SharedPtr<Node>> pointLightNodes_;
    /// Spot light node.
    SharedPtr<Node> spotLightNode_;
    /// Orbiting object nodes.
    Vector<SharedPtr<Node>> orbitingNodes_;
    /// Scene elapsed time.
    float sceneTime_{0.0f};
    /// Previous frame's view-projection matrix for motion blur.
    Matrix4 prevViewProj_;
    /// Whether prevViewProj_ has been initialized.
    bool prevViewProjValid_{false};
};
