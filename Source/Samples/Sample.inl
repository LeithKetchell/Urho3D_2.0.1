// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include <Urho3D/Core/Timer.h>
#include <Urho3D/Engine/Application.h>
#include <Urho3D/Engine/Console.h>
#include <Urho3D/Engine/DebugHud.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/Drawable.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/UI/Cursor.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Sprite.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

Sample::Sample(Context* context) :
    Application(context),
    yaw_(0.0f),
    pitch_(0.0f),
    roll_(0.0f),
    cameraMode_(CAM_GOD),
    cameraDistance_(10.0f),
    cameraHeight_(3.0f),
    cameraInterpolating_(false),
    cameraInterpTime_(0.0f),
    cameraInterpDuration_(0.5f),
    cameraSubjectActive_(false),
    touchEnabled_(false),
    useMouseMode_(MM_ABSOLUTE),
    screenJoystickIndex_(M_MAX_UNSIGNED),
    screenJoystickSettingsIndex_(M_MAX_UNSIGNED),
    paused_(false)
{
}

void Sample::Setup()
{
    // Modify engine startup parameters
    engineParameters_[EP_WINDOW_TITLE] = GetTypeName();
    engineParameters_[EP_LOG_NAME]     = GetSubsystem<FileSystem>()->GetAppPreferencesDir("urho3d", "logs") + GetTypeName() + ".log";
    engineParameters_[EP_FULL_SCREEN]  = false;
    engineParameters_[EP_HEADLESS]     = false;
    engineParameters_[EP_SOUND]        = true;
    engineParameters_[EP_WINDOW_RESIZABLE] = true;

    // Construct a search path to find the resource prefix with two entries:
    // The first entry is an empty path which will be substituted with program/bin directory -- this entry is for binary when it is still in build tree
    // The second and third entries are possible relative paths from the installed program/bin directory to the asset directory -- these entries are for binary when it is in the Urho3D SDK installation location
    if (!engineParameters_.Contains(EP_RESOURCE_PREFIX_PATHS))
        engineParameters_[EP_RESOURCE_PREFIX_PATHS] = ";../share/Resources;../share/Urho3D/Resources";
}

void Sample::Start()
{
    if (GetPlatform() == "Android" || GetPlatform() == "iOS")
        // On mobile platform, enable touch by adding a screen joystick
        InitTouchInput();
    else if (GetSubsystem<Input>()->GetNumJoysticks() == 0)
        // On desktop platform, do not detect touch when we already got a joystick
        SubscribeToEvent(E_TOUCHBEGIN, URHO3D_HANDLER(Sample, HandleTouchBegin));

    // Create logo
    CreateLogo();

    // Set custom window Title & Icon
    SetWindowTitleAndIcon();

    // Create console and debug HUD
    CreateConsoleAndDebugHud();

    // Subscribe key down event
    SubscribeToEvent(E_KEYDOWN, URHO3D_HANDLER(Sample, HandleKeyDown));
    // Subscribe key up event
    SubscribeToEvent(E_KEYUP, URHO3D_HANDLER(Sample, HandleKeyUp));
    // Subscribe scene update event
    SubscribeToEvent(E_SCENEUPDATE, URHO3D_HANDLER(Sample, HandleSceneUpdate));
}

void Sample::Stop()
{
    engine_->DumpResources(true);
}

void Sample::InitTouchInput()
{
    touchEnabled_ = true;

    ResourceCache* cache = GetSubsystem<ResourceCache>();
    Input* input = GetSubsystem<Input>();
    XMLFile* layout = cache->GetResource<XMLFile>("UI/ScreenJoystick_Samples.xml");
    const String& patchString = GetScreenJoystickPatchString();
    if (!patchString.Empty())
    {
        // Patch the screen joystick layout further on demand
        SharedPtr<XMLFile> patchFile(new XMLFile(context_));
        if (patchFile->FromString(patchString))
            layout->Patch(patchFile);
    }
    screenJoystickIndex_ = (unsigned)input->AddScreenJoystick(layout, cache->GetResource<XMLFile>("UI/DefaultStyle.xml"));
    input->SetScreenJoystickVisible(screenJoystickSettingsIndex_, true);
}

