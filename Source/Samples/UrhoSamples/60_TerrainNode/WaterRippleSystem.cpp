// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include <Urho3D/Precompiled.h>

#include "WaterRippleSystem.h"

#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/GraphicsEvents.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/Resource/ResourceCache.h>

#ifdef URHO3D_VULKAN
#include <Urho3D/GraphicsAPI/Vulkan/VulkanGraphicsImpl.h>
#include <Urho3D/GraphicsAPI/Vulkan/VulkanShaderCompiler.h>
#endif

#include <Urho3D/DebugNew.h>

using namespace Urho3D;

WaterRippleSystem::WaterRippleSystem(Context* context) :
    Object(context)
{
    memset(curr_,   0, sizeof(curr_));
    memset(prev_,   0, sizeof(prev_));
    memset(source_, 0, sizeof(source_));
}

WaterRippleSystem::~WaterRippleSystem()
{
#ifdef URHO3D_VULKAN
    CleanupCompute();
#endif
}

void WaterRippleSystem::Initialize()
{
    // L8 (1-channel, 8-bit). Centre 128 = zero displacement.
    image_ = new Image(context_);
    image_->SetSize(GRID, GRID, 1);
    memset(image_->GetData(), 128, GRID * GRID);

    texture_ = new Texture2D(context_);
    texture_->SetFilterMode(FILTER_BILINEAR);
    texture_->SetAddressMode(COORD_U, ADDRESS_CLAMP);
    texture_->SetAddressMode(COORD_V, ADDRESS_CLAMP);
    texture_->SetData(image_);

#ifdef URHO3D_VULKAN
    InitCompute();
#endif

    URHO3D_LOGINFOF("WaterRippleSystem: initialised %dx%d grid (world +/-%.0f units, compute=%s)",
        GRID, GRID, WORLD_HALF, computeReady_ ? "YES" : "NO (CPU fallback)");
}

void WaterRippleSystem::WorldToGrid(float wx, float wz, int& gx, int& gz) const
{
    gx = static_cast<int>((wx / (WORLD_HALF * 2.0f) + 0.5f) * GRID);
    gz = static_cast<int>((wz / (WORLD_HALF * 2.0f) + 0.5f) * GRID);
}

void WaterRippleSystem::SplashAt(float worldX, float worldZ, float strength)
{
    int gx, gz;
    WorldToGrid(worldX, worldZ, gx, gz);
    if (gx < 2 || gx >= GRID - 2 || gz < 2 || gz >= GRID - 2)
        return;

    // 3x3 impulse for visibility
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            source_[(gz + dz) * GRID + (gx + dx)] += strength;

    active_ = true;
    quiescentFrames_ = 0;
}

void WaterRippleSystem::StampImpact(float worldX, float worldZ, float worldRadius, float intensity)
{
    int cx, cz;
    WorldToGrid(worldX, worldZ, cx, cz);

    // Convert world radius to grid cells
    float cellSize = (WORLD_HALF * 2.0f) / GRID;
    int gridRadius = Max(1, static_cast<int>(worldRadius / cellSize + 0.5f));

    // Clamp stamp to safe grid bounds
    int minX = Max(2, cx - gridRadius);
    int maxX = Min(GRID - 3, cx + gridRadius);
    int minZ = Max(2, cz - gridRadius);
    int maxZ = Min(GRID - 3, cz + gridRadius);

    if (minX > maxX || minZ > maxZ)
        return;

    float invR2 = 1.0f / Max(1.0f, (float)(gridRadius * gridRadius));

    for (int z = minZ; z <= maxZ; ++z)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            float dx = (float)(x - cx);
            float dz = (float)(z - cz);
            float dist2 = dx * dx + dz * dz;
            // Gaussian falloff: exp(-3 * dist^2/r^2) — reaches ~5% at the edge
            float stamp = expf(-3.0f * dist2 * invR2) * intensity;
            source_[z * GRID + x] += stamp;
        }
    }

    active_ = true;
    quiescentFrames_ = 0;
}

void WaterRippleSystem::PropagateWaves(float dt)
{
    // Normalise decay rate to 60 fps so ripples look consistent at any frame rate.
    const float damping = expf(logf(baseDamping_) * dt * 60.0f);

    // Cells within this distance of any edge are progressively damped to absorb waves
    const int absorbEdge = 12;

    for (int y = 1; y < GRID - 1; ++y)
    {
        for (int x = 1; x < GRID - 1; ++x)
        {
            int i = y * GRID + x;
            float val = (curr_[i - 1] + curr_[i + 1] +
                         curr_[i - GRID] + curr_[i + GRID]) * 0.5f - prev_[i];
            val *= damping;
            val += source_[i];

            // Absorbing boundary
            float edge = 1.0f;
            if (x < absorbEdge)            edge = Min(edge, (float)x         / absorbEdge);
            if (x >= GRID - absorbEdge)    edge = Min(edge, (float)(GRID-1-x) / absorbEdge);
            if (y < absorbEdge)            edge = Min(edge, (float)y         / absorbEdge);
            if (y >= GRID - absorbEdge)    edge = Min(edge, (float)(GRID-1-y) / absorbEdge);
            val *= edge;

            prev_[i] = curr_[i];
            curr_[i] = Clamp(val, -2.0f, 2.0f);
        }
    }
    memset(source_, 0, sizeof(source_));
}

