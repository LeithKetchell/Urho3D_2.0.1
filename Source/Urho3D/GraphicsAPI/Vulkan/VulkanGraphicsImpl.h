//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/HashMap.h"
#include "../../Container/Vector.h"
#include "../../Core/Object.h"
#include "../../Core/Mutex.h"
#include "../../Core/Thread.h"
#include "../GraphicsDefs.h"
#include "../VulkanDefs.h"
#include "VulkanSamplerCache.h"
#include "VulkanShaderCache.h"
#include "VulkanConstantBufferPool.h"
#include "VulkanMemoryPoolManager.h"

/// Result of swapchain image acquisition attempt
enum class AcquireResult { Success, OutOfDate, Timeout, Error };
#include "VulkanSecondaryCommandBuffer.h"
#include "VulkanInstanceBufferManager.h"
#include "VulkanIndirectDrawManager.h"
#include "VulkanStagingBufferManager.h"
#include "VulkanMaterialDescriptorManager.h"
#include "VulkanPipelineCache.h"
#include "VulkanPipelineState.h"
#include "VulkanComputePipeline.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

// Forward declare SDL_Window in global namespace
struct SDL_Window;

// Descriptor pool sizing - increased for per-draw descriptor sets
// Since we create a new descriptor set for EVERY draw call, we need a much larger pool
// 50000 = ~1000 draw calls per frame * 3 frames in flight * safety margin
#ifndef VULKAN_DESCRIPTOR_POOL_SIZE
#define VULKAN_DESCRIPTOR_POOL_SIZE 50000  // Support 50000 descriptor sets
#endif

namespace Urho3D
{

class ConstantBuffer;
class Graphics;
class RenderSurface;
class ShaderProgram;
class Texture2D;

/// Maximum number of GPU timestamp queries per frame.
/// Each BeginEvent/EndEvent pair uses 2 queries. 32 queries = 16 named GPU spans per frame.
static const uint32_t MAX_GPU_TIMESTAMP_QUERIES = 32;

/// GPU timestamp label — name for each timestamp written per frame
struct GpuTimestampLabel
{
    String name;        ///< Human-readable label (e.g. "RenderPass Begin", "Frame End")
    String category;    ///< Category for color coding in timeline UI
};

/// Per-frame synchronization resources
/// Groups all resources needed for one frame in flight
struct FrameResources
{
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkSemaphore imageAcquired{VK_NULL_HANDLE};  // Per-frame: we don't know image index yet
    uint32_t imageIndex{0};  // Which swapchain image this frame acquired

    // GPU timestamp queries (Phase 2 of Profiler Timeline)
    VkQueryPool timestampQueryPool{VK_NULL_HANDLE};
    uint32_t nextTimestampQuery{0};  // Next available query slot
    GpuTimestampLabel timestampLabels[MAX_GPU_TIMESTAMP_QUERIES];  // Names for readback
};

/// Per-swapchain-image semaphores
/// Fixes validation error: each swapchain image needs its own renderComplete semaphore
struct ImageSemaphores
{
    VkSemaphore renderComplete{VK_NULL_HANDLE};
};

/// \brief Descriptor for render pass configuration
/// \details Allows caching multiple render pass configurations for future multi-pass support.
/// Contains all the information needed to create or retrieve a cached Vulkan render pass,
/// including attachment formats, sample counts, and subpass information.
/// Supports future MSAA and deferred rendering via configurable sample counts and subpass counts.
/// Phase 35: Extended descriptor for multi-pass deferred rendering support
struct RenderPassDescriptor
{
    /// Maximum color attachments (supports up to 8 for complex rendering passes)
    static constexpr uint32_t MAX_COLOR_ATTACHMENTS = 8;

    /// Maximum input attachments (for lighting pass reading G-Buffer)
    static constexpr uint32_t MAX_INPUT_ATTACHMENTS = 8;

    /// Number of color attachments (1 for forward, 4+ for deferred G-Buffer, default: 1)
    uint32_t colorAttachmentCount{1};

    /// Color attachment formats array (up to 8 attachments)
    /// Default: [0] = swapchain format, [1-7] = unused
    VkFormat colorFormats[MAX_COLOR_ATTACHMENTS]{
        VK_FORMAT_B8G8R8A8_SRGB,  // [0] Swapchain/final color
        VK_FORMAT_UNDEFINED,        // [1-7] Unused by default
        VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED,
        VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED
    };

    /// Depth attachment format (default: VK_FORMAT_D32_SFLOAT for 32-bit depth)
    VkFormat depthFormat{VK_FORMAT_D32_SFLOAT};

    /// Number of subpasses (1 for forward rendering, 2+ for deferred rendering, default: 1)
    /// Phase 35: Multiple subpasses for G-Buffer geometry pass + lighting pass
    uint32_t subpassCount{1};

    /// Number of input attachments for lighting/composition passes
    /// Phase 35: Used by lighting pass to read G-Buffer attachments
    uint32_t inputAttachmentCount{0};

    /// Input attachment indices (references to color attachments as input attachments)
    /// Phase 35: For lighting pass to read G-Buffer data
    uint32_t inputAttachmentIndices[MAX_INPUT_ATTACHMENTS]{};

    /// Sample count for MSAA support (default: VK_SAMPLE_COUNT_1_BIT, extensible for Issue #14A)
    VkSampleCountFlagBits sampleCount{VK_SAMPLE_COUNT_1_BIT};

    /// Whether this render pass targets a texture (SHADER_READ_ONLY finalLayout) vs swapchain (PRESENT_SRC)
    bool isRenderToTexture{false};

    /// Swapchain color + custom depth RTT (ForwardHWDepth): color uses PRESENT_SRC,
    /// depth uses READ_ONLY for later sampling. Both isRenderToTexture and isSwapchainHybrid
    /// can be true — isSwapchainHybrid overrides color finalLayout to PRESENT_SRC.
    bool isSwapchainHybrid{false};

    /// Load operation for color attachments (CLEAR for first use, LOAD to preserve content)
    VkAttachmentLoadOp colorLoadOp{VK_ATTACHMENT_LOAD_OP_CLEAR};

    /// \brief Calculate hash for caching
    /// \returns Hash value computed from all descriptor fields using DJB2 algorithm
    /// Phase 35: Updated to include all new multi-pass fields
    uint32_t Hash() const
    {
        // Hash combining all descriptor fields
        uint32_t h = 5381;
        h = ((h << 5) + h) + colorAttachmentCount;

        // Hash all color format entries
        for (uint32_t i = 0; i < colorAttachmentCount; ++i)
            h = ((h << 5) + h) + colorFormats[i];

        h = ((h << 5) + h) + depthFormat;
        h = ((h << 5) + h) + subpassCount;
        h = ((h << 5) + h) + inputAttachmentCount;

        // Hash input attachment indices
        for (uint32_t i = 0; i < inputAttachmentCount; ++i)
            h = ((h << 5) + h) + inputAttachmentIndices[i];

        h = ((h << 5) + h) + sampleCount;
        h = ((h << 5) + h) + (isRenderToTexture ? 1u : 0u);
        h = ((h << 5) + h) + (isSwapchainHybrid ? 1u : 0u);
        h = ((h << 5) + h) + colorLoadOp;
        return h;
    }

    /// \brief Equality comparison for caching
    /// \param other The descriptor to compare with
    /// \returns True if all fields match, false otherwise
    /// Phase 35: Updated to compare all multi-pass fields
    bool operator==(const RenderPassDescriptor& other) const
    {
        // Compare basic counts
        if (colorAttachmentCount != other.colorAttachmentCount ||
            depthFormat != other.depthFormat ||
            subpassCount != other.subpassCount ||
            inputAttachmentCount != other.inputAttachmentCount ||
            sampleCount != other.sampleCount ||
            isRenderToTexture != other.isRenderToTexture ||
            isSwapchainHybrid != other.isSwapchainHybrid ||
            colorLoadOp != other.colorLoadOp)
            return false;

        // Compare all color format entries
        for (uint32_t i = 0; i < colorAttachmentCount; ++i)
            if (colorFormats[i] != other.colorFormats[i])
                return false;

        // Compare all input attachment indices
        for (uint32_t i = 0; i < inputAttachmentCount; ++i)
            if (inputAttachmentIndices[i] != other.inputAttachmentIndices[i])
                return false;

        return true;
    }
};

/// \brief Vulkan graphics implementation
/// \details Core Vulkan backend implementing the graphics API abstraction.
/// Manages Vulkan instance, physical/logical devices, swapchain, command buffers,
/// synchronization primitives, and resource caches for efficient rendering.
/// Supports frame pipelining (triple-buffering) for optimal GPU/CPU synchronization.
///
/// **Architecture Overview:**
/// VulkanGraphicsImpl is the heart of Urho3D's Vulkan rendering backend. It encapsulates
/// all low-level Vulkan operations and presents a high-level interface to the Graphics class
/// via the dispatch pattern (Graphics::Method_Vulkan() -> VulkanGraphicsImpl::Method()).
///
/// **Key Responsibilities:**
/// - **Initialization**: Creates Vulkan instance, selects GPU, creates logical device and swapchain
/// - **Frame Management**: Acquires swapchain images, records command buffers, submits work, presents frames
/// - **Memory Management**: Uses Vulkan Memory Allocator (VMA) for GPU memory allocation
/// - **Resource Caching**: Maintains caches for samplers, shaders, pipelines, and descriptor sets
/// - **Synchronization**: Implements triple-buffering with fences and semaphores for frame pipelining
/// - **Render Targets**: Supports both swapchain rendering and render-to-texture via framebuffer management
///
/// **Frame Pipelining (Triple-Buffering):**
/// Frame 0: GPU executes while CPU prepares Frame 1 and Frame 2
/// Frame 1: GPU executes while CPU prepares Frame 2 and Frame 0
/// Frame 2: GPU executes while CPU prepares Frame 0 and Frame 1
/// This prevents CPU/GPU stalls and maximizes throughput. Uses frameIndex_ (0-2) to cycle.
///
/// **Resource Caching Strategy:**
/// - VulkanSamplerCache: Caches VkSampler objects by (filter, addressMode) pair
/// - VulkanShaderCache: Caches compiled shader modules by shader source + defines
/// - VulkanPipelineCache: Two-tier caching (memory + disk) of graphics pipelines
/// - VulkanConstantBufferPool: Allocates uniform buffers from pre-allocated pools (Phase 9)
/// - VulkanMemoryPoolManager: Manages VMA pools for different allocation patterns (Phase 8)
/// - VulkanMaterialDescriptorManager: Manages descriptor sets for material parameters (Phase 27)
///
/// **Memory Allocation:**
/// All GPU allocations go through VMA (Vulkan Memory Allocator):
/// - Buffers (vertex, index, uniform) allocated via VmaAllocator
/// - Textures created with VkImage and allocated via VMA
/// - Memory type selection optimized via FindMemoryType()
/// - Supports both GPU-local and host-visible memory for staging
///
/// **Synchronization Primitives:**
/// - frameFences_: Per-frame fences ensuring CPU doesn't overwrite frame N while GPU reads it
/// - imageAcquiredSemaphores_: Signal when swapchain image is ready for rendering
/// - renderCompleteSemaphores_: Signal when render pass completes (before presentation)
/// Frame flow: AcquireNextImage() -> WaitForFrameFence() -> RecordCommands() -> Present()
///
/// **Extensibility:**
/// - renderPassCache_: HashMap supporting future multi-pass rendering (e.g., deferred rendering)
/// - RenderPassDescriptor: Extensible descriptor for render pass configurations
/// - CurrentPipelineLayout: Tracks active layout for descriptor set binding (Phase 27)
class VulkanGraphicsImpl : public RefCounted
{
public:
    /// \brief Constructor
    /// Initializes member variables to null/default states
    VulkanGraphicsImpl();