void Sample::InitMouseMode(MouseMode mode)
{
    useMouseMode_ = mode;

    Input* input = GetSubsystem<Input>();

    if (GetPlatform() != "Web")
    {
        if (useMouseMode_ == MM_FREE)
            input->SetMouseVisible(true);

        Console* console = GetSubsystem<Console>();
        if (useMouseMode_ != MM_ABSOLUTE)
        {
            input->SetMouseMode(useMouseMode_);
            if (console && console->IsVisible())
                input->SetMouseMode(MM_ABSOLUTE, true);
        }
    }
    else
    {
        input->SetMouseVisible(true);
        SubscribeToEvent(E_MOUSEBUTTONDOWN, URHO3D_HANDLER(Sample, HandleMouseModeRequest));
        SubscribeToEvent(E_MOUSEMODECHANGED, URHO3D_HANDLER(Sample, HandleMouseModeChange));
    }
}

void Sample::SetLogoVisible(bool enable)
{
    if (logoSprite_)
        logoSprite_->SetVisible(enable);
}

void Sample::CreateLogo()
{
    // Get logo texture
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    Texture2D* logoTexture = cache->GetResource<Texture2D>("Textures/FishBoneLogo.png");
    if (!logoTexture)
        return;

    // Create logo sprite and add to the UI layout
    UI* ui = GetSubsystem<UI>();
    logoSprite_ = ui->GetRoot()->CreateChild<Sprite>();

    // Set logo sprite texture
    logoSprite_->SetTexture(logoTexture);

    int textureWidth = logoTexture->GetWidth();
    int textureHeight = logoTexture->GetHeight();

    // Set logo sprite scale
    logoSprite_->SetScale(256.0f / textureWidth);

    // Set logo sprite size
    logoSprite_->SetSize(textureWidth, textureHeight);

    // Set logo sprite hot spot
    logoSprite_->SetHotSpot(textureWidth, textureHeight);

    // Set logo sprite alignment
    logoSprite_->SetAlignment(HA_RIGHT, VA_BOTTOM);

    // Make logo not fully opaque to show the scene underneath
    logoSprite_->SetOpacity(0.9f);

    // Set a low priority for the logo so that other UI elements can be drawn on top
    logoSprite_->SetPriority(-100);
}

void Sample::SetWindowTitleAndIcon()
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    Graphics* graphics = GetSubsystem<Graphics>();
    Image* icon = cache->GetResource<Image>("Textures/UrhoIcon.png");
    graphics->SetWindowIcon(icon);
    graphics->SetWindowTitle("Urho3D Sample");
}

void Sample::CreateConsoleAndDebugHud()
{
    // Get default style
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    XMLFile* xmlFile = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

    // Create console
    Console* console = engine_->CreateConsole();
    console->SetDefaultStyle(xmlFile);
    console->GetBackground()->SetOpacity(0.8f);

    // Create debug HUD.
    DebugHud* debugHud = engine_->CreateDebugHud();
    debugHud->SetDefaultStyle(xmlFile);
}


void Sample::HandleKeyUp(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace KeyUp;

    int key = eventData[P_KEY].GetI32();

    // Close console (if open), peel editor layers, or exit when ESC is pressed
    if (key == KEY_ESCAPE)
    {
        Console* console = GetSubsystem<Console>();
        if (console->IsVisible())
            console->SetVisible(false);
        else if (OnEscapePressed())
        {
            // Derived class handled it (closed a panel, changed mode, etc.)
        }
        else
        {
            if (GetPlatform() == "Web")
            {
                GetSubsystem<Input>()->SetMouseVisible(true);
                if (useMouseMode_ != MM_ABSOLUTE)
                    GetSubsystem<Input>()->SetMouseMode(MM_FREE);
            }
            else
                engine_->Exit();
        }
    }
}

