//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//

#include "../../Graphics/Graphics.h"
#include "../RenderSurface.h"
#include "../Texture.h"
#include "VulkanGraphicsImpl.h"
#include "VulkanShaderCompiler.h"
#include "VulkanShaderModule.h"
#include "VulkanConstantBufferPool.h"
#include "../VulkanDefs.h"
#include "../GraphicsDefs.h"
#include "../Texture2D.h"
#include "../ShaderVariation.h"
#include "../Shader.h"
#include "../VertexBuffer.h"
#include "../../IO/Log.h"
#include <SDL/SDL.h>
#include <SDL/SDL_vulkan.h>

#ifdef _MSC_VER
#pragma warning(disable: 26812)  // Unscoped enum warning for Vulkan
#endif

namespace Urho3D
{

// Enable verbose pipeline debugging (WARNING: Severely impacts performance)
#define VULKAN_PIPELINE_DEBUG_LOGGING 1

VulkanGraphicsImpl::VulkanGraphicsImpl()
{
}

VulkanGraphicsImpl::~VulkanGraphicsImpl()
{
    Shutdown();
}

bool VulkanGraphicsImpl::Initialize(Graphics* graphics, SDL_Window* window, int width, int height)
{
    URHO3D_LOGDEBUG("[VULKAN] ========== Beginning Vulkan Initialization ==========");
    URHO3D_LOGDEBUG(String("[VULKAN] Window size: ") + String(width) + "x" + String(height));

    // Store Graphics pointer for context access throughout initialization
    graphics_ = graphics;

    // Check shader compiler availability early - prevents silent failures later
    URHO3D_LOGDEBUG("[VULKAN] Checking shader compiler availability");
    if (!VulkanShaderCompiler::CheckCompilerAvailability())
    {
        URHO3D_LOGERROR("Vulkan initialization aborted: No shader compiler available");
        return false;
    }
    URHO3D_LOGDEBUG("[VULKAN] Shader compiler check passed");

    URHO3D_LOGDEBUG("[VULKAN] Creating Vulkan instance");
    if (!CreateInstance())
    {
        URHO3D_LOGERROR("Failed to create Vulkan instance");
        return false;
    }
    URHO3D_LOGDEBUG("[VULKAN] Instance created successfully");

    URHO3D_LOGDEBUG("[VULKAN] Selecting physical device");
    if (!SelectPhysicalDevice())
    {
        URHO3D_LOGERROR("Failed to select physical device");
        return false;
    }
    URHO3D_LOGDEBUG("[VULKAN] Physical device selected successfully");

    if (!FindQueueFamilies())
    {
        URHO3D_LOGERROR("Failed to find queue families");
        return false;
    }

    if (!CreateLogicalDevice())
    {
        URHO3D_LOGERROR("Failed to create logical device");
        return false;
    }

    // Debug: Check actual window size before creating surface
    int actualWindowWidth = 0, actualWindowHeight = 0;
    SDL_GetWindowSize(window, &actualWindowWidth, &actualWindowHeight);
    URHO3D_LOGDEBUG(String("[VULKAN] SDL window size before CreateSurface: ") + String(actualWindowWidth) + "x" + String(actualWindowHeight));
    URHO3D_LOGDEBUG(String("[VULKAN] Requested init size: ") + String(width) + "x" + String(height));

    if (!CreateSurface(window))
    {
        URHO3D_LOGERROR("Failed to create window surface");
        return false;
    }

    if (!CreateSwapchain(width, height))
    {
        URHO3D_LOGERROR("Failed to create swapchain");
        return false;
    }

    // Phase 30: Use MSAA sample count for depth buffer
    if (!CreateDepthBuffer(VULKAN_PREFERRED_DEPTH_FORMAT, width, height, actualSampleCount_))
    {
        // Try fallback depth format
        if (!CreateDepthBuffer(VULKAN_FALLBACK_DEPTH_FORMAT, width, height, actualSampleCount_))
        {
            URHO3D_LOGERROR("Failed to create depth buffer");
            return false;
        }
    }

    // Phase 30: Create MSAA color image if sample count > 1x
    if (!CreateMSAAColorImage(width, height))
    {
        URHO3D_LOGWARNING("Failed to create MSAA color image, will use 1x MSAA");
        actualSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    }

    if (!CreateRenderPass())
    {
        URHO3D_LOGERROR("Failed to create render pass");
        return false;
    }

    // Create memory allocator BEFORE any image/buffer creation
    if (!CreateMemoryAllocator())
    {
        URHO3D_LOGERROR("Failed to create memory allocator");
        return false;
    }

    if (!CreateFramebuffers())
    {
        URHO3D_LOGERROR("Failed to create framebuffers");
        return false;
    }

    // Phase 34: Initialize G-Buffer for deferred rendering
    if (!CreateGBuffer(width, height))
    {
        URHO3D_LOGWARNING("Failed to create G-Buffer, deferred rendering unavailable");
        // Not fatal - forward rendering will still work
    }

    // Phase 36: Create full-screen quad for lighting pass
    if (!CreateFullScreenQuad())
    {
        URHO3D_LOGWARNING("Failed to create full-screen quad, deferred lighting unavailable");
        // Not fatal - forward rendering will still work
    }

    if (!CreateCommandBuffers())
    {
        URHO3D_LOGERROR("Failed to create command buffers");
        return false;
    }

    if (!CreateSynchronizationPrimitives())
    {
        URHO3D_LOGERROR("Failed to create synchronization primitives");
        return false;
    }

    if (!CreateDescriptorPool())
    {
        URHO3D_LOGERROR("Failed to create descriptor pool");
        return false;
    }

    // Phase 36A: Create descriptor set layouts for multi-set binding
    if (!CreateDescriptorSetLayouts())
    {
        URHO3D_LOGERROR("Failed to create descriptor set layouts");
        return false;
    }

    if (!CreatePipelineCache())
    {
        URHO3D_LOGERROR("Failed to create pipeline cache");
        return false;
    }

    // Initialize shader cache (Quick Win #5)
    shaderCache_ = MakeShared<VulkanShaderCache>(graphics->GetContext());
    if (!shaderCache_)
    {
        URHO3D_LOGERROR("Failed to create shader cache");
        return false;
    }

    // Initialize sampler cache (Quick Win #4)
    samplerCache_ = MakeShared<VulkanSamplerCache>(graphics->GetContext(), this);
    if (!samplerCache_)
    {
        URHO3D_LOGERROR("Failed to create sampler cache");
        return false;
    }

    // Initialize instance buffer manager (Phase 12)
    instanceBufferManager_ = MakeShared<VulkanInstanceBufferManager>(graphics->GetContext(), this);
    if (!instanceBufferManager_ || !instanceBufferManager_->Initialize(65536))  // 65K instances
    {
        URHO3D_LOGWARNING("Failed to initialize instance buffer manager - instancing disabled");
        instanceBufferManager_ = nullptr;
    }

    // Initialize indirect draw command manager (Phase 12)
    indirectDrawManager_ = MakeShared<VulkanIndirectDrawManager>(graphics->GetContext(), this);
    if (!indirectDrawManager_ || !indirectDrawManager_->Initialize(65536))  // 65K commands
    {
        URHO3D_LOGWARNING("Failed to initialize indirect draw manager - GPU-driven rendering disabled");
        indirectDrawManager_ = nullptr;
    }

    // Initialize staging buffer manager (Phase 10)
    stagingBufferManager_ = MakeShared<VulkanStagingBufferManager>(graphics->GetContext(), this);
    if (!stagingBufferManager_ || !stagingBufferManager_->Initialize(64 * 1024 * 1024))  // 64MB staging pool
    {
        URHO3D_LOGWARNING("Failed to initialize staging buffer manager - staging transfers disabled");
        stagingBufferManager_ = nullptr;
    }

    // Initialize compute pipeline manager (Phase 36+: Compute Shader Support)
    computePipeline_ = new VulkanComputePipeline(device_);
    if (!computePipeline_)
    {
        URHO3D_LOGWARNING("Failed to initialize compute pipeline - compute shaders disabled");
    }

    // Create compute descriptor set layout (4 SSBO bindings)
    {
        VkDescriptorSetLayoutBinding bindings[4]{};
        for (int i = 0; i < 4; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 4;
        layoutInfo.pBindings = bindings;

        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &computeDescriptorLayout_) != VK_SUCCESS)
        {
            URHO3D_LOGERROR("Failed to create compute descriptor set layout");
            computeDescriptorLayout_ = VK_NULL_HANDLE;
        }

        // Create compute pipeline layout from the descriptor set layout
        if (computeDescriptorLayout_)
        {
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &computeDescriptorLayout_;

            if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &computePipelineLayout_) != VK_SUCCESS)
            {
                URHO3D_LOGERROR("Failed to create compute pipeline layout");
                computePipelineLayout_ = VK_NULL_HANDLE;
            }
            else
            {
                URHO3D_LOGINFO("Compute pipeline layout created (4 SSBO bindings)");
            }
        }
    }

    // Phase 22A: Create default placeholder textures
    // These are used when materials don't have textures assigned
    // Diffuse: 1x1 white texture (255, 255, 255, 255)
    defaultDiffuseTexture_ = MakeShared<Texture2D>(graphics->GetContext());
    if (defaultDiffuseTexture_)
    {
        // Format: VK_FORMAT_R8G8B8A8_SRGB (44 in Vulkan)
        defaultDiffuseTexture_->SetSize(1, 1, VK_FORMAT_R8G8B8A8_SRGB, TEXTURE_STATIC);
        unsigned char diffuseData[] = {255, 255, 255, 255};
        if (!defaultDiffuseTexture_->SetData(0, 0, 0, 1, 1, diffuseData))
        {
            URHO3D_LOGWARNING("Failed to create default diffuse texture");
            defaultDiffuseTexture_ = nullptr;
        }
    }

    // Normal map: 1x1 neutral normal (128, 128, 255, 255) = (0.5, 0.5, 1.0) in float
    defaultNormalTexture_ = MakeShared<Texture2D>(graphics->GetContext());
    if (defaultNormalTexture_)
    {
        // Format: VK_FORMAT_R8G8B8A8_UNORM (37 in Vulkan)
        defaultNormalTexture_->SetSize(1, 1, VK_FORMAT_R8G8B8A8_UNORM, TEXTURE_STATIC);
        unsigned char normalData[] = {128, 128, 255, 255};
        if (!defaultNormalTexture_->SetData(0, 0, 0, 1, 1, normalData))
        {
            URHO3D_LOGWARNING("Failed to create default normal texture");
            defaultNormalTexture_ = nullptr;
        }
    }

    // Specular: 1x1 white texture (255, 255, 255, 255)
    defaultSpecularTexture_ = MakeShared<Texture2D>(graphics->GetContext());
    if (defaultSpecularTexture_)
    {
        // Format: VK_FORMAT_R8G8B8A8_SRGB (44 in Vulkan)
        defaultSpecularTexture_->SetSize(1, 1, VK_FORMAT_R8G8B8A8_SRGB, TEXTURE_STATIC);
        unsigned char specularData[] = {255, 255, 255, 255};
        if (!defaultSpecularTexture_->SetData(0, 0, 0, 1, 1, specularData))
        {
            URHO3D_LOGWARNING("Failed to create default specular texture");
            defaultSpecularTexture_ = nullptr;
        }
    }

    // Quick Win #6: Initialize constant buffer pool for material parameters
    // Use MAX_FRAMES_IN_FLIGHT regions - must match fence synchronization (not swapchain image count)
    constantBufferPool_ = MakeShared<VulkanConstantBufferPool>();
    if (constantBufferPool_)
    {
        constantBufferPool_->SetRegionCount(MAX_FRAMES_IN_FLIGHT);
        URHO3D_LOGINFO("Constant buffer pool will use " + String(MAX_FRAMES_IN_FLIGHT) + " regions (matching frames in flight)");
    }
    if (!constantBufferPool_ || !constantBufferPool_->Initialize(this))
    {
        URHO3D_LOGERROR("Failed to initialize constant buffer pool");
        return false;
    }

    /// \brief Phase 4: Initialize secondary command buffer pool for multi-threaded rendering
    /// \details Creates per-thread secondary command buffers for parallel batch recording.
    /// Uses sensible default of 4 worker threads for maximum compatibility.
    uint32_t numThreads = 4;  // Default: 4 worker threads for parallel batch recording
    secondaryCommandBufferPool_ = MakeShared<VulkanSecondaryCommandBufferPool>(this);
    if (!secondaryCommandBufferPool_ || !secondaryCommandBufferPool_->Initialize(numThreads))
    {
        URHO3D_LOGWARNING("Failed to initialize secondary command buffer pool - parallel rendering disabled");
        secondaryCommandBufferPool_ = nullptr;
    }

    URHO3D_LOGINFO("Vulkan graphics initialization successful");
    URHO3D_LOGINFO(String("Swapchain extent: ") + String(swapchainExtent_.width) + "x" + String(swapchainExtent_.height));

    return true;
}

void VulkanGraphicsImpl::DeferBufferDeletion(VkBuffer buffer, VmaAllocation allocation)
{
    if (!buffer) return;
    MutexLock lock(deferredDeletionMutex_);
    // Inside a compute-only pump the per-frame queues never drain (no BeginFrame/AcquireNextImage),
    // so route into the compute-owned bucket which FlushComputeDeferredDeletions() drains on the
    // compute fence. Outside that scope, keep the original render-frame behaviour untouched.
    if (deferToComputeBucket_)
    {
        computeDeferredDeletions_.Push({buffer, allocation});
        return;
    }
    uint32_t frame = currentFrame_ < MAX_FRAMES_IN_FLIGHT ? currentFrame_ : 0;
    deferredDeletions_[frame].Push({buffer, allocation});
}

void VulkanGraphicsImpl::ProcessDeferredDeletions()
{
    MutexLock lock(deferredDeletionMutex_);
    uint32_t frame = currentFrame_ < MAX_FRAMES_IN_FLIGHT ? currentFrame_ : 0;
    for (auto& del : deferredDeletions_[frame])
    {
        if (del.buffer && allocator_)
            vmaDestroyBuffer(allocator_, del.buffer, del.allocation);
    }
    deferredDeletions_[frame].Clear();
}

void VulkanGraphicsImpl::SetComputeDeletionScope(bool enable)
{
    MutexLock lock(deferredDeletionMutex_);
    deferToComputeBucket_ = enable;
}

void VulkanGraphicsImpl::FlushComputeDeferredDeletions()
{
    // Drains the compute-owned bucket only. The caller must have observed the compute fence for the
    // work that used these buffers signalled (page/batch boundary) — the render frame fence does NOT
    // guard these, so this path deliberately never touches deferredDeletions_[] (frame-owned).
    MutexLock lock(deferredDeletionMutex_);
    for (auto& del : computeDeferredDeletions_)
    {
        if (del.buffer && allocator_)
            vmaDestroyBuffer(allocator_, del.buffer, del.allocation);
    }
    computeDeferredDeletions_.Clear();
}

bool VulkanGraphicsImpl::RecreateSwapchainResources(SDL_Window* window, int width, int height)
{
    if (!device_)
        return false;

    vkDeviceWaitIdle(device_);

    // Flush deferred deletions (device is idle here, so both the per-frame and compute buckets are safe)
    {
        MutexLock lock(deferredDeletionMutex_);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            for (auto& del : deferredDeletions_[i])
            {
                if (del.buffer && allocator_)
                    vmaDestroyBuffer(allocator_, del.buffer, del.allocation);
            }
            deferredDeletions_[i].Clear();
        }
        for (auto& del : computeDeferredDeletions_)
        {
            if (del.buffer && allocator_)
                vmaDestroyBuffer(allocator_, del.buffer, del.allocation);
        }
        computeDeferredDeletions_.Clear();
    }

    // Destroy swapchain-dependent resources (reverse creation order)

    // G-Buffer
    DestroyGBuffer();

    // Framebuffers
    for (auto framebuffer : framebuffers_)
    {
        if (framebuffer)
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.Clear();

    // RTT framebuffer cache
    for (auto& pair : rttFramebufferCache_)
    {
        if (pair.second_)
            vkDestroyFramebuffer(device_, pair.second_, nullptr);
    }
    rttFramebufferCache_.Clear();
    renderTargetFramebuffer_ = VK_NULL_HANDLE;

    // Render passes
    for (auto& pair : renderPassCache_)
    {
        if (pair.second_ && pair.second_ != renderPass_)
            vkDestroyRenderPass(device_, pair.second_, nullptr);
    }
    renderPassCache_.Clear();
    renderPassToDescHash_.Clear();

    if (renderPassLoad_)
    {
        vkDestroyRenderPass(device_, renderPassLoad_, nullptr);
        renderPassLoad_ = nullptr;
    }
    if (renderPass_)
    {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = nullptr;
    }

    // MSAA color buffer
    if (msaaColorImageView_)
    {
        vkDestroyImageView(device_, msaaColorImageView_, nullptr);
        msaaColorImageView_ = nullptr;
    }
    if (msaaColorImage_)
    {
        vmaDestroyImage(allocator_, msaaColorImage_, msaaColorAllocation_);
        msaaColorImage_ = VK_NULL_HANDLE;
        msaaColorAllocation_ = nullptr;
    }

    // Depth buffer
    if (depthImageView_)
    {
        vkDestroyImageView(device_, depthImageView_, nullptr);
        depthImageView_ = nullptr;
    }
    if (depthImage_)
    {
        vkDestroyImage(device_, depthImage_, nullptr);
        depthImage_ = nullptr;
    }
    if (depthImageMemory_)
    {
        vkFreeMemory(device_, depthImageMemory_, nullptr);
        depthImageMemory_ = nullptr;
    }

    // RTT depth buffers (cached by dimensions/format)
    DestroyRttDepthCache();

    // Swapchain image views
    for (auto imageView : swapchainImageViews_)
    {
        if (imageView)
            vkDestroyImageView(device_, imageView, nullptr);
    }
    swapchainImageViews_.Clear();
    swapchainImages_.Clear();

    // Swapchain
    if (swapchain_)
    {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = nullptr;
    }

    // Per-image semaphores
    for (auto& semaphores : imageSemaphores_)
    {
        if (semaphores.renderComplete != VK_NULL_HANDLE)
            vkDestroySemaphore(device_, semaphores.renderComplete, nullptr);
    }
    imageSemaphores_.Clear();

    // Per-frame sync primitives
    for (auto& frame : frames_)
    {
        if (frame.imageAcquired != VK_NULL_HANDLE)
            vkDestroySemaphore(device_, frame.imageAcquired, nullptr);
        if (frame.fence != VK_NULL_HANDLE)
            vkDestroyFence(device_, frame.fence, nullptr);
    }
    frames_.Clear();

    // Surface
    if (surface_)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = nullptr;
    }

    // Reset descriptor pools (don't destroy — just reset for reuse)
    for (unsigned i = 0; i < descriptorPools_.Size(); ++i)
    {
        if (descriptorPools_[i])
            vkResetDescriptorPool(device_, descriptorPools_[i], 0);
    }

    // Reset frame tracking
    currentFrame_ = 0;
    currentImageIndex_ = 0;
    renderPassActive_ = false;
    renderingToTexture_ = false;

    // --- Recreate swapchain-dependent resources ---

    if (!CreateSurface(window))
    {
        URHO3D_LOGERROR("Failed to recreate surface");
        return false;
    }

    if (!CreateSwapchain(width, height))
    {
        URHO3D_LOGERROR("Failed to recreate swapchain");
        return false;
    }

    if (!CreateDepthBuffer(VULKAN_PREFERRED_DEPTH_FORMAT, width, height, actualSampleCount_))
    {
        if (!CreateDepthBuffer(VULKAN_FALLBACK_DEPTH_FORMAT, width, height, actualSampleCount_))
        {
            URHO3D_LOGERROR("Failed to recreate depth buffer");
            return false;
        }
    }

    if (!CreateMSAAColorImage(width, height))
    {
        URHO3D_LOGWARNING("Failed to recreate MSAA color image");
        actualSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    }

    if (!CreateRenderPass())
    {
        URHO3D_LOGERROR("Failed to recreate render pass");
        return false;
    }

    if (!CreateFramebuffers())
    {
        URHO3D_LOGERROR("Failed to recreate framebuffers");
        return false;
    }

    if (!CreateGBuffer(width, height))
    {
        URHO3D_LOGWARNING("Failed to recreate G-Buffer");
    }

    // Reset existing command pool instead of creating a new one
    if (commandPool_)
        vkResetCommandPool(device_, commandPool_, 0);

    // Recreate per-frame resources (command buffers, semaphores, fences)
    frames_.Resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        FrameResources& frame = frames_[i];
        vkAllocateCommandBuffers(device_, &allocInfo, &frame.commandBuffer);
        vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAcquired);
        vkCreateFence(device_, &fenceInfo, nullptr, &frame.fence);
    }

    return true;
}