    /// \brief Destructor
    /// Automatically calls Shutdown() to release Vulkan resources
    virtual ~VulkanGraphicsImpl();

    /// \brief Initialize Vulkan instance, device, swapchain
    /// \param graphics Pointer to Graphics object for state management
    /// \param window SDL_Window for surface creation
    /// \param width Desired swapchain width in pixels
    /// \param height Desired swapchain height in pixels
    /// \returns True if initialization successful, false on error
    /// \details Performs full Vulkan initialization in sequence:
    ///   1. Create Vulkan instance with validation layers
    ///   2. Select physical device (prefers discrete GPUs)
    ///   3. Find queue families and create logical device
    ///   4. Create window surface via SDL2
    ///   5. Create swapchain with intelligent format/mode selection
    ///   6. Create depth buffer and framebuffers
    ///   7. Initialize command buffers and synchronization primitives
    ///   8. Set up memory allocator (VMA) and caches
    bool Initialize(Graphics* graphics, SDL_Window* window, int width, int height);

    /// Enable or disable Vulkan validation layers. Must be called before Initialize().
    void SetEnableValidation(bool enable) { enableValidation_ = enable; }
    bool GetEnableValidation() const { return enableValidation_; }

    /// \brief Shutdown Vulkan resources
    /// \details Safely releases all Vulkan objects in reverse creation order.
    /// Can be called multiple times safely.
    void Shutdown();

    /// \brief Recreate swapchain-dependent resources for window/resolution change
    /// \details Destroys and recreates surface, swapchain, depth buffer, render passes,
    /// framebuffers, G-buffer, and synchronization primitives. Device, allocator,
    /// textures, buffers, shaders, and caches all survive.
    bool RecreateSwapchainResources(SDL_Window* window, int width, int height);

    /// \brief Acquire next swapchain image for rendering
    /// \returns AcquireResult indicating success, out-of-date, timeout, or error
    /// \details Waits for image availability and updates currentImageIndex_.
    /// Returns OutOfDate when swapchain needs recreation (DPMS, resize, etc.)
    AcquireResult AcquireNextImage();

    /// \brief Submit command buffer and present swapchain image
    /// \details Submits recorded command buffer to graphics queue and presents
    /// the swapchain image to the display. Handles synchronization with fences and semaphores.
    void Present();

    /// \brief Get current frame index (for frame pipelining)
    /// \returns Frame index (0-2 for triple buffering)
    uint32_t GetFrameIndex() const { return frameIndex_; }

    /// \brief Get current swapchain image index
    /// \returns Index of the swapchain image currently being rendered to
    uint32_t GetCurrentImageIndex() const { return currentImageIndex_; }

    /// \brief Get current frame in flight index (0 to MAX_FRAMES_IN_FLIGHT-1)
    /// \returns Index of the current frame being rendered (for per-frame resources)
    uint32_t GetCurrentFrame() const { return currentFrame_; }

    /// \brief Get frame command buffer for recording commands
    /// \returns VkCommandBuffer for the current frame (triple-buffered)
    VkCommandBuffer GetFrameCommandBuffer() const;

    /// \brief Wait for frame fence (GPU-CPU synchronization)
    /// \details Blocks CPU until the current frame's rendering is complete on GPU.
    /// This prevents overwriting a buffer that the GPU is still reading from.
    void WaitForFrameFence();

    /// \brief Reset frame command buffer for new frame
    /// \details Clears the command buffer and prepares it for new command recording.
    void ResetFrameCommandBuffer();

    /// \brief Begin render pass
    /// \details Starts a new render pass on the current frame's command buffer.
    /// Must be called before any draw commands.
    void BeginRenderPass();

    /// \brief End render pass
    /// \details Finishes the current render pass. Must be called after all draw commands.
    void EndRenderPass();

    /// \brief Check if render pass is currently active
    /// \returns true if a render pass is active, false otherwise
    bool IsRenderPassActive() const { return renderPassActive_; }

    /// \returns true if render targets have changed since last framebuffer rebuild
    bool IsRenderTargetsDirty() const { return renderTargetsDirty_; }

    /// \brief Ensure render pass is started (lazy initialization)
    /// \details Starts render pass if not already active. This allows instance buffers
    /// to be filled BEFORE the render pass begins, fixing synchronization issues
    /// where buffer barriers cannot be used inside render passes.
    void EnsureRenderPassStarted();

    /// \brief Transition to next subpass
    /// \details Phase 36: Transitions from geometry pass (subpass 0) to lighting pass (subpass 1) in deferred rendering.
    /// This must be called after all geometry rendering is complete and before lighting pass rendering begins.
    void NextSubpass();

    /// \name GPU Timestamp Queries (Phase 2 of Profiler Timeline)
    /// @{

    /// \brief Check if GPU timestamp queries are supported
    bool IsGpuTimestampSupported() const { return gpuTimestampSupported_; }

    /// \brief Write a GPU timestamp into the current frame's query pool
    /// \param label Name for this timestamp point (paired timestamps form spans in the timeline)
    /// \param category Category for color coding (e.g. "render", "shadow", "post")
    /// Returns the query index used, or ~0u if unavailable.
    uint32_t WriteGpuTimestamp(const String& label = String::EMPTY, const String& category = String::EMPTY);

    /// \brief Read back completed GPU timestamp results from a frame's query pool
    /// \param frameIndex Which frame-in-flight slot to read (typically the one just waited on)
    /// \param results Output: timestamps in nanoseconds, one per query written
    /// \param count Output: number of valid results
    /// \returns true if results were successfully read
    bool ReadGpuTimestampResults(uint32_t frameIndex, Vector<uint64_t>& results, uint32_t& count);

    /// \brief Get GPU timestamp period (nanoseconds per tick)
    float GetTimestampPeriodNs() const { return timestampPeriodNs_; }

    /// @}

    /// \brief Begin a one-time submit command buffer for uploads
    /// \returns VkCommandBuffer in recording state, ready for commands
    /// \details Creates and begins a transient command buffer for resource uploads (textures, buffers).
    /// Must be paired with EndUploadCommandBuffer() to submit and cleanup.
    VkCommandBuffer BeginUploadCommandBuffer();

    /// \brief End and submit upload command buffer
    /// \param commandBuffer The command buffer returned from BeginUploadCommandBuffer()
    /// \details Ends recording, submits to graphics queue, waits for completion, and frees the command buffer.
    void EndUploadCommandBuffer(VkCommandBuffer commandBuffer);

    /// \name Vulkan Object Accessors
    /// \brief Access to core Vulkan objects for direct usage
    /// @{

    /// \brief Get Vulkan instance
    /// \returns VkInstance handle
    VkInstance GetInstance() const { return instance_; }

    /// \brief Get physical device
    /// \returns VkPhysicalDevice handle (selected GPU)
    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice_; }

    /// \brief Get logical device
    /// \returns VkDevice handle for command recording and resource allocation
    VkDevice GetDevice() const { return device_; }

    /// \brief Get graphics queue
    /// \returns VkQueue for graphics operations
    VkQueue GetGraphicsQueue() const { return graphicsQueue_; }

    /// Queue for GPU-training compute submits. The dedicated async-compute queue when the device
    /// exposes one (keeps training off the render/present path); otherwise the graphics queue.
    VkQueue GetComputeQueue() const { return computeQueue_ ? computeQueue_ : graphicsQueue_; }
    /// Queue family the compute command pool + command buffers must be created against to match
    /// GetComputeQueue() (Vulkan requires a command buffer's pool family == its submit queue family).
    uint32_t GetComputeQueueFamily() const
    { return computeQueueFamily_ != VK_QUEUE_FAMILY_IGNORED ? computeQueueFamily_ : graphicsQueueFamily_; }
    /// True when GetComputeQueue() is a separate family from graphics (real async compute).
    bool ComputeQueueIsDedicated() const { return computeQueueIsDedicated_; }
    /// Submit mutex for GetComputeQueue(): its own mutex when dedicated (concurrent with graphics),
    /// else the shared graphics mutex (same queue → must serialise with render/upload).
    Mutex& GetComputeQueueSubmitMutex()
    { return computeQueueIsDedicated_ ? computeQueueSubmitMutex_ : queueSubmitMutex_; }