void WaterRippleSystem::UploadTexture()
{
    unsigned char* data = image_->GetData();
    for (int i = 0; i < GRID * GRID; ++i)
    {
        // Map [-2, 2] -> [0, 255], centre at 128
        float v = curr_[i] * 0.25f + 0.5f;
        data[i] = static_cast<unsigned char>(Clamp(static_cast<int>(v * 255.0f), 0, 255));
    }
    texture_->SetData(image_);
}

void WaterRippleSystem::Update(float dt)
{
    if (!active_)
        return;

#ifdef URHO3D_VULKAN
    if (computeReady_)
    {
        DispatchCompute(dt);
        ReadbackFromGPU();
    }
    else
#endif
    {
        PropagateWaves(dt);
    }

    // Check if waves have decayed to silence — scan a sparse sample
    float maxVal = 0.0f;
    for (int i = 0; i < GRID * GRID; i += 17)
        maxVal = Max(maxVal, Abs(curr_[i]));

    if (maxVal < 0.001f)
    {
        if (++quiescentFrames_ > 30)
        {
            active_ = false;
            return;
        }
    }
    else
        quiescentFrames_ = 0;

    UploadTexture();
}

// ============================================================================
// Vulkan compute path
// ============================================================================

#ifdef URHO3D_VULKAN

static VkBuffer CreateSSBO(VmaAllocator allocator, VkDeviceSize size, VmaAllocation& alloc)
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

void WaterRippleSystem::InitCompute()
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
        URHO3D_LOGWARNING("WaterRipple: compute pipeline layout not available");
        return;
    }

    // Load and compile the compute shader
    auto* cache = GetSubsystem<ResourceCache>();
    SharedPtr<File> shaderFile(cache->GetFile("Shaders/GLSL/WaterRipple.comp"));
    if (!shaderFile)
    {
        URHO3D_LOGWARNING("WaterRipple: WaterRipple.comp not found — CPU fallback");
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
        URHO3D_LOGERRORF("WaterRipple: shader compile failed:\n%s", compilerOutput.CString());
        return;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spirv.Size() * sizeof(uint32_t);
    moduleInfo.pCode = spirv.Buffer();
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &compShader_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("WaterRipple: failed to create shader module");
        return;
    }

    // Create compute pipeline via cache
    auto* compPipelineMgr = impl->GetComputePipeline();
    if (!compPipelineMgr)
    {
        URHO3D_LOGERROR("WaterRipple: compute pipeline manager not available");
        vkDestroyShaderModule(device, compShader_, nullptr);
        compShader_ = VK_NULL_HANDLE;
        return;
    }
    compPipeline_ = compPipelineMgr->GetOrCreatePipeline(pipelineLayout, compShader_);
    if (!compPipeline_)
    {
        URHO3D_LOGERROR("WaterRipple: failed to create compute pipeline");
        vkDestroyShaderModule(device, compShader_, nullptr);
        compShader_ = VK_NULL_HANDLE;
        return;
    }

    // Create SSBOs
    const VkDeviceSize gridBytes = GRID * GRID * sizeof(float);
    ssboCurr_   = CreateSSBO(allocator, gridBytes, allocCurr_);
    ssboPrev_   = CreateSSBO(allocator, gridBytes, allocPrev_);
    ssboSource_ = CreateSSBO(allocator, gridBytes, allocSource_);
    ssboParams_ = CreateSSBO(allocator, sizeof(ComputeParams), allocParams_);

    if (!ssboCurr_ || !ssboPrev_ || !ssboSource_ || !ssboParams_)
    {
        URHO3D_LOGERROR("WaterRipple: failed to create SSBOs");
        CleanupCompute();
        return;
    }

    // Zero-init SSBOs
    void* mapped = nullptr;
    if (vmaMapMemory(allocator, allocCurr_, &mapped) == VK_SUCCESS)
    { memset(mapped, 0, gridBytes); vmaUnmapMemory(allocator, allocCurr_); }
    if (vmaMapMemory(allocator, allocPrev_, &mapped) == VK_SUCCESS)
    { memset(mapped, 0, gridBytes); vmaUnmapMemory(allocator, allocPrev_); }
    if (vmaMapMemory(allocator, allocSource_, &mapped) == VK_SUCCESS)
    { memset(mapped, 0, gridBytes); vmaUnmapMemory(allocator, allocSource_); }

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
        URHO3D_LOGERROR("WaterRipple: failed to create descriptor pool");
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
        URHO3D_LOGERROR("WaterRipple: failed to allocate descriptor set");
        CleanupCompute();
        return;
    }

    // Write descriptor set — bind all 4 SSBOs
    VkDescriptorBufferInfo bufInfos[4] = {};
    bufInfos[0] = {ssboCurr_,   0, gridBytes};
    bufInfos[1] = {ssboPrev_,   0, gridBytes};
    bufInfos[2] = {ssboSource_, 0, gridBytes};
    bufInfos[3] = {ssboParams_, 0, sizeof(ComputeParams)};

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
    URHO3D_LOGINFO("WaterRipple: Vulkan compute pipeline ready (128x128, 4 SSBOs)");
}