void Sample::HandleKeyDown(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace KeyDown;

    int key = eventData[P_KEY].GetI32();

    // Toggle console with F1
    if (key == KEY_F1)
        GetSubsystem<Console>()->Toggle();

    // Toggle debug HUD with F2
    else if (key == KEY_F2)
        GetSubsystem<DebugHud>()->ToggleAll();

    // Common rendering quality controls, only when UI has no focused element
    else if (!GetSubsystem<UI>()->GetFocusElement())
    {
        Renderer* renderer = GetSubsystem<Renderer>();

        // Preferences / Pause
        if (key == KEY_SELECT && touchEnabled_)
        {
            paused_ = !paused_;

            Input* input = GetSubsystem<Input>();
            if (screenJoystickSettingsIndex_ == M_MAX_UNSIGNED)
            {
                // Lazy initialization
                ResourceCache* cache = GetSubsystem<ResourceCache>();
                screenJoystickSettingsIndex_ = (unsigned)input->AddScreenJoystick(cache->GetResource<XMLFile>("UI/ScreenJoystickSettings_Samples.xml"), cache->GetResource<XMLFile>("UI/DefaultStyle.xml"));
            }
            else
                input->SetScreenJoystickVisible(screenJoystickSettingsIndex_, paused_);
        }

        // Texture quality
        else if (key == '1')
        {
            auto quality = (unsigned)renderer->GetTextureQuality();
            ++quality;
            if (quality > QUALITY_HIGH)
                quality = QUALITY_LOW;
            renderer->SetTextureQuality((MaterialQuality)quality);
        }

        // Material quality
        else if (key == '2')
        {
            auto quality = (unsigned)renderer->GetMaterialQuality();
            ++quality;
            if (quality > QUALITY_HIGH)
                quality = QUALITY_LOW;
            renderer->SetMaterialQuality((MaterialQuality)quality);
        }

        // Specular lighting
        else if (key == '3')
            renderer->SetSpecularLighting(!renderer->GetSpecularLighting());

        // Shadow rendering
        else if (key == '4')
            renderer->SetDrawShadows(!renderer->GetDrawShadows());

        // Shadow map resolution
        else if (key == '5')
        {
            int shadowMapSize = renderer->GetShadowMapSize();
            shadowMapSize *= 2;
            if (shadowMapSize > 2048)
                shadowMapSize = 512;
            renderer->SetShadowMapSize(shadowMapSize);
        }

        // Shadow depth and filtering quality
        else if (key == '6')
        {
            ShadowQuality quality = renderer->GetShadowQuality();
            quality = (ShadowQuality)(quality + 1);
            if (quality > SHADOWQUALITY_BLUR_VSM)
                quality = SHADOWQUALITY_SIMPLE_16BIT;
            renderer->SetShadowQuality(quality);
        }

        // Occlusion culling
        else if (key == '7')
        {
            bool occlusion = renderer->GetMaxOccluderTriangles() > 0;
            occlusion = !occlusion;
            renderer->SetMaxOccluderTriangles(occlusion ? 5000 : 0);
        }

        // Instancing
        else if (key == '8')
            renderer->SetDynamicInstancing(!renderer->GetDynamicInstancing());

        // Cycle camera mode with C
        else if (key == KEY_C)
        {
            // Auto-select closest renderable node if no target set
            if (!cameraTarget_ && scene_ && cameraNode_)
            {
                Vector3 camPos = cameraNode_->GetWorldPosition();
                float bestDist = M_INFINITY;
                Node* bestNode = nullptr;
                Vector<Node*> children;
                scene_->GetChildren(children, true);
                for (unsigned i = 0; i < children.Size(); ++i)
                {
                    Node* child = children[i];
                    if (child == cameraNode_)
                        continue;
                    if (!child->GetComponent<StaticModel>() && !child->GetComponent<AnimatedModel>())
                        continue;
                    float dist = (child->GetWorldPosition() - camPos).Length();
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        bestNode = child;
                    }
                }
                if (bestNode)
                    cameraTarget_ = SharedPtr<Node>(bestNode);
            }

            CameraMode next = (CameraMode)((cameraMode_ + 1) % CAM_COUNT);
            // Skip FirstPerson unless target has an AnimatedModel (has a head to look from)
            bool hasAnimated = cameraTarget_ && cameraTarget_->GetComponent<AnimatedModel>();
            if (next == CAM_FIRSTPERSON && !hasAnimated)
                next = (CameraMode)((next + 1) % CAM_COUNT);
            cameraMode_ = next;
            roll_ = 0.0f;

            if (cameraNode_)
            {
                Vector3 targetPos = cameraNode_->GetPosition();
                Quaternion targetRot(pitch_, yaw_, 0.0f);

                if (next == CAM_CHASE && cameraTarget_)
                {
                    Vector3 tgtWorld = cameraTarget_->GetWorldPosition();
                    Quaternion orbit(pitch_, yaw_, 0.0f);
                    targetPos = tgtWorld + orbit * Vector3(0.0f, cameraHeight_, -cameraDistance_);
                    Vector3 dir = (tgtWorld - targetPos).Normalized();
                    targetRot = Quaternion(Vector3::FORWARD, dir);
                }
                else if (next == CAM_FIRSTPERSON && cameraTarget_)
                {
                    targetPos = cameraTarget_->GetWorldPosition();
                }

                BeginCameraInterpolation(targetPos, targetRot);
            }

            const char* modeNames[] = { "God", "Chase", "First Person", "Free Flight" };
            URHO3D_LOGINFOF("Camera mode: %s", modeNames[cameraMode_]);
            UpdateCameraModeText();
        }

        // Take screenshot
        else if (key == '9')
        {
            Graphics* graphics = GetSubsystem<Graphics>();
            Image screenshot(context_);
            graphics->TakeScreenShot(screenshot);
            // Here we save in the Data folder with date and time appended
            screenshot.SavePNG(GetSubsystem<FileSystem>()->GetProgramDir() + "Data/Screenshot_" +
                Time::GetTimeStamp().Replaced(':', '_').Replaced('.', '_').Replaced(' ', '_') + ".png");
        }
    }
}