    /// Queue PAIRED to GetActiveComputePool(): a command buffer must be submitted to a queue of its
    /// pool's family. The dedicated compute pool (batch override / worker pool) is compute-family, so
    /// it goes to computeQueue_; the shared commandPool_ is graphics-family, so it stays on
    /// graphicsQueue_. This keeps the one-shot dispatch path valid whether or not a compute pool is
    /// registered (== commandPool_ is the tell that we're on the shared graphics-family pool).
    VkQueue GetActiveComputeQueue() const
    { return (GetActiveComputePool() != commandPool_ && computeQueue_) ? computeQueue_ : graphicsQueue_; }
    /// Submit mutex paired to GetActiveComputeQueue() (own mutex only for the dedicated compute queue).
    Mutex& GetActiveComputeQueueSubmitMutex()
    { return (GetActiveComputePool() != commandPool_ && computeQueueIsDedicated_) ? computeQueueSubmitMutex_ : queueSubmitMutex_; }

    /// Get number of frames in flight
    uint32_t GetMaxFramesInFlight() const { return frames_.Size(); }

    /// Get fence for a specific in-flight frame (may be VK_NULL_HANDLE)
    VkFence GetFrameFence(unsigned index) const { return index < frames_.Size() ? frames_[index].fence : VK_NULL_HANDLE; }

    /// Get command pool for allocating command buffers.
    VkCommandPool GetCommandPool() const { return commandPool_; }

    /// Queue-submit mutex — serialises ALL graphics-queue submits (render + upload + the
    /// off-render compute trainer) across threads. vkQueueSubmit is not thread-safe.
    Mutex& GetQueueSubmitMutex() { return queueSubmitMutex_; }

    /// True once a compute-batch fence wait has timed out (a hung dispatch). The off-render GPU
    /// train worker polls this and exits its loop so it stays joinable. ClearComputeHang() resets
    /// it when (re)starting GPU training.
    bool ComputeHangDetected() const { return computeHang_; }
    void ClearComputeHang() { computeHang_ = false; }

    /// Scale the EndComputeBatch fence-wait bound. A GPU minibatch "page" of P sequences submits
    /// P× the dispatches of one sequence, so its single batch runs ~P× longer — without scaling the
    /// 10s bound a legitimate large page would false-positive as a hang. Clamped to >=1; the page
    /// worker sets it to P before a page and back to 1 for single steps.
    void SetComputeBatchTimeoutScale(unsigned scale) { computeBatchTimeoutScale_ = scale ? scale : 1u; }
    unsigned GetComputeBatchTimeoutScale() const { return computeBatchTimeoutScale_; }

    /// Register/clear a worker-thread-owned compute command pool + descriptor pool (STEP 4:
    /// off-render GPU training). Pass VK_NULL_HANDLE to clear. While set, compute command
    /// buffers AND descriptor sets allocated FROM that worker thread use these worker-owned
    /// pools; every other thread keeps the shared pools. Both are externally-synchronised in
    /// Vulkan, so they must never be shared across threads.
    void SetComputeWorkerPool(VkCommandPool cmdPool, VkDescriptorPool descPool, ThreadID tid)
    { computeWorkerPool_ = cmdPool; computeWorkerDescPool_ = descPool; computeWorkerThreadID_ = tid; }

    /// A1 (CPU/GPU training "superhighway" prereq): register an EXPLICIT compute-batch pool override
    /// that is honoured REGARDLESS of the calling thread. This decouples batch pool routing from thread
    /// identity. Once GPU training is pumped from the main tick (main tid == render tid), the tid-based
    /// SetComputeWorkerPool routing can no longer separate the trainer's pools from the shared frame
    /// descriptor pool that BeginFrame resets — so the owner registers its dedicated pools here instead.
    /// Pass VK_NULL_HANDLE to clear (restores tid routing). The render/frame path never sets this, so
    /// frame rendering stays on the shared pool. Takes precedence over SetComputeWorkerPool when set.
    void SetComputeBatchPools(VkCommandPool cmdPool, VkDescriptorPool descPool)
    { computeBatchOverridePool_ = cmdPool; computeBatchOverrideDescPool_ = descPool; }

    /// Pool for compute command buffers. The explicit batch override (SetComputeBatchPools) wins when
    /// set (tid-independent); otherwise returns the worker's own pool ONLY on the registered worker
    /// thread (Vulkan command pools are externally synchronised — never share one across threads), else
    /// the shared pool. Begin/EndComputeBatch run on one thread, so a Begin/End pair always resolves to
    /// the same pool (alloc and free match).
    VkCommandPool GetActiveComputePool() const
    {
        if (computeBatchOverridePool_ != VK_NULL_HANDLE)
            return computeBatchOverridePool_;
        return (computeWorkerPool_ != VK_NULL_HANDLE && Thread::GetCurrentThreadID() == computeWorkerThreadID_)
            ? computeWorkerPool_ : commandPool_;
    }

    /// Descriptor pool for compute descriptor sets. The explicit batch override (SetComputeBatchPools)
    /// wins when set (tid-independent). Otherwise: the shared per-frame pool is RESET every frame by the
    /// render thread (BeginFrame) — fatal for an off-render worker whose sets would be destroyed
    /// mid-flight — so the worker gets its OWN pool (reset by the worker each step), returned ONLY on the
    /// registered worker thread, else the shared frame pool.
    VkDescriptorPool GetActiveDescriptorPool() const
    {
        if (computeBatchOverrideDescPool_ != VK_NULL_HANDLE)
            return computeBatchOverrideDescPool_;
        return (computeWorkerDescPool_ != VK_NULL_HANDLE && Thread::GetCurrentThreadID() == computeWorkerThreadID_)
            ? computeWorkerDescPool_ : GetDescriptorPool();
    }

    /// Create a command pool on the graphics queue family for a worker thread's compute command
    /// buffers (RESET flag, matching the shared/upload pools). Caller owns it — pair with
    /// DestroyCommandPool. Returns VK_NULL_HANDLE on failure.
    VkCommandPool CreateThreadComputePool();
    /// Destroy a pool returned by CreateThreadComputePool.
    void DestroyCommandPool(VkCommandPool pool);
    /// Create a STORAGE_BUFFER descriptor pool for a worker thread's compute dispatches
    /// (reset-not-free). Caller owns it — pair with DestroyDescriptorPool. maxSets/descriptorCount
    /// default to one resident step; the GPU minibatch "page" (P seqs per submit) sizes them up to
    /// P*setsPerSeq so every page's descriptor sets coexist until the single submit completes.
    VkDescriptorPool CreateThreadComputeDescriptorPool(uint32_t maxSets = 256, uint32_t descriptorCount = 1024);
    /// Reset a worker descriptor pool (frees all its sets at once) — call at a step boundary.
    void ResetDescriptorPool(VkDescriptorPool pool);
    /// Destroy a pool returned by CreateThreadComputeDescriptorPool.
    void DestroyDescriptorPool(VkDescriptorPool pool);

    /// Return whether the frame command buffer is active (between BeginFrame and EndFrame).
    bool IsFrameActive() const { return frameActive_; }

    /// Set frame active state.
    void SetFrameActive(bool active) { frameActive_ = active; }

    /// \brief Get present queue
    /// \returns VkQueue for display presentation
    VkQueue GetPresentQueue() const { return presentQueue_; }

    /// \brief Get swapchain
    /// \returns VkSwapchainKHR handle
    VkSwapchainKHR GetSwapchain() const { return swapchain_; }

    /// \brief Get swapchain image format
    /// \returns VkFormat (e.g., VK_FORMAT_B8G8R8A8_SRGB)
    VkFormat GetSwapchainFormat() const { return swapchainFormat_; }

    /// \brief Get swapchain images vector
    const Vector<VkImage>& GetSwapchainImages() const { return swapchainImages_; }

    /// \brief Get swapchain extent (resolution)
    /// \returns VkExtent2D with width and height in pixels
    VkExtent2D GetSwapchainExtent() const { return swapchainExtent_; }

    /// \brief Get current render pass
    /// \returns VkRenderPass handle (swapchain or G-Buffer based on active render targets)
    VkRenderPass GetRenderPass() const;

    /// \brief Get current framebuffer
    /// \returns VkFramebuffer for swapchain image
    VkFramebuffer GetCurrentFramebuffer() const;

    /// \brief Get window surface
    /// \returns VkSurfaceKHR handle
    VkSurfaceKHR GetSurface() const { return surface_; }

    /// \brief Get Vulkan Memory Allocator
    /// \returns VmaAllocator for GPU memory management
    VmaAllocator GetAllocator() const { return allocator_; }

    /// \brief Get descriptor pool for current frame (SYNC FIX)
    /// \returns VkDescriptorPool for descriptor set allocation
    /// \details Returns the descriptor pool for the current FRAME (not swapchain image).
    /// CRITICAL: Must use currentFrame_ to match constant buffer pool synchronization.
    /// Fences are per-frame, so we wait on fence N before reusing frame N's resources.
    /// Using imageIndex would cause mismatch with constant buffer regions, causing flickering.
    VkDescriptorPool GetDescriptorPool() const {
        return currentFrame_ < descriptorPools_.Size() ? descriptorPools_[currentFrame_] : VK_NULL_HANDLE;
    }
    /// @}

    /// \brief Queue a VkBuffer for deferred deletion (destroyed after frame fence signals)
    void DeferBufferDeletion(VkBuffer buffer, VmaAllocation allocation);

    /// \brief Process deferred deletions for current frame (call after fence wait)
    void ProcessDeferredDeletions();

    /// \brief Route subsequent DeferBufferDeletion() calls to the compute-owned bucket (true) or the
    /// per-frame render bucket (false).
    /// \details Open this scope around a compute-only pump (GPU training) so buffers released while no
    /// render frame is being acquired land in a bucket drained by the COMPUTE fence, not the starved
    /// render frame fence. Only open it over a window where every released buffer is compute-owned —
    /// render-owned buffers must keep going to the per-frame queue so they are not freed before their
    /// frame fence signals.
    void SetComputeDeletionScope(bool enable);

    /// \brief Destroy every buffer in the compute-owned deferred-deletion bucket.
    /// \details Call ONLY once the compute fence for the work that used these buffers has signalled
    /// (e.g. at a page/batch boundary after EndComputeBatch()/PollComputeBatch() reports done). This is
    /// the drain the render path's ProcessDeferredDeletions() cannot provide during a compute-only pump.
    /// Thread-safe; a no-op when the bucket is empty.
    void FlushComputeDeferredDeletions();

