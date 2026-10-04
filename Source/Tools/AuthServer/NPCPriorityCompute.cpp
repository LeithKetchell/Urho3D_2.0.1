// NPCPriorityCompute — GPU compute dispatch for NPC task priority evaluation
// Follows WaterRippleSystem pattern exactly.

#include <Urho3D/Precompiled.h>
#include "NPCPriorityCompute.h"

#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/Resource/ResourceCache.h>

#ifdef URHO3D_VULKAN
#include <Urho3D/GraphicsAPI/Vulkan/VulkanGraphicsImpl.h>
#include <Urho3D/GraphicsAPI/Vulkan/VulkanShaderCompiler.h>
#endif

#include <Urho3D/DebugNew.h>

using namespace Urho3D;

NPCPriorityCompute::NPCPriorityCompute(Context* context) :
    Object(context)
{
}

NPCPriorityCompute::~NPCPriorityCompute()
{
#ifdef URHO3D_VULKAN
    CleanupCompute();
#endif
}

void NPCPriorityCompute::Initialize()
{
#ifdef URHO3D_VULKAN
    InitCompute();
#endif
    URHO3D_LOGINFOF("NPCPriorityCompute: initialized (compute=%s, maxNPCs=%d)",
        computeReady_ ? "YES" : "NO (CPU fallback)", NPC_PRIORITY_MAX_NPCS);
}

void NPCPriorityCompute::SetCurves(const Vector<PriorityCurveData>& curves)
{
    curves_ = curves;

#ifdef URHO3D_VULKAN
    if (!computeReady_ || !ssboCurves_)
        return;

    auto* graphics = GetSubsystem<Graphics>();
    auto* impl = graphics->GetImpl_Vulkan();
    VmaAllocator allocator = impl->GetAllocator();

    // Upload curves to GPU SSBO
    // Layout: [numCurves, numNPCs, pad, pad, curves...]
    struct CurveHeader {
        int numCurves;
        int numNPCs;
        int pad0;
        int pad1;
    };

    void* mapped = nullptr;
    if (vmaMapMemory(allocator, allocCurves_, &mapped) == VK_SUCCESS)
    {
        CurveHeader* header = (CurveHeader*)mapped;
        header->numCurves = (int)curves_.Size();
        header->numNPCs = 0;  // updated per dispatch
        header->pad0 = 0;
        header->pad1 = 0;

        // Copy curve data after header
        PriorityCurveData* dst = (PriorityCurveData*)((char*)mapped + sizeof(CurveHeader));
        for (unsigned i = 0; i < curves_.Size() && i < NPC_PRIORITY_MAX_CURVES; ++i)
            dst[i] = curves_[i];

        vmaUnmapMemory(allocator, allocCurves_);
    }
#endif
}

void NPCPriorityCompute::Dispatch(const Vector<GPUNPCDrivers>& npcData)
{
    numNPCs_ = (int)npcData.Size();
    if (numNPCs_ <= 0)
        return;

#ifdef URHO3D_VULKAN
    if (!computeReady_)
        return;

    auto* graphics = GetSubsystem<Graphics>();
    auto* impl = graphics->GetImpl_Vulkan();
    VmaAllocator allocator = impl->GetAllocator();
    VkPipelineLayout pipelineLayout = impl->GetComputePipelineLayout();

    int uploadCount = Min(numNPCs_, NPC_PRIORITY_MAX_NPCS);

    // Upload NPC driver data
    void* mapped = nullptr;
    if (vmaMapMemory(allocator, allocDrivers_, &mapped) == VK_SUCCESS)
    {
        memcpy(mapped, npcData.Buffer(), uploadCount * sizeof(GPUNPCDrivers));
        vmaUnmapMemory(allocator, allocDrivers_);
    }

    // Update numNPCs in curves header
    if (vmaMapMemory(allocator, allocCurves_, &mapped) == VK_SUCCESS)
    {
        // numNPCs is at offset 4 (second int in the header)
        ((int*)mapped)[1] = uploadCount;
        vmaUnmapMemory(allocator, allocCurves_);
    }

    // Record and submit compute dispatch
    VkCommandBuffer cmd = impl->BeginUploadCommandBuffer();
    if (!cmd)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelineLayout, 0, 1, &compDescSet_, 0, nullptr);

    // Dispatch: ceil(numNPCs / 64) workgroups
    unsigned numGroups = ((unsigned)uploadCount + 63) / 64;
    vkCmdDispatch(cmd, numGroups, 1, 1);

    impl->EndUploadCommandBuffer(cmd);
#endif
}

