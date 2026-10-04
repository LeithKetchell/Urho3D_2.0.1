// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class btPoint2PointConstraint;

namespace Urho3D
{

class Node;
class RigidBody;
class Scene;
class SoftBody;

}

/// Soft body physics example.
/// This sample demonstrates:
///     - Creating soft body cloth, rope, and ellipsoid objects
///     - Configuring soft body material properties (stiffness, damping, pressure)
///     - Soft-soft collision between ellipsoid bodies
///     - Physics picking: LMB to grab and throw rigid bodies
class SoftBodyDemo : public Sample
{
    URHO3D_OBJECT(SoftBodyDemo, Sample);

public:
    /// Construct.
    explicit SoftBodyDemo(Context* context);

    /// Setup after engine initialization and before running the main loop.
    void Start() override;

private:
    /// Construct the scene content.
    void CreateScene();
    /// Construct an instruction text to the UI.
    void CreateInstructions();
    /// Set up a viewport for displaying the scene.
    void SetupViewport();
    /// Subscribe to application-wide logic update and post-render update events.
    void SubscribeToEvents();
    /// Read input and moves the camera.
    void MoveCamera(float timeStep);
    /// Handle physics picking (grab/drag/throw).
    void HandlePicking(float timeStep);
    /// Handle the logic update event.
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    /// Handle the post-render update event.
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    /// Flag for drawing debug geometry.
    bool drawDebug_{};
    /// Profiler UI overlay.
    SharedPtr<ProfilerUI> profilerUI_;

    // Physics picking state
    /// Currently grabbed rigid body.
    WeakPtr<RigidBody> pickedBody_;
    /// Bullet constraint used for dragging.
    btPoint2PointConstraint* pickConstraint_{};
    /// Distance from camera to pick point.
    float pickDistance_{};

    // Soft body picking state
    /// Currently grabbed soft body.
    Urho3D::SoftBody* pickedSoftBody_{};
    /// Index of grabbed soft body node.
    int pickedSoftNode_{-1};
    /// Original inverse mass of grabbed node (restored on release).
    float savedInvMass_{};
};
