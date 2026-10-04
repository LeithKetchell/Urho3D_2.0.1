//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/Vector.h"
#include "../../Container/Ptr.h"
#include "../../Container/RefCounted.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class VulkanGraphicsImpl;

/// \brief Vulkan secondary command buffer wrapper for parallel batch recording
/// \details Manages per-thread command buffer allocation, recording, and reuse to enable
/// parallel batch recording across worker threads during View::Render(). Each thread gets
/// its own VkCommandPool to avoid synchronization overhead while recording batches in
/// parallel.
///
/// **Key Features:**
/// - Per-thread VkCommandPool with VK_COMMAND_POOL_CREATE_TRANSIENT_BIT for efficiency
/// - Secondary buffer level for execution within render pass context
/// - Automatic recording state tracking and buffer lifecycle management
/// - Frame reuse pattern: record -> end -> reset for next frame
/// - Thread-safe access by design (no shared mutable state)
///
/// **Typical Usage:**
/// 1. Initialize(threadIndex) once per thread to set up command pool
/// 2. Begin() at start of frame to prepare for recording
/// 3. Record commands directly via Vulkan API using GetHandle()
/// 4. End() to finalize recording
/// 5. Reset() to prepare for next frame
///
/// **Integration with Rendering:**
/// - Records within VkRenderPass context (secondary buffer privileges)
/// - Can be executed from primary buffer via vkCmdExecuteCommands()
/// - Enables parallel batch recording from worker threads
/// - Maintains frame pipelining for optimal GPU/CPU synchronization
///
/// **Memory Management:**
/// - Command buffers reused per frame (not reallocated)
/// - VkCommandPool manages allocation with TRANSIENT flag (optimized by driver)
/// - Destruction automatically releases all Vulkan resources
class VulkanSecondaryCommandBuffer : public RefCounted
{
public:
    /// \brief Constructor
    /// \param impl VulkanGraphicsImpl instance for accessing device and queue information
    explicit VulkanSecondaryCommandBuffer(VulkanGraphicsImpl* impl);

    /// \brief Destructor
    /// \details Automatically calls vkDestroyCommandPool() to release Vulkan resources
    ~VulkanSecondaryCommandBuffer();

    /// \brief Initialize secondary command buffer pool for a thread
    /// \param threadIndex Worker thread index (0 to numThreads-1)
    /// \param level Command buffer level (default: VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    /// \returns True if initialization successful, false on error
    /// \details Creates a new VkCommandPool with TRANSIENT flag and allocates a
    /// VkCommandBuffer for this thread. Should be called once per thread during View setup.
    bool Initialize(uint32_t threadIndex, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_SECONDARY);

    /// \brief Begin recording to this command buffer
    /// \param flags Command buffer usage flags (default: VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)
    /// \returns True if successful, false on error
    /// \details Must be called before recording any commands. Sets isRecording_ flag to true.
    /// Uses VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT by default since secondary
    /// buffers are recorded within render pass context.
    bool Begin(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT);

    /// \brief End recording to this command buffer
    /// \returns True if successful, false on error
    /// \details Must be called after all command recording is complete. Sets isRecording_ flag to false
    /// and finalizes the command buffer for submission.
    bool End();

