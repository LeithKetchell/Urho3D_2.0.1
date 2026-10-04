// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/Engine/Application.h>
#include <Urho3D/Input/Input.h>

namespace Urho3D
{

class Node;
class Scene;
class Sprite;
class Text;

}

// All Urho3D classes reside in namespace Urho3D
using namespace Urho3D;

const float TOUCH_SENSITIVITY = 2.0f;

/// Camera modes available in all samples.
enum CameraMode
{
    CAM_GOD,          ///< Free-fly WASD + mouse look (default)
    CAM_CHASE,        ///< Follow cameraTarget_ node, mouse orbits
    CAM_FIRSTPERSON,  ///< Locked to cameraTarget_ position, mouse look
    CAM_FREEFLIGHT,   ///< Like god mode but Q/E unlocks roll
    CAM_COUNT
};

/// Sample class, as framework for all samples.
///    - Initialization of the Urho3D engine (in Application class)
///    - Modify engine parameters for windowed mode and to show the class name as title
///    - Create Urho3D logo at screen
///    - Set custom window title and icon
///    - Create Console and Debug HUD, and use F1 and F2 key to toggle them
///    - Toggle rendering options from the keys 1-8
///    - Take screenshot with key 9
///    - Handle Esc key down to hide Console or exit application
///    - Init touch input on mobile platform using screen joysticks (patched for each individual sample)
class Sample : public Application
{
    // Enable type information.
    URHO3D_OBJECT(Sample, Application);

public:
    /// Construct.
    explicit Sample(Context* context);

    /// Setup before engine initialization. Modifies the engine parameters.
    void Setup() override;
    /// Setup after engine initialization. Creates the logo, console & debug HUD.
    void Start() override;
    /// Cleanup after the main loop. Called by Application.
    void Stop() override;

protected:
    /// Return XML patch instructions for screen joystick layout for a specific sample app, if any.
    virtual String GetScreenJoystickPatchString() const { return String::EMPTY; }
    /// Called when ESC is pressed. Return true if handled (e.g. closed a panel).
    virtual bool OnEscapePressed() { return false; }
    /// Move camera with WASD + mouse look. Override for custom camera controls.
    virtual void MoveCamera(float timeStep);
    /// Spawn a physics object from the camera position. Override to customize projectile.
    virtual void SpewForthObject();
    /// Create on-screen instruction text. Override to customize.
    virtual void CreateInstructions(const String& text = String::EMPTY);
    /// Initialize touch input on mobile platform.
    void InitTouchInput();
    /// Initialize mouse mode on non-web platform.
    void InitMouseMode(MouseMode mode);
    /// Control logo visibility.
    void SetLogoVisible(bool enable);
    /// Set the node that chase/first-person cameras will follow.
    void SetCameraTarget(Node* target) { cameraTarget_ = SharedPtr<Node>(target); }
    /// Set camera mode directly.
    void SetCameraMode(CameraMode mode) { cameraMode_ = mode; }
    /// Set chase camera distance from target.
    void SetChaseDistance(float distance) { cameraDistance_ = distance; }
    /// Set chase camera height offset.
    void SetChaseHeight(float height) { cameraHeight_ = height; }
    /// Set a node for the camera to focus on (smooth transition).
    void SetCameraSubject(Node* subject);
    /// Release subject focus, return to previous view (smooth transition).
    void ReleaseCameraSubject();
    /// Set interpolation duration in seconds.
    void SetCameraInterpDuration(float seconds) { cameraInterpDuration_ = seconds; }

    /// Flag for drawing physics debug geometry.
    bool drawDebug_{false};

    /// Logo sprite.
    SharedPtr<Sprite> logoSprite_;
    /// Scene.
    SharedPtr<Scene> scene_;
    /// Camera scene node.
    SharedPtr<Node> cameraNode_;
    /// Camera yaw angle.
    float yaw_;
    /// Camera pitch angle.
    float pitch_;
    /// Camera roll angle (free flight only).
    float roll_;
    /// Current camera mode.
    CameraMode cameraMode_;
    /// Target node for chase/first-person camera.
    SharedPtr<Node> cameraTarget_;
    /// Chase camera distance from target.
    float cameraDistance_;
    /// Chase camera height offset.
    float cameraHeight_;
    /// True while camera is blending between states.
    bool cameraInterpolating_;
    /// Elapsed time in current blend.
    float cameraInterpTime_;
    /// Total blend duration in seconds.
    float cameraInterpDuration_;
    /// Position at blend start.
    Vector3 cameraInterpStartPos_;
    /// Rotation at blend start.
    Quaternion cameraInterpStartRot_;
    /// Target position for blend.
    Vector3 cameraInterpEndPos_;
    /// Target rotation for blend.
    Quaternion cameraInterpEndRot_;
    /// Node the camera is focused on (subject of interest).
    SharedPtr<Node> cameraSubject_;
    /// Saved position before subject focus.
    Vector3 cameraPreSubjectPos_;
    /// Saved rotation before subject focus.
    Quaternion cameraPreSubjectRot_;
    /// True while focused on a subject.
    bool cameraSubjectActive_;
    /// Instruction text element (for dynamic updates).
    SharedPtr<Text> instructionText_;
    /// Flag to indicate whether touch input has been enabled.
    bool touchEnabled_;
    /// Mouse mode option to use in the sample.
    MouseMode useMouseMode_;

private:
    /// Create logo.
    void CreateLogo();
    /// Set custom window Title & Icon
    void SetWindowTitleAndIcon();
    /// Create console and debug HUD.
    void CreateConsoleAndDebugHud();
    /// Handle request for mouse mode on web platform.
    void HandleMouseModeRequest(StringHash eventType, VariantMap& eventData);
    /// Handle request for mouse mode change on web platform.
    void HandleMouseModeChange(StringHash eventType, VariantMap& eventData);
    /// Handle key down event to process key controls common to all samples.
    void HandleKeyDown(StringHash eventType, VariantMap& eventData);
    /// Handle key up event to process key controls common to all samples.
    void HandleKeyUp(StringHash eventType, VariantMap& eventData);
    /// Handle scene update event to control camera's pitch and yaw for all samples.
    void HandleSceneUpdate(StringHash eventType, VariantMap& eventData);
    /// Handle touch begin event to initialize touch input on desktop platform.
    void HandleTouchBegin(StringHash eventType, VariantMap& eventData);
    /// Begin a camera interpolation from current state to target.
    void BeginCameraInterpolation(const Vector3& targetPos, const Quaternion& targetRot);
    /// Update the camera mode indicator in instruction text.
    void UpdateCameraModeText();

    /// Screen joystick index for navigational controls (mobile platforms only).
    unsigned screenJoystickIndex_;
    /// Screen joystick index for settings (mobile platforms only).
    unsigned screenJoystickSettingsIndex_;
    /// Pause flag.
    bool paused_;
};

#include "Sample.inl"
