// HumanNPC — task-driven human character.

#include "HumanNPC.h"
#include "BuildingSystem.h"

#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/SmoothedTransform.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Physics/PhysicsEvents.h>

HumanNPC::HumanNPC(Context* context) :
    LandAnimal(context)
{
}

void HumanNPC::Start()
{
    LandAnimal::Start();

    Scene* scene = GetScene();
    if (scene)
    {
        // Discover campfire from the scene if not set externally
        if (!campfireNode_)
            campfireNode_ = scene->GetChild("Campfire", true);
    }

    // Find the model child node and AnimatedModel — used by skin, 4a, 4b, 4c
    Node* modelNode = node_ ? node_->GetChild(node_->GetName() + "Model") : nullptr;
    auto* animModel = modelNode ? modelNode->GetComponent<AnimatedModel>() : nullptr;

    // Phase 5: PP character skin variety — deterministic skin assignment based on
    // spawnId so each NPC always looks the same across sessions and all skins get
    // used evenly. ~20% keep default Caveman skin for variety.
    if (animModel && node_)
    {
        int creatureId = GetCreatureId();
        unsigned id = GetSpawnId();

        // 7 visual slots: 6 skins + 1 default Caveman (no ApplySkin call)
        static const char* maleSkins[] = {
            "PP_Farmer", "PP_Worker", "PP_Adventurer", "PP_Man",
            "Q_CharAnimated", "Q_AnimatedBaseChar"
        };
        static const int MALE_COUNT = 7;   // 6 skins + 1 default
        static const char* femaleSkins[] = {"PP_AnimatedWoman"};
        static const int FEMALE_COUNT = 2; // 1 PP + 1 default

        const char* skin = nullptr;
        if (creatureId == 20)
        {
            int slot = id % MALE_COUNT;
            if (slot < 6)
                skin = maleSkins[slot];
            // slot 6 = keep default CavemanMan
        }
        else if (creatureId == 21)
        {
            int slot = id % FEMALE_COUNT;
            if (slot < 1)
                skin = femaleSkins[0];
            // slot 1 = keep default CaveWoman
        }

        if (skin)
            ApplySkin(skin);
    }

    // Equipment visuals — read server-replicated Equip_* Vars and attach models.
    // Bone mapping: hand→RightHand, offhand→LeftHand, head→Head, body→Spine1, feet→RightFoot, back→Spine1
    // Animation text keys ("equip"/"unequip") override these at runtime.
    if (animModel && node_)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        if (cache)
        {
            struct SlotBone { const char* slot; const char* bone; };
            static const SlotBone slotBones[] = {
                {"hand",    "RightHand"},
                {"offhand", "LeftHand"},
                {"head",    "Head"},
                {"body",    "Spine1"},
                {"feet",    "RightFoot"},
                {"back",    "Spine1"},
            };

            for (int s = 0; s < 6; ++s)
            {
                String varName = "Equip_" + String(slotBones[s].slot);
                Variant v = node_->GetVar(StringHash(varName));
                if (v.IsEmpty() || v.GetString().Empty())
                    continue;

                String modelPath = v.GetString();
                Node* boneNode = modelNode->GetChild(slotBones[s].bone, true);
                if (!boneNode)
                    continue;

                auto* mdl = cache->GetResource<Model>(modelPath);
                if (!mdl)
                    continue;

                Node* equipNode = boneNode->CreateChild("Equip_" + String(slotBones[s].slot));
                auto* sm = equipNode->CreateComponent<StaticModel>();
                sm->SetModel(mdl);
                sm->SetCastShadows(false);

                // Apply material list if available (CharacterItems have .txt material lists)
                String matListPath = modelPath;
                matListPath.Replace(".mdl", ".txt");
                sm->ApplyMaterialList(matListPath);

                // Per-slot default transforms
                String slot(slotBones[s].slot);
                if (slot == "hand" || slot == "offhand")
                {
                    equipNode->SetScale(Vector3(0.15f, 0.15f, 0.15f));
                    equipNode->SetRotation(Quaternion(90.0f, Vector3::RIGHT));
                    equipNode->SetPosition(Vector3(0.0f, 0.05f, 0.0f));
                    heldItemNode_ = equipNode;
                }
                else if (slot == "back")
                {
                    // Behind the torso, slightly above center
                    equipNode->SetScale(Vector3(0.12f, 0.12f, 0.12f));
                    equipNode->SetRotation(Quaternion(0.0f, 180.0f, 0.0f));
                    equipNode->SetPosition(Vector3(0.0f, 0.05f, -0.1f));
                }
                else if (slot == "head")
                {
                    equipNode->SetScale(Vector3(0.1f, 0.1f, 0.1f));
                    equipNode->SetPosition(Vector3(0.0f, 0.08f, 0.0f));
                }
                else if (slot == "feet")
                {
                    equipNode->SetScale(Vector3(0.1f, 0.1f, 0.1f));
                }
            }
        }
    }

    // Phase 4a: Material tint variation — clone material, apply random skin/clothing hue shift
    // Each NPC gets a slightly different tint so groups don't look like clones.
    if (animModel)
    {
        for (unsigned i = 0; i < animModel->GetNumGeometries(); i++)
        {
            Material* orig = animModel->GetMaterial(i);
            if (orig)
            {
                SharedPtr<Material> clone = orig->Clone();
                // Random warm tint: slight variation in R/G/B around (1,1,1)
                float r = 0.85f + Random(0.3f);   // 0.85 - 1.15
                float g = 0.82f + Random(0.25f);   // 0.82 - 1.07
                float b = 0.75f + Random(0.2f);    // 0.75 - 0.95
                clone->SetShaderParameter("MatDiffColor", Color(r, g, b, 1.0f));
                animModel->SetMaterial(i, clone);
            }
        }
    }

    // Phase 4c: Scale variation — ±10% so groups don't look uniform
    if (node_)
    {
        float baseScale = node_->GetScale().x_;
        float variation = 1.0f + Random(-0.1f, 0.1f);
        node_->SetScale(baseScale * variation);
    }

}