void NPCPriorityCompute::Readback(Vector<GPUNPCResult>& results)
{
    results.Resize(numNPCs_);

#ifdef URHO3D_VULKAN
    if (!computeReady_ || numNPCs_ <= 0)
        return;

    auto* graphics = GetSubsystem<Graphics>();
    auto* impl = graphics->GetImpl_Vulkan();
    VmaAllocator allocator = impl->GetAllocator();

    int readCount = Min(numNPCs_, NPC_PRIORITY_MAX_NPCS);

    void* mapped = nullptr;
    if (vmaMapMemory(allocator, allocOutput_, &mapped) == VK_SUCCESS)
    {
        memcpy(results.Buffer(), mapped, readCount * sizeof(GPUNPCResult));
        vmaUnmapMemory(allocator, allocOutput_);
    }
#endif
}

int NPCPriorityCompute::EvaluateCPU(const GPUNPCDrivers& d) const
{
    float driverValues[6] = { d.hunger, d.thirst, d.warmth, d.stamina, d.darkness, d.hpFraction };

    int bestTask = 0;  // STASK_IDLE
    float bestPriority = 0.0f;

    for (unsigned c = 0; c < curves_.Size(); ++c)
    {
        const PriorityCurveData& curve = curves_[c];
        int di = Clamp(curve.driverIndex, 0, 5);
        float dv = driverValues[di];

        // Evaluate curve via linear interpolation (same as shader)
        float priority = 0.0f;
        if (curve.numPoints <= 0)
            priority = 0.0f;
        else if (curve.numPoints == 1)
            priority = curve.points[0].output;
        else if (dv <= curve.points[0].input)
            priority = curve.points[0].output;
        else if (dv >= curve.points[curve.numPoints - 1].input)
            priority = curve.points[curve.numPoints - 1].output;
        else
        {
            for (int i = 0; i < curve.numPoints - 1; ++i)
            {
                if (dv >= curve.points[i].input && dv <= curve.points[i + 1].input)
                {
                    float range = curve.points[i + 1].input - curve.points[i].input;
                    if (range < 0.001f)
                        priority = curve.points[i].output;
                    else
                    {
                        float t = (dv - curve.points[i].input) / range;
                        priority = Lerp(curve.points[i].output, curve.points[i + 1].output, t);
                    }
                    break;
                }
            }
        }

        if (priority > bestPriority)
        {
            bestPriority = priority;
            bestTask = curve.taskId;
        }
    }

    return bestTask;
}

// ============================================================================
// Vulkan compute path
// ============================================================================

#ifdef URHO3D_VULKAN

