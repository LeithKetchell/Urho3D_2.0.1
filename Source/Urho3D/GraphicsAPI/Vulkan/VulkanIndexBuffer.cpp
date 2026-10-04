//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan index buffer implementation (Phase 4)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "../IndexBuffer.h"
#include "../../Graphics/Graphics.h"
#include "../../IO/Log.h"
#include "VulkanGraphicsImpl.h"

namespace Urho3D
{

void IndexBuffer::OnDeviceLost_Vulkan()
{
    // Vulkan buffers survive device loss (Vulkan device doesn't have context loss like OpenGL)
}

void IndexBuffer::OnDeviceReset_Vulkan()
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

void IndexBuffer::Release_Vulkan()
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

bool IndexBuffer::Create_Vulkan()
{
    Release_Vulkan();

    if (!indexCount_ || !indexSize_)
        return true;

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl || !impl->GetAllocator())
        return false;

    // Calculate buffer size
    VkDeviceSize bufferSize = (VkDeviceSize)indexCount_ * indexSize_;

    // Create Vulkan buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
                URHO3D_LOGERROR("Failed to create Vulkan index buffer");
                return false;
            }
        }
        else
        {
            URHO3D_LOGERROR("Failed to create Vulkan index buffer");
            return false;
        }
    }

    object_.ptr_ = (void*)buffer;
    object_.ptr2_ = (void*)allocation;

    dataPending_ = true;
    return true;
}

bool IndexBuffer::SetData_Vulkan(const void* data)
{
    if (!data)
        return false;

    // Copy to shadow buffer first (unchanged shadowed path: stage into shadow, then full upload)
    if (shadowData_)
    {
        memcpy(shadowData_.Get(), data, (size_t)indexCount_ * indexSize_);
        return UpdateToGPU_Vulkan();
    }

    // No shadow buffer: UpdateToGPU_Vulkan early-returns on null shadow and would upload NOTHING. Fill that
    // gap with a direct FULL upload straight from the caller's data.
    return UploadDataToGPU_Vulkan(data, (size_t)indexCount_ * indexSize_);
}

bool IndexBuffer::SetDataRange_Vulkan(const void* data, i32 start, i32 count, bool discard)
{
    if (!data || start < 0 || count < 0 || start + count > indexCount_)
        return false;

    // Copy to shadow buffer (unchanged shadowed path: stage into shadow, then full upload via UpdateToGPU)
    if (shadowData_)
    {
        memcpy(shadowData_.Get() + start * indexSize_, data, (size_t)count * indexSize_);
        return UpdateToGPU_Vulkan();
    }

    // No shadow buffer: UpdateToGPU_Vulkan early-returns on null shadow and would upload NOTHING. Fill that
    // gap with a direct RANGED upload of just [start, start+count) at offset start*stride.
    return UploadDataToGPU_Vulkan(data, (size_t)count * indexSize_, (size_t)start * indexSize_);
}

bool IndexBuffer::UploadDataToGPU_Vulkan(const void* data, size_t dataSize, size_t dataOffset)
{
    // Direct upload for non-shadowed buffers: map the persistent allocation, memcpy dataSize bytes at
    // dataOffset, flush only that sub-range. Mirrors Unlock_Vulkan's no-shadow scratch path and the
    // VertexBuffer equivalent. Callers: SetData_Vulkan (offset 0, full) and SetDataRange_Vulkan (ranged).
    if (!data || !object_.ptr_)
        return false;

    Graphics* graphics = GetSubsystem<Graphics>();
    VulkanGraphicsImpl* impl = graphics ? graphics->GetImpl_Vulkan() : nullptr;
    if (!impl)
        return false;

    VmaAllocation allocation = (VmaAllocation)object_.ptr2_;
    void* mappedData;
    if (vmaMapMemory(impl->GetAllocator(), allocation, &mappedData) == VK_SUCCESS)
    {
        memcpy((byte*)mappedData + dataOffset, data, dataSize);
        // CRITICAL: Flush memory to ensure GPU visibility on non-coherent memory
        vmaFlushAllocation(impl->GetAllocator(), allocation, dataOffset, dataSize);
        vmaUnmapMemory(impl->GetAllocator(), allocation);
        dataPending_ = false;
        return true;
    }

    URHO3D_LOGERROR("Failed to map index buffer memory for upload");
    return false;
}

bool IndexBuffer::UpdateToGPU_Vulkan()
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

    size_t dataSize = (size_t)indexCount_ * indexSize_;

    // Use host memory mapping for all buffers
    void* mappedData;
    if (vmaMapMemory(impl->GetAllocator(), allocation, &mappedData) == VK_SUCCESS)
    {
        memcpy(mappedData, shadowData_.Get(), dataSize);
        // CRITICAL: Flush memory to ensure GPU visibility on non-coherent memory
        vmaFlushAllocation(impl->GetAllocator(), allocation, 0, dataSize);
        vmaUnmapMemory(impl->GetAllocator(), allocation);
    }

    dataPending_ = false;
    return true;
}

void* IndexBuffer::Lock_Vulkan(i32 start, i32 count, bool discard)
{
    if (start < 0 || count < 0 || start + count > indexCount_)
        return nullptr;

    lockStart_ = start;
    lockCount_ = count;

    // For shadow buffers, lock into shadow memory
    if (shadowData_)
    {
        lockState_ = LOCK_SHADOW;
        dataPending_ = true;
        return shadowData_.Get() + start * indexSize_;
    }

    // Fallback: allocate scratch memory
    lockScratchData_ = new byte[count * indexSize_];
    lockState_ = LOCK_SCRATCH;
    return lockScratchData_;
}

void IndexBuffer::Unlock_Vulkan()
{
    if (lockState_ == LOCK_NONE)
        return;

    if (lockState_ == LOCK_SCRATCH && lockScratchData_)
    {
        // Copy scratch data back to shadow buffer if available
        if (shadowData_)
        {
            memcpy(shadowData_.Get() + lockStart_ * indexSize_, lockScratchData_, lockCount_ * indexSize_);
        }
        else
        {
            // No shadow buffer - upload scratch data directly to GPU
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
                        size_t uploadSize = (size_t)lockCount_ * indexSize_;
                        size_t uploadOffset = (size_t)lockStart_ * indexSize_;
                        memcpy((byte*)mappedData + uploadOffset, lockScratchData_, uploadSize);
                        vmaFlushAllocation(impl->GetAllocator(), allocation, uploadOffset, uploadSize);
                        vmaUnmapMemory(impl->GetAllocator(), allocation);

                    }
                    else
                    {
                        URHO3D_LOGERROR("Failed to map index buffer scratch memory");
                    }
                }
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

void* IndexBuffer::MapBuffer_Vulkan(i32 start, i32 count, bool discard)
{
    if (start < 0 || count < 0 || start + count > indexCount_)
        return nullptr;

    // Vulkan doesn't support direct GPU memory mapping in the way OpenGL does
    // Fall back to shadow/scratch memory
    return Lock_Vulkan(start, count, discard);
}

void IndexBuffer::UnmapBuffer_Vulkan()
{
    Unlock_Vulkan();
}


} // namespace Urho3D

#endif  // URHO3D_VULKAN
