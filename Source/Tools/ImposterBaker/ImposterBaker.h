#pragma once

#include <Urho3D/Engine/Application.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Resource/Image.h>

using namespace Urho3D;

class ImposterBaker : public Application
{
    URHO3D_OBJECT(ImposterBaker, Application);

public:
    explicit ImposterBaker(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    /// Parse command-line arguments. Returns false on error.
    bool ParseArgs();
    /// Take an RGBA screenshot (4-component, preserving alpha).
    bool TakeScreenShotRGBA(Image& destImage);

    /// Generate an L-system tree model + materials. Returns null on unknown species.
    SharedPtr<Model> GenerateLTree(SharedPtr<Material>& barkMat, SharedPtr<Material>& leafMat);

    String modelPath_;
    String materialPath_;
    String outputPrefix_{"imposter_"};
    int numViews_{8};
    int outputWidth_{512};
    int outputHeight_{1024};

    /// L-system mode: species name (oak, pine, eucalyptus) or empty for model mode.
    String ltreeSpecies_;
    /// If non-empty, save the generated model to this path and exit.
    String saveModelPath_;
};