void HumanNPC::ApplySkin(const String& skinName)
{
    Node* modelNode = node_ ? node_->GetChild(node_->GetName() + "Model") : nullptr;
    auto* animModel = modelNode ? modelNode->GetComponent<AnimatedModel>() : nullptr;
    auto* cache = GetSubsystem<ResourceCache>();
    if (!animModel || !cache)
        return;

    String modelPath = "Models/Characters/" + skinName + ".mdl";
    String matList   = "Models/Characters/" + skinName + ".txt";

    auto* mdl = cache->GetResource<Model>(modelPath);
    if (!mdl)
    {
        URHO3D_LOGWARNINGF("ApplySkin: model '%s' not found", modelPath.CString());
        return;
    }

    // Swap model and materials
    animModel->SetModel(mdl, true, true);
    animModel->ApplyMaterialList(matList);

    // Re-scale to desired size after model swap (Creature::Start() scaled the old model)
    BoundingBox bb = animModel->GetBoundingBox();
    Vector3 size = bb.Size();
    float longest = Max(size.x_, Max(size.y_, size.z_));
    if (longest > 0.001f)
        node_->SetScale(GetDesiredSize() / longest);

    // Re-offset Y so feet sit at ground level
    if (modelNode != node_ && bb.min_.y_ < -0.001f)
        modelNode->SetPosition(Vector3(modelNode->GetPosition().x_, -bb.min_.y_, modelNode->GetPosition().z_));

    skinName_ = skinName;
    hasSkin_ = true;

    // Build animation paths based on the skin's armature naming convention.
    // PP_Man uses "HumanArmatureMan_" prefix; all other PP models use "CharacterArmature".
    String dir = "Models/Characters/";

    if (skinName == "PP_Man")
    {
        String p = dir + "PP_Man_HumanArmatureMan_";
        skinAnims_.idle         = p + "Idle.ani";
        skinAnims_.walk         = p + "Walk.ani";
        skinAnims_.run          = p + "Run.ani";
        skinAnims_.die          = p + "Death.ani";
        skinAnims_.greet        = p + "Clapping.ani";
        skinAnims_.jump         = p + "Jump.ani";
        skinAnims_.attack       = p + "Punch.ani";
        skinAnims_.victory      = p + "SwordSlash.ani";
        skinAnims_.eat          = p + "Standing.ani";
        skinAnims_.sit          = p + "Sitting.ani";
        skinAnims_.sleep        = p + "Sitting.ani";
        skinAnims_.look         = p + "Standing.ani";
        skinAnims_.scream       = p + "Punch.ani";
        skinAnims_.tend         = p + "Standing.ani";
        skinAnims_.swim         = p + "Walk.ani";
        skinAnims_.treadWater   = p + "Idle.ani";
        skinAnims_.crouchWalk   = p + "Walk.ani";
        skinAnims_.crawl        = p + "Walk.ani";
        skinAnims_.standing     = p + "Standing.ani";
        skinAnims_.disappointed = p + "Standing.ani";
        skinAnims_.dance        = p + "Clapping.ani";
        skinAnims_.shear        = p + "Standing.ani";
        skinAnims_.fish         = p + "Sitting.ani";
    }
    else if (skinName == "Q_CharAnimated")
    {
        // Quaternius character — uses CharacterArmature rig with combat-focused anims
        String p = dir + "Q_CharAnimated_CharacterArmature";
        skinAnims_.idle         = p + "Idle.ani";
        skinAnims_.walk         = p + "Walk.ani";
        skinAnims_.run          = p + "Run.ani";
        skinAnims_.die          = p + "Death.ani";
        skinAnims_.greet        = p + "PickUp.ani";
        skinAnims_.jump         = p + "Roll.ani";
        skinAnims_.attack       = p + "Punch.ani";
        skinAnims_.victory      = p + "Dagger_Attack.ani";
        skinAnims_.eat          = p + "PickUp.ani";
        skinAnims_.sit          = p + "Idle.ani";
        skinAnims_.sleep        = p + "Idle.ani";
        skinAnims_.look         = p + "Idle.ani";
        skinAnims_.scream       = p + "RecieveHit.ani";
        skinAnims_.tend         = p + "PickUp.ani";
        skinAnims_.swim         = p + "Walk.ani";
        skinAnims_.treadWater   = p + "Idle.ani";
        skinAnims_.crouchWalk   = p + "Walk.ani";
        skinAnims_.crawl        = p + "Walk.ani";
        skinAnims_.standing     = p + "Idle.ani";
        skinAnims_.disappointed = p + "RecieveHit.ani";
        skinAnims_.dance        = p + "Dagger_Attack2.ani";
        skinAnims_.shear        = p + "PickUp.ani";
        skinAnims_.fish         = p + "Idle.ani";
    }
    else if (skinName == "Q_AnimatedBaseChar")
    {
        // Quaternius base character — richest animation set (45 anims), unique Rig prefix
        String p = dir + "Q_AnimatedBaseChar_Rig";
        skinAnims_.idle         = p + "Idle_Loop.ani";
        skinAnims_.walk         = p + "Walk_Loop.ani";
        skinAnims_.run          = p + "Jog_Fwd_Loop.ani";
        skinAnims_.die          = p + "Death01.ani";
        skinAnims_.greet        = p + "Interact.ani";
        skinAnims_.jump         = p + "Jump_Start.ani";
        skinAnims_.attack       = p + "Punch_Jab.ani";
        skinAnims_.victory      = p + "Sword_Attack.ani";
        skinAnims_.eat          = p + "PickUp_Table.ani";
        skinAnims_.sit          = p + "Sitting_Idle_Loop.ani";
        skinAnims_.sleep        = p + "Sitting_Idle_Loop.ani";
        skinAnims_.look         = p + "Idle_Talking_Loop.ani";
        skinAnims_.scream       = p + "Hit_Chest.ani";
        skinAnims_.tend         = p + "Interact.ani";
        skinAnims_.swim         = p + "Swim_Fwd_Loop.ani";
        skinAnims_.treadWater   = p + "Swim_Idle_Loop.ani";
        skinAnims_.crouchWalk   = p + "Crouch_Fwd_Loop.ani";
        skinAnims_.crawl        = p + "Crouch_Fwd_Loop.ani";
        skinAnims_.standing     = p + "Idle_Loop.ani";
        skinAnims_.disappointed = p + "Hit_Head.ani";
        skinAnims_.dance        = p + "Dance_Loop.ani";
        skinAnims_.shear        = p + "Fixing_Kneeling.ani";
        skinAnims_.fish         = p + "Sitting_Idle_Loop.ani";
    }
    else
    {
        // CharacterArmature family: PP_Farmer, PP_Worker, PP_Adventurer, PP_AnimatedWoman
        String p = dir + skinName + "_CharacterArmature";
        skinAnims_.idle         = p + "Idle.ani";
        skinAnims_.walk         = p + "Walk.ani";
        skinAnims_.run          = p + "Run.ani";
        skinAnims_.die          = p + "Death.ani";
        skinAnims_.greet        = p + "Wave.ani";
        skinAnims_.jump         = p + "Roll.ani";
        skinAnims_.attack       = p + "Punch_Right.ani";
        skinAnims_.victory      = p + "Sword_Slash.ani";
        skinAnims_.eat          = p + "Interact.ani";
        skinAnims_.sit          = p + "Idle_Neutral.ani";
        skinAnims_.sleep        = p + "Idle_Neutral.ani";
        skinAnims_.look         = p + "Idle_Neutral.ani";
        skinAnims_.scream       = p + "HitRecieve.ani";
        skinAnims_.tend         = p + "Interact.ani";
        skinAnims_.swim         = p + "Walk.ani";
        skinAnims_.treadWater   = p + "Idle.ani";
        skinAnims_.crouchWalk   = p + "Walk.ani";
        skinAnims_.crawl        = p + "Walk.ani";
        skinAnims_.standing     = p + "Idle_Neutral.ani";
        skinAnims_.disappointed = p + "HitRecieve.ani";
        skinAnims_.dance        = p + "Wave.ani";
        skinAnims_.shear        = p + "Interact.ani";
        skinAnims_.fish         = p + "Idle_Neutral.ani";
    }

    URHO3D_LOGINFOF("NPC %u: applied PP skin '%s'", GetSpawnId(), skinName.CString());
}