void Sample::HandleSceneUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    // Move the camera by touch, if the camera node is initialized by descendant sample class
    if (touchEnabled_ && cameraNode_)
    {
        Input* input = GetSubsystem<Input>();
        for (i32 i = 0; i < input->GetNumTouches(); ++i)
        {
            TouchState* state = input->GetTouch(i);
            if (!state->touchedElement_)    // Touch on empty space
            {
                if (state->delta_.x_ ||state->delta_.y_)
                {
                    Camera* camera = cameraNode_->GetComponent<Camera>();
                    if (!camera)
                        return;

                    Graphics* graphics = GetSubsystem<Graphics>();
                    yaw_ += TOUCH_SENSITIVITY * camera->GetFov() / graphics->GetHeight() * state->delta_.x_;
                    pitch_ += TOUCH_SENSITIVITY * camera->GetFov() / graphics->GetHeight() * state->delta_.y_;

                    // Construct new orientation for the camera scene node from yaw and pitch; roll is fixed to zero
                    cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
                }
                else
                {
                    // Move the cursor to the touch position
                    Cursor* cursor = GetSubsystem<UI>()->GetCursor();
                    if (cursor && cursor->IsVisible())
                        cursor->SetPosition(state->position_);
                }
            }
        }
    }
}

void Sample::HandleTouchBegin(StringHash /*eventType*/, VariantMap& eventData)
{
    // On some platforms like Windows the presence of touch input can only be detected dynamically
    InitTouchInput();
    UnsubscribeFromEvent("TouchBegin");
}

// If the user clicks the canvas, attempt to switch to relative mouse mode on web platform
void Sample::HandleMouseModeRequest(StringHash /*eventType*/, VariantMap& eventData)
{
    Console* console = GetSubsystem<Console>();
    if (console && console->IsVisible())
        return;
    Input* input = GetSubsystem<Input>();
    if (useMouseMode_ == MM_ABSOLUTE)
        input->SetMouseVisible(false);
    else if (useMouseMode_ == MM_FREE)
        input->SetMouseVisible(true);
    input->SetMouseMode(useMouseMode_);
}