void VulkanGraphicsImpl::Shutdown()
{
    if (device_)
    {
        vkDeviceWaitIdle(device_);
    }

    // Flush all deferred deletions (both per-frame and compute buckets — device is idle at teardown)
    {
        MutexLock lock(deferredDeletionMutex_);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            for (auto& del : deferredDeletions_[i])
            {
                if (del.buffer && allocator_)
                    vmaDestroyBuffer(allocator_, del.buffer, del.allocation);
            }
            deferredDeletions_[i].Clear();
        }
        for (auto& del : computeDeferredDeletions_)
        {
            if (del.buffer && allocator_)
                vmaDestroyBuffer(allocator_, del.buffer, del.allocation);
        }
        computeDeferredDeletions_.Clear();
    }

    // Destroy cached descriptor set layouts (PERFORMANCE FIX)
    for (auto& pair : descriptorSetLayoutCache_)
    {
        if (pair.second_)
            vkDestroyDescriptorSetLayout(device_, pair.second_, nullptr);
    }
    descriptorSetLayoutCache_.Clear();

    // Destroy cached pipeline layouts (PERFORMANCE FIX)
    for (auto& pair : pipelineLayoutCache_)
    {
        if (pair.second_)
            vkDestroyPipelineLayout(device_, pair.second_, nullptr);
    }
    pipelineLayoutCache_.Clear();

    // Destroy cached shader modules (PERFORMANCE FIX)
    for (auto& pair : shaderModuleCache_)
    {
        if (pair.second_)
            vkDestroyShaderModule(device_, pair.second_, nullptr);
    }
    shaderModuleCache_.Clear();

    // Destroy framebuffers
    for (auto framebuffer : framebuffers_)
    {
        if (framebuffer)
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.Clear();

    // Destroy cached render passes (RTT, shadow maps, etc.)
    for (auto& pair : renderPassCache_)
    {
        if (pair.second_ && pair.second_ != renderPass_)
            vkDestroyRenderPass(device_, pair.second_, nullptr);
    }
    renderPassCache_.Clear();
    renderPassToDescHash_.Clear();

    // Destroy main render pass and LOAD variant
    if (renderPassLoad_)
    {
        vkDestroyRenderPass(device_, renderPassLoad_, nullptr);
        renderPassLoad_ = nullptr;
    }
    if (renderPass_)
    {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = nullptr;
    }

    // Destroy depth buffer
    if (depthImageView_)
    {
        vkDestroyImageView(device_, depthImageView_, nullptr);
        depthImageView_ = nullptr;
    }
    if (depthImage_)
    {
        vkDestroyImage(device_, depthImage_, nullptr);
        depthImage_ = nullptr;
    }
    if (depthImageMemory_)
    {
        vkFreeMemory(device_, depthImageMemory_, nullptr);
        depthImageMemory_ = nullptr;
    }

    // Destroy all cached RTT depth buffers
    DestroyRttDepthCache();

    // Phase 30: Destroy MSAA color buffer if present
    if (msaaColorImageView_)
    {
        vkDestroyImageView(device_, msaaColorImageView_, nullptr);
        msaaColorImageView_ = nullptr;
    }
    if (msaaColorImage_)
    {
        vmaDestroyImage(allocator_, msaaColorImage_, msaaColorAllocation_);
        msaaColorImage_ = VK_NULL_HANDLE;
        msaaColorAllocation_ = nullptr;
    }

    // RTT: Destroy cached framebuffers
    for (auto& pair : rttFramebufferCache_)
    {
        if (pair.second_)
            vkDestroyFramebuffer(device_, pair.second_, nullptr);
    }
    rttFramebufferCache_.Clear();
    renderTargetFramebuffer_ = VK_NULL_HANDLE;
    renderingToTexture_ = false;

    // Phase 34: Destroy G-Buffer for deferred rendering
    DestroyGBuffer();

    // Phase 36: Destroy full-screen quad buffers
    DestroyFullScreenQuad();

    // Destroy swapchain image views
    for (auto imageView : swapchainImageViews_)
    {
        if (imageView)
            vkDestroyImageView(device_, imageView, nullptr);
    }
    swapchainImageViews_.Clear();
    swapchainImages_.Clear();

    // Destroy swapchain
    if (swapchain_)
    {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = nullptr;
    }

    // Clean up per-image renderComplete semaphores
    for (auto& semaphores : imageSemaphores_)
    {
        if (semaphores.renderComplete != VK_NULL_HANDLE)
            vkDestroySemaphore(device_, semaphores.renderComplete, nullptr);
    }
    imageSemaphores_.Clear();

    // Clean up per-frame resources
    for (auto& frame : frames_)
    {
        if (frame.imageAcquired != VK_NULL_HANDLE)
            vkDestroySemaphore(device_, frame.imageAcquired, nullptr);
        if (frame.fence != VK_NULL_HANDLE)
            vkDestroyFence(device_, frame.fence, nullptr);
    }
    frames_.Clear();

    // Destroy timeline semaphore if present (Phase 33)
    if (timelineRenderSemaphore_)
    {
        vkDestroySemaphore(device_, timelineRenderSemaphore_, nullptr);
        timelineRenderSemaphore_ = nullptr;
    }

    // Destroy descriptor pools (Per-image fix: one per swapchain image)
    for (unsigned i = 0; i < descriptorPools_.Size(); ++i)
    {
        if (descriptorPools_[i])
        {
            vkDestroyDescriptorPool(device_, descriptorPools_[i], nullptr);
            descriptorPools_[i] = VK_NULL_HANDLE;
        }
    }
    descriptorPools_.Clear();

    // Phase 36A: Destroy descriptor set layouts
    if (materialDescriptorLayout_)
    {
        vkDestroyDescriptorSetLayout(device_, materialDescriptorLayout_, nullptr);
        materialDescriptorLayout_ = VK_NULL_HANDLE;
    }
    if (gbufferTextureLayout_)
    {
        vkDestroyDescriptorSetLayout(device_, gbufferTextureLayout_, nullptr);
        gbufferTextureLayout_ = VK_NULL_HANDLE;
    }
    if (constantBufferLayout_)
    {
        vkDestroyDescriptorSetLayout(device_, constantBufferLayout_, nullptr);
        constantBufferLayout_ = VK_NULL_HANDLE;
    }
    if (inputAttachmentLayout_)
    {
        vkDestroyDescriptorSetLayout(device_, inputAttachmentLayout_, nullptr);
        inputAttachmentLayout_ = VK_NULL_HANDLE;
    }

    // Destroy pipeline cache (SharedPtr auto-cleanup)
    if (pipelineCache_)
    {
        pipelineCache_->Release();
        pipelineCache_ = nullptr;
    }

    // Destroy compute pipeline layout and descriptor layout
    if (computePipelineLayout_)
    {
        vkDestroyPipelineLayout(device_, computePipelineLayout_, nullptr);
        computePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (computeDescriptorLayout_)
    {
        vkDestroyDescriptorSetLayout(device_, computeDescriptorLayout_, nullptr);
        computeDescriptorLayout_ = VK_NULL_HANDLE;
    }

    // Destroy compute pipeline (Phase 36+: Compute Shader Support)
    if (computePipeline_)
    {
        delete computePipeline_;
        computePipeline_ = nullptr;
    }

    // Destroy sampler cache (SharedPtr auto-cleanup)
    if (samplerCache_)
    {
        samplerCache_->Reset();
        samplerCache_ = nullptr;
    }

    // Destroy secondary command buffer pool (per-thread command pools and buffers)
    if (secondaryCommandBufferPool_)
    {
        secondaryCommandBufferPool_ = nullptr;
    }

    // Destroy thread-local upload command pools
    {
        MutexLock lock(threadUploadCommandPoolsMutex_);
        for (auto& pair : threadUploadCommandPools_)
        {
            if (pair.second_)
            {
                vkDestroyCommandPool(device_, pair.second_, nullptr);
            }
        }
        threadUploadCommandPools_.Clear();
    }

    // Destroy command pool (frees all command buffers allocated from it)
    if (commandPool_)
    {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = nullptr;
    }

    // Release constant buffer pool before VMA allocator
    if (constantBufferPool_)
    {
        constantBufferPool_->Release();
        constantBufferPool_ = nullptr;
    }

    // Release manager buffers before VMA allocator
    if (instanceBufferManager_)
    {
        instanceBufferManager_->Release();
        instanceBufferManager_ = nullptr;
    }
    if (indirectDrawManager_)
    {
        indirectDrawManager_->Release();
        indirectDrawManager_ = nullptr;
    }
    if (stagingBufferManager_)
    {
        stagingBufferManager_->Release();
        stagingBufferManager_ = nullptr;
    }

    // Destroy the global pipeline layout (created at init, not in cache)
    if (globalPipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, globalPipelineLayout_, nullptr);
        globalPipelineLayout_ = VK_NULL_HANDLE;
    }
    currentPipelineLayout_ = VK_NULL_HANDLE;

    // Destroy memory allocator
    if (allocator_)
    {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }

    // Destroy surface
    if (surface_)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = nullptr;
    }

    // Destroy device before instance-level objects
    if (device_)
    {
        vkDestroyDevice(device_, nullptr);
        device_ = nullptr;
    }

    // Destroy debug messenger (instance-level, must be after device)
    if (debugMessenger_ && instance_)
    {
        auto vkDestroyDebugUtilsMessengerEXT =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (vkDestroyDebugUtilsMessengerEXT)
            vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
        debugMessenger_ = nullptr;
    }

    // Destroy instance
    if (instance_)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = nullptr;
    }

    // Release caches
    if (shaderCache_)
    {
        shaderCache_->Release();
        shaderCache_ = nullptr;
    }
    if (samplerCache_)
    {
        samplerCache_ = nullptr;
    }
}

AcquireResult VulkanGraphicsImpl::AcquireNextImage()
{
    if (!swapchain_ || imageSemaphores_.Empty())
        return AcquireResult::Error;

    // Get current frame resources
    FrameResources& frame = frames_[currentFrame_];

    // Wait for this frame to complete (guarantees all resources are free)
    // Use 5-second timeout to avoid hanging on shutdown
    VkResult fenceResult = vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, 5000000000ULL);
    if (fenceResult == VK_TIMEOUT)
    {
        URHO3D_LOGWARNING("Fence wait timed out (5s), GPU may be stuck or app shutting down");
        // Don't reset fence — it was never signaled
        return AcquireResult::Timeout;
    }

    // Process deferred deletions for this frame (GPU finished using these resources)
    ProcessDeferredDeletions();

    // Acquire next swapchain image using THIS frame's imageAcquired semaphore
    // Use finite timeout — UINT64_MAX blocks forever if window is obscured/minimized
    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        device_,
        swapchain_,
        100000000ULL,  // 100ms timeout — retry on next frame if window obscured
        frame.imageAcquired,  // Per-frame semaphore (signaled when image is available)
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_TIMEOUT)
    {
        // Window likely obscured/minimized — skip this frame, don't log (normal behavior)
        // Don't reset fence — no new work will be submitted this frame
        return AcquireResult::Timeout;
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        URHO3D_LOGINFO("Swapchain out of date during acquire, needs recreation");
        // Don't reset fence — caller will recreate swapchain and retry
        return AcquireResult::OutOfDate;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        URHO3D_LOGERROR("Failed to acquire swapchain image: " + String((int)result));
        return AcquireResult::Error;
    }

    // Acquire succeeded — now safe to reset fence (we will submit new work this frame)
    vkResetFences(device_, 1, &frame.fence);

    // Store which image this frame will render to
    frame.imageIndex = imageIndex;

    // Update global index for compatibility (can remove later)
    currentImageIndex_ = frame.imageIndex;

    return AcquireResult::Success;
}

void VulkanGraphicsImpl::Present()
{
    if (!swapchain_ || imageSemaphores_.Empty())
        return;

    FrameResources& frame = frames_[currentFrame_];

    // Get the renderComplete semaphore for THIS image
    VkSemaphore imageRenderComplete = imageSemaphores_[frame.imageIndex].renderComplete;

    if (frame.commandBuffer && graphicsQueue_)
    {
        // Submit: wait on frame.imageAcquired, signal THIS image's renderComplete
        VkSemaphore waitSemaphores[] = { frame.imageAcquired };
        VkSemaphore signalSemaphores[] = { imageRenderComplete };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &frame.commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        // Serialise with every other graphics-queue submit (upload + the off-render compute
        // worker). vkQueueSubmit is externally-synchronised; the frame fence is waited at the
        // next BeginFrame, OUTSIDE this lock, so the lock only covers the submit call itself.
        VkResult submitResult;
        {
            MutexLock lock(queueSubmitMutex_);
            submitResult = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frame.fence);
        }
        if (submitResult != VK_SUCCESS)
        {
            URHO3D_LOGERROR("Failed to submit command buffer: " + String((int)submitResult));
            return;
        }
    }

    // Present: wait on THIS image's renderComplete semaphore
    VkSemaphore waitSemaphores[] = { imageRenderComplete };
    VkSwapchainKHR swapchains[] = { swapchain_ };
    uint32_t imageIndices[] = { frame.imageIndex };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = waitSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = imageIndices;

    // Present is a queue operation on the (graphics-family) present queue — serialise it with
    // the same mutex so it can't race the off-render compute worker's submit on the queue.
    VkResult presentResult;
    {
        MutexLock lock(queueSubmitMutex_);
        presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    }
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        URHO3D_LOGINFO("Swapchain out of date/suboptimal during present");
        // Frame was still submitted — just log and continue, next BeginFrame will handle it
    }
    else if (presentResult != VK_SUCCESS)
    {
        URHO3D_LOGWARNING("Present failed: " + String((int)presentResult));
    }

    // Advance to next frame
    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    frameIndex_ = (frameIndex_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkCommandBuffer VulkanGraphicsImpl::GetFrameCommandBuffer() const
{
    if (currentFrame_ < frames_.Size())
        return frames_[currentFrame_].commandBuffer;
    return VK_NULL_HANDLE;
}

void VulkanGraphicsImpl::WaitForFrameFence()
{
    if (frameIndex_ >= frames_.Size())
        return;

    // PHASE 33: Prefer timeline semaphore waits for non-blocking synchronization
    if (supportsTimelineSemaphores_ && timelineRenderSemaphore_ != VK_NULL_HANDLE)
    {
        // Wait on timeline semaphore instead of fence - allows other GPU work to proceed
        // This provides ~1-2ms improvement by avoiding CPU blocking
        WaitOnTimelineRenderSemaphore(timelineRenderCounter_);
    }
    else
    {
        // Fallback: CPU-blocking fence wait with timeout to detect hangs
        VkFence fence = (frameIndex_ < frames_.Size()) ? frames_[frameIndex_].fence : VK_NULL_HANDLE;
        if (fence != VK_NULL_HANDLE)
        {
            VkResult waitResult = vkWaitForFences(device_, 1, &fence, VK_TRUE, 10000000000ULL); // 10 seconds

            if (waitResult == VK_TIMEOUT)
            {
                URHO3D_LOGERROR(String("Frame fence wait TIMED OUT after 10 seconds! Frame=") + String(frameIndex_));
            }
            else if (waitResult != VK_SUCCESS)
            {
                URHO3D_LOGERROR(String("Frame fence wait FAILED with result=") + String((int)waitResult));

                // Detect device loss at fence wait point
                if (waitResult == VK_ERROR_DEVICE_LOST)
                {
                    deviceLost_ = true;
                    URHO3D_LOGERROR("DEVICE LOST detected at vkWaitForFences (frame fence, detection point)");
                }
            }
        }
    }
}

void VulkanGraphicsImpl::ResetFrameCommandBuffer()
{
    VkCommandBuffer cmdBuffer = GetFrameCommandBuffer();
    if (cmdBuffer)
    {
        vkResetCommandBuffer(cmdBuffer, 0);
    }
}

void VulkanGraphicsImpl::BeginRenderPass()
{
    if (!renderPass_ || !device_)
    {
        URHO3D_LOGERROR("BeginRenderPass: Missing renderPass or device");
        return;
    }

    VkCommandBuffer cmdBuffer = GetFrameCommandBuffer();
    if (!cmdBuffer)
    {
        URHO3D_LOGERROR("BeginRenderPass: No command buffer");
        return;
    }

    // Handle framebuffer rebuild if render targets changed
    if (renderTargetsDirty_)
        RebuildRenderTargetFramebuffer();

    // Determine which framebuffer and render pass to use
    VkFramebuffer currentFramebuffer;
    VkRenderPass currentRenderPass;
    VkExtent2D renderExtent;
    uint32_t colorAttachmentCount;

    if (renderingToTexture_ && renderTargetFramebuffer_ != VK_NULL_HANDLE && renderTargetRenderPass_ != VK_NULL_HANDLE)
    {
        currentFramebuffer = renderTargetFramebuffer_;
        currentRenderPass = renderTargetRenderPass_;
        renderExtent = {(uint32_t)rttWidth_, (uint32_t)rttHeight_};
        colorAttachmentCount = 0;
        if (graphics_)
        {
            for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
            {
                if (graphics_->GetRenderTarget(i))
                    colorAttachmentCount = i + 1;
            }
        }

        // Check if any current RT was already written this frame → use LOAD to preserve content
        bool anyAlreadyWritten = false;
        // Hybrid framebuffer: swapchain color was already written if swapchainPassUsedThisFrame_
        if (renderTargetRPDescriptor_.isSwapchainHybrid && swapchainPassUsedThisFrame_)
            anyAlreadyWritten = true;
        if (!anyAlreadyWritten && graphics_)
        {
            for (unsigned i = 0; i < colorAttachmentCount; ++i)
            {
                RenderSurface* rt = graphics_->GetRenderTarget(i);
                if (rt && writtenRenderTargets_.Contains((void*)rt))
                {
                    anyAlreadyWritten = true;
                    break;
                }
            }
        }
        if (anyAlreadyWritten)
        {
            // Create LOAD variant render pass (compatible with same framebuffer)
            RenderPassDescriptor loadDesc = renderTargetRPDescriptor_;
            loadDesc.colorLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            VkRenderPass loadPass = GetOrCreateRenderPass(loadDesc);
            if (loadPass != VK_NULL_HANDLE)
                currentRenderPass = loadPass;
        }
    }
    else
    {
        currentFramebuffer = GetCurrentFramebuffer();
        // Use LOAD variant if swapchain was already rendered to this frame
        // This preserves color+depth from the base pass after shadow map rendering
        currentRenderPass = swapchainPassUsedThisFrame_ ? renderPassLoad_ : renderPass_;
        renderExtent = swapchainExtent_;
        colorAttachmentCount = 1;
    }

    // Prepare clear values: N color attachments + 1 depth
    // Uses values stored by Clear_Vulkan (defaults to black if Clear not called)
    uint32_t clearValueCount = colorAttachmentCount + 1;
    VkClearValue clearValues[MAX_RENDERTARGETS + 1]{};
    for (uint32_t i = 0; i < colorAttachmentCount; ++i)
        clearValues[i].color = {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};
    clearValues[colorAttachmentCount].depthStencil = {clearDepth_, clearStencil_};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = currentRenderPass;
    renderPassInfo.framebuffer = currentFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = renderExtent;
    renderPassInfo.clearValueCount = clearValueCount;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    renderPassActive_ = true;

    // Save render targets active during this render pass (for tracking in EndRenderPass)
    activePassColorCount_ = colorAttachmentCount;
    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
    {
        if (renderingToTexture_ && graphics_ && i < colorAttachmentCount)
            activePassRenderTargets_[i] = (void*)graphics_->GetRenderTarget(i);
        else
            activePassRenderTargets_[i] = nullptr;
    }

    // Track swapchain usage for LOAD variant selection on re-entry
    // Hybrid framebuffers (swapchain color + custom depth) also write to swapchain
    if (!renderingToTexture_ || renderTargetRPDescriptor_.isSwapchainHybrid)
        swapchainPassUsedThisFrame_ = true;
}

void VulkanGraphicsImpl::EndRenderPass()
{
    if (!renderPassActive_)
        return;

    VkCommandBuffer cmdBuffer = GetFrameCommandBuffer();
    if (!cmdBuffer)
        return;

    // Track which render targets were written (for LOAD vs CLEAR on re-entry)
    // Use SAVED targets from BeginRenderPass, not current ones (which may have changed)
    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
    {
        if (activePassRenderTargets_[i])
            writtenRenderTargets_.Insert(activePassRenderTargets_[i]);
    }

    vkCmdEndRenderPass(cmdBuffer);
    renderPassActive_ = false;
    lastBoundPipeline_ = VK_NULL_HANDLE;  // Pipeline binding invalidated by render pass change
}

void VulkanGraphicsImpl::EnsureRenderPassStarted()
{
    // If render targets changed while pass is active, check if framebuffer actually changes
    if (renderTargetsDirty_ && renderPassActive_)
    {
        bool wasRTT = renderingToTexture_;
        VkFramebuffer oldFB = renderTargetFramebuffer_;
        RebuildRenderTargetFramebuffer();
        bool isRTT = renderingToTexture_;
        VkFramebuffer newFB = renderTargetFramebuffer_;

        // Restart if switching swapchain↔RTT, or between different RTTs (e.g. shadow maps)
        if (wasRTT != isRTT || (wasRTT && isRTT && oldFB != newFB))
        {
            EndRenderPass();

            // Insert barrier to ensure previous writes are visible when sampled
            if (wasRTT && !isRTT)
            {
                VkCommandBuffer cmdBuffer = GetFrameCommandBuffer();
                if (cmdBuffer)
                {
                    VkMemoryBarrier memBarrier{};
                    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    memBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                    vkCmdPipelineBarrier(cmdBuffer,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

                    // Depth RTT layout transition is handled by render pass finalLayout
                    // (ATTACHMENT_OPTIMAL -> READ_ONLY_OPTIMAL set when isRenderToTexture)
                }
            }
        }
    }

    if (!renderPassActive_)
    {
        // Insert memory barrier to synchronize host writes (instance buffer) with vertex input
        // This must happen BEFORE render pass begins
        VkCommandBuffer cmdBuffer = GetFrameCommandBuffer();
        if (cmdBuffer)
        {
            VkMemoryBarrier memBarrier{};
            memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

            vkCmdPipelineBarrier(cmdBuffer,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                0, 1, &memBarrier, 0, nullptr, 0, nullptr);
        }

        BeginRenderPass();
    }
}

void VulkanGraphicsImpl::NextSubpass()
{
    // Phase 36: Transition between subpasses (geometry to lighting in deferred rendering)
    if (!renderPassActive_)
    {
        URHO3D_LOGWARNING("NextSubpass called outside of active render pass");
        return;
    }

    VkCommandBuffer cmdBuffer = GetFrameCommandBuffer();
    if (!cmdBuffer)
        return;

    // Transition from geometry pass (subpass 0) to lighting pass (subpass 1)
    vkCmdNextSubpass(cmdBuffer, VK_SUBPASS_CONTENTS_INLINE);
}

VkCommandPool VulkanGraphicsImpl::CreateThreadComputePool()
{
    // Created against the COMPUTE queue family (GetComputeQueueFamily()) — command buffers must be
    // allocated from a pool whose family matches the queue they submit to, and training dispatches
    // now submit to GetComputeQueue() (the dedicated async-compute queue when available, else the
    // graphics queue — in which case this resolves to graphicsQueueFamily_, the old behaviour).
    // RESET_COMMAND_BUFFER so the per-dispatch alloc/free pattern is valid. Owned by the caller.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = GetComputeQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &pool) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create worker compute command pool");
        return VK_NULL_HANDLE;
    }
    return pool;
}

void VulkanGraphicsImpl::DestroyCommandPool(VkCommandPool pool)
{
    if (pool != VK_NULL_HANDLE && device_)
        vkDestroyCommandPool(device_, pool, nullptr);
}

VkDescriptorPool VulkanGraphicsImpl::CreateThreadComputeDescriptorPool(uint32_t maxSets, uint32_t descriptorCount)
{
    // Default size = one resident training step (~90 compute dispatches, each a set of <=4 storage
    // buffers). The GPU minibatch page passes larger maxSets/descriptorCount so a whole P-sequence
    // page's sets coexist until its single submit completes. The worker resets the whole pool at
    // each page boundary (reset-not-free), so no FREE_DESCRIPTOR_SET flag. Owned by the worker
    // thread; never the shared per-frame pool (the render thread resets that every frame — fatal to
    // an off-render worker's live sets).
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = descriptorCount;

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = maxSets;
    info.poolSizeCount = 1;
    info.pPoolSizes = &poolSize;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device_, &info, nullptr, &pool) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create worker compute descriptor pool");
        return VK_NULL_HANDLE;
    }
    return pool;
}