// FindNearbyResource() and PickTask() removed — all NPC decision-making
// is server-authoritative. Server runs PickCreatureTask() in AuthServer.cpp
// and broadcasts via MSG_CREATURE_AI_STATE.

void HumanNPC::SetPossessed(bool possessed)
{
    if (possessed_ == possessed)
        return;
    possessed_ = possessed;

    auto* body = node_ ? node_->GetComponent<RigidBody>() : nullptr;

    if (!possessed_)
    {
        // Returning to AI — switch body back to kinematic, clear controls
        if (body)
        {
            body->SetLinearVelocity(Vector3::ZERO);
            body->SetKinematic(true);
            body->SetCollisionEventMode(COLLISION_ACTIVE);
        }
        if (node_)
            UnsubscribeFromEvent(node_, E_NODECOLLISION);

        controls_.Reset();
        possessedAnim_.Clear();
        onGround_ = false;
        inAirTimer_ = 0.0f;
        okToJump_ = true;
        possessedInWater_ = false;
        SetState(CREATURE_IDLE);
    }
    else
    {
        // Being possessed — switch body to dynamic for physics-driven movement
        if (body)
        {
            body->SetKinematic(false);
            body->SetCollisionEventMode(COLLISION_ALWAYS);
            body->SetFriction(0.6f);
            body->SetLinearDamping(0.0f);
            body->SetAngularFactor(Vector3::ZERO);  // prevent tumbling
            body->Activate();
        }
        if (node_)
            SubscribeToEvent(node_, E_NODECOLLISION, URHO3D_HANDLER(HumanNPC, HandleNodeCollision));

        possessedAnim_.Clear();
        onGround_ = false;
        inAirTimer_ = 0.0f;
        okToJump_ = true;
    }
}

