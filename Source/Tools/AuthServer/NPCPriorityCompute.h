// NPCPriorityCompute — GPU-accelerated NPC task priority evaluation
// Phase 2 of Compute Shader integration. Follows WaterRippleSystem pattern.
// Input: NPC vitals (hunger, thirst, warmth, stamina, darkness, hp).
// Uniform: priority curves (threshold→weight mappings per task category).
// Output: chosen task category ID per NPC.
// One dispatch evaluates all NPCs in parallel.

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Container/Vector.h>

#ifdef URHO3D_VULKAN
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#endif

namespace Urho3D { class Graphics; }

/// Maximum NPCs the GPU buffer can hold. Reallocates if exceeded.
static constexpr int NPC_PRIORITY_MAX_NPCS = 1024;
/// Maximum priority curves (task-to-driver mappings).
static constexpr int NPC_PRIORITY_MAX_CURVES = 32;

/// GPU-side NPC driver data — matches shader struct NPCDrivers (8 floats, std430).
struct GPUNPCDrivers
{
    float hunger;
    float thirst;
    float warmth;
    float stamina;
    float darkness;
    float hpFraction;
    float pad0;
    float pad1;
};

/// A control point on a priority curve.
struct PriorityCurvePoint
{
    float input;    // driver value
    float output;   // priority score at this value
};

/// One priority curve: maps a single driver to a priority score via linear interpolation.
struct PriorityCurveData
{
    int driverIndex;    // 0=hunger, 1=thirst, 2=warmth, 3=stamina, 4=darkness, 5=hp
    int numPoints;      // active control points (max 8)
    int taskId;         // STASK_* value this curve produces
    int pad0;
    PriorityCurvePoint points[8];  // (in, out) pairs sorted ascending by in
};

/// GPU output per NPC — matches shader ivec2.
struct GPUNPCResult
{
    int taskId;
    int priorityFixed;  // priority * 1000 (fixed-point)
};

/// Manages GPU compute dispatch for NPC priority evaluation.
/// Falls back to CPU evaluation when Vulkan is unavailable.
class NPCPriorityCompute : public Urho3D::Object
{
    URHO3D_OBJECT(NPCPriorityCompute, Urho3D::Object);

public:
    explicit NPCPriorityCompute(Urho3D::Context* context);
    ~NPCPriorityCompute() override;

    /// Initialize GPU resources. Call once after Graphics is ready.
    void Initialize();

    /// Set the priority curves (call once at startup, or when tuning changes).
    void SetCurves(const Urho3D::Vector<PriorityCurveData>& curves);

    /// Upload NPC vitals and dispatch compute. Results available after Readback().
    void Dispatch(const Urho3D::Vector<GPUNPCDrivers>& npcData);

    /// Read results back from GPU. Call after Dispatch().
    void Readback(Urho3D::Vector<GPUNPCResult>& results);

    /// CPU fallback evaluation for a single NPC (when compute unavailable).
    int EvaluateCPU(const GPUNPCDrivers& drivers) const;

    /// True if GPU compute path is active.
    bool IsComputeReady() const { return computeReady_; }

private:
    bool computeReady_{false};
    Urho3D::Vector<PriorityCurveData> curves_;
    int numNPCs_{0};

#ifdef URHO3D_VULKAN
    void InitCompute();
    void CleanupCompute();

    VkShaderModule compShader_{VK_NULL_HANDLE};
    VkPipeline compPipeline_{VK_NULL_HANDLE};
    VkDescriptorPool compDescPool_{VK_NULL_HANDLE};
    VkDescriptorSet compDescSet_{VK_NULL_HANDLE};

    // SSBOs: drivers, curves, output, reserved
    VkBuffer ssboDrivers_{VK_NULL_HANDLE};
    VkBuffer ssboCurves_{VK_NULL_HANDLE};
    VkBuffer ssboOutput_{VK_NULL_HANDLE};
    VkBuffer ssboReserved_{VK_NULL_HANDLE};
    VmaAllocation allocDrivers_{};
    VmaAllocation allocCurves_{};
    VmaAllocation allocOutput_{};
    VmaAllocation allocReserved_{};
#endif
};