    /// \brief Get physical device properties for limits validation
    /// \returns VkPhysicalDeviceProperties including device limits and capabilities
    VkPhysicalDeviceProperties GetDeviceProperties() const { return deviceProperties_; }

    /// \brief Total VRAM across the physical device's DEVICE_LOCAL memory heaps (bytes).
    /// Read-only capability query (sums VK_MEMORY_HEAP_DEVICE_LOCAL_BIT heaps). Used by the GPU
    /// minibatch page sizing as a VRAM-headroom sanity check; 0 if no device. Not a live budget —
    /// for that use VMA's heap budgets.
    VkDeviceSize GetDeviceLocalHeapSize() const;

    /// \brief Get sampler cache
    /// \returns VulkanSamplerCache* for texture sampler management (Quick Win #4)
    VulkanSamplerCache* GetSamplerCache() const { return samplerCache_.Get(); }

    /// \brief Get shader cache
    /// \returns VulkanShaderCache* for compiled shader caching (Quick Win #5)
    VulkanShaderCache* GetShaderCache() const { return shaderCache_.Get(); }

    /// \brief Get pipeline cache
    /// \returns VulkanPipelineCache* for compiled pipeline disk persistence (Phase B Quick Win #10)
    VulkanPipelineCache* GetPipelineCache() const { return pipelineCache_.Get(); }

    /// \brief Get compute pipeline manager
    /// \returns VulkanComputePipeline* for compute shader pipeline management (Phase 36+)
    VulkanComputePipeline* GetComputePipeline() const { return computePipeline_; }

    /// Get compute descriptor set layout (4 SSBOs)
    VkDescriptorSetLayout GetComputeDescriptorLayout() const { return computeDescriptorLayout_; }

    /// Get compute pipeline layout
    VkPipelineLayout GetComputePipelineLayout() const { return computePipelineLayout_; }

    /// \brief Get constant buffer pool
    /// \returns VulkanConstantBufferPool* for efficient uniform buffer management (Quick Win #6)
    VulkanConstantBufferPool* GetConstantBufferPool() const { return constantBufferPool_.Get(); }

    /// \brief Get memory pool manager for optimized buffer allocations
    /// \returns VulkanMemoryPoolManager* for GPU buffer pooling (Quick Win #8)
    VulkanMemoryPoolManager* GetMemoryPoolManager() const { return memoryPoolManager_.Get(); }

    /// \brief Get instance buffer manager for GPU vertex stream instancing
    /// \returns VulkanInstanceBufferManager* for per-instance data streaming (Phase 12)
    VulkanInstanceBufferManager* GetInstanceBufferManager() const { return instanceBufferManager_.Get(); }

    /// \brief Get indirect draw command buffer manager
    /// \returns VulkanIndirectDrawManager* for GPU-driven rendering (Phase 12)
    VulkanIndirectDrawManager* GetIndirectDrawManager() const { return indirectDrawManager_.Get(); }

    /// \brief Get staging buffer manager for GPU uploads
    /// \returns VulkanStagingBufferManager* for asynchronous transfers (Phase 10)
    VulkanStagingBufferManager* GetStagingBufferManager() const { return stagingBufferManager_.Get(); }

    /// \brief Get the actual MSAA sample count selected for current device
    /// \returns VkSampleCountFlagBits clamped to device capabilities (1x, 2x, 4x, 8x, or 16x)
    VkSampleCountFlagBits GetActualSampleCount() const { return actualSampleCount_; }

    /// \brief Get bitmask of MSAA sample counts supported by physical device
    /// \returns uint32_t bitmask of supported VkSampleCountFlagBits values
    uint32_t GetSupportedSampleCountsMask() const { return supportedSampleCountsMask_; }

    /// \brief Set user-requested MSAA sample count
    /// \param sampleCount Requested sample count (1, 2, 4, 8, 16)
    /// \details Actual sample count will be clamped to device capabilities.
    /// Call before frame rendering to apply MSAA level.
    void SetRequestedSampleCount(uint32_t sampleCount) { requestedSampleCount_ = SelectBestSampleCount(sampleCount); }

    /// \brief Get or create sampler for given filter and address modes
    /// \param filter VkFilter mode (NEAREST, LINEAR)
    /// \param addressMode VkSamplerAddressMode (CLAMP, REPEAT, MIRROR)
    /// \returns Cached or newly created VkSampler
    VkSampler GetSampler(VkFilter filter, VkSamplerAddressMode addressMode);

    /// \brief Get or create graphics pipeline for given state
    /// \param createInfo VkGraphicsPipelineCreateInfo with complete pipeline configuration
    /// \param stateHash Hash of the pipeline state for caching
    /// \returns Cached or newly created VkPipeline
    VkPipeline GetGraphicsPipeline(const VkGraphicsPipelineCreateInfo& createInfo, uint64_t stateHash);

    /// \brief Get secondary command buffer pool for parallel batch recording
    /// \returns VulkanSecondaryCommandBufferPool* for multi-threaded rendering
    VulkanSecondaryCommandBufferPool* GetSecondaryCommandBufferPool() { return secondaryCommandBufferPool_.Get(); }

    /// \brief Phase 12 (Quick Win #9): Report all pool statistics for profiling
    /// \details Gathers statistics from all memory pools (constant buffers, descriptors, etc.)
    /// Enables profiling of pool utilization and optimization effectiveness
    void ReportPoolStatistics() const;

    /// \brief Get default white diffuse texture (1x1)
    /// \returns Pointer to default diffuse texture, or nullptr if not initialized
    /// Phase 22A: Default Texture Creation
    Texture2D* GetDefaultDiffuseTexture() const { return defaultDiffuseTexture_.Get(); }

    /// \brief Get default neutral normal map texture (1x1)
    /// \returns Pointer to default normal map (0.5, 0.5, 1.0), or nullptr if not initialized
    /// Phase 22A: Default Texture Creation
    Texture2D* GetDefaultNormalTexture() const { return defaultNormalTexture_.Get(); }

    /// \brief Get default white specular texture (1x1)
    /// \returns Pointer to default specular map, or nullptr if not initialized
    /// Phase 22A: Default Texture Creation
    Texture2D* GetDefaultSpecularTexture() const { return defaultSpecularTexture_.Get(); }

    /// \brief Get material descriptor manager for GPU binding
    /// \returns VulkanMaterialDescriptorManager* for material descriptor set management (Phase 27)
    class VulkanMaterialDescriptorManager* GetMaterialDescriptorManager() const { return materialDescriptorManager_.Get(); }

    /// \brief Get descriptor set for a material (Phase 33 Step 3)
    /// \param material Material to get descriptor for
    /// \returns VkDescriptorSet containing material parameters and textures, or VK_NULL_HANDLE on error
    VkDescriptorSet GetMaterialDescriptor(class Material* material);

    /// \brief Get current pipeline layout for descriptor binding
    /// \returns VkPipelineLayout for descriptor set binding commands (Phase 27)
    VkPipelineLayout GetCurrentPipelineLayout() const { return currentPipelineLayout_; }

    /// \brief Get material descriptor set layout (Phase 36A)
    /// \returns VkDescriptorSetLayout for Set 0 (materials: textures, samplers, parameters)
    VkDescriptorSetLayout GetMaterialDescriptorLayout() const { return materialDescriptorLayout_; }

    /// \brief Get G-Buffer texture descriptor set layout (Phase 36A)
    /// \returns VkDescriptorSetLayout for Set 1 (G-Buffer textures for deferred lighting)
    VkDescriptorSetLayout GetGBufferTextureLayout() const { return gbufferTextureLayout_; }

    /// \brief Get constant buffer descriptor set layout (Phase 36A)
    /// \returns VkDescriptorSetLayout for Set 2 (light parameters for deferred lighting)
    VkDescriptorSetLayout GetConstantBufferLayout() const { return constantBufferLayout_; }

    /// \brief Get input attachment descriptor set layout (Phase 36A)
    /// \returns VkDescriptorSetLayout for Set 3 (tile-local G-Buffer optimization)
    VkDescriptorSetLayout GetInputAttachmentLayout() const { return inputAttachmentLayout_; }

    /// \brief Set current pipeline layout (Phase 36A)
    /// \param layout VkPipelineLayout to use for subsequent descriptor bindings
    void SetCurrentPipelineLayout(VkPipelineLayout layout) { currentPipelineLayout_ = layout; }

    /// \brief Get current reflection-based descriptor set layout (Phase 36 Step 5)
    /// \returns VkDescriptorSetLayout created from SPIR-V reflection, or VK_NULL_HANDLE if not set
    /// \details Returns the descriptor set layout dynamically generated from shader reflection.
    /// This layout matches the actual SPIR-V bindings and is used for creating descriptor sets.
    VkDescriptorSetLayout GetCurrentDescriptorSetLayout() const { return currentDescriptorSetLayout_; }

    /// \brief Set current reflection-based descriptor set layout (Phase 36 Step 5)
    /// \param layout VkDescriptorSetLayout to use for descriptor set allocation
    /// \details Stores the descriptor set layout created from SPIR-V reflection so descriptor sets
    /// can be allocated with the same layout used for pipeline creation. This ensures compatibility
    /// between descriptor sets and pipeline layouts.
    void SetCurrentDescriptorSetLayout(VkDescriptorSetLayout layout) { currentDescriptorSetLayout_ = layout; }

    /// \brief Get current constant buffer for uniform buffer descriptors (Phase 36 Step 5)
    /// \returns VkBuffer containing shader parameters, or VK_NULL_HANDLE if not set
    /// \details Returns the constant buffer created by UploadPendingShaderParameters_Vulkan().
    /// This buffer contains packed shader parameters uploaded to GPU memory.
    VkBuffer GetCurrentConstantBuffer() const { return currentConstantBuffer_; }

    /// \brief Get current constant buffer size (Phase 36 Step 5)
    /// \returns Size of constant buffer in bytes, or 0 if not set
    /// \details Returns the size of the constant buffer for descriptor set updates.
    size_t GetCurrentConstantBufferSize() const { return currentConstantBufferSize_; }