void VulkanGraphicsImpl::ResetDescriptorPool(VkDescriptorPool pool)
{
    if (pool != VK_NULL_HANDLE && device_)
        vkResetDescriptorPool(device_, pool, 0);
}

void VulkanGraphicsImpl::DestroyDescriptorPool(VkDescriptorPool pool)
{
    if (pool != VK_NULL_HANDLE && device_)
        vkDestroyDescriptorPool(device_, pool, nullptr);
}

VkDeviceSize VulkanGraphicsImpl::GetDeviceLocalHeapSize() const
{
    if (physicalDevice_ == VK_NULL_HANDLE)
        return 0;
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
    VkDeviceSize total = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            total += memProps.memoryHeaps[i].size;
    return total;
}

VkCommandBuffer VulkanGraphicsImpl::BeginUploadCommandBuffer()
{
    // Get current thread ID
    ThreadID currentThread = Thread::GetCurrentThreadID();

    // Get or create command pool for this thread (thread-safe)
    VkCommandPool threadPool = VK_NULL_HANDLE;
    {
        MutexLock lock(threadUploadCommandPoolsMutex_);

        // Check if this thread already has a command pool
        auto it = threadUploadCommandPools_.Find(currentThread);
        if (it == threadUploadCommandPools_.End())
        {
            // Create new command pool for this thread
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = graphicsQueueFamily_;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            VkCommandPool newPool = VK_NULL_HANDLE;
            if (vkCreateCommandPool(device_, &poolInfo, nullptr, &newPool) != VK_SUCCESS)
            {
                URHO3D_LOGERROR("Failed to create thread-local upload command pool");
                return VK_NULL_HANDLE;
            }

            threadUploadCommandPools_[currentThread] = newPool;
            threadPool = newPool;
        }
        else
        {
            threadPool = it->second_;
        }
    }

    // Allocate command buffer from thread-local pool (no contention between threads)
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;  // CRITICAL: Must be nullptr to avoid garbage pointer
    allocInfo.commandPool = threadPool;  // Thread-local pool
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to allocate upload command buffer from thread-local pool");
        return VK_NULL_HANDLE;
    }

    // Begin recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.pNext = nullptr;  // CRITICAL: Must be nullptr to avoid garbage pointer
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr;  // CRITICAL: Must be nullptr (not using secondary buffers)

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to begin upload command buffer");
        vkFreeCommandBuffers(device_, threadPool, 1, &commandBuffer);
        return VK_NULL_HANDLE;
    }

    return commandBuffer;
}

void VulkanGraphicsImpl::EndUploadCommandBuffer(VkCommandBuffer commandBuffer)
{
    if (commandBuffer == VK_NULL_HANDLE)
        return;

    // Get current thread ID to retrieve thread-local command pool
    ThreadID currentThread = Thread::GetCurrentThreadID();

    // Get thread-local command pool (should always exist if BeginUploadCommandBuffer was called)
    VkCommandPool threadPool = VK_NULL_HANDLE;
    {
        MutexLock lock(threadUploadCommandPoolsMutex_);
        auto it = threadUploadCommandPools_.Find(currentThread);
        if (it != threadUploadCommandPools_.End())
        {
            threadPool = it->second_;
        }
        else
        {
            URHO3D_LOGERROR("EndUploadCommandBuffer: No thread-local pool found for current thread");
            return;
        }
    }

    // End recording
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to end upload command buffer");
        vkFreeCommandBuffers(device_, threadPool, 1, &commandBuffer);
        return;
    }

    // Submit to graphics queue
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;  // CRITICAL: Must be nullptr
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;

    // Create a fence for this upload
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.pNext = nullptr;  // CRITICAL: Must be nullptr
    fenceInfo.flags = 0;

    VkFence uploadFence;
    if (vkCreateFence(device_, &fenceInfo, nullptr, &uploadFence) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create upload fence");
        vkFreeCommandBuffers(device_, threadPool, 1, &commandBuffer);
        return;
    }

    // Protect queue submission with mutex (vkQueueSubmit is NOT thread-safe)
    VkResult submitResult;
    {
        MutexLock lock(queueSubmitMutex_);
        submitResult = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, uploadFence);
    }

    if (submitResult != VK_SUCCESS)
    {
        URHO3D_LOGERROR(String("Failed to submit upload command buffer: ") + String((int)submitResult));

        // Detect device loss at upload submission point
        if (submitResult == VK_ERROR_DEVICE_LOST)
        {
            deviceLost_ = true;
            URHO3D_LOGERROR("DEVICE LOST detected at vkQueueSubmit (upload submission, detection point)");
        }

        vkDestroyFence(device_, uploadFence, nullptr);
        vkFreeCommandBuffers(device_, threadPool, 1, &commandBuffer);
        return;
    }

    // Wait for completion with a timeout to detect hangs
    VkResult waitResult = vkWaitForFences(device_, 1, &uploadFence, VK_TRUE, 10000000000ULL); // 10 seconds

    if (waitResult == VK_TIMEOUT)
    {
        URHO3D_LOGERROR("EndUploadCommandBuffer: Fence wait TIMED OUT after 10 seconds!");

        // Check fence status again
        VkResult fenceStatus = vkGetFenceStatus(device_, uploadFence);
        URHO3D_LOGERROR(String("EndUploadCommandBuffer: Fence status after timeout=") + String((int)fenceStatus));

        // Try to check queue status with a short timeout
        URHO3D_LOGERROR("EndUploadCommandBuffer: Checking queue status with vkQueueWaitIdle...");
        VkResult queueStatus;
        {
            MutexLock lock(queueSubmitMutex_);
            queueStatus = vkQueueWaitIdle(graphicsQueue_);
        }
        URHO3D_LOGERROR(String("EndUploadCommandBuffer: vkQueueWaitIdle result=") + String((int)queueStatus));

        // Check if device is lost
        URHO3D_LOGERROR("EndUploadCommandBuffer: Checking device status with vkDeviceWaitIdle...");
        VkResult deviceStatus = vkDeviceWaitIdle(device_);
        URHO3D_LOGERROR(String("EndUploadCommandBuffer: vkDeviceWaitIdle result=") + String((int)deviceStatus));

        if (deviceStatus == VK_ERROR_DEVICE_LOST)
        {
            deviceLost_ = true;
            URHO3D_LOGERROR("DEVICE LOST detected at vkDeviceWaitIdle (upload timeout, detection point)");
        }
        else if (queueStatus == VK_ERROR_DEVICE_LOST)
        {
            deviceLost_ = true;
            URHO3D_LOGERROR("DEVICE LOST detected at vkQueueWaitIdle (upload timeout, detection point)");
        }
        else if (queueStatus == VK_SUCCESS && deviceStatus == VK_SUCCESS)
        {
            URHO3D_LOGERROR("EndUploadCommandBuffer: Queue and device are OK, but fence still not signaled - fence attachment bug?");
        }

        // Clean up and return (don't wait forever)
        vkDestroyFence(device_, uploadFence, nullptr);
        vkFreeCommandBuffers(device_, threadPool, 1, &commandBuffer);
        return;
    }
    else if (waitResult != VK_SUCCESS)
    {
        URHO3D_LOGERROR(String("EndUploadCommandBuffer: vkWaitForFences failed with result=") + String((int)waitResult));

        // Detect device loss at upload fence wait point
        if (waitResult == VK_ERROR_DEVICE_LOST)
        {
            deviceLost_ = true;
            URHO3D_LOGERROR("DEVICE LOST detected at vkWaitForFences (upload fence, detection point)");
        }

        vkDestroyFence(device_, uploadFence, nullptr);
        vkFreeCommandBuffers(device_, threadPool, 1, &commandBuffer);
        return;
    }

    URHO3D_LOGDEBUG("EndUploadCommandBuffer: Fence signaled, upload complete");

    vkDestroyFence(device_, uploadFence, nullptr);

    // Free the command buffer from thread-local pool
    URHO3D_LOGDEBUG(String("EndUploadCommandBuffer: Freeing cmdBuf=") +
                   String((unsigned long long)(uintptr_t)commandBuffer) +
                   " to pool=" + String((unsigned long long)(uintptr_t)threadPool));
    vkFreeCommandBuffers(device_, threadPool, 1, &commandBuffer);
}

VkFramebuffer VulkanGraphicsImpl::GetCurrentFramebuffer() const
{
    if (renderingToTexture_ && renderTargetFramebuffer_ != VK_NULL_HANDLE)
        return renderTargetFramebuffer_;
    if (currentImageIndex_ < framebuffers_.Size())
        return framebuffers_[currentImageIndex_];
    return nullptr;
}

VkRenderPass VulkanGraphicsImpl::GetRenderPass() const
{
    if (renderingToTexture_ && renderTargetRenderPass_ != VK_NULL_HANDLE)
        return renderTargetRenderPass_;
    return renderPass_;
}

VkSampler VulkanGraphicsImpl::GetSampler(VkFilter filter, VkSamplerAddressMode addressMode)
{
    if (!samplerCache_)
        return VK_NULL_HANDLE;

    // Delegate to sampler cache (Quick Win #4) - convert Vulkan types to uint8_t
    uint8_t filterMode = static_cast<uint8_t>(filter);  // 0=VK_FILTER_NEAREST, 1=VK_FILTER_LINEAR
    uint8_t addrMode = static_cast<uint8_t>(addressMode);  // 0=CLAMP, 1=REPEAT, 2=MIRROR
    return samplerCache_->GetSampler(filterMode, addrMode, 1);  // maxAnisotropy=1
}

VkPipeline VulkanGraphicsImpl::GetGraphicsPipeline(const VkGraphicsPipelineCreateInfo& createInfo, uint64_t stateHash)
{
    if (!pipelineCache_)
        return VK_NULL_HANDLE;

    // Delegate to pipeline cache (Quick Win #10)
    return pipelineCache_->GetOrCreatePipeline(stateHash, createInfo);
}

// ============================================
// Private Initialization Methods
// ============================================

bool VulkanGraphicsImpl::CreateInstance()
{
    URHO3D_LOGDEBUG("[VULKAN] CreateInstance: Getting required extensions from SDL");
    // Get required extensions from SDL
    unsigned int extensionCount = 0;
    const char** extensionNames = nullptr;

    if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &extensionCount, nullptr))
    {
        URHO3D_LOGERROR("Failed to get Vulkan instance extension count");
        return false;
    }
    URHO3D_LOGDEBUG(String("[VULKAN] SDL requires ") + String(extensionCount) + " Vulkan extensions");

    Vector<const char*> extensions(extensionCount);
    if (!SDL_Vulkan_GetInstanceExtensions(nullptr, &extensionCount, !extensions.Empty() ? &extensions[0] : nullptr))
    {
        URHO3D_LOGERROR("Failed to get Vulkan instance extensions");
        return false;
    }
    URHO3D_LOGDEBUG("[VULKAN] SDL extension names retrieved");

    // Add validation layer extension if available
    const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    Vector<VkLayerProperties> availableLayers(layerCount);
    if (layerCount > 0)
    {
        vkEnumerateInstanceLayerProperties(&layerCount, &availableLayers[0]);
    }

    bool validationLayerAvailable = false;
    for (const auto& layer : availableLayers)
    {
        if (strcmp(layer.layerName, validationLayerName) == 0)
        {
            validationLayerAvailable = true;
            break;
        }
    }

    Vector<const char*> layers;
    if (validationLayerAvailable)
    {
        layers.Push(validationLayerName);
        // Add debug utils extension for validation layer messages
        extensions.Push(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensionCount++;
        URHO3D_LOGINFO("Vulkan validation layer enabled with debug utils");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Urho3D";
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 9, 0);
    appInfo.pEngineName = "Urho3D";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 9, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = !extensions.Empty() ? &extensions[0] : nullptr;
    createInfo.enabledLayerCount = layers.Size();
    createInfo.ppEnabledLayerNames = !layers.Empty() ? &layers[0] : nullptr;

    URHO3D_LOGDEBUG(String("[VULKAN] Calling vkCreateInstance with ") + String(extensionCount) +
                    " extensions and " + String((unsigned)layers.Size()) + " layers");
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    URHO3D_LOGDEBUG(String("[VULKAN] vkCreateInstance returned: ") + String((int)result) + " (VK_SUCCESS=0)");

    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR(String("Failed to create Vulkan instance (error code: ") + String((int)result) + ")");
        return false;
    }

    URHO3D_LOGINFO("Vulkan instance created successfully");
    URHO3D_LOGDEBUG(String("[VULKAN] Instance handle: ") + String((unsigned long long)instance_));

    // Setup debug messenger if validation layers are enabled
    if (validationLayerAvailable)
    {
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = [](
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData) -> VkBool32
        {
            // Only log errors, not warnings (per-draw warnings cause I/O freeze)
            if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                URHO3D_LOGERROR(String("[VULKAN VALIDATION] ") + pCallbackData->pMessage);
            }
            return VK_FALSE;
        };

        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(instance_, &debugCreateInfo, nullptr, &debugMessenger_);
            URHO3D_LOGINFO("Vulkan debug messenger created");
        }
    }

    return true;
}

bool VulkanGraphicsImpl::SelectPhysicalDevice()
{
    URHO3D_LOGDEBUG("[VULKAN] Entering SelectPhysicalDevice()");
    URHO3D_LOGDEBUG(String("[VULKAN] Instance handle: ") + String((unsigned long long)instance_));

    uint32_t deviceCount = 0;
    URHO3D_LOGDEBUG("[VULKAN] Calling vkEnumeratePhysicalDevices (first call to get count)");
    VkResult result = vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    URHO3D_LOGDEBUG(String("[VULKAN] vkEnumeratePhysicalDevices returned: ") + String((int)result) +
                    " (VK_SUCCESS=0), deviceCount=" + String(deviceCount));

    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR(String("vkEnumeratePhysicalDevices failed with error code: ") + String((int)result));
        return false;
    }

    if (deviceCount == 0)
    {
        URHO3D_LOGERROR("No Vulkan-capable physical devices found");
        return false;
    }

    URHO3D_LOGDEBUG(String("[VULKAN] Found ") + String(deviceCount) + " physical device(s)");
    Vector<VkPhysicalDevice> devices(deviceCount);
    URHO3D_LOGDEBUG("[VULKAN] Calling vkEnumeratePhysicalDevices (second call to get devices)");
    result = vkEnumeratePhysicalDevices(instance_, &deviceCount, !devices.Empty() ? &devices[0] : nullptr);
    URHO3D_LOGDEBUG(String("[VULKAN] vkEnumeratePhysicalDevices (2nd call) returned: ") + String((int)result));

    // Priority: discrete GPU > integrated GPU > virtual GPU > CPU
    int selectedIndex = -1;
    VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;

    // Helper function to get device type name
    auto getDeviceTypeName = [](VkPhysicalDeviceType type) -> const char*
    {
        switch (type)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "CPU";
        default:
            return "Other";
        }
    };

    // First pass: Try to find the best match (discrete GPU preferred)
    URHO3D_LOGDEBUG(String("[VULKAN] Iterating through ") + String(deviceCount) + " device(s)");
    for (uint32_t i = 0; i < deviceCount; ++i)
    {
        URHO3D_LOGDEBUG(String("[VULKAN] Processing device ") + String(i) + " / " + String(deviceCount - 1));
        URHO3D_LOGDEBUG(String("[VULKAN] Device handle: ") + String((unsigned long long)devices[i]));

        VkPhysicalDeviceProperties properties;
        URHO3D_LOGDEBUG(String("[VULKAN] Calling vkGetPhysicalDeviceProperties for device ") + String(i));
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        URHO3D_LOGDEBUG("[VULKAN] vkGetPhysicalDeviceProperties returned successfully");

        URHO3D_LOGINFO(String("GPU ") + String((int)i) + ": " + properties.deviceName +
                      " (" + getDeviceTypeName(properties.deviceType) + ")");

        // Prefer discrete GPUs
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            if (selectedIndex < 0 || selectedIndex >= 0)  // Take first discrete GPU
            {
                selectedDevice = devices[i];
                selectedIndex = i;
                break;
            }
        }
        else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            // Use as fallback if no discrete GPU found
            if (selectedIndex < 0)
            {
                selectedDevice = devices[i];
                selectedIndex = i;
            }
        }
        else if (selectedIndex < 0)
        {
            // Last resort: use any device
            selectedDevice = devices[i];
            selectedIndex = i;
        }
    }

    if (!selectedDevice)
    {
        // Fallback: use first available device
        if (deviceCount > 0)
        {
            selectedDevice = devices[0];
            selectedIndex = 0;
            URHO3D_LOGWARNING("Using first available GPU as fallback");
        }
        else
        {
            URHO3D_LOGERROR("Failed to select physical device");
            return false;
        }
    }

    physicalDevice_ = selectedDevice;
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProperties_);

    URHO3D_LOGINFO(String("Selected GPU: ") + deviceProperties_.deviceName +
                   " (" + getDeviceTypeName(deviceProperties_.deviceType) + ")");

    // Detect MSAA capabilities for this device
    if (!DetectMSAACapabilities())
    {
        URHO3D_LOGWARNING("Failed to detect MSAA capabilities, defaulting to 1x");
    }

    // Detect timeline semaphore support for advanced synchronization
    DetectTimelineSemaphoreSupport();

    return true;
}

bool VulkanGraphicsImpl::FindQueueFamilies()
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);

    Vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    if (queueFamilyCount > 0)
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, &queueFamilies[0]);

    graphicsQueueFamily_ = VK_QUEUE_FAMILY_IGNORED;
    presentQueueFamily_ = VK_QUEUE_FAMILY_IGNORED;

    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && graphicsQueueFamily_ == VK_QUEUE_FAMILY_IGNORED)
        {
            graphicsQueueFamily_ = i;
        }

        VkBool32 presentSupport = false;
        if (surface_)
        {
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &presentSupport);
        }

        if (presentSupport && presentQueueFamily_ == VK_QUEUE_FAMILY_IGNORED)
        {
            presentQueueFamily_ = i;
        }

        if (graphicsQueueFamily_ != VK_QUEUE_FAMILY_IGNORED && presentQueueFamily_ != VK_QUEUE_FAMILY_IGNORED)
            break;
    }

    if (graphicsQueueFamily_ == VK_QUEUE_FAMILY_IGNORED)
    {
        URHO3D_LOGERROR("Failed to find graphics queue family");
        return false;
    }

    // If present queue family not found, use graphics queue family
    if (presentQueueFamily_ == VK_QUEUE_FAMILY_IGNORED)
    {
        presentQueueFamily_ = graphicsQueueFamily_;
    }

    // Dedicated async-compute family: a family with COMPUTE but NOT GRAPHICS. Submitting training
    // dispatches here (instead of graphicsQueue_) keeps a long compute batch from sitting in front
    // of the frame Present submit — which is what starved the render frame-fence and drove the
    // reuse-before-signal validation cascade → VK_ERROR_DEVICE_LOST (the "GPU learning hangs the
    // window" bug). If the device exposes no compute-only family we leave it IGNORED and fall back
    // to the graphics queue (GetComputeQueue()), preserving the old behaviour.
    computeQueueFamily_ = VK_QUEUE_FAMILY_IGNORED;
    computeQueueIsDedicated_ = false;
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        const VkQueueFlags flags = queueFamilies[i].queueFlags;
        if ((flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) && queueFamilies[i].queueCount > 0)
        {
            computeQueueFamily_ = i;
            computeQueueIsDedicated_ = true;
            break;
        }
    }
    if (computeQueueIsDedicated_)
        URHO3D_LOGINFO("Dedicated async-compute queue family " + String(computeQueueFamily_) +
            " selected — GPU training submits off the render/present queue");
    else
        URHO3D_LOGWARNING("No compute-only queue family — GPU training shares the graphics queue "
            "(render/compute contention possible on large-corpus training)");

    return true;
}