// Possessed movement tuning — sprint/swim/jump costs from DB StaminaRules
#define SPRINT_STAMINA_DRAIN (Creature::GetStaminaRules().sprintCostSec)
#define SWIM_STAMINA_DRAIN   (Creature::GetStaminaRules().swimCostSec)
#define JUMP_STAMINA_COST    (Creature::GetStaminaRules().meleeCost)
static constexpr float SWIM_SPEED_MULT = 0.6f;        // fraction of wander speed in water
#define MIN_SPRINT_STAMINA ((float)Creature::GetStaminaRules().lowThreshold)

// Phase 2: Physics-driven possessed movement tuning.
// At steady state: maxVelocity = moveForce / brakeFactor.
// Walk: 0.8 / 0.4 = 2.0 m/s. Sprint: 2.4 / 0.4 = 6.0 m/s.
static constexpr float POSSESSED_WALK_FORCE   = 0.8f;   // horizontal impulse per physics step (walk)
static constexpr float POSSESSED_SPRINT_FORCE = 2.4f;   // horizontal impulse per physics step (sprint)
static constexpr float POSSESSED_BRAKE        = 0.4f;   // velocity brake multiplier
static constexpr float POSSESSED_AIR_FORCE    = 0.32f;  // air control impulse (40% of walk)
static constexpr float POSSESSED_JUMP_FORCE   = 7.0f;   // upward jump impulse
static constexpr float INAIR_THRESHOLD        = 0.1f;   // seconds of "soft grounding" tolerance

void HumanNPC::SetSmoothedPosition(const Vector3& pos)
{
    auto* smooth = node_->GetComponent<SmoothedTransform>();
    if (smooth)
        smooth->SetTargetWorldPosition(pos);
    else
        node_->SetWorldPosition(pos);
}