void Sample::HandleMouseModeChange(StringHash /*eventType*/, VariantMap& eventData)
{
    Input* input = GetSubsystem<Input>();
    bool mouseLocked = eventData[MouseModeChanged::P_MOUSELOCKED].GetBool();
    input->SetMouseVisible(!mouseLocked);
}

void Sample::MoveCamera(float timeStep)
{
    if (GetSubsystem<UI>()->GetFocusElement())
        return;

    // Handle camera interpolation (mode transitions, subject focus)
    if (cameraInterpolating_)
    {
        cameraInterpTime_ += timeStep;
        float t = Clamp(cameraInterpTime_ / cameraInterpDuration_, 0.0f, 1.0f);
        cameraNode_->SetPosition(cameraInterpStartPos_.Lerp(cameraInterpEndPos_, t));
        cameraNode_->SetRotation(cameraInterpStartRot_.Slerp(cameraInterpEndRot_, t));
        if (t >= 1.0f)
            cameraInterpolating_ = false;
        return;
    }

    // While focused on a subject, hold LookAt and skip normal mode controls
    if (cameraSubjectActive_ && cameraSubject_)
    {
        cameraNode_->LookAt(cameraSubject_->GetWorldPosition());
        return;
    }

    auto* input = GetSubsystem<Input>();

    const float MOVE_SPEED = 20.0f;
    const float MOUSE_SENSITIVITY = 0.1f;

    IntVector2 mouseMove = input->GetMouseMove();
    yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
    pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
    pitch_ = Clamp(pitch_, -90.0f, 90.0f);

    switch (cameraMode_)
    {
    case CAM_GOD:
    default:
        cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
        if (input->GetKeyDown(KEY_W))
            cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
        if (input->GetKeyDown(KEY_S))
            cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
        if (input->GetKeyDown(KEY_A))
            cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
        if (input->GetKeyDown(KEY_D))
            cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);
        break;

    case CAM_CHASE:
        if (cameraTarget_)
        {
            Vector3 targetPos = cameraTarget_->GetWorldPosition();
            Quaternion orbit(pitch_, yaw_, 0.0f);
            Vector3 offset = orbit * Vector3(0.0f, cameraHeight_, -cameraDistance_);
            Vector3 desiredPos = targetPos + offset;
            Vector3 currentPos = cameraNode_->GetPosition();
            cameraNode_->SetPosition(currentPos.Lerp(desiredPos, Clamp(8.0f * timeStep, 0.0f, 1.0f)));
            cameraNode_->LookAt(targetPos);
        }
        break;

    case CAM_FIRSTPERSON:
        if (cameraTarget_)
        {
            cameraNode_->SetPosition(cameraTarget_->GetWorldPosition());
            cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
        }
        break;

    case CAM_FREEFLIGHT:
        if (input->GetKeyDown(KEY_Q))
            roll_ -= 60.0f * timeStep;
        if (input->GetKeyDown(KEY_E))
            roll_ += 60.0f * timeStep;
        cameraNode_->SetRotation(Quaternion(pitch_, yaw_, roll_));
        if (input->GetKeyDown(KEY_W))
            cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
        if (input->GetKeyDown(KEY_S))
            cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
        if (input->GetKeyDown(KEY_A))
            cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
        if (input->GetKeyDown(KEY_D))
            cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);
        break;
    }

    if (input->GetMouseButtonPress(MOUSEB_LEFT))
        SpewForthObject();

    if (input->GetKeyPress(KEY_SPACE))
        drawDebug_ = !drawDebug_;
}