bool VulkanGraphicsImpl::DetectMSAACapabilities()
{
    // Query device limits for supported sample counts
    // Both color and depth attachments must support the same sample count
    VkSampleCountFlags colorSamples = deviceProperties_.limits.framebufferColorSampleCounts;
    VkSampleCountFlags depthSamples = deviceProperties_.limits.framebufferDepthSampleCounts;

    // Store the intersection of supported sample counts (both must support)
    supportedSampleCountsMask_ = colorSamples & depthSamples;

    // Log available MSAA levels
    String msaaInfo = "Supported MSAA levels: ";
    if (supportedSampleCountsMask_ & VK_SAMPLE_COUNT_1_BIT) msaaInfo += "1x ";
    if (supportedSampleCountsMask_ & VK_SAMPLE_COUNT_2_BIT) msaaInfo += "2x ";
    if (supportedSampleCountsMask_ & VK_SAMPLE_COUNT_4_BIT) msaaInfo += "4x ";
    if (supportedSampleCountsMask_ & VK_SAMPLE_COUNT_8_BIT) msaaInfo += "8x ";
    if (supportedSampleCountsMask_ & VK_SAMPLE_COUNT_16_BIT) msaaInfo += "16x ";

    URHO3D_LOGINFO(msaaInfo);

    // Default to 1x MSAA if nothing is supported (shouldn't happen)
    if (!supportedSampleCountsMask_)
    {
        supportedSampleCountsMask_ = VK_SAMPLE_COUNT_1_BIT;
        actualSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
        return false;
    }

    // Start with 1x MSAA as default (can be changed via SetRequestedSampleCount)
    actualSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    requestedSampleCount_ = VK_SAMPLE_COUNT_1_BIT;

    return true;
}

VkSampleCountFlagBits VulkanGraphicsImpl::SelectBestSampleCount(uint32_t requestedCount)
{
    // If 1x requested or invalid count, return 1x
    if (requestedCount <= 1)
        return VK_SAMPLE_COUNT_1_BIT;

    // Map requested count to VkSampleCountFlagBits and find best supported match
    VkSampleCountFlagBits requested = VK_SAMPLE_COUNT_1_BIT;
    if (requestedCount >= 2) requested = VK_SAMPLE_COUNT_2_BIT;
    if (requestedCount >= 4) requested = VK_SAMPLE_COUNT_4_BIT;
    if (requestedCount >= 8) requested = VK_SAMPLE_COUNT_8_BIT;
    if (requestedCount >= 16) requested = VK_SAMPLE_COUNT_16_BIT;

    // Check if requested sample count is supported
    if (supportedSampleCountsMask_ & requested)
    {
        actualSampleCount_ = requested;
        URHO3D_LOGINFO(String("MSAA set to ") + String((int)requestedCount) + "x");
        return requested;
    }

    // If not supported, find best fallback (prefer higher MSAA)
    if (requested > VK_SAMPLE_COUNT_8_BIT && (supportedSampleCountsMask_ & VK_SAMPLE_COUNT_8_BIT))
        return (actualSampleCount_ = VK_SAMPLE_COUNT_8_BIT);
    if (requested > VK_SAMPLE_COUNT_4_BIT && (supportedSampleCountsMask_ & VK_SAMPLE_COUNT_4_BIT))
        return (actualSampleCount_ = VK_SAMPLE_COUNT_4_BIT);
    if (requested > VK_SAMPLE_COUNT_2_BIT && (supportedSampleCountsMask_ & VK_SAMPLE_COUNT_2_BIT))
        return (actualSampleCount_ = VK_SAMPLE_COUNT_2_BIT);

    // Fallback to 1x
    actualSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    URHO3D_LOGWARNING(String("Requested MSAA ") + String((int)requestedCount) +
                     "x not supported, falling back to 1x");
    return VK_SAMPLE_COUNT_1_BIT;
}

bool VulkanGraphicsImpl::DetectTimelineSemaphoreSupport()
{
    // Check if VK_KHR_timeline_semaphore is available
    // This extension provides VkSemaphoreTypeCreateInfo for timeline semaphores

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures{};
    timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &timelineFeatures;

    vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);

    // Physical device reports timeline semaphore support, but we don't enable the feature
    // on the logical device (would need pNext chain + extension). Force-disable to use
    // reliable fence-based synchronization instead.
    supportsTimelineSemaphores_ = false;
    URHO3D_LOGINFO("Timeline semaphores disabled (feature not enabled on logical device), using fence-based sync");

    return true;
}

bool VulkanGraphicsImpl::CreateTimelineSemaphore()
{
    if (!supportsTimelineSemaphores_)
    {
        URHO3D_LOGINFO("Timeline semaphores not supported, skipping creation");
        return true;  // Not an error, just use fallback
    }

    // Create timeline semaphore type info
    VkSemaphoreTypeCreateInfo typeCreateInfo{};
    typeCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
    typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
    typeCreateInfo.initialValue = VULKAN_TIMELINE_INITIAL_VALUE;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &typeCreateInfo;

    if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &timelineRenderSemaphore_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create timeline semaphore");
        return false;
    }

    timelineRenderCounter_ = VULKAN_TIMELINE_INITIAL_VALUE;

    URHO3D_LOGINFO("Timeline render semaphore created (replaces 3 binary semaphores)");
    return true;
}

bool VulkanGraphicsImpl::WaitOnTimelineRenderSemaphore(uint64_t targetValue)
{
    if (!supportsTimelineSemaphores_ || timelineRenderSemaphore_ == VK_NULL_HANDLE)
    {
        // Fallback: use fence-based waiting (original behavior)
        // This maintains compatibility on devices without timeline semaphores
        return true;
    }

    // Wait for timeline semaphore to reach targetValue
    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &timelineRenderSemaphore_;
    waitInfo.pValues = &targetValue;

    VkResult result = vkWaitSemaphores(device_, &waitInfo, VULKAN_TIMELINE_TIMEOUT_NS);

    if (result == VK_TIMEOUT)
    {
        URHO3D_LOGWARNING(String("Timeline semaphore wait timeout at value ") + String((unsigned long long)targetValue));
        return false;
    }
    else if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Timeline semaphore wait failed");
        return false;
    }

    return true;
}

void VulkanGraphicsImpl::SignalTimelineRenderSemaphore()
{
    if (supportsTimelineSemaphores_ && timelineRenderSemaphore_ != VK_NULL_HANDLE)
    {
        // Increment timeline counter after frame submission
        timelineRenderCounter_++;
    }
}

void VulkanGraphicsImpl::InsertPipelineBarrier(VkCommandBuffer cmdBuffer,
                                              VkPipelineStageFlags srcStage,
                                              VkPipelineStageFlags dstStage,
                                              VkAccessFlags srcAccess,
                                              VkAccessFlags dstAccess)
{
    if (!cmdBuffer)
    {
        URHO3D_LOGWARNING("InsertPipelineBarrier: Invalid command buffer");
        return;
    }

    // Memory barrier for global synchronization
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = srcAccess;
    memoryBarrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(
        cmdBuffer,
        srcStage,  // srcStageMask: stage that must complete before barrier
        dstStage,  // dstStageMask: stage that waits on barrier
        0,         // dependencyFlags (0 = default)
        1,         // memoryBarrierCount
        &memoryBarrier,
        0,         // bufferMemoryBarrierCount
        nullptr,   // pBufferMemoryBarriers
        0,         // imageMemoryBarrierCount
        nullptr    // pImageMemoryBarriers
    );

}

bool VulkanGraphicsImpl::CreateLogicalDevice()
{
    Vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    Vector<uint32_t> uniqueQueueFamilies;

    uniqueQueueFamilies.Push(graphicsQueueFamily_);
    if (presentQueueFamily_ != graphicsQueueFamily_)
    {
        uniqueQueueFamilies.Push(presentQueueFamily_);
    }
    // Request a queue from the dedicated compute family too (distinct from graphics/present by
    // construction — it lacks the GRAPHICS bit), so GPU training runs off the render queue.
    if (computeQueueIsDedicated_ && computeQueueFamily_ != VK_QUEUE_FAMILY_IGNORED &&
        !uniqueQueueFamilies.Contains(computeQueueFamily_))
    {
        uniqueQueueFamilies.Push(computeQueueFamily_);
    }

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.Push(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.sampleRateShading = VK_FALSE;
    deviceFeatures.shaderClipDistance = VK_TRUE;  // CRITICAL: Required for gl_ClipDistance in shaders
    deviceFeatures.robustBufferAccess = VK_TRUE;  // CRITICAL: Allow pipelines with missing vertex attributes

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    uint32_t deviceExtensionCount = 1;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = queueCreateInfos.Size();
    createInfo.pQueueCreateInfos = !queueCreateInfos.Empty() ? &queueCreateInfos[0] : nullptr;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = deviceExtensionCount;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create logical device");
        return false;
    }

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
    // Dedicated compute queue handle (only when a compute-only family was requested above). Left
    // null otherwise — GetComputeQueue() then transparently returns graphicsQueue_.
    if (computeQueueIsDedicated_ && computeQueueFamily_ != VK_QUEUE_FAMILY_IGNORED)
        vkGetDeviceQueue(device_, computeQueueFamily_, 0, &computeQueue_);

    URHO3D_LOGINFO("Vulkan logical device created");
    return true;
}

bool VulkanGraphicsImpl::CreateSurface(SDL_Window* window)
{
    if (!SDL_Vulkan_CreateSurface(window, instance_, &surface_))
    {
        URHO3D_LOGERROR("Failed to create Vulkan surface");
        return false;
    }

    URHO3D_LOGINFO("Vulkan surface created");
    return true;
}

bool VulkanGraphicsImpl::CreateSwapchain(int width, int height)
{
    VkSurfaceFormatKHR surfaceFormat = FindSurfaceFormat();
    VkPresentModeKHR presentMode = FindPresentMode();

    uint32_t imageCount = VULKAN_DEFAULT_SWAPCHAIN_IMAGES;
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);

    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }
    if (imageCount < capabilities.minImageCount)
    {
        imageCount = capabilities.minImageCount;
    }

    // Log current extent before deciding
    URHO3D_LOGDEBUG("Surface currentExtent: " + String((int)capabilities.currentExtent.width) + "x" +
                    String((int)capabilities.currentExtent.height) +
                    " (UINT32_MAX=" + String(capabilities.currentExtent.width == UINT32_MAX ? "YES" : "NO") + ")");
    URHO3D_LOGDEBUG("Requested window size: " + String((int)width) + "x" + String((int)height));

    // FIXED: Always use requested dimensions if currentExtent is undefined (UINT32_MAX)
    // OR if it's invalid (0 or 1, which happens with hidden/minimized windows)
    if (capabilities.currentExtent.width == UINT32_MAX ||
        capabilities.currentExtent.width <= 1 ||
        capabilities.currentExtent.height <= 1)
    {
        swapchainExtent_.width = (uint32_t)width;
        swapchainExtent_.height = (uint32_t)height;

        // Clamp to supported range
        if (swapchainExtent_.width < capabilities.minImageExtent.width)
            swapchainExtent_.width = capabilities.minImageExtent.width;
        if (swapchainExtent_.height < capabilities.minImageExtent.height)
            swapchainExtent_.height = capabilities.minImageExtent.height;

        if (swapchainExtent_.width > capabilities.maxImageExtent.width)
            swapchainExtent_.width = capabilities.maxImageExtent.width;
        if (swapchainExtent_.height > capabilities.maxImageExtent.height)
            swapchainExtent_.height = capabilities.maxImageExtent.height;

        URHO3D_LOGDEBUG("Using requested extent (currentExtent was invalid): " +
                       String((int)swapchainExtent_.width) + "x" + String((int)swapchainExtent_.height));
    }
    else
    {
        swapchainExtent_ = capabilities.currentExtent;
        URHO3D_LOGDEBUG("Using surface currentExtent: " +
                       String((int)swapchainExtent_.width) + "x" + String((int)swapchainExtent_.height));
    }

    // Final validation: reject degenerate swapchain extents
    // This can happen when the window is minimized, not yet realized, or during mode transitions
    if (swapchainExtent_.width < 320 || swapchainExtent_.height < 200)
    {
        URHO3D_LOGWARNING("Swapchain extent too small (" + String((int)swapchainExtent_.width) + "x" +
                         String((int)swapchainExtent_.height) + "), aborting swapchain creation");
        return false;
    }

    swapchainFormat_ = surfaceFormat.format;

    // Log swapchain capabilities for debugging
    URHO3D_LOGDEBUG("Swapchain capabilities: images " + String((int)capabilities.minImageCount) +
                    "-" + String((int)capabilities.maxImageCount) +
                    ", extent " + String((int)capabilities.minImageExtent.width) + "x" +
                    String((int)capabilities.minImageExtent.height) + " to " +
                    String((int)capabilities.maxImageExtent.width) + "x" +
                    String((int)capabilities.maxImageExtent.height));

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    uint32_t queueFamilyIndices[] = {graphicsQueueFamily_, presentQueueFamily_};
    if (graphicsQueueFamily_ != presentQueueFamily_)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    URHO3D_LOGDEBUG("[VULKAN] Calling vkCreateSwapchainKHR");
    if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create swapchain");
        return false;
    }

    // Get swapchain images
    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &swapchainImageCount, nullptr);
    swapchainImages_.Resize(swapchainImageCount);
    if (!swapchainImages_.Empty())
        vkGetSwapchainImagesKHR(device_, swapchain_, &swapchainImageCount, &swapchainImages_[0]);

    // Create image views
    swapchainImageViews_.Resize(swapchainImages_.Size());
    for (size_t i = 0; i < swapchainImages_.Size(); ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat_;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &viewInfo, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS)
        {
            URHO3D_LOGERROR("Failed to create swapchain image view");
            return false;
        }
    }

    // Create per-image renderComplete semaphores (fixes validation error about semaphore reuse)
    // imageAcquired semaphores are per-frame (created with command buffers)
    imageSemaphores_.Resize(swapchainImages_.Size());
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < imageSemaphores_.Size(); ++i)
    {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageSemaphores_[i].renderComplete) != VK_SUCCESS)
        {
            URHO3D_LOGERROR("Failed to create renderComplete semaphore for swapchain image " + String((unsigned)i));
            return false;
        }
    }
    URHO3D_LOGINFO("Created renderComplete semaphores for " + String((unsigned)imageSemaphores_.Size()) + " swapchain images");

    URHO3D_LOGINFO(String("Swapchain created with ") + String((int)swapchainImageCount) +
                   " images at " + String((int)swapchainExtent_.width) + "x" +
                   String((int)swapchainExtent_.height));
    return true;
}

bool VulkanGraphicsImpl::CreateDepthBuffer(VkFormat format, int width, int height, VkSampleCountFlagBits sampleCount)
{
    // Validate dimensions
    if (width <= 0 || height <= 0)
    {
        URHO3D_LOGWARNING("CreateDepthBuffer called with invalid dimensions: " + String(width) + "x" + String(height));
        return false;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = (uint32_t)width;
    imageInfo.extent.height = (uint32_t)height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = sampleCount;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &imageInfo, nullptr, &depthImage_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create depth image");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device_, depthImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &depthImageMemory_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to allocate depth image memory");
        return false;
    }

    vkBindImageMemory(device_, depthImage_, depthImageMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create depth image view");
        return false;
    }

    URHO3D_LOGINFO("Depth buffer created");
    return true;
}

bool VulkanGraphicsImpl::CreateMSAAColorImage(int width, int height)
{
    // Phase 30: Only create MSAA color image if sample count > 1x
    if (actualSampleCount_ == VK_SAMPLE_COUNT_1_BIT)
    {
        return true;  // No MSAA needed
    }

    // Create intermediate multi-sample color image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = (uint32_t)width;
    imageInfo.extent.height = (uint32_t)height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = swapchainFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // Render target only
    imageInfo.samples = actualSampleCount_;  // Use MSAA sample count
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, &imageInfo, &allocCreateInfo, &msaaColorImage_, &msaaColorAllocation_, nullptr) != VK_SUCCESS)
    {
        URHO3D_LOGERROR(String("Failed to create MSAA color image (") + String((int)actualSampleCount_) + "x)");
        return false;
    }

    // Create image view for MSAA color attachment
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = msaaColorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = swapchainFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &msaaColorImageView_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create MSAA color image view");
        vmaDestroyImage(allocator_, msaaColorImage_, msaaColorAllocation_);
        msaaColorImage_ = VK_NULL_HANDLE;
        return false;
    }

    URHO3D_LOGINFO(String("MSAA color image created (") + String((int)actualSampleCount_) + "x samples)");
    return true;
}

bool VulkanGraphicsImpl::CreateGBuffer(int width, int height)
{
    // Phase 31: Create G-Buffer attachments for deferred rendering
    // G-Buffer layout: Position (RGBA32F), Normal (RGBA16F), Albedo (RGBA8), Specular (RGBA8)

    struct GBufferAttachmentInfo
    {
        VkImage* image;
        VmaAllocation* allocation;
        VkImageView* view;
        VkFormat format;
        const char* name;
    };

    GBufferAttachmentInfo attachments[4] = {
        {&gBufferPositionImage_, &gBufferPositionAlloc_, &gBufferPositionView_, VK_FORMAT_R32G32B32A32_SFLOAT, "Position"},
        {&gBufferNormalImage_, &gBufferNormalAlloc_, &gBufferNormalView_, VK_FORMAT_R16G16B16A16_SFLOAT, "Normal"},
        {&gBufferAlbedoImage_, &gBufferAlbedoAlloc_, &gBufferAlbedoView_, VK_FORMAT_R8G8B8A8_UNORM, "Albedo"},
        {&gBufferSpecularImage_, &gBufferSpecularAlloc_, &gBufferSpecularView_, VK_FORMAT_R8G8B8A8_UNORM, "Specular"}
    };

    // Create each G-Buffer attachment
    for (int i = 0; i < 4; ++i)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = (uint32_t)width;
        imageInfo.extent.height = (uint32_t)height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = attachments[i].format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;  // G-Buffer always 1x sampled
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator_, &imageInfo, &allocCreateInfo, attachments[i].image, attachments[i].allocation, nullptr) != VK_SUCCESS)
        {
            URHO3D_LOGERROR(String("Failed to create G-Buffer ") + attachments[i].name + " image");
            DestroyGBuffer();
            return false;
        }

        // Create image view for G-Buffer attachment
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = *attachments[i].image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = attachments[i].format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &viewInfo, nullptr, attachments[i].view) != VK_SUCCESS)
        {
            URHO3D_LOGERROR(String("Failed to create G-Buffer ") + attachments[i].name + " image view");
            DestroyGBuffer();
            return false;
        }
    }

    URHO3D_LOGINFO("G-Buffer created (Position RGBA32F, Normal RGBA16F, Albedo RGBA8, Specular RGBA8)");
    return true;
}

void VulkanGraphicsImpl::DestroyGBuffer()
{
    // Phase 31: Destroy G-Buffer attachments

    VkImage* images[4] = {&gBufferPositionImage_, &gBufferNormalImage_, &gBufferAlbedoImage_, &gBufferSpecularImage_};
    VmaAllocation* allocations[4] = {&gBufferPositionAlloc_, &gBufferNormalAlloc_, &gBufferAlbedoAlloc_, &gBufferSpecularAlloc_};
    VkImageView* views[4] = {&gBufferPositionView_, &gBufferNormalView_, &gBufferAlbedoView_, &gBufferSpecularView_};

    for (int i = 0; i < 4; ++i)
    {
        if (*views[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, *views[i], nullptr);
            *views[i] = VK_NULL_HANDLE;
        }

        if (*images[i] != VK_NULL_HANDLE)
        {
            vmaDestroyImage(allocator_, *images[i], *allocations[i]);
            *images[i] = VK_NULL_HANDLE;
            *allocations[i] = nullptr;
        }
    }
}

bool VulkanGraphicsImpl::CreateFullScreenQuad()
{
    // Phase 36: Create full-screen quad for lighting pass
    // Using a single triangle that covers the entire screen via NDC coordinates

    // Full-screen triangle vertices in NDC space (-1 to +1)
    // Each vertex: 2D position (x, y) + 2D texture coordinate (u, v)
    struct Vertex
    {
        float x, y;      // NDC position
        float u, v;      // Texture coordinates
    };

    // Triangle covering full screen (CCW winding)
    const Vertex vertices[] = {
        {-1.0f, -1.0f, 0.0f, 1.0f},  // Bottom-left
        { 3.0f, -1.0f, 2.0f, 1.0f},  // Bottom-right (extends off-screen)
        {-1.0f,  3.0f, 0.0f,-1.0f},  // Top-left (extends off-screen)
    };
    const uint32_t vertexCount = 3;

    // Indices for triangle
    const uint16_t indices[] = {0, 1, 2};
    const uint32_t indexCount = 3;

    // Create vertex buffer
    size_t vertexBufferSize = vertexCount * sizeof(Vertex);
    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = vertexBufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo vertexAllocInfo{};
    vertexAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;  // Allow CPU mapping for initial upload

    if (vmaCreateBuffer(allocator_, &vertexBufferInfo, &vertexAllocInfo,
                       &fullScreenQuadVertexBuffer_, &fullScreenQuadVertexAlloc_, nullptr) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("CreateFullScreenQuad: Failed to create vertex buffer");
        return false;
    }

    // Copy vertex data to GPU
    void* mappedVertexData = nullptr;
    vmaMapMemory(allocator_, fullScreenQuadVertexAlloc_, &mappedVertexData);
    memcpy(mappedVertexData, vertices, vertexBufferSize);
    vmaUnmapMemory(allocator_, fullScreenQuadVertexAlloc_);

    // Create index buffer
    size_t indexBufferSize = indexCount * sizeof(uint16_t);
    VkBufferCreateInfo indexBufferInfo{};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.size = indexBufferSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    VmaAllocationCreateInfo indexAllocInfo{};
    indexAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;  // Allow CPU mapping for initial upload

    if (vmaCreateBuffer(allocator_, &indexBufferInfo, &indexAllocInfo,
                       &fullScreenQuadIndexBuffer_, &fullScreenQuadIndexAlloc_, nullptr) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("CreateFullScreenQuad: Failed to create index buffer");
        vmaDestroyBuffer(allocator_, fullScreenQuadVertexBuffer_, fullScreenQuadVertexAlloc_);
        fullScreenQuadVertexBuffer_ = VK_NULL_HANDLE;
        fullScreenQuadVertexAlloc_ = nullptr;
        return false;
    }

    // Copy index data to GPU
    void* mappedIndexData = nullptr;
    vmaMapMemory(allocator_, fullScreenQuadIndexAlloc_, &mappedIndexData);
    memcpy(mappedIndexData, indices, indexBufferSize);
    vmaUnmapMemory(allocator_, fullScreenQuadIndexAlloc_);

    URHO3D_LOGINFO("CreateFullScreenQuad: Full-screen quad buffers created successfully");
    return true;
}