void HumanNPC::UpdatePossessedMovement(float timeStep)
{
    if (!node_)
        return;

    auto* body = node_->GetComponent<RigidBody>();
    if (!body)
        return;

    Vector3 pos = node_->GetWorldPosition();
    float terrainY = terrain_ ? terrain_->GetHeight(pos) : pos.y_;

    // ── Water transition ──
    // When entering water, switch body to kinematic for swim code compatibility.
    // When leaving water, switch back to dynamic for physics-driven ground movement.
    bool wasInWater = possessedInWater_;
    possessedInWater_ = (terrainY < waterLevel_);

    if (possessedInWater_ && !wasInWater)
    {
        // Entering water — go kinematic for swim
        body->SetLinearVelocity(Vector3::ZERO);
        body->SetKinematic(true);
    }
    else if (!possessedInWater_ && wasInWater)
    {
        // Leaving water — go dynamic for ground physics
        body->SetKinematic(false);
        body->Activate();
        onGround_ = false;
        inAirTimer_ = 0.0f;
    }

    if (possessedInWater_)
    {
        UpdatePossessedSwim(timeStep);
        return;
    }

    // ── Ground detection bookkeeping ──
    // onGround_ is set by HandleNodeCollision each frame, reset here.
    if (!onGround_)
        inAirTimer_ += timeStep;
    else
        inAirTimer_ = 0.0f;
    bool softGrounded = inAirTimer_ < INAIR_THRESHOLD;

    auto* animCtrl = node_->GetComponent<AnimationController>(true);
    const Vector3& velocity = body->GetLinearVelocity();
    Vector3 planeVelocity(velocity.x_, 0.0f, velocity.z_);

    // Build movement direction from controls
    Vector3 moveDir = Vector3::ZERO;
    if (controls_.IsDown(CTRL_FORWARD))
        moveDir += Vector3::FORWARD;
    if (controls_.IsDown(CTRL_BACK))
        moveDir += Vector3::BACK;
    if (controls_.IsDown(CTRL_LEFT))
        moveDir += Vector3::LEFT;
    if (controls_.IsDown(CTRL_RIGHT))
        moveDir += Vector3::RIGHT;

    bool moving = moveDir.LengthSquared() > 0.0f;
    bool sprinting = controls_.IsDown(CTRL_SPRINT) && moving && softGrounded && stamina_ > MIN_SPRINT_STAMINA;

    // Extra stamina drain while sprinting
    if (sprinting)
        stamina_ = Max(0.0f, stamina_ - SPRINT_STAMINA_DRAIN * timeStep);

    if (moving)
    {
        moveDir.Normalize();

        // Rotate movement direction by yaw (camera-relative)
        Quaternion yawRot(0.0f, controls_.yaw_, 0.0f);
        moveDir = yawRot * moveDir;

        // Apply movement impulse — force depends on grounded/airborne/sprint
        float force = softGrounded
            ? (sprinting ? POSSESSED_SPRINT_FORCE : POSSESSED_WALK_FORCE)
            : POSSESSED_AIR_FORCE;
        body->ApplyImpulse(moveDir * force);

        // Smooth turning toward movement direction
        Vector3 currentFwd = node_->GetWorldDirection();
        currentFwd.y_ = 0.0f;
        currentFwd.Normalize();
        float turnRate = 8.0f * timeStep;
        Vector3 newFwd = currentFwd.Lerp(moveDir, Min(turnRate, 1.0f));
        newFwd.y_ = 0.0f;
        if (newFwd.LengthSquared() > 0.001f)
        {
            newFwd.Normalize();
            node_->SetWorldDirection(newFwd);
        }
    }

    // Braking — limit ground velocity so we don't slide forever
    if (softGrounded)
    {
        Vector3 brakeForce = -planeVelocity * POSSESSED_BRAKE;
        body->ApplyImpulse(brakeForce);
    }

    // ── Jump ──
    if (controls_.IsDown(CTRL_JUMP))
    {
        if (softGrounded && okToJump_ && stamina_ > JUMP_STAMINA_COST)
        {
            body->ApplyImpulse(Vector3::UP * POSSESSED_JUMP_FORCE);
            stamina_ = Max(0.0f, stamina_ - JUMP_STAMINA_COST);
            okToJump_ = false;

            String jumpAnim = GetJumpAnim();
            if (!jumpAnim.Empty() && animCtrl)
            {
                animCtrl->PlayExclusive(jumpAnim, 0, false, 0.1f);
                possessedAnim_ = jumpAnim;
            }
        }
    }
    else
    {
        okToJump_ = true;
    }

    // ── Animation ──
    if (!onGround_)
    {
        // Airborne — keep jump anim playing
        String jumpAnim = GetJumpAnim();
        if (!jumpAnim.Empty() && jumpAnim != possessedAnim_ && animCtrl)
        {
            animCtrl->PlayExclusive(jumpAnim, 0, false, 0.2f);
            possessedAnim_ = jumpAnim;
        }
    }
    else if (moving)
    {
        String targetAnim = sprinting ? GetRunAnim() : GetWalkAnim();
        if (!targetAnim.Empty() && targetAnim != possessedAnim_ && animCtrl)
        {
            animCtrl->PlayExclusive(targetAnim, 0, true, 0.2f);
            possessedAnim_ = targetAnim;
        }
    }
    else
    {
        String idleAnim = GetIdleAnim();
        if (!idleAnim.Empty() && idleAnim != possessedAnim_ && animCtrl)
        {
            animCtrl->PlayExclusive(idleAnim, 0, true, 0.3f);
            possessedAnim_ = idleAnim;
        }
    }

    // Reset ground flag — will be set again by HandleNodeCollision before next FixedUpdate
    onGround_ = false;
}

void HumanNPC::HandleNodeCollision(StringHash eventType, VariantMap& eventData)
{
    using namespace NodeCollision;

    MemoryBuffer contacts(eventData[P_CONTACTS].GetBuffer());

    while (!contacts.IsEof())
    {
        Vector3 contactPosition = contacts.ReadVector3();
        Vector3 contactNormal = contacts.ReadVector3();
        /*float contactDistance = */contacts.ReadFloat();
        /*float contactImpulse = */contacts.ReadFloat();

        // Ground contact: normal points up (y > 0.75) and contact is below node center
        if (contactPosition.y_ < (node_->GetPosition().y_ + 1.0f))
        {
            if (contactNormal.y_ > 0.75f)
                onGround_ = true;
        }
    }
}