void WaterRippleSystem::DispatchCompute(float dt)
{
    auto* graphics = GetSubsystem<Graphics>();
    auto* impl = graphics->GetImpl_Vulkan();
    VmaAllocator allocator = impl->GetAllocator();
    VkPipelineLayout pipelineLayout = impl->GetComputePipelineLayout();

    // Upload source_ impulses to GPU (CPU writes splash data each frame)
    void* mapped = nullptr;
    const VkDeviceSize gridBytes = GRID * GRID * sizeof(float);
    if (vmaMapMemory(allocator, allocSource_, &mapped) == VK_SUCCESS)
    {
        memcpy(mapped, source_, gridBytes);
        vmaUnmapMemory(allocator, allocSource_);
    }
    memset(source_, 0, sizeof(source_));  // clear CPU side

    // Update params
    const float damping = expf(logf(baseDamping_) * dt * 60.0f);
    ComputeParams params{GRID, damping, 12, 0.0f};
    if (vmaMapMemory(allocator, allocParams_, &mapped) == VK_SUCCESS)
    {
        memcpy(mapped, &params, sizeof(params));
        vmaUnmapMemory(allocator, allocParams_);
    }

    // Record and submit compute dispatch
    VkCommandBuffer cmd = impl->BeginUploadCommandBuffer();
    if (!cmd)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelineLayout, 0, 1, &compDescSet_, 0, nullptr);

    // Dispatch: 128/8 = 16 workgroups per dimension
    vkCmdDispatch(cmd, GRID / 8, GRID / 8, 1);

    impl->EndUploadCommandBuffer(cmd);
}

void WaterRippleSystem::ReadbackFromGPU()
{
    auto* graphics = GetSubsystem<Graphics>();
    auto* impl = graphics->GetImpl_Vulkan();
    VmaAllocator allocator = impl->GetAllocator();

    // Read curr_ back from GPU SSBO
    void* mapped = nullptr;
    if (vmaMapMemory(allocator, allocCurr_, &mapped) == VK_SUCCESS)
    {
        memcpy(curr_, mapped, GRID * GRID * sizeof(float));
        vmaUnmapMemory(allocator, allocCurr_);
    }
}

void WaterRippleSystem::CleanupCompute()
{
    auto* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return;
    auto* impl = graphics->GetImpl_Vulkan();
    if (!impl || !impl->GetDevice())
        return;

    VkDevice device = impl->GetDevice();
    VmaAllocator allocator = impl->GetAllocator();

    // Wait for GPU to finish before cleanup
    vkDeviceWaitIdle(device);

    if (compDescPool_)  { vkDestroyDescriptorPool(device, compDescPool_, nullptr); compDescPool_ = VK_NULL_HANDLE; }
    if (compShader_)    { vkDestroyShaderModule(device, compShader_, nullptr); compShader_ = VK_NULL_HANDLE; }

    // Pipeline is owned by the VulkanComputePipeline cache — don't destroy here
    compPipeline_ = VK_NULL_HANDLE;

    if (ssboCurr_)   { vmaDestroyBuffer(allocator, ssboCurr_,   allocCurr_);   ssboCurr_ = VK_NULL_HANDLE; }
    if (ssboPrev_)   { vmaDestroyBuffer(allocator, ssboPrev_,   allocPrev_);   ssboPrev_ = VK_NULL_HANDLE; }
    if (ssboSource_) { vmaDestroyBuffer(allocator, ssboSource_, allocSource_); ssboSource_ = VK_NULL_HANDLE; }
    if (ssboParams_) { vmaDestroyBuffer(allocator, ssboParams_, allocParams_); ssboParams_ = VK_NULL_HANDLE; }

    compDescSet_ = VK_NULL_HANDLE;
    computeReady_ = false;
}

#endif // URHO3D_VULKAN