void VulkanGraphicsImpl::DestroyFullScreenQuad()
{
    // Phase 36: Destroy full-screen quad buffers

    if (fullScreenQuadIndexBuffer_ != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator_, fullScreenQuadIndexBuffer_, fullScreenQuadIndexAlloc_);
        fullScreenQuadIndexBuffer_ = VK_NULL_HANDLE;
        fullScreenQuadIndexAlloc_ = nullptr;
    }

    if (fullScreenQuadVertexBuffer_ != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator_, fullScreenQuadVertexBuffer_, fullScreenQuadVertexAlloc_);
        fullScreenQuadVertexBuffer_ = VK_NULL_HANDLE;
        fullScreenQuadVertexAlloc_ = nullptr;
    }
}

// RTT depth buffers, keyed by {width,height,format}. Alternating render-target sizes reuse cached
// buffers instead of thrashing a single slot (destroy+recreate every frame). Mirrors the framebuffer
// cache. Freed wholesale in DestroyRttDepthCache() from Shutdown / RecreateSwapchainResources.
VkImageView VulkanGraphicsImpl::GetOrCreateRttDepthView(int width, int height, VkFormat format)
{
    if (!device_ || width <= 0 || height <= 0)
        return VK_NULL_HANDLE;

    unsigned long long key = (unsigned long long)(unsigned)width;
    key = key * 31 + (unsigned long long)(unsigned)height;
    key = key * 31 + (unsigned long long)format;

    HashMap<unsigned long long, RttDepthBuffer>::Iterator it = rttDepthCache_.Find(key);
    if (it != rttDepthCache_.End())
        return it->second_.view_;

    RttDepthBuffer db;

    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = (uint32_t)width;
    depthImageInfo.extent.height = (uint32_t)height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = format;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &depthImageInfo, nullptr, &db.image_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("GetOrCreateRttDepthView: Failed to create RTT depth image");
        return VK_NULL_HANDLE;
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device_, db.image_, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &db.memory_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("GetOrCreateRttDepthView: Failed to allocate RTT depth memory");
        vkDestroyImage(device_, db.image_, nullptr);
        return VK_NULL_HANDLE;
    }

    vkBindImageMemory(device_, db.image_, db.memory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = db.image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &db.view_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("GetOrCreateRttDepthView: Failed to create RTT depth image view");
        vkFreeMemory(device_, db.memory_, nullptr);
        vkDestroyImage(device_, db.image_, nullptr);
        return VK_NULL_HANDLE;
    }

    rttDepthCache_[key] = db;
    URHO3D_LOGINFO("Created RTT depth buffer: " + String(width) + "x" + String(height) +
        " (cached, " + String(rttDepthCache_.Size()) + " total)");
    return db.view_;
}

void VulkanGraphicsImpl::DestroyRttDepthCache()
{
    if (!device_)
    {
        rttDepthCache_.Clear();
        return;
    }
    for (HashMap<unsigned long long, RttDepthBuffer>::Iterator it = rttDepthCache_.Begin();
         it != rttDepthCache_.End(); ++it)
    {
        if (it->second_.view_)
            vkDestroyImageView(device_, it->second_.view_, nullptr);
        if (it->second_.image_)
            vkDestroyImage(device_, it->second_.image_, nullptr);
        if (it->second_.memory_)
            vkFreeMemory(device_, it->second_.memory_, nullptr);
    }
    rttDepthCache_.Clear();
}

// Generic RTT: Build framebuffer from whatever render targets are currently set
bool VulkanGraphicsImpl::RebuildRenderTargetFramebuffer()
{
    renderTargetsDirty_ = false;

    if (!graphics_)
        return false;


    // Check if any color render targets are set
    bool hasColorTarget = false;
    unsigned colorCount = 0;
    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
    {
        if (graphics_->GetRenderTarget(i))
        {
            hasColorTarget = true;
            colorCount = i + 1;
        }
    }

    // Check for custom depth stencil (e.g. ForwardHWDepth's readable depth RTT, or shadow maps)
    RenderSurface* ds = graphics_->GetDepthStencil();

    // Detect "swapchain color + custom depth RTT" case (ForwardHWDepth):
    // No explicit color render targets (null = swapchain implicit), but custom depth is set.
    // If the custom depth matches swapchain dimensions, create a hybrid framebuffer
    // with swapchain color image + custom depth image.
    // Shadow maps have different dimensions than the swapchain, so they still get depth-only.
    bool hasSwapchainColorWithCustomDepth = false;
    if (!hasColorTarget && ds)
    {
        unsigned dsWidth = ds->GetWidth();
        unsigned dsHeight = ds->GetHeight();
        if (dsWidth == swapchainExtent_.width && dsHeight == swapchainExtent_.height)
        {
            // Custom depth matches swapchain size — this is ForwardHWDepth style:
            // render color to swapchain, depth to custom RTT for later sampling
            hasSwapchainColorWithCustomDepth = true;
        }
    }

    bool hasDepthOnly = !hasColorTarget && ds && !hasSwapchainColorWithCustomDepth;

    if (!hasColorTarget && !hasDepthOnly && !hasSwapchainColorWithCustomDepth)
    {
        // No color render targets and no depth-only target — switch to swapchain
        renderingToTexture_ = false;
        renderTargetFramebuffer_ = VK_NULL_HANDLE;
        renderTargetRenderPass_ = VK_NULL_HANDLE;
        rttWidth_ = 0;
        rttHeight_ = 0;
        return true;
    }

    // Get dimensions
    if (hasSwapchainColorWithCustomDepth)
    {
        rttWidth_ = swapchainExtent_.width;
        rttHeight_ = swapchainExtent_.height;
    }
    else if (hasColorTarget)
    {
        RenderSurface* firstRT = nullptr;
        for (unsigned i = 0; i < colorCount; ++i)
        {
            if (graphics_->GetRenderTarget(i))
            {
                firstRT = graphics_->GetRenderTarget(i);
                break;
            }
        }
        rttWidth_ = firstRT->GetWidth();
        rttHeight_ = firstRT->GetHeight();
    }
    else
    {
        rttWidth_ = ds->GetWidth();
        rttHeight_ = ds->GetHeight();
    }

    // Build render pass descriptor from actual render target formats
    RenderPassDescriptor descriptor;
    descriptor.subpassCount = 1;
    descriptor.sampleCount = VK_SAMPLE_COUNT_1_BIT;

    // Always create CLEAR variant here — LOAD selection happens in BeginRenderPass
    // (because by RebuildRTFB time, the previous render pass hasn't ended yet)
    descriptor.colorLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

    if (hasSwapchainColorWithCustomDepth)
    {
        // Hybrid framebuffer: swapchain color + custom depth RTT
        // isRenderToTexture = true so depth finalLayout = READ_ONLY (for later sampling)
        // isSwapchainHybrid = true so color finalLayout = PRESENT_SRC (swapchain, not SHADER_READ_ONLY)
        descriptor.colorAttachmentCount = 1;
        descriptor.isRenderToTexture = true;
        descriptor.isSwapchainHybrid = true;
        descriptor.colorFormats[0] = swapchainFormat_;
    }
    else
    {
        descriptor.colorAttachmentCount = colorCount;
        descriptor.isRenderToTexture = true;

        for (unsigned i = 0; i < colorCount; ++i)
        {
            RenderSurface* rt = graphics_->GetRenderTarget(i);
            if (rt)
                descriptor.colorFormats[i] = (VkFormat)rt->GetParentTexture()->GetFormat();
            else
                descriptor.colorFormats[i] = descriptor.colorFormats[0];
        }
    }

    // ds was declared above when checking for depth-only rendering
    if (ds)
        descriptor.depthFormat = (VkFormat)ds->GetParentTexture()->GetFormat();
    else
        descriptor.depthFormat = VK_FORMAT_D32_SFLOAT;

    // Get or create render pass (CLEAR variant — LOAD selected at BeginRenderPass time)
    renderTargetRenderPass_ = GetOrCreateRenderPass(descriptor);
    renderTargetRPDescriptor_ = descriptor;  // Save for LOAD variant creation in BeginRenderPass
    if (renderTargetRenderPass_ == VK_NULL_HANDLE)
    {
        URHO3D_LOGERROR("RebuildRenderTargetFramebuffer: Failed to create RTT render pass");
        return false;
    }

    // Collect VkImageViews for framebuffer attachments
    Vector<VkImageView> attachmentViews;
    unsigned long long cacheKey = 0;

    if (hasSwapchainColorWithCustomDepth)
    {
        // Use current swapchain image as color attachment
        VkImageView swapView = swapchainImageViews_[currentImageIndex_];
        if (swapView == VK_NULL_HANDLE)
        {
            URHO3D_LOGERROR("RebuildRenderTargetFramebuffer: Swapchain image view null for hybrid FB");
            return false;
        }
        attachmentViews.Push(swapView);
        cacheKey = cacheKey * 31 + (unsigned long long)(uintptr_t)swapView;
    }
    else
    {
        for (unsigned i = 0; i < colorCount; ++i)
        {
            VkImageView view = VK_NULL_HANDLE;
            RenderSurface* rt = graphics_->GetRenderTarget(i);
            if (rt)
                view = (VkImageView)rt->GetRenderTargetView();

            if (view == VK_NULL_HANDLE)
            {
                Texture* parentTex = rt ? rt->GetParentTexture() : nullptr;
                URHO3D_LOGERROR("RebuildRenderTargetFramebuffer: RT " + String(i) + " null VkImageView"
                    + " rt=" + String(rt != nullptr)
                    + " parent=" + String(parentTex != nullptr)
                    + " name=" + (parentTex ? parentTex->GetName() : "?")
                    + " size=" + (parentTex ? String(parentTex->GetWidth()) + "x" + String(parentTex->GetHeight()) : "?")
                    + " fmt=" + (parentTex ? String(parentTex->GetFormat()) : "?")
                    + " usage=" + (parentTex ? String((int)parentTex->GetUsage()) : "?"));
                return false;
            }
            attachmentViews.Push(view);
            cacheKey = cacheKey * 31 + (unsigned long long)(uintptr_t)view;
        }
    }

    // Add depth attachment
    VkImageView depthView = VK_NULL_HANDLE;
    if (ds)
    {
        depthView = (VkImageView)ds->GetRenderTargetView();
    }
    else
    {
        // Get (or lazily create) a depth buffer matching this RTT's dimensions/format. Cached by
        // {w,h,format} so alternating RTT sizes reuse buffers instead of thrashing a single slot
        // (the old single-slot path destroyed+recreated the depth image every frame when RTTs of
        // different dimensions alternated).
        depthView = GetOrCreateRttDepthView(rttWidth_, rttHeight_, descriptor.depthFormat);
        if (depthView == VK_NULL_HANDLE)
            return false;
    }

    if (depthView != VK_NULL_HANDLE)
    {
        attachmentViews.Push(depthView);
        cacheKey = cacheKey * 31 + (unsigned long long)(uintptr_t)depthView;
    }

    cacheKey = cacheKey * 31 + (unsigned long long)rttWidth_;
    cacheKey = cacheKey * 31 + (unsigned long long)rttHeight_;

    // Check framebuffer cache
    if (rttFramebufferCache_.Contains(cacheKey))
    {
        renderTargetFramebuffer_ = rttFramebufferCache_[cacheKey];
        renderingToTexture_ = true;
        return true;
    }

    // Create new framebuffer
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderTargetRenderPass_;
    fbInfo.attachmentCount = attachmentViews.Size();
    fbInfo.pAttachments = attachmentViews.Buffer();
    fbInfo.width = (uint32_t)rttWidth_;
    fbInfo.height = (uint32_t)rttHeight_;
    fbInfo.layers = 1;

    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &fb) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("RebuildRenderTargetFramebuffer: Failed to create RTT framebuffer");
        return false;
    }

    rttFramebufferCache_[cacheKey] = fb;
    renderTargetFramebuffer_ = fb;
    renderingToTexture_ = true;
    URHO3D_LOGDEBUG("RebuildRTFB: Created MRT framebuffer " + String(rttWidth_) + "x" + String(rttHeight_) + " with " + String(attachmentViews.Size()) + " attachments");
    return true;
}

bool VulkanGraphicsImpl::CreateRenderPass()
{
    // Phase 30: Support MSAA with resolve attachments
    // Layout: [0] = MSAA color (or swapchain if 1x), [1] = depth, [2] = resolve (if MSAA)
    bool useMSAA = (actualSampleCount_ != VK_SAMPLE_COUNT_1_BIT);
    int attachmentCount = useMSAA ? 3 : 2;

    Vector<VkAttachmentDescription> attachments;
    attachments.Resize(attachmentCount);

    // Attachment 0: Color attachment (MSAA if enabled, else swapchain)
    attachments[0].flags = 0;  // No special flags needed
    attachments[0].format = swapchainFormat_;
    attachments[0].samples = actualSampleCount_;  // Use MSAA sample count
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = useMSAA ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = useMSAA ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Attachment 1: Depth attachment
    attachments[1].flags = 0;  // No special flags needed
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = actualSampleCount_;  // Must match color attachment
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Attachment 2: Resolve attachment (only if MSAA enabled)
    if (useMSAA)
    {
        attachments[2].flags = 0;  // No special flags needed
        attachments[2].format = swapchainFormat_;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;  // Resolve target is always 1x
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolveRef{};
    if (useMSAA)
    {
        resolveRef.attachment = 2;
        resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    if (useMSAA)
    {
        subpass.pResolveAttachments = &resolveRef;  // Resolve MSAA to swapchain
    }

    // Single dependency: External -> subpass 0 for swapchain image acquisition
    // Instance buffer synchronization is handled by pipeline barrier in EnsureRenderPassStarted()
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachmentCount;
    renderPassInfo.pAttachments = &attachments[0];
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create render pass");
        return false;
    }

    // Create LOAD variant for mid-frame re-entry (e.g. after shadow map rendering)
    // Uses LOAD instead of CLEAR so color+depth from base pass are preserved
    VkAttachmentLoadOp origColorLoad = attachments[0].loadOp;
    VkAttachmentLoadOp origDepthLoad = attachments[1].loadOp;
    VkImageLayout origColorInitial = attachments[0].initialLayout;
    VkImageLayout origDepthInitial = attachments[1].initialLayout;

    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].initialLayout = attachments[0].finalLayout;  // Was transitioned to finalLayout by previous pass
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPassLoad_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create LOAD render pass variant");
        return false;
    }

    // Restore original values (for clarity, not strictly needed)
    attachments[0].loadOp = origColorLoad;
    attachments[0].initialLayout = origColorInitial;
    attachments[1].loadOp = origDepthLoad;
    attachments[1].initialLayout = origDepthInitial;

    // Register swapchain render passes in reverse lookup for stable hashing
    RenderPassDescriptor swapDesc;
    swapDesc.colorAttachmentCount = 1;
    swapDesc.colorFormats[0] = swapchainFormat_;
    swapDesc.depthFormat = VK_FORMAT_D32_SFLOAT;
    swapDesc.subpassCount = 1;
    swapDesc.sampleCount = actualSampleCount_;
    swapDesc.isRenderToTexture = false;
    uint32_t swapDescHash = swapDesc.Hash();
    renderPassToDescHash_[(uintptr_t)renderPass_] = swapDescHash;
    renderPassToDescHash_[(uintptr_t)renderPassLoad_] = swapDescHash;

    if (useMSAA)
    {
        URHO3D_LOGINFO(String("Render pass created with MSAA (") + String((int)actualSampleCount_) + "x) and resolve");
    }
    else
    {
        URHO3D_LOGINFO("Render pass created (1x MSAA) + LOAD variant");
    }
    return true;
}