    /// \brief Get current constant buffer offset (Triple-buffering fix)
    /// \returns Base offset into constant buffer for current frame, or 0 if not set
    /// \details Returns the frame-specific offset from VulkanConstantBufferPool.
    /// This offset must be added to per-block offsets when creating descriptor sets.
    VkDeviceSize GetCurrentConstantBufferOffset() const { return currentConstantBufferOffset_; }

    /// \brief Set current constant buffer for uniform buffer descriptors (Phase 36 Step 5)
    /// \param buffer VkBuffer containing shader parameters
    /// \param size Size of buffer in bytes
    /// \param offset Base offset into buffer (for triple-buffering)
    /// \details Stores the constant buffer created by UploadPendingShaderParameters_Vulkan() so
    /// descriptor sets can bind it to uniform buffer bindings. This buffer contains the packed
    /// shader parameters (camera matrices, object transforms, material properties).
    void SetCurrentConstantBuffer(VkBuffer buffer, size_t size, VkDeviceSize offset = 0) {
        currentConstantBuffer_ = buffer;
        currentConstantBufferSize_ = size;
        currentConstantBufferOffset_ = offset;
    }

    /// \brief Create texture descriptor set for currently bound textures (Phase 36B)
    /// \returns VkDescriptorSet for Set 1 containing texture bindings, or VK_NULL_HANDLE on error
    /// \details Creates descriptor set with currently bound textures from textures_[] array.
    /// Used for deferred lighting G-Buffer texture access (albedo, normal, depth).
    VkDescriptorSet CreateTextureDescriptorSet();

    /// \brief Bind texture descriptor set to Set 1 (Phase 36B)
    /// \param descriptorSet VkDescriptorSet containing texture bindings
    /// \details Binds texture descriptor set to Set 1 for shader access to G-Buffer textures.
    /// Must be called before draw commands that need texture access.
    void BindTextureDescriptorSet(VkDescriptorSet descriptorSet);

    /// \brief Create constant buffer descriptor set for shader parameters (Phase 36C)
    /// \param data Constant buffer data to upload
    /// \param dataSize Size of data in bytes
    /// \returns VkDescriptorSet for Set 2 containing uniform buffer binding, or VK_NULL_HANDLE on error
    /// \details Allocates constant buffer from pool, uploads data, and creates descriptor set.
    /// Used for light parameters, material properties, and other per-draw uniform data.
    VkDescriptorSet CreateConstantBufferDescriptorSet(const void* data, uint32_t dataSize);

    /// \brief Bind constant buffer descriptor set to Set 2 (Phase 36C)
    /// \param descriptorSet VkDescriptorSet containing uniform buffer binding
    /// \details Binds constant buffer descriptor set to Set 2 for shader parameter access.
    /// Must be called before draw commands that need constant buffer access.
    void BindConstantBufferDescriptorSet(VkDescriptorSet descriptorSet);

    /// \brief Create input attachment descriptor set for G-Buffer access (Phase 36D)
    /// \returns VkDescriptorSet for Set 3 containing input attachment bindings, or VK_NULL_HANDLE on error
    /// \details Creates descriptor set with G-Buffer input attachments (albedo, normal, depth, specular).
    /// Used for tile-local deferred lighting optimization on tile-based GPUs.
    /// Input attachments allow fragment shaders to read framebuffer data without texture sampling.
    VkDescriptorSet CreateInputAttachmentDescriptorSet();

    /// \brief Bind input attachment descriptor set to Set 3 (Phase 36D)
    /// \param descriptorSet VkDescriptorSet containing input attachment bindings
    /// \details Binds input attachment descriptor set to Set 3 for tile-local G-Buffer access.
    /// Must be called in lighting pass subpass to enable input attachment optimization.
    void BindInputAttachmentDescriptorSet(VkDescriptorSet descriptorSet);

    /// \brief Transition image layout for texture operations
    /// \param image VkImage to transition
    /// \param format VkFormat of the image
    /// \param oldLayout Previous VkImageLayout
    /// \param newLayout Desired VkImageLayout
    /// \param mipLevels Number of mip levels to transition
    void TransitionImageLayout(VkImage image, VkFormat format,
                              VkImageLayout oldLayout, VkImageLayout newLayout,
                              uint32_t mipLevels);

    /// \brief Transition image layout using a specific command buffer
    /// \param commandBuffer VkCommandBuffer to record transition commands into
    /// \param image VkImage to transition
    /// \param format VkFormat of the image
    /// \param oldLayout Previous VkImageLayout
    /// \param newLayout Desired VkImageLayout
    /// \param mipLevels Number of mip levels to transition
    void TransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format,
                              VkImageLayout oldLayout, VkImageLayout newLayout,
                              uint32_t mipLevels);

    /// \brief Get framebuffer for current render targets (or swapchain if none set)
    /// \returns VkFramebuffer for render-to-texture operations
    VkFramebuffer GetCurrentFramebufferRT();

    /// \brief Rebuild framebuffer for render targets
    /// \returns True if rebuild successful, false on error
    /// \details Called when render targets change to update the framebuffer attachments
    bool RebuildRenderTargetFramebuffer();