void HumanNPC::UpdatePossessedSwim(float timeStep)
{
    if (!node_)
        return;

    auto* animCtrl = node_->GetComponent<AnimationController>(true);
    Vector3 pos = node_->GetWorldPosition();

    // Extra stamina drain while swimming
    stamina_ = Max(0.0f, stamina_ - SWIM_STAMINA_DRAIN * timeStep);

    // Build movement direction from controls
    Vector3 moveDir = Vector3::ZERO;
    if (controls_.IsDown(CTRL_FORWARD))
        moveDir += Vector3::FORWARD;
    if (controls_.IsDown(CTRL_BACK))
        moveDir += Vector3::BACK;
    if (controls_.IsDown(CTRL_LEFT))
        moveDir += Vector3::LEFT;
    if (controls_.IsDown(CTRL_RIGHT))
        moveDir += Vector3::RIGHT;

    bool moving = moveDir.LengthSquared() > 0.0f;
    float swimSpeed = GetWanderSpeed() * SWIM_SPEED_MULT;

    if (moving)
    {
        moveDir.Normalize();

        // 3D swim direction — pitch + yaw so looking down + W = dive
        Quaternion swimRot(controls_.pitch_, controls_.yaw_, 0.0f);
        Vector3 swimDir = swimRot * moveDir;

        // Smooth turning (horizontal component only for model facing)
        Vector3 horizDir(swimDir.x_, 0.0f, swimDir.z_);
        if (horizDir.LengthSquared() > 0.001f)
        {
            horizDir.Normalize();
            Vector3 currentFwd = node_->GetWorldDirection();
            currentFwd.y_ = 0.0f;
            currentFwd.Normalize();
            float turnRate = 6.0f * timeStep;
            Vector3 newFwd = currentFwd.Lerp(horizDir, Min(turnRate, 1.0f));
            newFwd.Normalize();
            node_->SetWorldDirection(newFwd);
        }

        Vector3 newPos = pos + swimDir * swimSpeed * timeStep;

        // Don't swim below terrain floor
        if (terrain_)
        {
            float groundY = terrain_->GetHeight(newPos);
            if (newPos.y_ < groundY + 0.2f)
                newPos.y_ = groundY + 0.2f;
        }

        if (!IsBlockedByWall(pos, newPos))
            SetSmoothedPosition(newPos);
    }

    // Space (CTRL_JUMP) = rise toward surface
    if (controls_.IsDown(CTRL_JUMP))
    {
        pos = node_->GetWorldPosition();
        pos.y_ += swimSpeed * timeStep;
        // Don't breach too far above water surface
        if (pos.y_ > waterLevel_ + 0.3f)
            pos.y_ = waterLevel_ + 0.3f;
        SetSmoothedPosition(pos);
    }

    // Buoyancy — gently push toward water surface when not actively diving
    if (!moving || controls_.pitch_ >= 0.0f)
    {
        pos = node_->GetWorldPosition();
        float surfaceY = waterLevel_;
        if (pos.y_ < surfaceY - 0.5f)
        {
            pos.y_ += 1.5f * timeStep;  // gentle upward drift
            SetSmoothedPosition(pos);
        }
    }

    // Play swim animation
    String swimAnim = GetSwimAnim();
    if (!swimAnim.Empty() && swimAnim != possessedAnim_ && animCtrl)
    {
        animCtrl->PlayExclusive(swimAnim, 0, true, 0.3f);
        possessedAnim_ = swimAnim;
    }
}

void HumanNPC::FixedUpdate(float timeStep)
{
    // When possessed by a player, skip AI entirely
    if (possessed_)
    {
        // Vitals still tick when possessed
        if (HasVitals())
        {
            float sunAlt = 0.5f;
            Scene* scene = GetScene();
            if (scene)
            {
                Node* dl = scene->GetChild("DirectionalLight", true);
                if (dl)
                    sunAlt = -dl->GetDirection().y_;
            }
            UpdateVitals(timeStep, sunAlt);
        }

        UpdatePossessedMovement(timeStep);
        PostMovementUpdate(timeStep);
        return;
    }

    // Server-driven mode: server owns the brain. Client lerps toward
    // server position and plays server-chosen animation. No local decisions.
    if (serverDriven_ && !possessed_)
    {
        if (node_)
        {
            Vector3 pos = node_->GetWorldPosition();
            Vector3 diff = serverPos_ - pos;
            float dist = diff.Length();

            if (dist > 0.1f)
            {
                // Smooth lerp toward server position — snap if too far behind
                float lerpSpeed = Max(serverMoveSpeed_, 3.0f);
                float step = lerpSpeed * timeStep;
                if (dist > 10.0f)
                    node_->SetWorldPosition(serverPos_); // Snap
                else
                {
                    Vector3 newPos = pos + diff.Normalized() * Min(step, dist);
                    // Y-snap to local terrain
                    if (terrain_)
                        newPos.y_ = terrain_->GetHeight(newPos);
                    node_->SetWorldPosition(newPos);
                    // Face movement direction
                    Vector3 faceDir = diff;
                    faceDir.y_ = 0.0f;
                    if (faceDir.LengthSquared() > 0.01f)
                        node_->SetWorldDirection(faceDir.Normalized());
                }
            }
        }

        // Phase 4b: Show/hide held item based on server state
        if (heldItemNode_)
        {
            bool showItem = (state_ == CREATURE_IDLE || state_ == CREATURE_WANDER ||
                             state_ == CREATURE_FIGHT || state_ == CREATURE_GREET ||
                             state_ == CREATURE_ALERT || state_ == CREATURE_FLEE ||
                             state_ == CREATURE_LOOK || state_ == CREATURE_FISH);
            heldItemNode_->SetEnabled(showItem);
        }

        PostMovementUpdate(timeStep);
        return;
    }

    // Not possessed and not server-driven: waiting for server AI state.
    // All NPC decision-making is server-authoritative (even offline mode
    // has a local AuthServer). Client just idles until first MSG_CREATURE_AI_STATE.
    PostMovementUpdate(timeStep);
}