VkRenderPass VulkanGraphicsImpl::GetOrCreateRenderPass(const RenderPassDescriptor& descriptor)
{
    // Calculate hash for this descriptor
    uint32_t descriptorHash = descriptor.Hash();

    // Check if we already have a cached render pass for this configuration
    if (renderPassCache_.Contains(descriptorHash))
    {
        return renderPassCache_[descriptorHash];
    }

    // Need to create a new render pass for this descriptor
    // Phase 35: Support multiple color attachments for deferred rendering

    // Validate attachment configuration
    if (descriptor.colorAttachmentCount > RenderPassDescriptor::MAX_COLOR_ATTACHMENTS)
    {
        URHO3D_LOGERROR("GetOrCreateRenderPass: Invalid color attachment count " + String(descriptor.colorAttachmentCount));
        return VK_NULL_HANDLE;
    }

    bool depthOnly = (descriptor.colorAttachmentCount == 0);

    // Total attachments = color attachments + depth
    uint32_t totalAttachmentCount = descriptor.colorAttachmentCount + 1;

    Vector<VkAttachmentDescription> attachments;
    attachments.Resize(totalAttachmentCount);

    // Color attachments (indices 0 to colorAttachmentCount-1)
    for (uint32_t i = 0; i < descriptor.colorAttachmentCount; ++i)
    {
        attachments[i].flags = 0;
        attachments[i].format = descriptor.colorFormats[i];
        attachments[i].samples = descriptor.sampleCount;
        attachments[i].loadOp = descriptor.colorLoadOp;
        attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // LOAD: content already valid from previous pass (SHADER_READ_ONLY after RTT, PRESENT after swapchain)
        // CLEAR: content undefined, will be cleared
        if (descriptor.colorLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD && descriptor.isRenderToTexture && !descriptor.isSwapchainHybrid)
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        else if (descriptor.colorLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD && descriptor.isSwapchainHybrid)
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        else
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (descriptor.isSwapchainHybrid)
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // Swapchain color needs PRESENT layout
        else if (descriptor.isRenderToTexture)
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        else
            attachments[i].finalLayout = (i == 0) ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Depth attachment (after all color attachments)
    uint32_t depthIndex = descriptor.colorAttachmentCount;
    attachments[depthIndex].flags = 0;
    attachments[depthIndex].format = descriptor.depthFormat;
    attachments[depthIndex].samples = descriptor.sampleCount;
    // When color uses LOAD (re-entry), depth should also LOAD to preserve scene depth for light volumes
    attachments[depthIndex].loadOp = (descriptor.colorLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD && !depthOnly)
        ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    // STORE depth for RTT (may be sampled later, e.g. ForwardHWDepth, shadow maps)
    attachments[depthIndex].storeOp = (depthOnly || descriptor.isRenderToTexture)
        ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[depthIndex].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[depthIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // For LOAD: depth was already written — initialLayout must match the finalLayout
    // that the previous render pass left the image in (READ_ONLY for RTT/shadow, ATTACHMENT for swapchain)
    if (attachments[depthIndex].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
    {
        attachments[depthIndex].initialLayout = (depthOnly || descriptor.isRenderToTexture)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    else
        attachments[depthIndex].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Transition depth to shader-readable layout when it may be sampled later:
    // - Shadow maps (depthOnly): always sampled by scene shaders
    // - RTT depth (ForwardHWDepth): sampled by post-process (e.g. motion blur)
    // Swapchain depth stays ATTACHMENT_OPTIMAL (never sampled)
    attachments[depthIndex].finalLayout = (depthOnly || descriptor.isRenderToTexture) ?
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Color attachment references for all color attachments
    Vector<VkAttachmentReference> colorRefs;
    colorRefs.Resize(descriptor.colorAttachmentCount);
    for (uint32_t i = 0; i < descriptor.colorAttachmentCount; ++i)
    {
        colorRefs[i].attachment = i;
        colorRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkAttachmentReference depthRef{};
    depthRef.attachment = depthIndex;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Phase 36: Input attachment references for lighting pass (deferred rendering)
    Vector<VkAttachmentReference> inputRefs;
    if (descriptor.inputAttachmentCount > 0)
    {
        inputRefs.Resize(descriptor.inputAttachmentCount);
        for (uint32_t i = 0; i < descriptor.inputAttachmentCount; ++i)
        {
            // Map input attachment to G-Buffer color attachment index
            uint32_t attachmentIndex = descriptor.inputAttachmentIndices[i];
            inputRefs[i].attachment = attachmentIndex;
            inputRefs[i].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    // Create subpass descriptions (geometry + lighting for deferred rendering)
    Vector<VkSubpassDescription> subpasses;
    subpasses.Resize(descriptor.subpassCount);

    // Zero-initialize all subpasses to avoid validation layer crashes
    for (uint32_t i = 0; i < descriptor.subpassCount; ++i)
    {
        memset(&subpasses[i], 0, sizeof(VkSubpassDescription));
    }

    // Subpass 0: Geometry pass (writes to color attachments)
    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount = descriptor.colorAttachmentCount;
    subpasses[0].pColorAttachments = colorRefs.Buffer();
    subpasses[0].pDepthStencilAttachment = &depthRef;
    subpasses[0].inputAttachmentCount = 0;  // Geometry doesn't read inputs
    subpasses[0].pInputAttachments = nullptr;

    // Subpass 1: Lighting pass (reads from input attachments, writes to color[0])
    if (descriptor.subpassCount > 1)
    {
        subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        // Lighting pass only writes to first color attachment (final output)
        subpasses[1].colorAttachmentCount = 1;
        subpasses[1].pColorAttachments = colorRefs.Buffer();  // Points to first color attachment
        subpasses[1].pDepthStencilAttachment = nullptr;       // No depth in lighting pass
        subpasses[1].inputAttachmentCount = descriptor.inputAttachmentCount;
        subpasses[1].pInputAttachments = !inputRefs.Empty() ? inputRefs.Buffer() : nullptr;
    }

    // Create subpass dependencies
    Vector<VkSubpassDependency> dependencies;
    uint32_t dependencyCount = 1;  // At minimum: external -> subpass 0
    if (descriptor.subpassCount > 1)
        dependencyCount = descriptor.subpassCount + 1;  // Add inter-subpass dependencies

    dependencies.Resize(dependencyCount);

    // Dependency: VK_SUBPASS_EXTERNAL -> Subpass 0 (geometry)
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = 0;  // Initialize to prevent garbage memory

    // Dependency: Subpass 0 -> Subpass 1 (geometry to lighting)
    if (descriptor.subpassCount > 1)
    {
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = 1;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;  // Allow pixel-local optimization
    }

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = totalAttachmentCount;
    renderPassInfo.pAttachments = attachments.Buffer();
    renderPassInfo.subpassCount = descriptor.subpassCount;
    renderPassInfo.pSubpasses = subpasses.Buffer();
    renderPassInfo.dependencyCount = dependencyCount;
    renderPassInfo.pDependencies = dependencies.Buffer();

    // Debug logging
    URHO3D_LOGDEBUG(String("Creating render pass: attachments=") + String(totalAttachmentCount) +
                   ", subpasses=" + String(descriptor.subpassCount) +
                   ", dependencies=" + String(dependencyCount));
    for (uint32_t i = 0; i < descriptor.subpassCount; ++i)
    {
        URHO3D_LOGDEBUG(String("  Subpass ") + String(i) + ": colorAttachments=" +
                       String(subpasses[i].colorAttachmentCount) +
                       ", inputAttachments=" + String(subpasses[i].inputAttachmentCount));
    }

    VkRenderPass renderPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create render pass from descriptor");
        return VK_NULL_HANDLE;
    }

    // Cache and return the render pass (both forward and reverse lookup)
    renderPassCache_[descriptorHash] = renderPass;
    renderPassToDescHash_[(uintptr_t)renderPass] = descriptorHash;
    URHO3D_LOGDEBUG("Created and cached render pass for descriptor hash: " + String((int)descriptorHash));

    return renderPass;
}

bool VulkanGraphicsImpl::CreateFramebuffers()
{
    framebuffers_.Resize(swapchainImageViews_.Size());

    // Phase 30: Create framebuffers with MSAA support
    bool useMSAA = (actualSampleCount_ != VK_SAMPLE_COUNT_1_BIT);

    for (size_t i = 0; i < swapchainImageViews_.Size(); ++i)
    {
        Vector<VkImageView> attachments;
        if (useMSAA)
        {
            // MSAA: [0] = msaa color, [1] = depth, [2] = swapchain resolve
            attachments.Push(msaaColorImageView_);
            attachments.Push(depthImageView_);
            attachments.Push(swapchainImageViews_[i]);
        }
        else
        {
            // 1x MSAA: [0] = swapchain color, [1] = depth
            attachments.Push(swapchainImageViews_[i]);
            attachments.Push(depthImageView_);
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = (uint32_t)attachments.Size();
        framebufferInfo.pAttachments = !attachments.Empty() ? &attachments[0] : nullptr;
        framebufferInfo.width = swapchainExtent_.width;
        framebufferInfo.height = swapchainExtent_.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS)
        {
            URHO3D_LOGERROR("Failed to create framebuffer");
            return false;
        }
    }

    if (useMSAA)
    {
        URHO3D_LOGINFO(String("Framebuffers created with MSAA (") + String((int)actualSampleCount_) + "x)");
    }
    else
    {
        URHO3D_LOGINFO("Framebuffers created (1x MSAA)");
    }
    return true;
}

bool VulkanGraphicsImpl::CreateCommandBuffers()
{
    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily_;

    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create command pool");
        return false;
    }

    // Allocate per-frame resources
    frames_.Resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        FrameResources& frame = frames_[i];

        // Command buffer
        if (vkAllocateCommandBuffers(device_, &allocInfo, &frame.commandBuffer) != VK_SUCCESS)
        {
            URHO3D_LOGERROR("Failed to allocate command buffer for frame " + String(i));
            return false;
        }

        // Per-frame imageAcquired semaphore
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAcquired) != VK_SUCCESS)
        {
            URHO3D_LOGERROR("Failed to create imageAcquired semaphore for frame " + String(i));
            return false;
        }

        // Fence (start signaled so first frame doesn't wait)
        if (vkCreateFence(device_, &fenceInfo, nullptr, &frame.fence) != VK_SUCCESS)
        {
            URHO3D_LOGERROR("Failed to create fence for frame " + String(i));
            return false;
        }
    }

    URHO3D_LOGINFO("Created " + String(MAX_FRAMES_IN_FLIGHT) + " frame resource bundles");
    return true;
}

bool VulkanGraphicsImpl::CreateSynchronizationPrimitives()
{
    // Per-frame resources (fences and semaphores) are now created in CreateCommandBuffers()
    // This function only creates timeline semaphore for advanced GPU-CPU synchronization (Phase 33)

    if (!CreateTimelineSemaphore())
    {
        URHO3D_LOGWARNING("Failed to create timeline semaphore, will use binary semaphores");
    }

    URHO3D_LOGINFO("Synchronization primitives created");
    return true;
}

bool VulkanGraphicsImpl::CreateMemoryAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.instance = instance_;

    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create memory allocator");
        return false;
    }

    URHO3D_LOGINFO("Memory allocator created");
    return true;
}

bool VulkanGraphicsImpl::CreateDescriptorPool()
{
    // Create descriptor pool with support for both uniform buffers and image samplers
    VkDescriptorPoolSize poolSizes[2];

    // Uniform buffers for shader parameters
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = VULKAN_DESCRIPTOR_POOL_SIZE;

    // Combined image samplers for textures
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = VULKAN_DESCRIPTOR_POOL_SIZE * 8;  // 8 texture units per set

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;  // Both uniform buffers and image samplers
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = VULKAN_DESCRIPTOR_POOL_SIZE;

    // SYNC FIX: Create one descriptor pool per FRAME (not per swapchain image)
    // CRITICAL: Must match constant buffer pool synchronization (MAX_FRAMES_IN_FLIGHT).
    // Fences are per-frame, so descriptor pool reset must be per-frame too.
    // Using swapchain image count would cause mismatch with constant buffers.
    descriptorPools_.Resize(MAX_FRAMES_IN_FLIGHT);

    for (unsigned i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPools_[i]) != VK_SUCCESS)
        {
            URHO3D_LOGERROR("Failed to create descriptor pool for frame " + String(i));
            return false;
        }
    }

    URHO3D_LOGINFO("Created " + String(MAX_FRAMES_IN_FLIGHT) + " descriptor pools (one per frame in flight) with uniform buffers and image samplers");
    return true;
}

bool VulkanGraphicsImpl::CreateDescriptorSetLayouts()
{
    // Phase 36A: Create descriptor set layouts for multi-set binding
    // These layouts define the structure of descriptor sets that can be bound simultaneously

    // Phase 36A Profiler Integration: Track descriptor set layout creation time
    VulkanProfiler* profiler = graphics_ ? graphics_->GetVulkanProfiler() : nullptr;
    if (profiler)
        profiler->StartPhase("Phase36A: Descriptor Set Layout Creation");

    // Set 0: Material descriptor layout (shader uniforms + textures)
    // Binding 0: Uniform buffer for shader parameters (ObjectVS, CameraVS, MaterialPS)
    // Bindings 1-7: Combined image samplers for textures
    VkDescriptorSetLayoutBinding materialBindings[MAX_TEXTURE_UNITS + 1];

    // Binding 0: Shader parameter uniform buffer
    materialBindings[0].binding = 0;
    materialBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    materialBindings[0].descriptorCount = 1;
    materialBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    materialBindings[0].pImmutableSamplers = nullptr;

    // Bindings 100-115: Texture samplers (match shader layout qualifiers)
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        materialBindings[i + 1].binding = 100 + i;  // Start at 100 to match shader bindings
        materialBindings[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        materialBindings[i + 1].descriptorCount = 1;
        materialBindings[i + 1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        materialBindings[i + 1].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
    materialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    materialLayoutInfo.bindingCount = MAX_TEXTURE_UNITS + 1;  // +1 for uniform buffer
    materialLayoutInfo.pBindings = materialBindings;

    if (vkCreateDescriptorSetLayout(device_, &materialLayoutInfo, nullptr, &materialDescriptorLayout_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create material descriptor set layout");
        return false;
    }
    URHO3D_LOGDEBUG("Created material descriptor set layout (Set 0)");

    // Set 1: G-Buffer texture layout (albedo, normal, depth for deferred lighting)
    // Bindings: 8 combined image samplers for G-Buffer textures
    VkDescriptorSetLayoutBinding gbufferBindings[MAX_TEXTURE_UNITS];
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        gbufferBindings[i].binding = i;
        gbufferBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbufferBindings[i].descriptorCount = 1;
        gbufferBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        gbufferBindings[i].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo gbufferLayoutInfo{};
    gbufferLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    gbufferLayoutInfo.bindingCount = MAX_TEXTURE_UNITS;
    gbufferLayoutInfo.pBindings = gbufferBindings;

    if (vkCreateDescriptorSetLayout(device_, &gbufferLayoutInfo, nullptr, &gbufferTextureLayout_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create G-Buffer texture descriptor set layout");
        return false;
    }
    URHO3D_LOGDEBUG("Created G-Buffer texture descriptor set layout (Set 1)");

    // Set 2: Constant buffer layout (all uniform buffers for vertex and fragment shaders)
    // Multiple bindings for different uniform blocks (based on Uniforms.glsl std140 layout)
    // Binding 0: FrameVS (cDeltaTime, cElapsedTime)
    // Binding 1: CameraVS (cCameraPos, cNearClip, cFarClip, cDepthMode, cFrustumSize, cView, cViewInv, cViewProj, cClipPlane)
    // Binding 5: ObjectVS (cModel)
    // Binding 6: FramePS (cDeltaTimePS, cElapsedTimePS)
    // Binding 7: CameraPS (cCameraPosPS, cDepthReconstruct, cGBufferInvSize, cNearClipPS, cFarClipPS)
    // Binding 8: ZonePS (cAmbientColor, cFogParams, cFogColor, cZoneMin, cZoneMax)
    // Binding 9: LightPS (cLightColor, cLightPosPS, cLightDirPS, cShadowParams, cLightMatricesPS[4])
    // Binding 10: MaterialPS (cMatDiffColor, cMatEmissiveColor, cMatEnvMapColor, cMatSpecColor)
    VkDescriptorSetLayoutBinding constantBufferBindings[8];

    // Binding 0: FrameVS
    constantBufferBindings[0].binding = 0;
    constantBufferBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constantBufferBindings[0].descriptorCount = 1;
    constantBufferBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    constantBufferBindings[0].pImmutableSamplers = nullptr;

    // Binding 1: CameraVS
    constantBufferBindings[1].binding = 1;
    constantBufferBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constantBufferBindings[1].descriptorCount = 1;
    constantBufferBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    constantBufferBindings[1].pImmutableSamplers = nullptr;

    // Binding 5: ObjectVS
    constantBufferBindings[2].binding = 5;
    constantBufferBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constantBufferBindings[2].descriptorCount = 1;
    constantBufferBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    constantBufferBindings[2].pImmutableSamplers = nullptr;

    // Binding 6: FramePS
    constantBufferBindings[3].binding = 6;
    constantBufferBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constantBufferBindings[3].descriptorCount = 1;
    constantBufferBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    constantBufferBindings[3].pImmutableSamplers = nullptr;

    // Binding 7: CameraPS
    constantBufferBindings[4].binding = 7;
    constantBufferBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constantBufferBindings[4].descriptorCount = 1;
    constantBufferBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    constantBufferBindings[4].pImmutableSamplers = nullptr;

    // Binding 8: ZonePS
    constantBufferBindings[5].binding = 8;
    constantBufferBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constantBufferBindings[5].descriptorCount = 1;
    constantBufferBindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    constantBufferBindings[5].pImmutableSamplers = nullptr;

    // Binding 9: LightPS
    constantBufferBindings[6].binding = 9;
    constantBufferBindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constantBufferBindings[6].descriptorCount = 1;
    constantBufferBindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    constantBufferBindings[6].pImmutableSamplers = nullptr;

    // Binding 10: MaterialPS
    constantBufferBindings[7].binding = 10;
    constantBufferBindings[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constantBufferBindings[7].descriptorCount = 1;
    constantBufferBindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    constantBufferBindings[7].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo constantBufferLayoutInfo{};
    constantBufferLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    constantBufferLayoutInfo.bindingCount = 8;
    constantBufferLayoutInfo.pBindings = constantBufferBindings;

    if (vkCreateDescriptorSetLayout(device_, &constantBufferLayoutInfo, nullptr, &constantBufferLayout_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create constant buffer descriptor set layout");
        return false;
    }
    URHO3D_LOGDEBUG("Created constant buffer descriptor set layout (Set 2)");

    // Set 3: Input attachment layout (tile-local G-Buffer optimization)
    // Bindings: 4 input attachments for G-Buffer (albedo, normal, depth, specular)
    VkDescriptorSetLayoutBinding inputAttachmentBindings[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        inputAttachmentBindings[i].binding = i;
        inputAttachmentBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        inputAttachmentBindings[i].descriptorCount = 1;
        inputAttachmentBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        inputAttachmentBindings[i].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo inputAttachmentLayoutInfo{};
    inputAttachmentLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    inputAttachmentLayoutInfo.bindingCount = 4;
    inputAttachmentLayoutInfo.pBindings = inputAttachmentBindings;

    if (vkCreateDescriptorSetLayout(device_, &inputAttachmentLayoutInfo, nullptr, &inputAttachmentLayout_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create input attachment descriptor set layout");
        return false;
    }
    URHO3D_LOGDEBUG("Created input attachment descriptor set layout (Set 3)");

    URHO3D_LOGINFO("All descriptor set layouts created successfully");

    // Phase 36A Profiler Integration: End tracking
    if (profiler)
        profiler->EndPhase();

    // Create global pipeline layout using all 4 descriptor set layouts
    Vector<VkDescriptorSetLayout> allLayouts;
    allLayouts.Push(materialDescriptorLayout_);
    allLayouts.Push(gbufferTextureLayout_);
    allLayouts.Push(constantBufferLayout_);
    allLayouts.Push(inputAttachmentLayout_);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = allLayouts.Size();
    pipelineLayoutInfo.pSetLayouts = &allLayouts[0];
    pipelineLayoutInfo.pushConstantRangeCount = 0;  // No push constants for now
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &currentPipelineLayout_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create global pipeline layout");
        return false;
    }
    globalPipelineLayout_ = currentPipelineLayout_;

    URHO3D_LOGINFO("Global pipeline layout created and set as current");

    return true;
}

bool VulkanGraphicsImpl::CreatePipelineCache()
{
    // Create VulkanPipelineCache object (Phase B Quick Win #10)
    // Use graphics_->GetContext() to get Context for Object-derived classes
    pipelineCache_ = MakeShared<VulkanPipelineCache>(graphics_->GetContext());

    if (!pipelineCache_ || !pipelineCache_->Initialize(this))
    {
        URHO3D_LOGERROR("Failed to initialize pipeline cache");
        pipelineCache_ = nullptr;
        return false;
    }

    URHO3D_LOGINFO("Pipeline cache initialized");
    return true;
}

VkSurfaceFormatKHR VulkanGraphicsImpl::FindSurfaceFormat()
{
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);

    Vector<VkSurfaceFormatKHR> availableFormats(formatCount);
    if (!availableFormats.Empty())
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, &availableFormats[0]);

    if (availableFormats.Empty())
    {
        URHO3D_LOGWARNING("No surface formats available, using fallback");
        return VkSurfaceFormatKHR{VULKAN_FALLBACK_SURFACE_FORMAT, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }

    // Priority list of preferred formats (all with SRGB color space)
    VkFormat preferredFormats[] = {
        VULKAN_PREFERRED_SURFACE_FORMAT,      // B8G8R8A8 SRGB
        VULKAN_FALLBACK_SURFACE_FORMAT,       // R8G8B8A8 SRGB
        VK_FORMAT_B8G8R8A8_UNORM,            // B8G8R8A8 Linear (fallback)
        VK_FORMAT_R8G8B8A8_UNORM             // R8G8B8A8 Linear (fallback)
    };

    // Try to find a format with SRGB color space first
    for (VkFormat preferred : preferredFormats)
    {
        for (const auto& format : availableFormats)
        {
            if (format.format == preferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                URHO3D_LOGDEBUG("Selected surface format: preferred");
                return format;
            }
        }
    }

    // If no SRGB format found, try preferred formats in linear color space
    for (VkFormat preferred : preferredFormats)
    {
        for (const auto& format : availableFormats)
        {
            if (format.format == preferred)
            {
                URHO3D_LOGDEBUG("Selected surface format: preferred (linear)");
                return format;
            }
        }
    }

    // Fallback to first available format
    URHO3D_LOGWARNING("Using first available surface format as fallback");
    return availableFormats[0];
}

VkPresentModeKHR VulkanGraphicsImpl::FindPresentMode()
{
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, nullptr);

    Vector<VkPresentModeKHR> availableModes(modeCount);
    if (!availableModes.Empty())
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, &availableModes[0]);

    if (availableModes.Empty())
    {
        URHO3D_LOGWARNING("No present modes available, using FIFO fallback");
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    // Priority: mailbox (low latency, triple buffering) > FIFO (vsync) > immediate
    for (const auto& mode : availableModes)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            URHO3D_LOGDEBUG("Selected present mode: MAILBOX (triple buffering)");
            return mode;
        }
    }

    // FIFO is always available but not in the list sometimes, explicitly return it as fallback
    for (const auto& mode : availableModes)
    {
        if (mode == VK_PRESENT_MODE_FIFO_KHR)
        {
            URHO3D_LOGDEBUG("Selected present mode: FIFO (vsync)");
            return mode;
        }
    }

    URHO3D_LOGWARNING("Mailbox and FIFO modes not available, using first available mode");
    return availableModes[0];
}

uint32_t VulkanGraphicsImpl::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    URHO3D_LOGERROR("Failed to find suitable memory type");
    return 0;
}

void VulkanGraphicsImpl::TransitionImageLayout(VkImage image, VkFormat format,
                                            VkImageLayout oldLayout, VkImageLayout newLayout,
                                            uint32_t mipLevels)
{
    // Get current frame command buffer for recording transition command
    VkCommandBuffer commandBuffer = GetFrameCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE)
        return;

    // Create image memory barrier for layout transition
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    // Detect depth formats and use appropriate aspect mask
    bool isDepth = (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT ||
                    format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                    format == VK_FORMAT_D16_UNORM_S8_UINT);
    barrier.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // Determine pipeline stage and access flags based on layout transition
    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
    }

    if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        barrier.dstAccessMask = 0;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }

    // Record pipeline barrier to handle the layout transition
    vkCmdPipelineBarrier(commandBuffer,
                         srcStageMask, dstStageMask,
                         0,  // no dependency flags
                         0, nullptr,  // no memory barriers
                         0, nullptr,  // no buffer barriers
                         1, &barrier);  // one image barrier
}

void VulkanGraphicsImpl::TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format,
                                            VkImageLayout oldLayout, VkImageLayout newLayout,
                                            uint32_t mipLevels)
{
    // Use provided command buffer instead of getting frame command buffer
    if (commandBuffer == VK_NULL_HANDLE)
    {
        URHO3D_LOGERROR("TransitionImageLayout: NULL command buffer");
        return;
    }

    if (image == VK_NULL_HANDLE)
    {
        URHO3D_LOGERROR("TransitionImageLayout: NULL image");
        return;
    }

    // Create image memory barrier for layout transition
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    // Detect depth formats and use appropriate aspect mask
    bool isDepthFormat = (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT ||
                          format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                          format == VK_FORMAT_D16_UNORM_S8_UINT);
    barrier.subresourceRange.aspectMask = isDepthFormat ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // Determine pipeline stage and access flags based on layout transition
    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
    }

    if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        barrier.dstAccessMask = 0;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }

    // Record pipeline barrier to handle the layout transition
    vkCmdPipelineBarrier(commandBuffer,
                         srcStageMask, dstStageMask,
                         0,  // no dependency flags
                         0, nullptr,  // no memory barriers
                         0, nullptr,  // no buffer barriers
                         1, &barrier);  // one image barrier

}

