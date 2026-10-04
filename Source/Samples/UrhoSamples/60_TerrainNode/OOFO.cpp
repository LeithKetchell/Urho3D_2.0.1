// OOFO — a dyslexic NASA analyst's flying saucer easter egg.

#include "OOFO.h"

#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/IO/Log.h>

Vector<Vector3> OOFO::BuildCloudPositions(ResourceCache* cache)
{
    Vector<Vector3> positions;

    const char* faceNames[] = {
        "Textures/BrightDay1_PosX.png", "Textures/BrightDay1_NegX.png",
        "Textures/BrightDay1_PosY.png", "Textures/BrightDay1_NegY.png",
        "Textures/BrightDay1_PosZ.png", "Textures/BrightDay1_NegZ.png"
    };
    for (int face = 0; face < 6; ++face)
    {
        SharedPtr<Image> img(cache->GetResource<Image>(faceNames[face]));
        if (!img) continue;
        int w = img->GetWidth();
        int h = img->GetHeight();
        for (int y = 0; y < h; y += 32)
        {
            for (int x = 0; x < w; x += 32)
            {
                Color c = img->GetPixel(x, y);
                if (c.a_ > 0.6f)
                {
                    float u = (float)x / w * 2.0f - 1.0f;
                    float v = (float)y / h * 2.0f - 1.0f;
                    Vector3 dir;
                    switch (face)
                    {
                    case 0: dir = Vector3( 1, -v, -u); break;
                    case 1: dir = Vector3(-1, -v,  u); break;
                    case 2: dir = Vector3( u,  1,  v); break;
                    case 3: dir = Vector3( u, -1, -v); break;
                    case 4: dir = Vector3( u, -v,  1); break;
                    case 5: dir = Vector3(-u, -v, -1); break;
                    }
                    dir.Normalize();
                    if (dir.y_ > 0.1f)
                        positions.Push(dir);
                }
            }
        }
    }

    if (positions.Empty())
    {
        positions.Push(Vector3(0.5f, 0.5f, 0.5f).Normalized());
        positions.Push(Vector3(-0.5f, 0.5f, -0.5f).Normalized());
    }

    URHO3D_LOGINFOF("OOFO: found %u cloud positions", positions.Size());
    return positions;
}

void OOFO::Init(Scene* scene, ResourceCache* cache, const Vector<Vector3>* cloudPositions)
{
    cloudPositions_ = cloudPositions;

    node_ = scene->CreateChild("OOFO", LOCAL);
    mat_ = cache->GetResource<Material>("Materials/UFO.xml")->Clone();
    auto* bb = node_->CreateComponent<BillboardSet>();
    bb->SetNumBillboards(1);
    bb->SetMaterial(mat_);
    bb->SetFaceCameraMode(FC_ROTATE_XYZ);
    bb->SetSorted(false);
    Billboard* quad = bb->GetBillboard(0);
    quad->position_ = Vector3::ZERO;
    quad->size_ = Vector2(8.0f, 8.0f);
    quad->enabled_ = true;
    bb->Commit();

    // Start at a random cloud
    DartToCloud();
    dir_ = target_;
    darting_ = false;
    idleTimer_ = 0.0f;
    nextIdleDuration_ = Random(5.0f, 15.0f);
}

void OOFO::DartToCloud()
{
    if (!cloudPositions_ || cloudPositions_->Empty()) return;

    // Pick randomly from nearby clouds
    Vector<int> nearby;
    for (int i = 0; i < (int)cloudPositions_->Size(); ++i)
    {
        float d = ((*cloudPositions_)[i] - dir_).Length();
        if (d > 0.15f && d < 0.8f)
            nearby.Push(i);
    }

    int bestIdx;
    if (!nearby.Empty())
        bestIdx = nearby[Random(0, (int)nearby.Size())];
    else
        bestIdx = Random(0, (int)cloudPositions_->Size());

    target_ = (*cloudPositions_)[bestIdx];
    darting_ = true;
    dartTimer_ = 0.0f;
}

void OOFO::Update(float timeStep, const Vector3& camPos, float cloudAngle, float nightFactor)
{
    if (!node_ || !cloudPositions_ || cloudPositions_->Empty()) return;

    auto* bb = node_->GetComponent<BillboardSet>();

    // Hide at night
    if (nightFactor > 0.8f)
    {
        if (bb) { bb->GetBillboard(0)->enabled_ = false; bb->Commit(); }
        return;
    }
    if (bb) bb->GetBillboard(0)->enabled_ = true;

    // Check if current position is near a cloud
    float minDist = 999.0f;
    for (unsigned i = 0; i < cloudPositions_->Size(); ++i)
    {
        float d = ((*cloudPositions_)[i] - dir_).Length();
        if (d < minDist) minDist = d;
    }

    // If exposed, panic and dart
    bool exposed = minDist > 0.3f;
    if (exposed && !darting_)
        DartToCloud();

    // Periodically dart even when hidden — duration decided once per idle period
    idleTimer_ += timeStep;
    if (!darting_ && idleTimer_ > nextIdleDuration_)
    {
        DartToCloud();
        idleTimer_ = 0.0f;
        nextIdleDuration_ = Random(5.0f, 15.0f);
    }

    // Darting animation (smoothstep)
    if (darting_)
    {
        dartTimer_ += timeStep * (exposed ? 0.5f : 0.2f);
        if (dartTimer_ >= 1.0f)
        {
            dir_ = target_;
            darting_ = false;
            dartTimer_ = 0.0f;
            idleTimer_ = 0.0f;
            nextIdleDuration_ = Random(5.0f, 15.0f);
        }
        else
        {
            float t = dartTimer_ * dartTimer_ * (3.0f - 2.0f * dartTimer_);
            dir_ = (dir_ * (1.0f - t) + target_ * t).Normalized();
        }
    }

    // Position at skybox distance from camera
    node_->SetPosition(camPos + dir_ * 400.0f);
    node_->LookAt(camPos);

    // Pass cloud angle and night factor to material
    if (mat_)
    {
        mat_->SetShaderParameter("CloudAngle", cloudAngle);
        mat_->SetShaderParameter("NightFactor", nightFactor);
    }

    if (bb) bb->Commit();
}