static VkBuffer CreateSSBO_NPC(VmaAllocator allocator, VkDeviceSize size, VmaAllocation& alloc)
{
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer buffer = VK_NULL_HANDLE;
    if (vmaCreateBuffer(allocator, &bufInfo, &allocInfo, &buffer, &alloc, nullptr) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return buffer;
}

void NPCPriorityCompute::InitCompute()
{
    auto* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return;
    auto* impl = graphics->GetImpl_Vulkan();
    if (!impl || !impl->GetDevice() || !impl->GetAllocator())
        return;

    VkDevice device = impl->GetDevice();
    VmaAllocator allocator = impl->GetAllocator();
    VkPipelineLayout pipelineLayout = impl->GetComputePipelineLayout();
    VkDescriptorSetLayout descLayout = impl->GetComputeDescriptorLayout();

    if (!pipelineLayout || !descLayout)
    {
        URHO3D_LOGWARNING("NPCPriority: compute pipeline layout not available");
        return;
    }

    // Load and compile the compute shader
    auto* cache = GetSubsystem<ResourceCache>();
    SharedPtr<File> shaderFile(cache->GetFile("Shaders/GLSL/NPCPriority.comp"));
    if (!shaderFile)
    {
        URHO3D_LOGWARNING("NPCPriority: NPCPriority.comp not found — CPU fallback");
        return;
    }

    unsigned fileSize = shaderFile->GetSize();
    Vector<char> shaderSource(fileSize + 1);
    shaderFile->Read(&shaderSource[0], fileSize);
    shaderSource[fileSize] = '\0';

    Vector<uint32_t> spirv;
    String compilerOutput;
    if (!VulkanShaderCompiler::CompileGLSLToSPIRV(String(&shaderSource[0]), "", CS, spirv, compilerOutput))
    {
        URHO3D_LOGERRORF("NPCPriority: shader compile failed:\n%s", compilerOutput.CString());
        return;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spirv.Size() * sizeof(uint32_t);
    moduleInfo.pCode = spirv.Buffer();
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &compShader_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("NPCPriority: failed to create shader module");
        return;
    }

    // Create compute pipeline
    auto* compPipelineMgr = impl->GetComputePipeline();
    if (!compPipelineMgr)
    {
        URHO3D_LOGERROR("NPCPriority: compute pipeline manager not available");
        vkDestroyShaderModule(device, compShader_, nullptr);
        compShader_ = VK_NULL_HANDLE;
        return;
    }
    compPipeline_ = compPipelineMgr->GetOrCreatePipeline(pipelineLayout, compShader_);
    if (!compPipeline_)
    {
        URHO3D_LOGERROR("NPCPriority: failed to create compute pipeline");
        vkDestroyShaderModule(device, compShader_, nullptr);
        compShader_ = VK_NULL_HANDLE;
        return;
    }

    // Create SSBOs
    const VkDeviceSize driversSize = NPC_PRIORITY_MAX_NPCS * sizeof(GPUNPCDrivers);
    const VkDeviceSize curvesSize = 16 + NPC_PRIORITY_MAX_CURVES * sizeof(PriorityCurveData); // header + curves
    const VkDeviceSize outputSize = NPC_PRIORITY_MAX_NPCS * sizeof(GPUNPCResult);
    const VkDeviceSize reservedSize = 16;  // minimal placeholder

    ssboDrivers_  = CreateSSBO_NPC(allocator, driversSize, allocDrivers_);
    ssboCurves_   = CreateSSBO_NPC(allocator, curvesSize, allocCurves_);
    ssboOutput_   = CreateSSBO_NPC(allocator, outputSize, allocOutput_);
    ssboReserved_ = CreateSSBO_NPC(allocator, reservedSize, allocReserved_);

    if (!ssboDrivers_ || !ssboCurves_ || !ssboOutput_ || !ssboReserved_)
    {
        URHO3D_LOGERROR("NPCPriority: failed to create SSBOs");
        CleanupCompute();
        return;
    }

    // Zero-init SSBOs
    void* mapped = nullptr;
    if (vmaMapMemory(allocator, allocDrivers_, &mapped) == VK_SUCCESS)
    { memset(mapped, 0, driversSize); vmaUnmapMemory(allocator, allocDrivers_); }
    if (vmaMapMemory(allocator, allocCurves_, &mapped) == VK_SUCCESS)
    { memset(mapped, 0, curvesSize); vmaUnmapMemory(allocator, allocCurves_); }
    if (vmaMapMemory(allocator, allocOutput_, &mapped) == VK_SUCCESS)
    { memset(mapped, 0, outputSize); vmaUnmapMemory(allocator, allocOutput_); }
    if (vmaMapMemory(allocator, allocReserved_, &mapped) == VK_SUCCESS)
    { memset(mapped, 0, reservedSize); vmaUnmapMemory(allocator, allocReserved_); }

    // Create descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 4;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &compDescPool_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("NPCPriority: failed to create descriptor pool");
        CleanupCompute();
        return;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocSetInfo{};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = compDescPool_;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &descLayout;
    if (vkAllocateDescriptorSets(device, &allocSetInfo, &compDescSet_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("NPCPriority: failed to allocate descriptor set");
        CleanupCompute();
        return;
    }

    // Write descriptor set — bind all 4 SSBOs
    VkDescriptorBufferInfo bufInfos[4] = {};
    bufInfos[0] = {ssboDrivers_, 0, driversSize};
    bufInfos[1] = {ssboCurves_,  0, curvesSize};
    bufInfos[2] = {ssboOutput_,  0, outputSize};
    bufInfos[3] = {ssboReserved_, 0, reservedSize};

    VkWriteDescriptorSet writes[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = compDescSet_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufInfos[i];
    }
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);

    computeReady_ = true;
    URHO3D_LOGINFO("NPCPriority: Vulkan compute pipeline ready (4 SSBOs, 64 threads/group)");
}

void NPCPriorityCompute::CleanupCompute()
{
    auto* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return;
    auto* impl = graphics->GetImpl_Vulkan();
    if (!impl || !impl->GetDevice())
        return;

    VkDevice device = impl->GetDevice();
    VmaAllocator allocator = impl->GetAllocator();

    vkDeviceWaitIdle(device);

    if (compDescPool_)  { vkDestroyDescriptorPool(device, compDescPool_, nullptr); compDescPool_ = VK_NULL_HANDLE; }
    if (compShader_)    { vkDestroyShaderModule(device, compShader_, nullptr); compShader_ = VK_NULL_HANDLE; }

    // Pipeline owned by cache — don't destroy
    compPipeline_ = VK_NULL_HANDLE;

    if (ssboDrivers_)  { vmaDestroyBuffer(allocator, ssboDrivers_,  allocDrivers_);  ssboDrivers_ = VK_NULL_HANDLE; }
    if (ssboCurves_)   { vmaDestroyBuffer(allocator, ssboCurves_,   allocCurves_);   ssboCurves_ = VK_NULL_HANDLE; }
    if (ssboOutput_)   { vmaDestroyBuffer(allocator, ssboOutput_,   allocOutput_);   ssboOutput_ = VK_NULL_HANDLE; }
    if (ssboReserved_) { vmaDestroyBuffer(allocator, ssboReserved_, allocReserved_); ssboReserved_ = VK_NULL_HANDLE; }

    compDescSet_ = VK_NULL_HANDLE;
    computeReady_ = false;
}

#endif // URHO3D_VULKAN