void VulkanGraphicsImpl::ReportPoolStatistics() const
{
    /// \brief Phase 12 (Quick Win #9): Gather and report pool statistics
    /// \details Consolidates profiling data from all memory pools (constant buffers,
    /// descriptor sets, secondary command buffers) for performance analysis.
    /// Enables identification of pool utilization bottlenecks and optimization opportunities.

    if (!constantBufferPool_)
    {
        URHO3D_LOGWARNING("ReportPoolStatistics: Constant buffer pool not initialized");
        return;
    }

    // Gather constant buffer pool statistics
    VulkanConstantBufferPool::PoolStats cbStats = constantBufferPool_->GetStatistics();

    URHO3D_LOGINFO("=== Vulkan Memory Pool Statistics ===");
    URHO3D_LOGINFO("Constant Buffer Pool:");
    URHO3D_LOGINFO("  Total pool size: " + String(cbStats.totalPoolSize / 1024 / 1024) + " MB");
    URHO3D_LOGINFO("  Used this frame: " + String(cbStats.usedSize) + " bytes");
    URHO3D_LOGINFO("  Wasted (fragmentation): " + String(cbStats.wastedSize) + " bytes");
    URHO3D_LOGINFO("  Allocated buffers: " + String(cbStats.allocatedBuffers));
    URHO3D_LOGINFO("  Peak frame size: " + String(cbStats.peakFrameSize) + " bytes");
    URHO3D_LOGINFO("  Allocation count: " + String(cbStats.allocationCount));
    URHO3D_LOGINFO("  Fragmentation ratio: " + String(cbStats.averageFragmentation, 2) + "%");

    // Gather material descriptor statistics
    if (materialDescriptorManager_)
    {
        URHO3D_LOGINFO("Material Descriptor Manager:");

        // Quick Win #8: Dirty flag optimization statistics
        // Note: These counters are internal to materialDescriptorManager_ and would need accessor methods
        // For now, report the descriptor cache size
        uint32_t descriptorCount = materialDescriptorManager_->GetCachedDescriptorCount();
        URHO3D_LOGINFO("  Cached material descriptors: " + String(descriptorCount));
    }
    else
    {
        URHO3D_LOGWARNING("ReportPoolStatistics: Material descriptor manager not initialized");
    }

    // Gather descriptor pool statistics if available (Triple-buffering: 3 pools)
    if (descriptorPools_[0])
    {
        URHO3D_LOGINFO("Descriptor Pools (per-frame):");

        // Count active descriptor sets (indirect count from pool state)
        // Note: VulkanDescriptorPool doesn't expose statistics directly yet
        // This is a placeholder for future enhancement with detailed descriptor stats
        URHO3D_LOGINFO("  " + String(VULKAN_FRAME_COUNT) + " descriptor pools allocated (one per frame, basic tracking only)");
    }

    // Gather secondary command buffer pool statistics
    if (secondaryCommandBufferPool_)
    {
        URHO3D_LOGINFO("Secondary Command Buffer Pool (Phase 4 Multi-threading):");
        URHO3D_LOGINFO("  Pool initialized for parallel batch recording");
        URHO3D_LOGINFO("  Worker threads: 4");
    }

    URHO3D_LOGINFO("=== End Pool Statistics ===");
}

// ============================================
// Reflection-Based Pipeline Layout Creation
// ============================================

/// Helper: Convert SPIRVResource to VkDescriptorSetLayoutBinding
static inline VkDescriptorSetLayoutBinding SPIRVResourceToBinding(const SPIRVResource& resource)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = resource.binding;
    binding.descriptorType = resource.descriptorType;
    binding.descriptorCount = resource.descriptorCount;
    binding.stageFlags = resource.stageFlags;
    binding.pImmutableSamplers = nullptr;
    return binding;
}

/// Create descriptor set layout dynamically from reflected SPIR-V resources
/// Merges vertex and pixel shader resources, combining stage flags for shared bindings
VkDescriptorSetLayout VulkanGraphicsImpl::CreateReflectionBasedLayout(
    const Vector<SPIRVResource>& vsResources,
    const Vector<SPIRVResource>& psResources)
{
    // Merge VS + PS resources (combine bindings from both stages)
    HashMap<uint32_t, VkDescriptorSetLayoutBinding> bindingMap;  // binding number → binding info

    // Add vertex shader resources
    for (const auto& res : vsResources)
    {
        VkDescriptorSetLayoutBinding binding = SPIRVResourceToBinding(res);
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        bindingMap[binding.binding] = binding;
    }

    // Add pixel shader resources (merge if binding number already exists)
    for (const auto& res : psResources)
    {
        VkDescriptorSetLayoutBinding binding = SPIRVResourceToBinding(res);

        if (bindingMap.Contains(binding.binding))
        {
            // Same binding in VS and PS - combine stage flags
            bindingMap[binding.binding].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        else
        {
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindingMap[binding.binding] = binding;
        }
    }

    // Convert HashMap to Vector for Vulkan
    Vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.Reserve(bindingMap.Size());
    for (auto& pair : bindingMap)
    {
        bindings.Push(pair.second_);
    }

    // DEBUG: Disabled for performance
    // static int layoutDebugCount = 0;
    // if (layoutDebugCount < 5)
    // {
    //             layoutDebugCount, bindings.Size());
    //     for (const auto& binding : bindings)
    //     {
    //         const char* typeStr = (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
    //                               ? "UBO" : "SAMPLER";
    //                 binding.binding, typeStr, binding.descriptorCount, binding.stageFlags);
    //     }
    //     layoutDebugCount++;
    // }

    // Create descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.Size());
    layoutInfo.pBindings = bindings.Empty() ? nullptr : &bindings[0];

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout);

    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("CreateReflectionBasedLayout: Failed to create descriptor set layout");
        return VK_NULL_HANDLE;
    }

    // Disabled for performance - this happens per draw call
    // URHO3D_LOGDEBUG("CreateReflectionBasedLayout: Created layout with " + String(bindings.Size()) + " bindings");
    return layout;
}

/// Get or create cached descriptor set layout - PERFORMANCE FIX
VkDescriptorSetLayout VulkanGraphicsImpl::GetOrCreateDescriptorSetLayout(
    const Vector<SPIRVResource>& vsResources,
    const Vector<SPIRVResource>& psResources)
{
    // Compute hash of binding configuration using Urho3D hashing
    unsigned layoutHash = 0;
    for (const auto& res : vsResources)
    {
        CombineHash(layoutHash, res.set);
        CombineHash(layoutHash, res.binding);
        CombineHash(layoutHash, (unsigned)res.descriptorType);
    }
    for (const auto& res : psResources)
    {
        CombineHash(layoutHash, res.set);
        CombineHash(layoutHash, res.binding);
        CombineHash(layoutHash, (unsigned)res.descriptorType);
    }

    // Check cache first
    auto it = descriptorSetLayoutCache_.Find(layoutHash);
    if (it != descriptorSetLayoutCache_.End())
    {
        return it->second_;
    }

    // Not cached - create new layout
    VkDescriptorSetLayout layout = CreateReflectionBasedLayout(vsResources, psResources);
    if (layout)
    {
        descriptorSetLayoutCache_[layoutHash] = layout;
    }
    return layout;
}

/// Get or create cached pipeline layout - PERFORMANCE FIX
VkPipelineLayout VulkanGraphicsImpl::GetOrCreatePipelineLayout(VkDescriptorSetLayout descriptorSetLayout)
{
    unsigned long long layoutKey = (unsigned long long)descriptorSetLayout;

    // Check cache first
    auto it = pipelineLayoutCache_.Find(layoutKey);
    if (it != pipelineLayoutCache_.End())
    {
        return it->second_;
    }

    // Not cached - create new pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout) == VK_SUCCESS)
    {
        pipelineLayoutCache_[layoutKey] = layout;
    }
    return layout;
}

// ============================================
// Phase 33: Shader Module Creation from Variations
// ============================================

// Content identity for the persistent shader-module cache. Keyed by (owner resource name, stage,
// defines) — mirrors the compute module cache's content key and replaces the old raw ShaderVariation*
// key, which could hand back a stale module if a variation was freed and a different one reallocated
// at the same address (coder2's recycled-pointer hazard).
// STAGE IS REQUIRED: a shader's VS and PS variations share the same owner name AND defines (they come
// from one .glsl source), so without the stage they would collide in this shared map and the second
// stage would receive the first stage's module.
static inline uint64_t ShaderModuleContentKey(ShaderVariation* v)
{
    Shader* owner = v->GetOwner();
    const String& name = owner ? owner->GetName() : v->GetName();
    return (uint64_t)StringHash(name + "|" + String((int)v->GetShaderType()) + "|" + v->GetDefines()).Value();
}

bool VulkanGraphicsImpl::CreateShaderModules(ShaderVariation* vertexShader, ShaderVariation* pixelShader,
                                           VkShaderModule& vsModule, VkShaderModule& fsModule,
                                           ShaderVariation* geometryShader, VkShaderModule* gsModule)
{
    vsModule = VK_NULL_HANDLE;
    fsModule = VK_NULL_HANDLE;
    if (gsModule)
        *gsModule = VK_NULL_HANDLE;

    // Compile vertex shader if provided (persistent content-keyed cache)
    if (vertexShader)
    {
        const uint64_t key = ShaderModuleContentKey(vertexShader);
        auto it = shaderModuleCache_.Find(key);
        const bool haveModule = (it != shaderModuleCache_.End());
        // Reflection lives on the ShaderVariation, NOT the module cache: a content-key HIT can be a
        // freshly-reloaded variation reusing an existing module — it still needs its own bindings
        // populated. In steady state (module cached AND already reflected) this whole block is skipped.
        const bool needReflect = vertexShader->GetReflectedResources().Empty();

        if (haveModule)
            vsModule = it->second_;

        if (!haveModule || needReflect)
        {
            Vector<uint32_t> spirvBytecode;
            String errorOutput;

            if (!VulkanShaderModule::GetOrCompileSPIRV(vertexShader, spirvBytecode, errorOutput))
            {
                URHO3D_LOGERROR("CreateShaderModules: Failed to compile vertex shader: " + errorOutput);
                return false;
            }

            if (!haveModule)
            {
                vsModule = VulkanShaderModule::CreateShaderModule(device_, spirvBytecode);
                if (!vsModule)
                {
                    URHO3D_LOGERROR("CreateShaderModules: Failed to create vertex shader module");
                    return false;
                }
                shaderModuleCache_[key] = vsModule;
            }

            // Reflect SPIR-V to extract bindings and store in ShaderVariation (only if not already reflected)
            if (needReflect)
            {
                VulkanSPIRVReflect reflector;
                Vector<SPIRVResource> resources;
                if (reflector.ReflectShaderResources(spirvBytecode, VK_SHADER_STAGE_VERTEX_BIT, resources))
                {
                    vertexShader->SetReflectedResources(resources);
                }
            }
        }
    }

    // Compile fragment/pixel shader if provided (persistent content-keyed cache)
    if (pixelShader)
    {
        const uint64_t key = ShaderModuleContentKey(pixelShader);
        auto it = shaderModuleCache_.Find(key);
        const bool haveModule = (it != shaderModuleCache_.End());
        // Reflection lives on the ShaderVariation, NOT the module cache (see the VS block above).
        const bool needReflect = pixelShader->GetReflectedResources().Empty();

        if (haveModule)
            fsModule = it->second_;

        if (!haveModule || needReflect)
        {
            Vector<uint32_t> spirvBytecode;
            String errorOutput;

            if (!VulkanShaderModule::GetOrCompileSPIRV(pixelShader, spirvBytecode, errorOutput))
            {
                URHO3D_LOGERROR("CreateShaderModules: Failed to compile pixel shader: " + errorOutput);
                return false;
            }

            if (!haveModule)
            {
                fsModule = VulkanShaderModule::CreateShaderModule(device_, spirvBytecode);
                if (!fsModule)
                {
                    URHO3D_LOGERROR("CreateShaderModules: Failed to create fragment shader module");
                    return false;
                }
                shaderModuleCache_[key] = fsModule;
            }

            // Reflect SPIR-V to extract bindings and store in ShaderVariation (only if not already reflected)
            if (needReflect)
            {
                VulkanSPIRVReflect reflector;
                Vector<SPIRVResource> resources;
                if (reflector.ReflectShaderResources(spirvBytecode, VK_SHADER_STAGE_FRAGMENT_BIT, resources))
                {
                    pixelShader->SetReflectedResources(resources);
                }
            }
        }
    }

    // Compile geometry shader if provided (Phase 36+: Geometry Shader Support) (persistent content-keyed cache)
    if (geometryShader && gsModule)
    {
        const uint64_t key = ShaderModuleContentKey(geometryShader);
        auto it = shaderModuleCache_.Find(key);
        if (it != shaderModuleCache_.End())
        {
            *gsModule = it->second_;
        }
        else
        {
        Vector<uint32_t> spirvBytecode;
        String errorOutput;

        if (!VulkanShaderModule::GetOrCompileSPIRV(geometryShader, spirvBytecode, errorOutput))
        {
            URHO3D_LOGERROR("CreateShaderModules: Failed to compile geometry shader: " + errorOutput);
            return false;
        }

        *gsModule = VulkanShaderModule::CreateShaderModule(device_, spirvBytecode);
        if (!(*gsModule))
        {
            URHO3D_LOGERROR("CreateShaderModules: Failed to create geometry shader module");
            return false;
        }

        shaderModuleCache_[key] = *gsModule;
        }
    }

    return true;
}

// ============================================
// Phase 32 Helper Functions for State Conversion
// ============================================

/// \brief Convert Urho3D CullMode to Vulkan cull mode flags
static VkCullModeFlags ConvertCullMode(CullMode mode)
{
    switch (mode)
    {
    case CULL_NONE: return VK_CULL_MODE_NONE;
    case CULL_CCW:  return VK_CULL_MODE_BACK_BIT;
    case CULL_CW:   return VK_CULL_MODE_FRONT_BIT;
    default:        return VK_CULL_MODE_BACK_BIT;
    }
}

/// \brief Convert Urho3D CompareMode to Vulkan comparison operator
static VkCompareOp ConvertCompareMode(CompareMode mode)
{
    switch (mode)
    {
    case CMP_ALWAYS:       return VK_COMPARE_OP_ALWAYS;
    case CMP_EQUAL:        return VK_COMPARE_OP_EQUAL;
    case CMP_NOTEQUAL:     return VK_COMPARE_OP_NOT_EQUAL;
    case CMP_LESS:         return VK_COMPARE_OP_LESS;
    case CMP_LESSEQUAL:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CMP_GREATER:      return VK_COMPARE_OP_GREATER;
    case CMP_GREATEREQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    default:               return VK_COMPARE_OP_ALWAYS;
    }
}

/// \brief Convert Urho3D StencilOp to Vulkan stencil operation
static VkStencilOp ConvertStencilOp(StencilOp op)
{
    switch (op)
    {
    case OP_KEEP: return VK_STENCIL_OP_KEEP;
    case OP_ZERO: return VK_STENCIL_OP_ZERO;
    case OP_REF:  return VK_STENCIL_OP_REPLACE;
    case OP_INCR: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case OP_DECR: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    default:      return VK_STENCIL_OP_KEEP;
    }
}