private:
    /// \brief Create Vulkan instance with required extensions
    /// \returns True if instance created successfully, false on error
    /// \details Creates VkInstance with validation layer support (if available).
    /// Queries required platform extensions via SDL2 and loads instance-level functions.
    /// Critical first step in Vulkan initialization.
    bool CreateInstance();

    /// \brief Select physical device (GPU) for rendering
    /// \returns True if suitable device found, false if no compatible GPU available
    /// \details Enumerates all available GPUs and selects the most suitable one.
    /// Preference order: discrete GPU > integrated GPU > virtual GPU.
    /// Validates device support for graphics queue and surface presentation.
    bool SelectPhysicalDevice();

    /// \brief Create logical device and extract queue handles
    /// \returns True if device created successfully, false on error
    /// \details Creates VkDevice from selected physical device.
    /// Requests graphics and presentation queues (may be same queue family).
    /// Loads device-level Vulkan functions for command recording and resource management.
    bool CreateLogicalDevice();

    /// \brief Create window surface via SDL2
    /// \param window SDL_Window pointer from application
    /// \returns True if surface created successfully, false on error
    /// \details Creates VkSurfaceKHR for window. Platform-specific implementation
    /// handled transparently by vkCreateSurfaceKHR() and SDL2 integration.
    bool CreateSurface(SDL_Window* window);

    /// \brief Create swapchain for display presentation
    /// \param width Desired swapchain width in pixels
    /// \param height Desired swapchain height in pixels
    /// \returns True if swapchain created successfully, false on error
    /// \details Creates VkSwapchainKHR with intelligent format/mode selection.
    /// Allocates VkImage handles and creates corresponding VkImageView objects.
    /// Format chosen via FindSurfaceFormat(), present mode via FindPresentMode().
    bool CreateSwapchain(int width, int height);

    /// \brief Create depth buffer for depth testing
    /// \param format VkFormat for depth buffer (typically VK_FORMAT_D32_SFLOAT)
    /// \param width Depth buffer width in pixels (matches swapchain)
    /// \param height Depth buffer height in pixels (matches swapchain)
    /// \returns True if depth buffer created successfully, false on error
    /// \details Allocates VkImage and VkImageView for depth attachment.
    /// Memory allocated via VMA for optimal performance.
    /// Layout initialized to VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
    bool CreateDepthBuffer(VkFormat format, int width, int height, VkSampleCountFlagBits sampleCount);

    /// \brief Create MSAA color image for multi-sample rendering (Phase 30)
    /// \returns True if MSAA color image created successfully, false on error
    /// \details Creates intermediate VkImage for MSAA rendering when sampleCount > 1x.
    /// Allocates via VMA and creates corresponding VkImageView.
    /// Used as render target, resolved to swapchain before presentation.
    bool CreateMSAAColorImage(int width, int height);

    /// \brief Create G-Buffer attachments for deferred rendering (Phase 31)
    /// \param width Render target width
    /// \param height Render target height
    /// \returns True if G-Buffer created successfully, false on error
    /// \details Creates 4 G-Buffer attachments: Position (RGBA32F), Normal (RGBA16F),
    /// Albedo (RGBA8), Specular (RGBA8). Used for deferred rendering geometry pass output.
    bool CreateGBuffer(int width, int height);

    /// \brief Destroy G-Buffer attachments
    /// \details Releases all G-Buffer image resources and views
    void DestroyGBuffer();

    /// Get (or lazily create + cache) a render-target depth buffer for the given dimensions and
    /// format. Cached in rttDepthCache_ so alternating RTT sizes reuse buffers instead of
    /// thrashing a single slot. Returns VK_NULL_HANDLE on allocation failure.
    VkImageView GetOrCreateRttDepthView(int width, int height, VkFormat format);
    /// Destroy every cached RTT depth buffer (image/memory/view) and clear the cache.
    void DestroyRttDepthCache();

    /// \brief Create full-screen quad buffers for lighting pass
    /// \details Phase 36: Creates vertex and index buffers for a full-screen triangle used in deferred lighting pass.
    /// Stores buffers in fullScreenQuadVertexBuffer_ and fullScreenQuadIndexBuffer_.
    /// \returns True if buffers created successfully
    bool CreateFullScreenQuad();

    /// \brief Destroy full-screen quad buffers
    /// \details Phase 36: Releases full-screen quad vertex and index buffers via VMA
    void DestroyFullScreenQuad();

    /// \brief Create default render pass for single-pass rendering
    /// \returns True if render pass created successfully, false on error
    /// \details Creates VkRenderPass with standard color and depth attachments.
    /// Subpass: load color, clear depth, output color. Caches in renderPassCache_.
    /// Future enhancement: GetOrCreateRenderPass() supports multi-pass rendering.
    bool CreateRenderPass();

    /// \brief Get or create render pass from descriptor (extensible for multi-pass)
    /// \param descriptor RenderPassDescriptor specifying render pass configuration
    /// \returns VkRenderPass handle if created/found, nullptr if creation fails
    /// \details Checks renderPassCache_ for existing render pass matching descriptor.
    /// If not found, creates new render pass and caches for future use.
    /// Supports MSAA sample counts and multiple subpasses for deferred rendering.
    VkRenderPass GetOrCreateRenderPass(const RenderPassDescriptor& descriptor);

    /// \brief Create framebuffers for all swapchain images
    /// \returns True if all framebuffers created successfully, false on error
    /// \details Creates VkFramebuffer for each swapchain image.
    /// Each framebuffer has color attachment (swapchain image) + depth attachment.
    /// Stored in framebuffers_ vector (indexed by swapchain image index).
    bool CreateFramebuffers();

    /// \brief Create command pool and frame command buffers
    /// \returns True if buffers created successfully, false on error
    /// \details Creates VkCommandPool and allocates triple-buffered command buffers.
    /// commandBuffers_[frameIndex_] gives current frame's command buffer.
    /// Used for recording render commands via vkCmdBindPipeline, vkCmdDraw, etc.
    bool CreateCommandBuffers();

    /// \brief Create synchronization primitives for frame pipelining
    /// \returns True if primitives created successfully, false on error
    /// \details Allocates triple-buffered fences and semaphores:
    /// - frameFences_: CPU waits for GPU completion before reusing buffer
    /// - imageAcquiredSemaphores_: GPU waits for swapchain image availability
    /// - renderCompleteSemaphores_: GPU signals render completion before present
    bool CreateSynchronizationPrimitives();

    /// \brief Create Vulkan Memory Allocator instance
    /// \returns True if allocator created successfully, false on error
    /// \details Initializes VMA with device and queue family information.
    /// Used for all GPU buffer and image allocations (via VmaAllocator).
    /// Manages memory pools for optimal allocation patterns.
    bool CreateMemoryAllocator();

    /// \brief Create descriptor pool for descriptor set allocation
    /// \returns True if pool created successfully, false on error
    /// \details Allocates VkDescriptorPool with support for multiple descriptor types:
    /// - Uniform buffers (shader parameters)
    /// - Sampled images and samplers (textures)
    /// - Storage images (compute/UAV operations)
    /// Pool size determined by expected descriptor set count.
    bool CreateDescriptorPool();

    /// \brief Create descriptor set layouts for multi-set binding (Phase 36A)
    /// \returns True if layouts created successfully, false on error
    /// \details Creates descriptor set layouts for:
    /// - Set 0: Material descriptors (textures, samplers, material parameters)
    /// - Set 1: G-Buffer textures (albedo, normal, depth for deferred lighting)
    /// - Set 2: Constant buffers (light parameters for deferred lighting)
    /// - Set 3: Input attachments (tile-local G-Buffer optimization)
    bool CreateDescriptorSetLayouts();

    /// \brief Create pipeline cache for persistent pipeline storage
    /// \returns True if cache created successfully, false on error
    /// \details Initializes VulkanPipelineCache with optional disk persistence.
    /// Caches compiled graphics pipelines by state hash for fast retrieval.
    /// Reduces compilation time on subsequent runs if disk cache available.
    bool CreatePipelineCache();

    /// \brief Get or create graphics pipeline with given state (Phase 32-33)
    /// \param layout Pipeline layout for descriptor sets
    /// \param renderPass Render pass the pipeline is compatible with
    /// \param state Pipeline state describing blend, depth, stencil, cull, etc.
    /// \param vsModule Compiled vertex shader module (VK_NULL_HANDLE for none)
    /// \param fsModule Compiled fragment shader module (VK_NULL_HANDLE for none)
    /// \returns VkPipeline handle for binding, or VK_NULL_HANDLE on failure
    /// \details Checks pipelineCache_ for existing pipeline matching state hash and shader modules.
    /// If not found, creates new graphics pipeline with given state and shader stages, then caches it.
    /// Phase 32: Supports graphics state (blend, depth, stencil, cull)
    /// Phase 33: Supports shader module binding (vertex + fragment stages)
    /// Phase 36+: Supports geometry shader stage
    VkPipeline GetOrCreateGraphicsPipeline(VkPipelineLayout layout, VkRenderPass renderPass,
                                          const VulkanPipelineState& state,
                                          class VertexBuffer* vertexBuffer = nullptr,
                                          class VertexBuffer* instanceBuffer = nullptr,
                                          VkShaderModule vsModule = VK_NULL_HANDLE,
                                          VkShaderModule fsModule = VK_NULL_HANDLE,
                                          VkShaderModule gsModule = VK_NULL_HANDLE,
                                          class ShaderVariation* vertexShader = nullptr,
                                          class ShaderVariation* pixelShader = nullptr,
                                          class ShaderVariation* geometryShader = nullptr);

    /// \brief Phase 33: Create shader modules from shader variations
    /// \param vertexShader Vertex shader variation (may be nullptr)
    /// \param pixelShader Pixel/fragment shader variation (may be nullptr)
    /// \param vsModule Output: compiled vertex shader module (VK_NULL_HANDLE if nullptr input)
    /// \param fsModule Output: compiled fragment shader module (VK_NULL_HANDLE if nullptr input)
    /// \returns true if compilation successful (or shaders were nullptr), false on error
    /// \details Compiles GLSL shaders to SPIR-V and creates VkShaderModule objects.
    /// Uses VulkanShaderModule for compilation and caching. Caller is responsible
    /// for destroying modules via vkDestroyShaderModule().
    /// \param geometryShader Optional geometry shader (nullptr if not used)
    bool CreateShaderModules(class ShaderVariation* vertexShader, class ShaderVariation* pixelShader,
                            VkShaderModule& vsModule, VkShaderModule& fsModule,
                            class ShaderVariation* geometryShader = nullptr, VkShaderModule* gsModule = nullptr);

    /// \brief Create descriptor set layout from reflected SPIR-V resources
    /// \param vsResources Reflected resources from vertex shader SPIR-V
    /// \param psResources Reflected resources from pixel shader SPIR-V
    /// \returns VkDescriptorSetLayout with merged bindings, or VK_NULL_HANDLE on failure
    /// \details Dynamically generates descriptor set layouts based on actual shader bindings.
    /// Merges vertex and pixel shader resources, combining stage flags for shared binding numbers.
    /// This enables automatic pipeline layout creation without hardcoded expectations.
    VkDescriptorSetLayout CreateReflectionBasedLayout(
        const Vector<struct SPIRVResource>& vsResources,
        const Vector<struct SPIRVResource>& psResources);

    /// \brief Get or create cached descriptor set layout from reflected shader resources
    /// \param vsResources Reflected resources from vertex shader SPIR-V
    /// \param psResources Reflected resources from pixel shader SPIR-V
    /// \returns Cached VkDescriptorSetLayout, or newly created one if not in cache
    /// \details Computes hash of binding configuration and checks cache before creating.
    /// Dramatically reduces vkCreateDescriptorSetLayout calls from ~1500/sec to ~2-5 total.
    VkDescriptorSetLayout GetOrCreateDescriptorSetLayout(
        const Vector<struct SPIRVResource>& vsResources,
        const Vector<struct SPIRVResource>& psResources);

    /// \brief Get or create cached pipeline layout from descriptor set layout
    /// \param descriptorSetLayout Descriptor set layout to create pipeline layout from
    /// \returns Cached VkPipelineLayout, or newly created one if not in cache
    /// \details Uses descriptor set layout handle as cache key.
    /// Dramatically reduces vkCreatePipelineLayout calls from ~1500/sec to ~2-5 total.
    VkPipelineLayout GetOrCreatePipelineLayout(VkDescriptorSetLayout descriptorSetLayout);

    /// \brief Find optimal surface format for swapchain
    /// \returns VkSurfaceFormatKHR with preferred color space and format
    /// \details Queries device surface capabilities and selects best format.
    /// Preference: VK_FORMAT_B8G8R8A8_SRGB (typical desktop) with SRGB color space.
    /// Falls back to first available format if preferred format unavailable.
    VkSurfaceFormatKHR FindSurfaceFormat();

    /// \brief Find optimal present mode for swapchain
    /// \returns VkPresentModeKHR for swapchain presentation
    /// \details Queries available present modes and selects best for latency/tearing tradeoff.
    /// Preference: VK_PRESENT_MODE_MAILBOX_KHR (triple-buffered, low latency).
    /// Falls back to VK_PRESENT_MODE_FIFO_KHR (guaranteed available, may vsync).
    VkPresentModeKHR FindPresentMode();

    /// \brief Find memory type supporting required properties
    /// \param typeFilter Bitmask of allowed memory type indices (from device limits)
    /// \param properties Required VkMemoryPropertyFlags (GPU-local, host-visible, etc.)
    /// \returns Memory type index suitable for allocation
    /// \details Called during buffer/image allocation to select optimal memory heap.
    /// Raises error if no suitable memory type available for given constraints.
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    /// \brief Find queue family indices supporting graphics and presentation
    /// \returns True if suitable queue families found, false if device incompatible
    /// \details Sets graphicsQueueFamily_ and presentQueueFamily_.
    /// These may be the same queue family or different (depends on device).
    /// All rendering operations submitted to graphics queue.
    bool FindQueueFamilies();

    /// \brief Detect MSAA capabilities supported by physical device
    /// \returns True if capable of detecting MSAA support, false on error
    /// \details Queries physical device limits for supported sample counts.
    /// Stores supported sample count bitmask and determines best available MSAA level.
    /// Called during device selection to enable MSAA configuration.
    bool DetectMSAACapabilities();

    /// \brief Select best supported sample count for requested MSAA level
    /// \param requestedCount User-requested sample count (1, 2, 4, 8, 16)
    /// \returns VkSampleCountFlagBits for nearest supported count, fallback to 1x if none match
    /// \details Maps requested MSAA level to nearest device-supported sample count.
    /// Returns VK_SAMPLE_COUNT_1_BIT if requested count unavailable.
    VkSampleCountFlagBits SelectBestSampleCount(uint32_t requestedCount);

    /// \brief Detect timeline semaphore extension support
    /// \returns True if device supports VK_KHR_timeline_semaphore, false otherwise
    /// \details Queries device features for timeline semaphore capability.
    /// Called during physical device selection to enable timeline-based synchronization.
    bool DetectTimelineSemaphoreSupport();

    /// \brief Detect GPU timestamp query support and create per-frame query pools
    /// \returns True if timestamps supported and pools created, false otherwise
    /// \details Checks timestampPeriod and queueTimestampValidBits from device/queue properties.
    /// Creates one VkQueryPool per frame-in-flight with MAX_GPU_TIMESTAMP_QUERIES slots each.
    bool CreateTimestampQueryPools();

    /// \brief Create timeline semaphore for render completion tracking
    /// \returns True if semaphore created successfully, false on error
    /// \details Creates VkSemaphore with VK_SEMAPHORE_TYPE_TIMELINE for GPU-CPU sync.
    /// Replaces 3 binary render complete semaphores with single timeline counter.
    /// Initial counter value set to 0 (VULKAN_TIMELINE_INITIAL_VALUE).
    bool CreateTimelineSemaphore();

    /// \brief Wait on timeline semaphore reaching specific counter value
    /// \param targetValue Expected timeline counter value to wait for
    /// \returns True if semaphore reached value, false on timeout
    /// \details Non-blocking alternative to WaitForFrameFence().
    /// GPU waits on specific timeline counter instead of CPU blocking on fence.
    bool WaitOnTimelineRenderSemaphore(uint64_t targetValue);

    /// \brief Signal timeline semaphore after frame completion
    /// \details Increments timelineRenderCounter_ after vkQueueSubmit().
    /// Called from Present() to mark frame as GPU-complete.
    void SignalTimelineRenderSemaphore();

    /// \brief Insert pipeline barrier for compute-to-graphics or graphics-to-compute synchronization (Phase 36+)
    /// \param cmdBuffer Command buffer to record barrier into
    /// \param srcStage Source pipeline stage (e.g., VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
    /// \param dstStage Destination pipeline stage (e.g., VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
    /// \param srcAccess Source access mask (e.g., VK_ACCESS_SHADER_WRITE_BIT)
    /// \param dstAccess Destination access mask (e.g., VK_ACCESS_SHADER_READ_BIT)
    /// \details Inserts vkCmdPipelineBarrier to prevent race conditions between compute and graphics work.
    /// Essential for compute shaders writing to buffers/textures read by graphics pipeline.
    void InsertPipelineBarrier(VkCommandBuffer cmdBuffer,
                              VkPipelineStageFlags srcStage,
                              VkPipelineStageFlags dstStage,
                              VkAccessFlags srcAccess,
                              VkAccessFlags dstAccess);

    // Validation toggle — off by default, enable with -VulkanValidation=true
    bool enableValidation_{false};

    // Vulkan instance and device objects
    VkInstance instance_{};
    VkPhysicalDevice physicalDevice_{};
    VkPhysicalDeviceProperties deviceProperties_{};
    VkDevice device_{};
    VkSurfaceKHR surface_{};

    // Queues
    VkQueue graphicsQueue_{};
    VkQueue presentQueue_{};
    uint32_t graphicsQueueFamily_{VK_QUEUE_FAMILY_IGNORED};
    uint32_t presentQueueFamily_{VK_QUEUE_FAMILY_IGNORED};
    // Dedicated async-compute queue for GPU training. When the device exposes a COMPUTE-capable
    // queue family WITHOUT the GRAPHICS bit, training dispatches submit here instead of sharing
    // graphicsQueue_ — so a long compute batch never sits in front of the frame Present submit and
    // the render frame-fence signals promptly (the fix for the render/compute reuse-before-signal
    // race → VK_ERROR_DEVICE_LOST). Falls back to the graphics queue when no separate family exists.
    VkQueue computeQueue_{};
    uint32_t computeQueueFamily_{VK_QUEUE_FAMILY_IGNORED};
    bool computeQueueIsDedicated_{false};

    // Swapchain
    VkSwapchainKHR swapchain_{};
    Vector<VkImage> swapchainImages_;
    Vector<VkImageView> swapchainImageViews_;
    Vector<ImageSemaphores> imageSemaphores_;  // One set per swapchain image
    VkFormat swapchainFormat_{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchainExtent_{};

    // Depth buffer
    VkImage depthImage_{};
    VkDeviceMemory depthImageMemory_{};
    VkImageView depthImageView_{};

    // MSAA color buffer (Phase 30) - used when sample count > 1x
    VkImage msaaColorImage_{};            ///< Multi-sample color attachment image
    VmaAllocation msaaColorAllocation_{}; ///< VMA allocation for MSAA color image
    VkImageView msaaColorImageView_{};    ///< Image view for multi-sample color attachment

    // G-Buffer for deferred rendering (Phase 31)
    VkImage gBufferPositionImage_{};      ///< G-Buffer position (world position) texture
    VmaAllocation gBufferPositionAlloc_{}; ///< VMA allocation for position G-Buffer
    VkImageView gBufferPositionView_{};   ///< Image view for position attachment

    VkImage gBufferNormalImage_{};        ///< G-Buffer normal (world normal) texture
    VmaAllocation gBufferNormalAlloc_{};  ///< VMA allocation for normal G-Buffer
    VkImageView gBufferNormalView_{};     ///< Image view for normal attachment

    VkImage gBufferAlbedoImage_{};        ///< G-Buffer albedo (diffuse color) texture
    VmaAllocation gBufferAlbedoAlloc_{};  ///< VMA allocation for albedo G-Buffer
    VkImageView gBufferAlbedoView_{};     ///< Image view for albedo attachment

    VkImage gBufferSpecularImage_{};      ///< G-Buffer specular (specular properties) texture
    VmaAllocation gBufferSpecularAlloc_{}; ///< VMA allocation for specular G-Buffer
    VkImageView gBufferSpecularView_{};   ///< Image view for specular attachment

    // Phase 36: Full-screen quad for lighting pass (deferred rendering)
    VkBuffer fullScreenQuadVertexBuffer_{};      ///< Vertex buffer for full-screen quad
    VmaAllocation fullScreenQuadVertexAlloc_{};  ///< VMA allocation for vertex buffer
    VkBuffer fullScreenQuadIndexBuffer_{};       ///< Index buffer for full-screen quad
    VmaAllocation fullScreenQuadIndexAlloc_{};   ///< VMA allocation for index buffer

    // Render pass and framebuffers
    VkRenderPass renderPass_{};           ///< Swapchain render pass with loadOp=CLEAR (first use per frame)
    VkRenderPass renderPassLoad_{};       ///< Swapchain render pass with loadOp=LOAD (re-entry mid-frame)
    bool swapchainPassUsedThisFrame_{};   ///< Track whether swapchain pass was already used this frame
    HashSet<void*> writtenRenderTargets_; ///< Track which render targets were written this frame (for LOAD vs CLEAR)
    void* activePassRenderTargets_[MAX_RENDERTARGETS]{}; ///< Render targets at time BeginRenderPass was called
    unsigned activePassColorCount_{}; ///< Number of color attachments in the active render pass
    RenderPassDescriptor renderTargetRPDescriptor_{}; ///< Descriptor from last RebuildRTFB (for LOAD variant creation)
    Vector<VkFramebuffer> framebuffers_;

    // Render pass cache - supports future multi-pass rendering (Issue #2)
    HashMap<uint32_t, VkRenderPass> renderPassCache_;
    // Reverse lookup: VkRenderPass handle → descriptor hash (stable across sessions)
    HashMap<uintptr_t, uint32_t> renderPassToDescHash_;

    // Command buffers (double/triple buffering)
    VkCommandPool commandPool_{};

    // Worker-thread-owned compute command pool (STEP 4: off-render GPU training). Set by the
    // training worker via SetComputeWorkerPool; used by Begin/EndComputeBatch only when they
    // run on this exact thread, so a Leith-run /gpu* on the main thread stays on commandPool_.
    VkCommandPool computeWorkerPool_{};
    VkDescriptorPool computeWorkerDescPool_{};   ///< Worker-own descriptor pool (shared frame pool is reset every frame by render).
    ThreadID computeWorkerThreadID_{};

    // A1: explicit, tid-INDEPENDENT compute-batch pool override (SetComputeBatchPools). When set,
    // GetActiveComputePool/GetActiveDescriptorPool return these regardless of the calling thread —
    // the routing the main-thread-coordinated CPU/GPU training superhighway needs once thread identity
    // can no longer separate the trainer's lane from render. Null = fall back to tid routing above.
    VkCommandPool computeBatchOverridePool_{};
    VkDescriptorPool computeBatchOverrideDescPool_{};

    // Thread-local command pools for upload operations (thread safety fix)
    HashMap<ThreadID, VkCommandPool> threadUploadCommandPools_;
    Mutex threadUploadCommandPoolsMutex_;

    // Queue submission mutex (thread safety for vkQueueSubmit)
    Mutex queueSubmitMutex_;

    // Submit mutex for the dedicated compute queue. Only used when computeQueueIsDedicated_ — a
    // distinct queue is externally-synchronised independently of graphicsQueue_, so its submits
    // must not contend on queueSubmitMutex_ (that would re-serialise compute behind render/upload
    // and defeat the whole point). In the shared-queue fallback GetComputeQueueSubmitMutex()
    // returns queueSubmitMutex_ instead, so compute still serialises with graphics correctly.
    Mutex computeQueueSubmitMutex_;

    // Per-frame synchronization resources
    // Using 4 for safe pipelining with triple-buffered swapchain
    static const uint32_t MAX_FRAMES_IN_FLIGHT = 4;
    Vector<FrameResources> frames_;  // Size = MAX_FRAMES_IN_FLIGHT
    uint32_t currentFrame_{0};

    // Descriptor management (Per-image fix: one pool per swapchain image, not per frame)
    // Must match swapchain image count to avoid resetting pools while images are presenting
    Vector<VkDescriptorPool> descriptorPools_;

    // Deferred buffer deletion queue — buffers destroyed after frame fence signals
    struct DeferredBufferDeletion {
        VkBuffer buffer;
        VmaAllocation allocation;
    };
    Vector<DeferredBufferDeletion> deferredDeletions_[4]; // One queue per frame in flight

    // Compute-owned deferred deletions — buffers released during a compute-only pump, when no render
    // frame is being acquired to drain the per-frame queues above. Drained by
    // FlushComputeDeferredDeletions() on the compute fence at a page/batch boundary. Kept SEPARATE so
    // it never frees a frame-fence-owned buffer before that frame's fence has signalled.
    Vector<DeferredBufferDeletion> computeDeferredDeletions_;
    // When true, DeferBufferDeletion() routes into computeDeferredDeletions_ instead of the per-frame queue.
    bool deferToComputeBucket_ = false;
    // Guards BOTH deferredDeletions_[] and computeDeferredDeletions_ (and the routing flag).
    // DeferBufferDeletion() may run on a worker/pump thread while the render thread runs
    // ProcessDeferredDeletions() — these Vectors were previously mutated from two threads with no lock.
    Mutex deferredDeletionMutex_;

    // Enhanced sampler cache with expanded configurations (Quick Win #4)
    SharedPtr<VulkanSamplerCache> samplerCache_;

    // Shader compilation result cache (Quick Win #5)
    SharedPtr<VulkanShaderCache> shaderCache_;

    // Pipeline disk persistence cache (Phase B Quick Win #10)
    SharedPtr<VulkanPipelineCache> pipelineCache_;

    // Descriptor set layout cache - reduces vkCreateDescriptorSetLayout calls from ~1500/sec to ~2-5 total
    HashMap<unsigned, VkDescriptorSetLayout> descriptorSetLayoutCache_;

    // Pipeline layout cache - reduces vkCreatePipelineLayout calls from ~1500/sec to ~2-5 total
    HashMap<unsigned long long, VkPipelineLayout> pipelineLayoutCache_;

    // Shader module cache - avoids vkCreateShaderModule/vkDestroyShaderModule per draw call.
    // Keyed by CONTENT identity (owner name + stage + defines via ShaderModuleContentKey), NOT the
    // raw ShaderVariation* — a recycled pointer could otherwise return a stale module. Freed at teardown.
    HashMap<uint64_t, VkShaderModule> shaderModuleCache_;

    // Last bound pipeline - skip redundant vkCmdBindPipeline calls
    VkPipeline lastBoundPipeline_{VK_NULL_HANDLE};

    // Compute pipeline cache (Phase 36+: Compute shader support)
    VulkanComputePipeline* computePipeline_;

    // Compute descriptor set layout (4 SSBO bindings) and pipeline layout
    VkDescriptorSetLayout computeDescriptorLayout_{VK_NULL_HANDLE};
    VkPipelineLayout computePipelineLayout_{VK_NULL_HANDLE};

    // Constant buffer pooling (Quick Win #6)
    SharedPtr<VulkanConstantBufferPool> constantBufferPool_;

    // Memory pool manager for optimized buffer allocations (Quick Win #8)
    SharedPtr<VulkanMemoryPoolManager> memoryPoolManager_;

    // GPU instance buffer for vertex stream instancing (Phase 12)
    SharedPtr<VulkanInstanceBufferManager> instanceBufferManager_;

    // Indirect draw command buffer manager (Phase 12)
    SharedPtr<VulkanIndirectDrawManager> indirectDrawManager_;

    // Staging buffer manager for GPU uploads (Phase 10)
    SharedPtr<VulkanStagingBufferManager> stagingBufferManager_;

    // Memory allocator (VMA)
    VmaAllocator allocator_{};

    // MSAA (Multisample Anti-Aliasing) support
    VkSampleCountFlagBits requestedSampleCount_{VK_SAMPLE_COUNT_1_BIT};  ///< User-requested sample count
    VkSampleCountFlagBits actualSampleCount_{VK_SAMPLE_COUNT_1_BIT};     ///< Device-supported sample count (clamped)
    uint32_t supportedSampleCountsMask_{VK_SAMPLE_COUNT_1_BIT};           ///< Bitmask of device-supported sample counts

    // Timeline semaphore support (Phase 33)
    bool supportsTimelineSemaphores_{false};         ///< Device supports VK_KHR_timeline_semaphore
    VkSemaphore timelineRenderSemaphore_{};          ///< Timeline semaphore for render completion (replaces 3 binary semaphores)
    uint64_t timelineRenderCounter_{0};              ///< Current timeline counter value (incremented after each frame)

    // GPU timestamp query support (Phase 2 of Profiler Timeline)
    bool gpuTimestampSupported_{false};              ///< Device + queue support timestamp queries
    float timestampPeriodNs_{0.0f};                  ///< Nanoseconds per GPU timestamp tick
    uint32_t timestampValidBits_{0};                 ///< Number of valid bits in timestamp values

    // Frame tracking
    uint32_t frameIndex_{0};
    uint32_t currentImageIndex_{0};
    bool renderPassActive_{false};
    bool frameActive_{false};

    // Deferred resize — set by OnWindowResized_Vulkan, consumed by BeginFrame_Vulkan
    bool resizePending_{false};
    int pendingWidth_{0};
    int pendingHeight_{0};

    // Debug callback
    VkDebugUtilsMessengerEXT debugMessenger_{};

    // Secondary command buffer pool for parallel batch recording (Vulkan only)
    SharedPtr<VulkanSecondaryCommandBufferPool> secondaryCommandBufferPool_;

    // Shader parameter tracking (Phase 9)
    ShaderProgram* currentShaderProgram_{};
    Vector<ConstantBuffer*> dirtyConstantBuffers_;

    // Render target tracking (Phase 5 + RTT)
    RenderSurface* renderTargets_[MAX_RENDERTARGETS]{};
    RenderSurface* depthStencil_{};
    bool renderTargetsDirty_{true};  // Flag to rebuild framebuffer when targets change
    VkFramebuffer renderTargetFramebuffer_{};  // Framebuffer for render-to-texture
    VkRenderPass renderTargetRenderPass_{};   // Render pass for render-to-texture
    Vector<VkImageView> renderTargetViews_;    // Image views for render target attachments

    // RTT: Framebuffer cache and state
    HashMap<unsigned long long, VkFramebuffer> rttFramebufferCache_;
    int rttWidth_{0};
    int rttHeight_{0};
    bool renderingToTexture_{false};

    // Clear values (set by Clear_Vulkan, used by BeginRenderPass)
    float clearColor_[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float clearDepth_{1.0f};
    unsigned clearStencil_{0};

    // RTT: Per-RTT depth buffers (when no explicit depth stencil is provided).
    // Keyed by {width,height,format} so render targets of differing sizes reuse cached depth
    // buffers instead of thrashing a single slot (which destroyed+recreated the depth image
    // every frame when RTTs of different dimensions alternated). Mirrors rttFramebufferCache_.
    struct RttDepthBuffer
    {
        VkImage image_{};
        VkDeviceMemory memory_{};
        VkImageView view_{};
    };
    HashMap<unsigned long long, RttDepthBuffer> rttDepthCache_;

    // Default placeholder textures (Phase 22A)
    /// Default 1x1 white diffuse texture for materials without diffuse maps
    SharedPtr<Texture2D> defaultDiffuseTexture_;
    /// Default 1x1 neutral normal map (0.5, 0.5, 1.0) for materials without normal maps
    SharedPtr<Texture2D> defaultNormalTexture_;
    /// Default 1x1 white specular texture for materials without specular maps
    SharedPtr<Texture2D> defaultSpecularTexture_;

    // Material descriptor management (Phase 27)
    /// Material descriptor manager for GPU binding (textures, samplers, parameters)
    SharedPtr<class VulkanMaterialDescriptorManager> materialDescriptorManager_;
    /// Current pipeline layout for descriptor set binding (may be overwritten per-draw)
    VkPipelineLayout currentPipelineLayout_{};
    /// Global pipeline layout created at init (owned, must be destroyed)
    VkPipelineLayout globalPipelineLayout_{};
    /// Current reflection-based descriptor set layout (Phase 36 Step 5)
    /// Dynamically generated from SPIR-V reflection to match actual shader bindings
    VkDescriptorSetLayout currentDescriptorSetLayout_{VK_NULL_HANDLE};
    /// Current constant buffer for uniform buffer descriptors (Phase 36 Step 5)
    /// Contains packed shader parameters uploaded to GPU memory
    VkBuffer currentConstantBuffer_{VK_NULL_HANDLE};
    /// Size of current constant buffer in bytes (Phase 36 Step 5)
    size_t currentConstantBufferSize_{0};
    /// Base offset into constant buffer for current frame (Triple-buffering fix)
    VkDeviceSize currentConstantBufferOffset_{0};

    // Phase 36A: Descriptor set layouts for multi-set binding (DEPRECATED - use reflection-based layouts)
    /// Descriptor set layout for material descriptors (Set 0: textures, samplers, material params)
    VkDescriptorSetLayout materialDescriptorLayout_{VK_NULL_HANDLE};
    /// Descriptor set layout for G-Buffer textures (Set 1: albedo, normal, depth for deferred lighting)
    VkDescriptorSetLayout gbufferTextureLayout_{VK_NULL_HANDLE};
    /// Descriptor set layout for constant buffers (Set 2: light parameters for deferred lighting)
    VkDescriptorSetLayout constantBufferLayout_{VK_NULL_HANDLE};
    /// Descriptor set layout for input attachments (Set 3: tile-local G-Buffer optimization)
    VkDescriptorSetLayout inputAttachmentLayout_{VK_NULL_HANDLE};

    // Graphics context for resource management
    Graphics* graphics_{nullptr};

    // Device loss tracking
    bool deviceLost_{false};

    // Set when a compute-batch fence wait exceeds its bounded timeout — a hung dispatch. The GPU
    // train worker polls ComputeHangDetected() and exits its loop, so it stays joinable: clean
    // teardown depends on the worker being joined before vkDeviceWaitIdle / device destroy.
    volatile bool computeHang_{false};

    // Multiplier on the EndComputeBatch fence-wait bound (>=1). 1 for single steps; the GPU
    // minibatch page worker raises it to P so a P× longer page doesn't false-positive as a hang.
    unsigned computeBatchTimeoutScale_{1};

    friend class Graphics;
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