void Sample::SpewForthObject()
{
    if (!scene_ || !cameraNode_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();

    Node* boxNode = scene_->CreateChild("SmallBox");
    boxNode->SetPosition(cameraNode_->GetPosition());
    boxNode->SetRotation(cameraNode_->GetRotation());
    boxNode->SetScale(0.25f);
    auto* boxObject = boxNode->CreateComponent<StaticModel>();
    boxObject->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    boxObject->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    boxObject->SetCastShadows(true);

    auto* body = boxNode->CreateComponent<RigidBody>();
    body->SetMass(0.25f);
    body->SetFriction(0.75f);
    body->SetBoxShape(Vector3::ONE);

    const float OBJECT_VELOCITY = 10.0f;
    body->SetLinearVelocity(cameraNode_->GetRotation() * Vector3(0.0f, 0.25f, 1.0f) * OBJECT_VELOCITY);
}

void Sample::BeginCameraInterpolation(const Vector3& targetPos, const Quaternion& targetRot)
{
    if (!cameraNode_)
        return;

    // Always start from current actual camera state (handles mid-transition gracefully)
    cameraInterpStartPos_ = cameraNode_->GetPosition();
    cameraInterpStartRot_ = cameraNode_->GetRotation();
    cameraInterpEndPos_ = targetPos;
    cameraInterpEndRot_ = targetRot;
    cameraInterpTime_ = 0.0f;
    cameraInterpolating_ = true;
}

void Sample::SetCameraSubject(Node* subject)
{
    if (!cameraNode_ || !subject)
        return;

    // Save current state for return (use actual current transform, not stale saved values)
    cameraPreSubjectPos_ = cameraNode_->GetPosition();
    cameraPreSubjectRot_ = cameraNode_->GetRotation();

    // Compute rotation to look at the subject from current position
    Vector3 dir = (subject->GetWorldPosition() - cameraNode_->GetPosition()).Normalized();
    Quaternion lookAtRot(Vector3::FORWARD, dir);

    cameraSubject_ = SharedPtr<Node>(subject);
    cameraSubjectActive_ = true;

    // Interpolate rotation toward subject (position stays)
    BeginCameraInterpolation(cameraNode_->GetPosition(), lookAtRot);
}

void Sample::ReleaseCameraSubject()
{
    if (!cameraSubjectActive_)
        return;

    // Interpolate back to saved state from current actual position
    BeginCameraInterpolation(cameraPreSubjectPos_, cameraPreSubjectRot_);

    cameraSubjectActive_ = false;
    cameraSubject_ = nullptr;
}

void Sample::CreateInstructions(const String& text)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();

    String instructions = text;
    if (instructions.Empty())
        instructions = "Use WASD keys and mouse to move\n"
                       "LMB to spawn physics objects\n"
                       "Space to toggle debug geometry";

    instructions += "\nC to cycle camera: ";
    const char* modeNames[] = { "God", "Chase", "1stPerson", "FreeFlight" };
    for (int i = 0; i < CAM_COUNT; ++i)
    {
        if (i > 0)
            instructions += " | ";
        if (i == cameraMode_)
            instructions += String("[") + modeNames[i] + "]";
        else
            instructions += modeNames[i];
    }

    instructionText_ = ui->GetRoot()->CreateChild<Text>();
    instructionText_->SetText(instructions);
    instructionText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);
    instructionText_->SetTextAlignment(HA_CENTER);
    instructionText_->SetHorizontalAlignment(HA_CENTER);
    instructionText_->SetVerticalAlignment(VA_CENTER);
    instructionText_->SetPosition(0, ui->GetRoot()->GetHeight() / 4);
}

void Sample::UpdateCameraModeText()
{
    if (!instructionText_)
        return;

    String current = instructionText_->GetText();
    // Find and replace the camera mode line
    unsigned pos = current.Find("C to cycle camera: ");
    if (pos == String::NPOS)
        return;

    // Rebuild from that point
    String prefix = current.Substring(0, pos);
    prefix += "C to cycle camera: ";
    const char* modeNames[] = { "God", "Chase", "1stPerson", "FreeFlight" };
    for (int i = 0; i < CAM_COUNT; ++i)
    {
        if (i > 0)
            prefix += " | ";
        if (i == cameraMode_)
            prefix += String("[") + modeNames[i] + "]";
        else
            prefix += modeNames[i];
    }
    instructionText_->SetText(prefix);
}