/// \brief Convert Urho3D PrimitiveType to Vulkan primitive topology
static VkPrimitiveTopology ConvertPrimitiveType(PrimitiveType type)
{
    switch (type)
    {
    case TRIANGLE_LIST:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case LINE_LIST:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case POINT_LIST:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case LINE_STRIP:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case TRIANGLE_FAN:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    default:             return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// Phase 32 Step 3 & Phase 33 Step 2: Pipeline creation with graphics state and shader modules
VkPipeline VulkanGraphicsImpl::GetOrCreateGraphicsPipeline(
    VkPipelineLayout layout,
    VkRenderPass renderPass,
    const VulkanPipelineState& state,
    VertexBuffer* vertexBuffer,
    VertexBuffer* instanceBuffer,
    VkShaderModule vsModule,
    VkShaderModule fsModule,
    VkShaderModule gsModule,
    ShaderVariation* vertexShader,
    ShaderVariation* pixelShader,
    ShaderVariation* geometryShader)
{
    if (!pipelineCache_)
    {
        URHO3D_LOGERROR("GetOrCreateGraphicsPipeline: Pipeline cache not initialized");
        return VK_NULL_HANDLE;
    }

    // Content-based hash — stable across sessions for disk cache.
    // Uses shader name+defines and render pass format instead of Vulkan handle pointers.
    uint64_t stateHash = state.Hash();
    stateHash = stateHash * 31 + (instanceBuffer ? 1 : 0);
    stateHash = stateHash * 31 + (vertexBuffer ? vertexBuffer->GetBufferHash(0) : 0);
    if (vertexShader)
        stateHash = stateHash * 31 + (uint64_t)StringHash(vertexShader->GetFullName()).Value();
    if (pixelShader)
        stateHash = stateHash * 31 + (uint64_t)StringHash(pixelShader->GetFullName()).Value();
    if (geometryShader)
        stateHash = stateHash * 31 + (uint64_t)StringHash(geometryShader->GetFullName()).Value();
    auto rpIt = renderPassToDescHash_.Find((uintptr_t)renderPass);
    stateHash = stateHash * 31 + (rpIt != renderPassToDescHash_.End() ? rpIt->second_ : 0);

    // PERFORMANCE FIX: Early-out on pipeline cache hit — skip 300+ lines of VkCreateInfo building
    VkPipeline cachedPipeline = pipelineCache_->FindCachedPipeline(stateHash);
    if (cachedPipeline != VK_NULL_HANDLE)
    {
        pipelineCache_->IncrementHits();
        return cachedPipeline;
    }

    // Build rasterization state from CullMode and FillMode
    VkPipelineRasterizationStateCreateInfo rasterizationState{};
    rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationState.polygonMode = (state.fillMode == FILL_WIREFRAME) ?
        VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizationState.cullMode = ConvertCullMode(state.cullMode);
    rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationState.lineWidth = 1.0f;
    // Always enable depth bias (controlled dynamically via vkCmdSetDepthBias)
    rasterizationState.depthBiasEnable = VK_TRUE;

    // Build color blend state from blend mode
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = state.colorWrite ?
        (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) : 0;

    switch (state.blendMode)
    {
    case BLEND_REPLACE:
        blendAttachment.blendEnable = false;
        break;
    case BLEND_ADD:
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case BLEND_SUBTRACT:
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        break;
    case BLEND_MULTIPLY:
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case BLEND_ALPHA:
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case BLEND_ADDALPHA:
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case BLEND_INVDESTALPHA:
        blendAttachment.blendEnable = true;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    default:
        blendAttachment.blendEnable = false;
        break;
    }

    // Determine color attachment count from current render target configuration
    // Shadow maps (depth-only) = 0, regular RTT = 1, G-Buffer = 4, swapchain = 1
    uint32_t attachmentCount = 1; // default for swapchain
    if (renderPass == renderTargetRenderPass_ && graphics_)
    {
        attachmentCount = 0;
        for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
        {
            if (graphics_->GetRenderTarget(i))
                attachmentCount = i + 1;
        }
        // Swapchain hybrid (swapchain color + custom depth) has 1 color attachment
        // but GetRenderTarget() returns null since swapchain isn't tracked as an RTT
        if (attachmentCount == 0 && renderTargetRPDescriptor_.isSwapchainHybrid)
            attachmentCount = 1;
        // Also handle case where render targets were cleared (e.g. debug draw after render path)
        // but the render pass is still the RTT one with a color attachment
        if (attachmentCount == 0 && renderTargetRPDescriptor_.colorAttachmentCount > 0)
            attachmentCount = renderTargetRPDescriptor_.colorAttachmentCount;
    }

    // DIAG: Log blend attachment count for pipeline creation

    // Create blend attachments for all color attachments (all use same blend mode)
    Vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    blendAttachments.Resize(attachmentCount);
    for (uint32_t i = 0; i < attachmentCount; ++i)
    {
        blendAttachments[i] = blendAttachment;  // Copy same blend state to all attachments
    }

    VkPipelineColorBlendStateCreateInfo colorBlendState{};
    colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendState.attachmentCount = attachmentCount;
    colorBlendState.pAttachments = blendAttachments.Buffer();
    colorBlendState.logicOpEnable = false;

    // Build depth-stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilState.depthTestEnable = (state.depthTest != CMP_ALWAYS);
    depthStencilState.depthWriteEnable = state.depthWrite;
    depthStencilState.depthCompareOp = ConvertCompareMode(state.depthTest);


    // Stencil state (front and back faces same)
    if (state.stencilTest)
    {
        VkStencilOpState stencilOpState{};
        stencilOpState.compareMask = state.stencilCompareMask;
        stencilOpState.writeMask = state.stencilWriteMask;
        stencilOpState.reference = state.stencilRef;
        stencilOpState.compareOp = ConvertCompareMode(state.stencilTestMode);
        stencilOpState.passOp = ConvertStencilOp(state.stencilPass);
        stencilOpState.failOp = ConvertStencilOp(state.stencilFail);
        stencilOpState.depthFailOp = ConvertStencilOp(state.stencilZFail);

        depthStencilState.stencilTestEnable = true;
        depthStencilState.front = stencilOpState;
        depthStencilState.back = stencilOpState;
    }

    // Create minimal pipeline state structures for this frame
    // Full structures would come from vertex/input assembly setup
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
    inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyState.topology = ConvertPrimitiveType(state.primitiveType);
    inputAssemblyState.primitiveRestartEnable = false;

    // CRITICAL FIX: Build vertex input state from vertex buffer layout
    // Only create attributes that actually exist in the vertex buffer
    Vector<VkVertexInputBindingDescription> bindingDescriptions;
    Vector<VkVertexInputAttributeDescription> attributeDescriptions;

    // Standard vertex attribute locations — used to fill dummy entries for locations
    // not provided by vertex or instance buffers (SPIR-V doesn't strip unused interface vars)
    struct DefaultAttr { unsigned location; VkFormat format; };
    static const DefaultAttr standardLocations[] = {
        {0, VK_FORMAT_R32G32B32A32_SFLOAT},   // iPos (vec4)
        {1, VK_FORMAT_R32G32B32_SFLOAT},       // iNormal (vec3)
        {2, VK_FORMAT_R32G32B32A32_SFLOAT},    // iColor (vec4)
        {3, VK_FORMAT_R32G32_SFLOAT},           // iTexCoord (vec2)
        {4, VK_FORMAT_R32G32_SFLOAT},           // iTexCoord1 (vec2)
        {5, VK_FORMAT_R32G32B32A32_SFLOAT},    // iTangent (vec4)
        {6, VK_FORMAT_R32G32B32A32_SFLOAT},    // iBlendWeights (vec4)
        {7, VK_FORMAT_R32G32B32A32_SFLOAT},    // iBlendIndices (vec4)
        {8, VK_FORMAT_R32G32B32_SFLOAT},       // iCubeTexCoord (vec3)
        {9, VK_FORMAT_R32G32B32A32_SFLOAT},    // iCubeTexCoord1 (vec4)
        {10, VK_FORMAT_R32G32B32A32_SFLOAT},   // iTexCoord4 (vec4) — instanced matrix row 0
        {11, VK_FORMAT_R32G32B32A32_SFLOAT},   // iTexCoord5 (vec4) — instanced matrix row 1
        {12, VK_FORMAT_R32G32B32A32_SFLOAT},   // iTexCoord6 (vec4) — instanced matrix row 2
        {13, VK_FORMAT_R32_SFLOAT},             // iObjectIndex (float) — instanced shaders
    };
    static const unsigned numStandardLocations = sizeof(standardLocations) / sizeof(standardLocations[0]);

    if (vertexBuffer)
    {
        const Vector<VertexElement>& elements = vertexBuffer->GetElements();

        // Create binding description (one binding at index 0)
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = vertexBuffer->GetVertexSize();
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions.Push(binding);
        for (unsigned i = 0; i < elements.Size(); ++i)
        {
            const VertexElement& element = elements[i];

            VkVertexInputAttributeDescription attr{};
            attr.binding = 0;

            // Map vertex element semantic to shader input location
            // Must match Transform.glsl attribute declaration order:
            // 0=iPos, 1=iNormal, 2=iColor, 3=iTexCoord, 4=iTexCoord1, 5=iTangent,
            // 6=iBlendWeights, 7=iBlendIndices, 8=iBinormal, 13=iObjectIndex
            // [VTXLOC] diagnostic removed — was per-pipeline-creation log spam
            switch (element.semantic_)
            {
                case SEM_POSITION:
                    attr.location = 0;
                    break;
                case SEM_NORMAL:
                    attr.location = 1;
                    break;
                case SEM_COLOR:
                    attr.location = (element.index_ == 0) ? 2 : 9;  // iColor=2, extra color channels to unused location 9
                    break;
                case SEM_TEXCOORD:
                    attr.location = 3 + element.index_;  // iTexCoord=3, iTexCoord1=4
                    break;
                case SEM_TANGENT:
                    attr.location = 5;
                    break;
                case SEM_BLENDWEIGHTS:
                    attr.location = 6;
                    break;
                case SEM_BLENDINDICES:
                    attr.location = 7;
                    break;
                case SEM_BINORMAL:
                    attr.location = 8;
                    break;
                case SEM_OBJECTINDEX:
                    attr.location = 13;
                    break;
                default:
                    attr.location = 14 + i;  // Safe fallback — high locations avoid conflicts
                    break;
            }


            // Map VertexElementType to VkFormat
            switch (element.type_)
            {
            case TYPE_INT:
                attr.format = VK_FORMAT_R32_SINT;
                break;
            case TYPE_FLOAT:
                attr.format = VK_FORMAT_R32_SFLOAT;
                break;
            case TYPE_VECTOR2:
                attr.format = VK_FORMAT_R32G32_SFLOAT;
                break;
            case TYPE_VECTOR3:
                attr.format = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case TYPE_VECTOR4:
                attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            case TYPE_UBYTE4:
                attr.format = VK_FORMAT_R8G8B8A8_USCALED;  // Byte→float without normalization (5 → 5.0, not 0.0196)
                break;
            case TYPE_UBYTE4_NORM:
                attr.format = VK_FORMAT_R8G8B8A8_UNORM;
                break;
            default:
                attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            }

            // Set actual offset for this attribute
            attr.offset = element.offset_;

            attributeDescriptions.Push(attr);

            // Verbose logging disabled for performance
            // URHO3D_LOGDEBUG("Vertex element " + String(i) + ": semantic=" + String((int)element.semantic_) +
            //                ", location=" + String(attr.location) + ", offset=" + String(attr.offset) +
            //                ", format=" + String((int)attr.format));
        }

    }
    else
    {
    }

    // Add instance buffer attributes if present (binding 1, per-instance rate)
    if (instanceBuffer)
    {
        const Vector<VertexElement>& elements = instanceBuffer->GetElements();
        // Create binding description for instance buffer (binding 1, per-instance rate)
        VkVertexInputBindingDescription binding{};
        binding.binding = 1;
        binding.stride = instanceBuffer->GetVertexSize();
        binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        bindingDescriptions.Push(binding);

        // Add attribute descriptions for instance matrix (TEXCOORD4/5/6 → locations 10/11/12)
        // Shader preprocessor assigns locations sequentially, so instance attributes come after
        // all geometry attributes (iPos=0, iNormal=1, iColor=2, iTexCoord=3, iTexCoord1=4,
        // iTangent=5, iBlendWeights=6, iBlendIndices=7, iCubeTexCoord=8, iCubeTexCoord1=9)
        for (unsigned i = 0; i < elements.Size(); ++i)
        {
            const VertexElement& element = elements[i];

            VkVertexInputAttributeDescription attr{};
            attr.binding = 1;  // Instance buffer is binding 1

            // Map instance buffer elements to shader locations
            // Shader Transform.glsl declares: layout(location = 10/11/12) for iTexCoord4/5/6
            if (element.semantic_ == SEM_TEXCOORD)
            {
                attr.location = 10 + (element.index_ - 4);  // TEXCOORD4 → 10, TEXCOORD5 → 11, TEXCOORD6 → 12
            }
            else
            {
                attr.location = 10 + i;  // Fallback to sequential locations starting at 10
            }

            // Map VertexElementType to VkFormat (should be TYPE_VECTOR4 for instance matrix)
            switch (element.type_)
            {
            case TYPE_VECTOR4:
                attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            case TYPE_VECTOR3:
                attr.format = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case TYPE_VECTOR2:
                attr.format = VK_FORMAT_R32G32_SFLOAT;
                break;
            case TYPE_FLOAT:
                attr.format = VK_FORMAT_R32_SFLOAT;
                break;
            default:
                attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            }

            attr.offset = element.offset_;


            attributeDescriptions.Push(attr);
        }
    }

    // Fill dummy attributes for any shader locations not provided by vertex or instance buffers.
    // This MUST happen after both vertex and instance attributes are added to avoid duplicates.
    {
        unsigned providedLocations = 0;
        for (unsigned i = 0; i < attributeDescriptions.Size(); ++i)
            providedLocations |= (1u << attributeDescriptions[i].location);

        for (unsigned i = 0; i < numStandardLocations; ++i)
        {
            if (!(providedLocations & (1u << standardLocations[i].location)))
            {
                VkVertexInputAttributeDescription dummy{};
                dummy.binding = 0;
                dummy.location = standardLocations[i].location;
                dummy.format = standardLocations[i].format;
                dummy.offset = 0;
                attributeDescriptions.Push(dummy);
            }
        }
    }

    // Check for duplicate locations and log them
    {
        HashMap<unsigned, unsigned> locationCount;
        for (unsigned i = 0; i < attributeDescriptions.Size(); ++i)
            locationCount[attributeDescriptions[i].location]++;
        for (auto it = locationCount.Begin(); it != locationCount.End(); ++it)
        {
            if (it->second_ > 1)
            {
                URHO3D_LOGDEBUGF("DUPLICATE vertex location %d (%d times). Full attribute list:", it->first_, it->second_);
                for (unsigned i = 0; i < attributeDescriptions.Size(); ++i)
                    URHO3D_LOGINFOF("  attr[%d]: binding=%d location=%d format=%d offset=%d",
                        i, attributeDescriptions[i].binding, attributeDescriptions[i].location,
                        attributeDescriptions[i].format, attributeDescriptions[i].offset);
                break;
            }
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInputState{};
    vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputState.vertexBindingDescriptionCount = bindingDescriptions.Size();
    vertexInputState.pVertexBindingDescriptions = bindingDescriptions.Empty() ? nullptr : &bindingDescriptions[0];
    vertexInputState.vertexAttributeDescriptionCount = attributeDescriptions.Size();
    vertexInputState.pVertexAttributeDescriptions = attributeDescriptions.Empty() ? nullptr : &attributeDescriptions[0];


    VkPipelineMultisampleStateCreateInfo multisampleState{};
    multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampleState.sampleShadingEnable = false;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 1920.0f;    // Default, will be updated by viewport calls
    viewport.height = 1080.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {1920, 1080};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 3;
    dynamicState.pDynamicStates = dynamicStates;

    // Phase 33 Step 2: Prepare shader stages (vertex, geometry, and fragment shaders)
    // Create VkPipelineShaderStageCreateInfo array from compiled shader modules
    // Phase 36+: Added geometry shader support
    Vector<VkPipelineShaderStageCreateInfo> shaderStages;

    // DIAGNOSTIC: depthTest, depthWrite (disabled for performance)

    // Add vertex shader stage if provided
    if (vsModule != VK_NULL_HANDLE)
    {
        // DIAGNOSTIC: VS module (disabled for performance)
        VkPipelineShaderStageCreateInfo vsStage{};
        vsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vsStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vsStage.module = vsModule;
        vsStage.pName = "main";  // GLSL entry point
        shaderStages.Push(vsStage);
    }
    else
    {
#if VULKAN_PIPELINE_DEBUG_LOGGING
#endif
    }

    // Add geometry shader stage if provided (Phase 36+: Geometry Shader Support)
    if (gsModule != VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo gsStage{};
        gsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        gsStage.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        gsStage.module = gsModule;
        gsStage.pName = "main";  // GLSL entry point
        shaderStages.Push(gsStage);
    }

    // Add fragment shader stage if provided
    if (fsModule != VK_NULL_HANDLE)
    {
#if VULKAN_PIPELINE_DEBUG_LOGGING
#endif
        VkPipelineShaderStageCreateInfo fsStage{};
        fsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fsStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fsStage.module = fsModule;
        fsStage.pName = "main";  // GLSL entry point
        shaderStages.Push(fsStage);
    }
    else
    {
#if VULKAN_PIPELINE_DEBUG_LOGGING
#endif
    }

    // Build graphics pipeline create info
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.pRasterizationState = &rasterizationState;
    pipelineInfo.pColorBlendState = &colorBlendState;
    pipelineInfo.pDepthStencilState = &depthStencilState;
    pipelineInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineInfo.pVertexInputState = &vertexInputState;
    pipelineInfo.pMultisampleState = &multisampleState;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.stageCount = shaderStages.Size();        // Phase 33: Set from shader modules
    pipelineInfo.pStages = shaderStages.Size() > 0 ? &shaderStages[0] : nullptr;  // Phase 33: Bind stages

    // DIAGNOSTIC: stageCount, pStages (disabled for performance)

    // Use pipeline cache for creation (handles memory + disk caching)
    VkPipeline pipeline = pipelineCache_->GetOrCreatePipeline(stateHash, pipelineInfo);

    if (pipeline == VK_NULL_HANDLE)
    {
        URHO3D_LOGERROR("GetOrCreateGraphicsPipeline: Failed to create graphics pipeline");
        return VK_NULL_HANDLE;
    }

    return pipeline;
}

// Phase 33 Step 3: Get material descriptor set for GPU binding
VkDescriptorSet VulkanGraphicsImpl::GetMaterialDescriptor(Material* material)
{
    if (!material || !materialDescriptorManager_)
    {
        return VK_NULL_HANDLE;
    }

    // Get the descriptor set from the material descriptor manager
    // This includes all material parameters and textures
    VkDescriptorSet descriptorSet = materialDescriptorManager_->GetDescriptor(material);

    if (descriptorSet == VK_NULL_HANDLE)
    {
        URHO3D_LOGWARNING("GetMaterialDescriptor: Failed to get descriptor for material");
        return VK_NULL_HANDLE;
    }

    return descriptorSet;
}

// Phase 36B: Texture Descriptor Management
VkDescriptorSet VulkanGraphicsImpl::CreateTextureDescriptorSet()
{
    VkDescriptorPool pool = GetDescriptorPool();  // Get current frame's pool
    if (!device_ || !pool || gbufferTextureLayout_ == VK_NULL_HANDLE)
    {
        URHO3D_LOGERROR("CreateTextureDescriptorSet: Invalid state (device, pool, or layout is null)");
        return VK_NULL_HANDLE;
    }

    // Allocate descriptor set from pool using G-Buffer texture layout (Set 1)
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &gbufferTextureLayout_;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet);
    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("CreateTextureDescriptorSet: Failed to allocate descriptor set");
        return VK_NULL_HANDLE;
    }

    // Get currently bound textures from Graphics instance
    // Access graphics_->textures_[] to get texture bindings
    if (!graphics_)
    {
        URHO3D_LOGERROR("CreateTextureDescriptorSet: Graphics instance is null");
        return VK_NULL_HANDLE;
    }

    // Create descriptor writes for each bound texture
    Vector<VkDescriptorImageInfo> imageInfos;
    Vector<VkWriteDescriptorSet> descriptorWrites;

    // Phase 36B: Iterate through texture units and create descriptor writes
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        Texture* texture = graphics_->GetTexture(i);
        if (!texture)
            continue;

        // Get Vulkan image view and sampler from texture
        VkImageView imageView = reinterpret_cast<VkImageView>(texture->GetGPUObjectName());
        if (!imageView)
            continue;

        // Get sampler from sampler cache with per-axis address modes
        VkSampler sampler = samplerCache_->GetSampler(
            texture->GetFilterMode(),
            texture->GetAddressMode(COORD_U),
            texture->GetAddressMode(COORD_V),
            texture->GetAddressMode(COORD_W),
            1);
        if (!sampler)
            continue;

        // Create image info for this texture binding
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;
        imageInfos.Push(imageInfo);

        // Create write descriptor for this binding
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = i;  // Binding matches texture unit index
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos[imageInfos.Size() - 1];  // Point to last added image info
        descriptorWrites.Push(write);
    }

    // Update descriptor set with all texture bindings
    if (!descriptorWrites.Empty())
    {
        vkUpdateDescriptorSets(device_, descriptorWrites.Size(), descriptorWrites.Buffer(), 0, nullptr);
    }

    return descriptorSet;
}

void VulkanGraphicsImpl::BindTextureDescriptorSet(VkDescriptorSet descriptorSet)
{
    if (!descriptorSet || currentPipelineLayout_ == VK_NULL_HANDLE)
    {
        URHO3D_LOGWARNING("BindTextureDescriptorSet: Invalid descriptor set or pipeline layout");
        return;
    }

    VkCommandBuffer cmdBuffer = GetFrameCommandBuffer();
    if (!cmdBuffer)
    {
        URHO3D_LOGERROR("BindTextureDescriptorSet: No command buffer available");
        return;
    }

    // Bind texture descriptor set to Set 1 (G-Buffer textures for deferred lighting)
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        currentPipelineLayout_,
        1,  // Set 1 (Set 0 is materials, Set 1 is G-Buffer textures)
        1,  // Bind 1 descriptor set
        &descriptorSet,
        0,  // No dynamic offsets
        nullptr
    );

}

// Phase 36C: Constant Buffer Descriptor Management
VkDescriptorSet VulkanGraphicsImpl::CreateConstantBufferDescriptorSet(const void* data, uint32_t dataSize)
{
    VkDescriptorPool pool = GetDescriptorPool();  // Get current frame's pool
    if (!device_ || !pool || constantBufferLayout_ == VK_NULL_HANDLE)
    {
        URHO3D_LOGERROR("CreateConstantBufferDescriptorSet: Invalid state (device, pool, or layout is null)");
        return VK_NULL_HANDLE;
    }

    if (!data || dataSize == 0)
    {
        return VK_NULL_HANDLE;
    }

    // Allocate constant buffer from pool and upload data
    if (!constantBufferPool_)
    {
        URHO3D_LOGERROR("CreateConstantBufferDescriptorSet: Constant buffer pool is null");
        return VK_NULL_HANDLE;
    }

    VkBuffer constantBuffer = VK_NULL_HANDLE;
    VkDeviceSize bufferOffset = 0;
    if (!constantBufferPool_->AllocateBuffer(data, dataSize, constantBuffer, bufferOffset))
    {
        URHO3D_LOGERROR("CreateConstantBufferDescriptorSet: Failed to allocate constant buffer from pool");
        return VK_NULL_HANDLE;
    }

    // Allocate descriptor set from pool using constant buffer layout (Set 2)
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &constantBufferLayout_;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet);
    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("CreateConstantBufferDescriptorSet: Failed to allocate descriptor set");
        return VK_NULL_HANDLE;
    }

    // Create descriptor buffer info for constant buffer binding
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = constantBuffer;
    bufferInfo.offset = bufferOffset;
    bufferInfo.range = dataSize;

    // Create write descriptor for uniform buffer binding
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;  // Binding 0 in constant buffer layout
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    // Update descriptor set with constant buffer binding
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    return descriptorSet;
}

void VulkanGraphicsImpl::BindConstantBufferDescriptorSet(VkDescriptorSet descriptorSet)
{
    if (!descriptorSet || currentPipelineLayout_ == VK_NULL_HANDLE)
    {
        URHO3D_LOGWARNING("BindConstantBufferDescriptorSet: Invalid descriptor set or pipeline layout");
        return;
    }

    VkCommandBuffer cmdBuffer = GetFrameCommandBuffer();
    if (!cmdBuffer)
    {
        URHO3D_LOGERROR("BindConstantBufferDescriptorSet: No command buffer available");
        return;
    }

    // Bind constant buffer descriptor set to Set 2 (light/material parameters for shaders)
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        currentPipelineLayout_,
        2,  // Set 2 (Set 0 is materials, Set 1 is textures, Set 2 is constant buffers)
        1,  // Bind 1 descriptor set
        &descriptorSet,
        0,  // No dynamic offsets
        nullptr
    );

}

// Phase 36D: Input Attachment Descriptor Management
VkDescriptorSet VulkanGraphicsImpl::CreateInputAttachmentDescriptorSet()
{
    VkDescriptorPool pool = GetDescriptorPool();  // Get current frame's pool
    if (!device_ || !pool || inputAttachmentLayout_ == VK_NULL_HANDLE)
    {
        URHO3D_LOGERROR("CreateInputAttachmentDescriptorSet: Invalid state (device, pool, or layout is null)");
        return VK_NULL_HANDLE;
    }

    // Validate G-Buffer image views exist
    if (gBufferAlbedoView_ == VK_NULL_HANDLE || gBufferNormalView_ == VK_NULL_HANDLE ||
        depthImageView_ == VK_NULL_HANDLE || gBufferSpecularView_ == VK_NULL_HANDLE)
    {
        URHO3D_LOGWARNING("CreateInputAttachmentDescriptorSet: G-Buffer views not initialized");
        return VK_NULL_HANDLE;
    }

    // Allocate descriptor set from pool using input attachment layout (Set 3)
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &inputAttachmentLayout_;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet);
    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("CreateInputAttachmentDescriptorSet: Failed to allocate descriptor set");
        return VK_NULL_HANDLE;
    }

    // Create descriptor writes for G-Buffer input attachments
    VkDescriptorImageInfo imageInfos[4];

    // Binding 0: Albedo (diffuse color)
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = gBufferAlbedoView_;
    imageInfos[0].sampler = VK_NULL_HANDLE;  // Input attachments don't use samplers

    // Binding 1: Normal (world space normals)
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = gBufferNormalView_;
    imageInfos[1].sampler = VK_NULL_HANDLE;

    // Binding 2: Depth (scene depth for lighting calculations)
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    imageInfos[2].imageView = depthImageView_;
    imageInfos[2].sampler = VK_NULL_HANDLE;

    // Binding 3: Specular (specular properties)
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[3].imageView = gBufferSpecularView_;
    imageInfos[3].sampler = VK_NULL_HANDLE;

    // Create write descriptors for all 4 input attachments
    VkWriteDescriptorSet writes[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].pNext = nullptr;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &imageInfos[i];
        writes[i].pBufferInfo = nullptr;
        writes[i].pTexelBufferView = nullptr;
    }

    // Update descriptor set with all input attachment bindings
    vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);

    return descriptorSet;
}

void VulkanGraphicsImpl::BindInputAttachmentDescriptorSet(VkDescriptorSet descriptorSet)
{
    if (!descriptorSet || currentPipelineLayout_ == VK_NULL_HANDLE)
    {
        URHO3D_LOGWARNING("BindInputAttachmentDescriptorSet: Invalid descriptor set or pipeline layout");
        return;
    }

    VkCommandBuffer cmdBuffer = GetFrameCommandBuffer();
    if (!cmdBuffer)
    {
        URHO3D_LOGERROR("BindInputAttachmentDescriptorSet: No command buffer available");
        return;
    }

    // Bind input attachment descriptor set to Set 3 (tile-local G-Buffer for deferred lighting)
    vkCmdBindDescriptorSets(
        cmdBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        currentPipelineLayout_,
        3,  // Set 3 (Set 0: materials, Set 1: textures, Set 2: constants, Set 3: input attachments)
        1,  // Bind 1 descriptor set
        &descriptorSet,
        0,  // No dynamic offsets
        nullptr
    );

}

} // namespace Urho3D