    /// \brief Reset command buffer for reuse in next frame
    /// \param flags Reset flags (default: VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT)
    /// \returns True if successful, false on error
    /// \details Clears all recorded commands and prepares the buffer for reuse.
    /// Called between frames to maintain the buffer pool's frame reuse pattern.
    bool Reset(VkCommandBufferResetFlags flags = VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

    /// \brief Get the underlying VkCommandBuffer handle
    /// \returns VkCommandBuffer handle for direct Vulkan API calls during recording
    VkCommandBuffer GetHandle() const { return commandBuffer_; }

    /// \brief Check if command buffer is currently recording
    /// \returns True if Begin() called and End() not yet called, false otherwise
    bool IsRecording() const { return isRecording_; }

    /// \brief Get command buffer level
    /// \returns VkCommandBufferLevel (typically VK_COMMAND_BUFFER_LEVEL_SECONDARY)
    VkCommandBufferLevel GetLevel() const { return level_; }

private:
    /// Vulkan graphics implementation
    VulkanGraphicsImpl* impl_;

    /// Command buffer handle
    VkCommandBuffer commandBuffer_{};

    /// Command buffer pool for this thread
    VkCommandPool commandPool_{};

    /// Command buffer level (primary or secondary)
    VkCommandBufferLevel level_{VK_COMMAND_BUFFER_LEVEL_SECONDARY};

    /// Recording state flag
    bool isRecording_{false};

    /// Allocation index for frame pipelining
    uint32_t allocationIndex_{0};

    /// Thread index this buffer belongs to
    uint32_t threadIndex_{UINT32_MAX};
};

/// \brief Thread-safe pool for managing secondary command buffers across worker threads
/// \details Manages a collection of per-thread secondary command buffers to enable parallel
/// batch recording during View::Render(). The pool maintains one VulkanSecondaryCommandBuffer
/// per worker thread, each with its own VkCommandPool to eliminate synchronization overhead.
/// This architecture enables lock-free concurrent recording from multiple threads.
///
/// **Architecture:**
/// - One VulkanSecondaryCommandBuffer per worker thread (Vector<SharedPtr<...>> storage)
/// - Per-thread VkCommandPool avoids vkQueueSubmit() contention
/// - Thread-safe by design: each thread accesses only its own buffer
/// - Synchronized batch point: ResetAllBuffers() called from main thread
///
/// **Lifecycle per Frame:**
/// 1. Initialize(numThreads) once during View setup
/// 2. Worker threads: GetThreadBuffer(threadIndex) -> Begin() -> Record -> End()
/// 3. Main thread: GetRecordedCommandBuffers() to collect all recorded buffers
/// 4. Main thread: vkCmdExecuteCommands() submits all buffers to primary buffer
/// 5. Main thread: ResetAllBuffers() prepares all buffers for next frame
///
/// **Integration with Rendering Pipeline:**
/// - Called by View::InitializeSecondaryCommandBuffers() to set up threading
/// - GetThreadBuffer() accessed by View::DrawBatchQueueParallel() work distribution
/// - GetRecordedCommandBuffers() provides buffers to View::SubmitSecondaryCommandBuffers()
/// - ResetAllBuffers() called between frames for buffer reuse
///
/// **Thread Safety Guarantees:**
/// - Concurrent GetThreadBuffer() calls safe from multiple threads
/// - Each thread accesses only its own buffer (no locking needed)
/// - ResetAllBuffers() is synchronization point (called from main thread only)
/// - Vector<SharedPtr<>> provides refcounted lifetime management
///
/// **Performance Benefits:**
/// - Per-thread pools eliminate vkQueueSubmit() serialization
/// - TRANSIENT flag allows driver optimizations (no persistent storage)
/// - Reuse pattern avoids allocation overhead per frame
/// - Parallel recording: CPU can prepare next frame while GPU renders current
class VulkanSecondaryCommandBufferPool : public RefCounted
{
public:
    /// \brief Constructor
    /// \param impl VulkanGraphicsImpl instance for device and queue access
    explicit VulkanSecondaryCommandBufferPool(VulkanGraphicsImpl* impl);

    /// \brief Destructor
    /// \details Automatically releases all VulkanSecondaryCommandBuffer instances
    /// and their underlying Vulkan resources via SharedPtr cleanup
    ~VulkanSecondaryCommandBufferPool();

    /// \brief Initialize command buffer pool for given number of threads
    /// \param numThreads Number of worker threads (typically std::thread::hardware_concurrency())
    /// \returns True if all buffers initialized successfully, false on error
    /// \details Creates numThreads VulkanSecondaryCommandBuffer instances, each with
    /// its own VkCommandPool. Called once during View::Render() setup phase.
    bool Initialize(uint32_t numThreads);

    /// \brief Get secondary command buffer for current thread
    /// \param threadIndex Worker thread index (0 to numThreads-1)
    /// \returns VulkanSecondaryCommandBuffer* for the specified thread, nullptr if out of bounds
    /// \details Returns pointer to this thread's buffer for recording. Safe for concurrent
    /// calls from multiple threads (each accesses different buffer). Called by View::DrawBatchQueueParallel().
    VulkanSecondaryCommandBuffer* GetThreadBuffer(uint32_t threadIndex);

    /// \brief Reset all command buffers for new frame
    /// \details Calls Reset() on all buffers to prepare for next frame. This is the
    /// synchronization point called from main thread after all worker threads complete.
    void ResetAllBuffers();

    /// \brief Get all recorded command buffers ready for submission
    /// \returns Vector<VkCommandBuffer> containing all thread buffers (or only recorded ones)
    /// \details Collects all VkCommandBuffer handles from threads and returns as vector
    /// for bulk submission via vkCmdExecuteCommands(). Called by View::SubmitSecondaryCommandBuffers().
    Vector<VkCommandBuffer> GetRecordedCommandBuffers() const;

    /// \brief Get number of threads this pool manages
    /// \returns Number of VulkanSecondaryCommandBuffer instances in the pool
    uint32_t GetNumThreads() const { return buffers_.Size(); }

private:
    /// Vulkan graphics implementation
    VulkanGraphicsImpl* impl_;

    /// Per-thread secondary command buffers
    Vector<SharedPtr<VulkanSecondaryCommandBuffer>> buffers_;
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
