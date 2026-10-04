//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan vertex buffer implementation (Phase 4)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "../VertexBuffer.h"
#include "../../Graphics/Graphics.h"
#include "../../IO/Log.h"
#include "VulkanGraphicsImpl.h"

namespace Urho3D
{

void VertexBuffer::OnDeviceLost_Vulkan()
{
    // Vulkan buffers survive device loss (Vulkan device doesn't have context loss like OpenGL)
    // However, mark data as pending for recreation if needed
}

void VertexBuffer::OnDeviceReset_Vulkan()
{
    if (!object_.name_)
    {
        Create_Vulkan();
        dataLost_ = !UpdateToGPU_Vulkan();
    }
    else if (dataPending_)
        dataLost_ = !UpdateToGPU_Vulkan();

    dataPending_ = false;
}

void VertexBuffer::Release_Vulkan()
{
    if (!object_.ptr_)
        return;

    Graphics* graphics = GetSubsystem<Graphics>();
    VulkanGraphicsImpl* impl = graphics ? graphics->GetImpl_Vulkan() : nullptr;

    VkBuffer buffer = (VkBuffer)(void*)object_.ptr_;
    VmaAllocation allocation = (VmaAllocation)object_.ptr2_;

    // Always clear handles, even if we can't destroy (shutdown order)
    object_.ptr_ = nullptr;
    object_.ptr2_ = nullptr;
    dataPending_ = false;

    // Defer deletion until GPU is done using this buffer
    if (impl && buffer)
    {
        impl->DeferBufferDeletion(buffer, allocation);
    }
}

bool VertexBuffer::Create_Vulkan()
{
    Release_Vulkan();

    if (!vertexCount_ || !vertexSize_)
        return true;

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    // Calculate buffer size
    VkDeviceSize bufferSize = (VkDeviceSize)vertexCount_ * vertexSize_;

    // Create Vulkan buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    // ALWAYS need HOST_VISIBLE for CPU mapping (static buffers are mapped too)
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    // Attempt to use memory pool for optimized allocation
    VulkanMemoryPoolManager* poolMgr = impl->GetMemoryPoolManager();
    if (poolMgr)
    {
        VulkanMemoryPoolType poolType = dynamic_ ? VulkanMemoryPoolType::DynamicGeometry : VulkanMemoryPoolType::StaticGeometry;
        VmaPool pool = poolMgr->GetPool(poolType);
        if (pool)
        {
            allocInfo.pool = pool;
        }
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    if (vmaCreateBuffer(impl->GetAllocator(), &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS)
    {
        // Fallback: attempt without pool if pool allocation failed
        if (poolMgr)
        {
            allocInfo.pool = nullptr;
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            if (vmaCreateBuffer(impl->GetAllocator(), &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS)
            {
                URHO3D_LOGERROR("Failed to create Vulkan vertex buffer");
                return false;
            }
        }
        else
        {
            URHO3D_LOGERROR("Failed to create Vulkan vertex buffer");
            return false;
        }
    }

    object_.ptr_ = (void*)buffer;
    object_.ptr2_ = (void*)allocation;

    dataPending_ = true;
    return true;
}

bool VertexBuffer::SetData_Vulkan(const void* data)
{
    if (!data)
        return false;

    // Copy to shadow buffer first
    if (shadowData_)
        memcpy(shadowData_.Get(), data, (size_t)vertexCount_ * vertexSize_);

    // If no shadow buffer, upload data directly to GPU
    if (!shadowData_)
    {
        return UploadDataToGPU_Vulkan(data, vertexCount_ * vertexSize_);
    }

    return UpdateToGPU_Vulkan();
}

bool VertexBuffer::SetDataRange_Vulkan(const void* data, i32 start, i32 count, bool discard)
{
    if (!data || start < 0 || count < 0 || start + count > vertexCount_)
        return false;

    // Copy to shadow buffer (unchanged shadowed path: stage into shadow, then full upload via UpdateToGPU)
    if (shadowData_)
    {
        memcpy(shadowData_.Get() + start * vertexSize_, data, (size_t)count * vertexSize_);
        return UpdateToGPU_Vulkan();
    }

    // No shadow buffer: UpdateToGPU_Vulkan early-returns on null shadow and would upload NOTHING. Fill that
    // gap with a direct RANGED upload of just [start, start+count) — map the persistent allocation, write
    // count*stride bytes at offset start*stride, and flush only that sub-range. Mirrors Unlock_Vulkan's
    // no-shadow scratch path; deliberately NOT a full-buffer write (that would read past `data`).
    if (!object_.ptr_)
        return false;

    Graphics* graphics = GetSubsystem<Graphics>();
    VulkanGraphicsImpl* impl = graphics ? graphics->GetImpl_Vulkan() : nullptr;
    if (!impl)
        return false;

    VmaAllocation allocation = (VmaAllocation)object_.ptr2_;
    void* mappedData;
    if (vmaMapMemory(impl->GetAllocator(), allocation, &mappedData) == VK_SUCCESS)
    {
        const size_t uploadOffset = (size_t)start * vertexSize_;
        const size_t uploadSize = (size_t)count * vertexSize_;
        memcpy((byte*)mappedData + uploadOffset, data, uploadSize);
        vmaFlushAllocation(impl->GetAllocator(), allocation, uploadOffset, uploadSize);
        vmaUnmapMemory(impl->GetAllocator(), allocation);
        return true;
    }

    URHO3D_LOGERROR("Failed to map vertex buffer memory for ranged upload");
    return false;
}

bool VertexBuffer::UploadDataToGPU_Vulkan(const void* data, size_t dataSize)
{
    if (!data || !object_.ptr_)
        return false;

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    VkBuffer buffer = (VkBuffer)(void*)object_.ptr_;
    VmaAllocation allocation = (VmaAllocation)object_.ptr2_;

    // Map GPU memory and copy data directly
    void* mappedData;
    if (vmaMapMemory(impl->GetAllocator(), allocation, &mappedData) == VK_SUCCESS)
    {
        memcpy(mappedData, data, dataSize);
        // CRITICAL: Flush memory to ensure GPU visibility on non-coherent memory
        vmaFlushAllocation(impl->GetAllocator(), allocation, 0, dataSize);
        vmaUnmapMemory(impl->GetAllocator(), allocation);
        dataPending_ = false;
        return true;
    }

    URHO3D_LOGERROR("Failed to map vertex buffer memory for upload");
    return false;
}

bool VertexBuffer::GetData_Vulkan(void* destData)
{
    if (!destData || !object_.ptr_)
        return false;

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    VmaAllocation allocation = (VmaAllocation)object_.ptr2_;
    size_t dataSize = (size_t)vertexCount_ * vertexSize_;

    void* mappedData;
    if (vmaMapMemory(impl->GetAllocator(), allocation, &mappedData) == VK_SUCCESS)
    {
        // Invalidate to ensure CPU sees GPU writes (non-coherent memory)
        vmaInvalidateAllocation(impl->GetAllocator(), allocation, 0, dataSize);
        memcpy(destData, mappedData, dataSize);
        vmaUnmapMemory(impl->GetAllocator(), allocation);
        return true;
    }

    URHO3D_LOGERROR("Failed to map vertex buffer memory for readback");
    return false;
}

bool VertexBuffer::UpdateToGPU_Vulkan()
{
    if (!shadowData_ || !object_.ptr_)
        return false;

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    VkBuffer buffer = (VkBuffer)(void*)object_.ptr_;
    VmaAllocation allocation = (VmaAllocation)object_.ptr2_;

    size_t dataSize = (size_t)vertexCount_ * vertexSize_;

    // For dynamic buffers or small updates, use host memory mapping
    void* mappedData;
    VkResult mapResult = vmaMapMemory(impl->GetAllocator(), allocation, &mappedData);
    if (mapResult == VK_SUCCESS)
    {
        memcpy(mappedData, shadowData_.Get(), dataSize);
        // CRITICAL: Flush memory to ensure GPU visibility on non-coherent memory
        vmaFlushAllocation(impl->GetAllocator(), allocation, 0, dataSize);
        vmaUnmapMemory(impl->GetAllocator(), allocation);
    }

    dataPending_ = false;
    return true;
}

void* VertexBuffer::Lock_Vulkan(i32 start, i32 count, bool discard)
{
    if (start < 0 || count < 0 || start + count > vertexCount_)
        return nullptr;

    lockStart_ = start;
    lockCount_ = count;

    // For shadow buffers, lock into shadow memory
    if (shadowData_)
    {
        lockState_ = LOCK_SHADOW;
        dataPending_ = true;
        return shadowData_.Get() + start * vertexSize_;
    }

    // Fallback: allocate scratch memory
    lockScratchData_ = new byte[count * vertexSize_];
    lockState_ = LOCK_SCRATCH;
    return lockScratchData_;
}

void VertexBuffer::Unlock_Vulkan()
{
    if (lockState_ == LOCK_NONE)
        return;


    if (lockState_ == LOCK_SCRATCH && lockScratchData_)
    {

        // Copy scratch data back to shadow buffer if available
        if (shadowData_)
        {
            memcpy(shadowData_.Get() + lockStart_ * vertexSize_, lockScratchData_, lockCount_ * vertexSize_);
        }
        else
        {
            // No shadow buffer - upload scratch data directly to GPU
            // This is needed for instancing buffers which don't use shadow data
            if (object_.ptr_)
            {
                Graphics* graphics = GetSubsystem<Graphics>();
                VulkanGraphicsImpl* impl = graphics ? graphics->GetImpl_Vulkan() : nullptr;
                if (impl)
                {
                    VmaAllocation allocation = (VmaAllocation)object_.ptr2_;
                    void* mappedData;
                    VkResult mapResult = vmaMapMemory(impl->GetAllocator(), allocation, &mappedData);
                    if (mapResult == VK_SUCCESS)
                    {
                        // Copy to the correct offset in the GPU buffer
                        size_t uploadSize = lockCount_ * vertexSize_;
                        size_t uploadOffset = lockStart_ * vertexSize_;
                        memcpy((byte*)mappedData + uploadOffset, lockScratchData_, uploadSize);

                        // CRITICAL: Flush memory to ensure GPU visibility on non-coherent memory
                        vmaFlushAllocation(impl->GetAllocator(), allocation, uploadOffset, uploadSize);

                        vmaUnmapMemory(impl->GetAllocator(), allocation);

                    }
                    else
                    {
                        URHO3D_LOGERROR("Failed to map vertex buffer scratch memory");
                    }
                }
            }
            else
            {
                URHO3D_LOGERROR("No GPU object for vertex buffer scratch upload");
            }
        }

        delete[](byte*)lockScratchData_;
        lockScratchData_ = nullptr;
    }

    lockState_ = LOCK_NONE;
    dataPending_ = true;

    // Upload to GPU (for shadow buffer path)
    UpdateToGPU_Vulkan();
}

void* VertexBuffer::MapBuffer_Vulkan(i32 start, i32 count, bool discard)
{
    if (start < 0 || count < 0 || start + count > vertexCount_)
        return nullptr;

    // Vulkan doesn't support direct GPU memory mapping in the way OpenGL does
    // Fall back to shadow/scratch memory
    return Lock_Vulkan(start, count, discard);
}

void VertexBuffer::UnmapBuffer_Vulkan()
{
    Unlock_Vulkan();
}


} // namespace Urho3D

#endif  // URHO3D_VULKAN