// ============================================================
// Observation / Insight System
// ============================================================

// DC table by phenomenon complexity tier.
// Tier 0 = trivial (fire burns things), tier 4 = subtle (metal smelting temperature).
static int GetPhenomenonDC(int phenomenonType)
{
    // Group into complexity tiers: low byte = specific phenomenon, high byte = tier
    int tier = (phenomenonType >> 8) & 0xF;
    switch (tier)
    {
    case 0: return 8;   // obvious — fire, water, falling
    case 1: return 11;  // moderate — animal patterns, weather signs
    case 2: return 14;  // complex — material properties, tool physics
    case 3: return 17;  // subtle — metallurgy, fermentation, agriculture
    case 4: return 20;  // profound — astronomy, medicine, engineering
    default: return 12;
    }
}

bool HumanNPC::ObservePhenomenon(int phenomenonType, const Vector3& pos)
{
    // WIS check: d20 + WisMod vs DC based on phenomenon complexity
    int dc = GetPhenomenonDC(phenomenonType);
    int roll = (int)(Random(1, 21));  // d20
    int total = roll + stats_.WisMod();

    if (total < dc)
        return false;  // NPC didn't notice or understand

    // Get or create insight entry
    unsigned key = (unsigned)phenomenonType;
    auto it = insights_.Find(key);
    if (it == insights_.End())
    {
        InsightEntry entry;
        entry.phenomenonType = phenomenonType;
        entry.count = 0;
        entry.lastSeen = 0.0f;
        insights_[key] = entry;
        it = insights_.Find(key);
    }

    InsightEntry& insight = it->second_;
    insight.count++;

    // Record scene time of observation
    Scene* scene = GetScene();
    if (scene)
        insight.lastSeen = scene->GetElapsedTime();

    bool reachedThreshold = (insight.count == INSIGHT_THRESHOLD);

    if (reachedThreshold)
    {
        URHO3D_LOGINFOF("NPC %u: reached insight threshold for phenomenon %d (%d observations) — ready for discovery",
                        GetSpawnId(), phenomenonType, insight.count);
    }

    return reachedThreshold;
}

int HumanNPC::GetInsightCount(int phenomenonType) const
{
    auto it = insights_.Find((unsigned)phenomenonType);
    if (it == insights_.End()) return 0;
    return it->second_.count;
}

bool HumanNPC::IsReadyForDiscovery(int phenomenonType) const
{
    auto it = insights_.Find((unsigned)phenomenonType);
    if (it == insights_.End()) return false;
    return it->second_.count >= INSIGHT_THRESHOLD;
}

// ============================================================
// Technique Discovery System
// ============================================================

Vector<int> HumanNPC::xpThresholds_;
bool HumanNPC::xpThresholdsLoaded_ = false;

static int GetXPForLevel(int level)
{
    if (!HumanNPC::xpThresholdsLoaded_)
    {
        // Hardcoded fallback matching skill_levels table
        HumanNPC::xpThresholds_.Push(0);
        HumanNPC::xpThresholds_.Push(10);
        HumanNPC::xpThresholds_.Push(30);
        HumanNPC::xpThresholds_.Push(60);
        HumanNPC::xpThresholds_.Push(100);
        HumanNPC::xpThresholds_.Push(200);
        HumanNPC::xpThresholds_.Push(350);
        HumanNPC::xpThresholds_.Push(550);
        HumanNPC::xpThresholds_.Push(800);
        HumanNPC::xpThresholds_.Push(1100);
        HumanNPC::xpThresholds_.Push(1500);
        HumanNPC::xpThresholdsLoaded_ = true;
    }
    if (level < 0) return 0;
    if (level >= (int)HumanNPC::xpThresholds_.Size())
        return HumanNPC::xpThresholds_.Back();
    return HumanNPC::xpThresholds_[level];
}

