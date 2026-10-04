// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Container/Ptr.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/Math/MathDefs.h>

#ifdef URHO3D_VULKAN
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#endif

namespace Urho3D { class Image; }

/// CPU-side 2D wave simulation with GPU texture upload.
/// When Vulkan is available, propagation runs as a compute shader dispatch.
/// Call SplashAt() for impacts, Update() each frame, bind GetTexture() to the water material.
/// Grid covers a fixed world-space square centred at the origin.
class WaterRippleSystem : public Urho3D::Object
{
    URHO3D_OBJECT(WaterRippleSystem, Urho3D::Object);

public:
    /// Grid dimensions — 128×128 = 16 KB per buffer, propagation ~0.1 ms/frame.
    static const int GRID = 128;
    /// Half-size of the simulation area in world units. Grid covers [-WORLD_HALF, +WORLD_HALF] on X and Z.
    static constexpr float WORLD_HALF = 256.0f;

    explicit WaterRippleSystem(Urho3D::Context* context);
    ~WaterRippleSystem() override;

    /// Create the GPU texture. Call once after construction.
    void Initialize();

    /// Add a splash impulse at world-space (x, z). strength in [0,1].
    void SplashAt(float worldX, float worldZ, float strength = 1.0f);

    /// Phase 2: Gaussian impact stamp with variable radius.
    /// worldRadius is in world units (converted to grid cells internally).
    /// Produces a smooth radial impulse — better visual quality than SplashAt's 3x3 block.
    void StampImpact(float worldX, float worldZ, float worldRadius, float intensity = 1.0f);

    /// Propagate waves and upload result to GPU texture. Call once per frame.
    void Update(float dt);

    /// Per-frame decay at 60 fps. 0.97 = fast decay (~1 s), 0.995 = slow decay (~4 s).
    void SetBaseDamping(float d) { baseDamping_ = Urho3D::Clamp(d, 0.80f, 0.999f); }

    /// The ripple height texture (L8 image — centre 128, range 0-255).
    Urho3D::Texture2D* GetTexture() const { return texture_; }

    /// True if compute dispatch is active (Vulkan path).
    bool IsComputeActive() const { return computeReady_; }

private:
    void PropagateWaves(float dt);
    void UploadTexture();
    void WorldToGrid(float wx, float wz, int& gx, int& gz) const;

    float baseDamping_{0.97f};
    bool active_{false};       ///< False when all energy has decayed — skip processing.
    int quiescentFrames_{0};   ///< Consecutive frames with near-zero energy.

    float curr_  [GRID * GRID];
    float prev_  [GRID * GRID];
    float source_[GRID * GRID];

    Urho3D::SharedPtr<Urho3D::Texture2D> texture_;
    Urho3D::SharedPtr<Urho3D::Image>     image_;

    // --- Vulkan compute path ---
    bool computeReady_{false};

#ifdef URHO3D_VULKAN
    void InitCompute();
    void DispatchCompute(float dt);
    void ReadbackFromGPU();
    void CleanupCompute();

    VkShaderModule compShader_{VK_NULL_HANDLE};
    VkPipeline compPipeline_{VK_NULL_HANDLE};
    VkDescriptorPool compDescPool_{VK_NULL_HANDLE};
    VkDescriptorSet compDescSet_{VK_NULL_HANDLE};

    // SSBOs: curr, prev, source, params
    VkBuffer ssboCurr_{VK_NULL_HANDLE};
    VkBuffer ssboPrev_{VK_NULL_HANDLE};
    VkBuffer ssboSource_{VK_NULL_HANDLE};
    VkBuffer ssboParams_{VK_NULL_HANDLE};
    VmaAllocation allocCurr_{};
    VmaAllocation allocPrev_{};
    VmaAllocation allocSource_{};
    VmaAllocation allocParams_{};

    struct ComputeParams {
        int gridSize;
        float damping;
        int absorbEdge;
        float pad0;
    };
#endif
};