bool HumanNPC::AwardTechniqueXP(int skillId, int baseXP)
{
    auto it = techniques_.Find((unsigned)skillId);
    if (it == techniques_.End() || !it->second_.discovered)
        return false;

    NPCTechnique& tech = it->second_;

    // INT modifier accelerates learning: each +1 = +10% XP
    float intBonus = 1.0f + stats_.IntMod() * 0.1f;
    intBonus = Max(intBonus, 0.5f);  // minimum 50% even with low INT
    int xpGain = Max(1, (int)(baseXP * intBonus));

    tech.xp += xpGain;

    // Check for level-up
    int nextLevel = tech.rating + 1;
    int xpNeeded = GetXPForLevel(nextLevel);
    if (tech.xp >= xpNeeded && nextLevel <= 10)
    {
        tech.rating = nextLevel;
        tech.xp = 0;  // reset XP for next level
        URHO3D_LOGINFOF("NPC %u: %s advanced to rating %d",
                        GetSpawnId(), "technique", tech.rating);
        return true;
    }
    return false;
}

int HumanNPC::GetTechniqueRating(int skillId) const
{
    auto it = techniques_.Find((unsigned)skillId);
    if (it == techniques_.End()) return 0;
    return it->second_.rating;
}

bool HumanNPC::KnowsTechnique(int skillId) const
{
    auto it = techniques_.Find((unsigned)skillId);
    if (it == techniques_.End()) return false;
    return it->second_.discovered;
}

int HumanNPC::AttemptDiscovery(int sourceSkillId)
{
    // Get source technique rating
    int sourceRating = GetTechniqueRating(sourceSkillId);
    if (sourceRating < 1) return 0;

    // Discovery table: source_skill → target_skill + prereq_rating + DC
    // Hardcoded for now — matches technique_discovery SQL table
    struct DiscoveryRule { int source; int target; int prereq; int dc; };
    static const DiscoveryRule rules[] = {
        {23, 25, 3, 12}, {23, 26, 4, 14}, {20, 21, 3, 11},
        {21, 22, 3, 13}, {28, 24, 3, 10}, {10, 11, 3, 12},
        {10, 14, 4, 13}, {12, 13, 3, 12}, {14, 15, 5, 16},
        {15, 16, 4, 14}, {30, 20, 2, 10}, {1, 4, 4, 13},
        {11, 2, 4, 14}
    };

    for (const auto& rule : rules)
    {
        if (rule.source != sourceSkillId) continue;
        if (sourceRating < rule.prereq) continue;
        if (KnowsTechnique(rule.target)) continue;  // already known

        // INT check: d20 + INT modifier + source rating vs DC
        int roll = (int)(Random(1, 21));  // d20
        int total = roll + stats_.IntMod() + sourceRating;
        if (total >= rule.dc)
        {
            // Discovered!
            NPCTechnique entry;
            entry.skillId = rule.target;
            entry.rating = 1;  // start at novice
            entry.xp = 0;
            entry.discovered = true;
            techniques_[(unsigned)rule.target] = entry;

            URHO3D_LOGINFOF("NPC %u discovered technique %d (from %d, roll %d+%d+%d=%d vs DC %d)",
                            GetSpawnId(), rule.target, sourceSkillId,
                            roll, stats_.IntMod(), sourceRating, total, rule.dc);
            return rule.target;
        }
    }
    return 0;
}

int HumanNPC::TeachTechnique(int skillId, HumanNPC* student)
{
    if (!student) return 0;
    if (!KnowsTechnique(skillId)) return 0;
    if (student->KnowsTechnique(skillId) && student->GetTechniqueRating(skillId) >= GetTechniqueRating(skillId))
        return 0;  // student is at or above teacher's level

    // Grant knowledge if student doesn't know it
    if (!student->KnowsTechnique(skillId))
    {
        NPCTechnique entry;
        entry.skillId = skillId;
        entry.rating = 0;
        entry.xp = 0;
        entry.discovered = true;
        student->techniques_[(unsigned)skillId] = entry;
    }

    // Transfer XP: base 5, scaled by teacher's CHA mod
    int baseXP = 5;
    float chaBonus = 1.0f + stats_.ChaMod() * 0.15f;  // CHA affects teaching speed
    chaBonus = Max(chaBonus, 0.5f);
    int xpTransferred = Max(1, (int)(baseXP * chaBonus));

    student->AwardTechniqueXP(skillId, xpTransferred);
    return xpTransferred;
}

void HumanNPC::GrantPrimitiveTechniques()
{
    // All NPCs start knowing Stone Age primitives at rating 1
    static const int primitives[] = {23, 20, 28, 10, 1, 3, 27};  // foraging, tracking, fire, knapping, melee, defense, swimming
    for (int id : primitives)
    {
        NPCTechnique entry;
        entry.skillId = id;
        entry.rating = 1;
        entry.xp = 0;
        entry.discovered = true;
        techniques_[(unsigned)id] = entry;
    }
}

