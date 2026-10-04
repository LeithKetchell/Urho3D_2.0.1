//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan 2D texture implementation (Phase 5)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "../Texture2D.h"
#include "../TextureCube.h"
#include "../../Graphics/Graphics.h"
#include "../../IO/Log.h"
#include "../../Resource/Image.h"
#include "../../Resource/ResourceCache.h"
#include "../../Container/Vector.h"
#include "VulkanGraphicsImpl.h"
#include "VulkanStagingBufferManager.h"

namespace Urho3D
{

// Helper function to map Urho3D texture format to Vulkan format
// WORKAROUND: Uses placeholder format values until Graphics::Get*Format() is implemented for Vulkan
static VkFormat GetVulkanFormat(unsigned urhoFormat)
{
    // urhoFormat is already a VkFormat enum value (not a placeholder)
    // SetData_Vulkan now assigns actual VkFormat values to format_
    return (VkFormat)urhoFormat;
}

void Texture2D::OnDeviceLost_Vulkan()
{
    // Vulkan textures survive device loss
}

void Texture2D::OnDeviceReset_Vulkan()
{
    if (!object_.ptr_ || dataPending_)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        if (cache && cache->Exists(GetName()))
            dataLost_ = !cache->ReloadResource(this);

        if (!object_.ptr_)
        {
            Create_Vulkan();
            dataLost_ = true;
        }
    }

    dataPending_ = false;
}

void Texture2D::Release_Vulkan()
{
    if (!object_.ptr_ && !shaderResourceView_ && !sampler_)
        return;

    Graphics* graphics = GetSubsystem<Graphics>();

    // Clear any Graphics texture slots referencing this texture (matches D3D11 pattern)
    if (graphics)
    {
        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        {
            if (graphics->GetTexture(i) == this)
                graphics->SetTexture(i, nullptr);
        }
    }

    VulkanGraphicsImpl* impl = graphics ? graphics->GetImpl_Vulkan() : nullptr;
    VkDevice device = (impl && impl->GetDevice()) ? impl->GetDevice() : VK_NULL_HANDLE;
    VmaAllocator allocator = (impl && impl->GetAllocator()) ? impl->GetAllocator() : VK_NULL_HANDLE;

    // Release MSAA resources
    if (msaaImageView_)
    {
        if (device)
            vkDestroyImageView(device, (VkImageView)msaaImageView_, nullptr);
        msaaImageView_ = nullptr;
    }
    if (msaaImage_)
    {
        if (allocator)
            vmaDestroyImage(allocator, (VkImage)msaaImage_, (VmaAllocation)msaaAllocation_);
        msaaImage_ = nullptr;
        msaaAllocation_ = nullptr;
    }

    // Release image view
    if (shaderResourceView_)
    {
        if (device)
            vkDestroyImageView(device, (VkImageView)shaderResourceView_, nullptr);
        shaderResourceView_ = nullptr;
    }

    // Release sampler
    if (sampler_)
    {
        if (device)
            vkDestroySampler(device, (VkSampler)sampler_, nullptr);
        sampler_ = nullptr;
    }

    // Release image
    if (object_.ptr_)
    {
        if (allocator)
        {
            VkImage image = (VkImage)(void*)object_.ptr_;
            VmaAllocation allocation = (VmaAllocation)vmaAllocation_;
            vmaDestroyImage(allocator, image, allocation);
        }
        object_.ptr_ = nullptr;
        object_.name_ = 0;
    }
}

bool Texture2D::Create_Vulkan()
{
    Release_Vulkan();

    if (!width_ || !height_)
        return false;

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    // Calculate mipmap levels
    // For render targets, force 1 mip level since we don't regenerate mipmaps after rendering
    // (OpenGL uses glGenerateMipmap; Vulkan equivalent vkCmdBlitImage not yet implemented)
    if (usage_ == TEXTURE_RENDERTARGET || usage_ == TEXTURE_DEPTHSTENCIL)
        levels_ = 1;
    else
        levels_ = CheckMaxLevels(width_, height_, requestedLevels_);

    // Map Urho3D format to Vulkan format
    VkFormat vkFormat = GetVulkanFormat(format_);
    URHO3D_LOGDEBUG("Create_Vulkan: format_=" + String(format_) + ", vkFormat=" + String((unsigned)vkFormat));

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = vkFormat;
    imageInfo.extent = {(uint32_t)width_, (uint32_t)height_, 1};
    imageInfo.mipLevels = levels_;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage_ == TEXTURE_RENDERTARGET)
        imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    else if (usage_ == TEXTURE_DEPTHSTENCIL)
        imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    // Attempt to use memory pool for optimized allocation
    VulkanMemoryPoolManager* poolMgr = impl->GetMemoryPoolManager();
    if (poolMgr)
    {
        VmaPool pool = poolMgr->GetPool(VulkanMemoryPoolType::Textures);
        if (pool)
        {
            allocInfo.pool = pool;
        }
    }

    VkImage image;
    VmaAllocation allocation;

    // Log VMA stats before allocation
    VmaTotalStatistics stats;
    vmaCalculateStatistics(impl->GetAllocator(), &stats);
    URHO3D_LOGDEBUG(String("VMA BEFORE Create: allocations=") + String(stats.total.statistics.allocationCount) +
                   ", blocks=" + String(stats.total.statistics.blockCount) +
                   ", used=" + String((unsigned long long)stats.total.statistics.allocationBytes / 1024) + "KB");

    URHO3D_LOGDEBUG(String("About to call vmaCreateImage: size=") + String(width_) + "x" + String(height_) +
                   ", mips=" + String(levels_) + ", format=" + String((unsigned)vkFormat));

    VkResult createResult = vmaCreateImage(impl->GetAllocator(), &imageInfo, &allocInfo, &image, &allocation, nullptr);

    URHO3D_LOGDEBUG(String("vmaCreateImage returned, VkResult=") + String((int)createResult));

    if (createResult != VK_SUCCESS)
    {
        // Fallback: attempt without pool if pool allocation failed
        if (poolMgr)
        {
            allocInfo.pool = nullptr;
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            if (vmaCreateImage(impl->GetAllocator(), &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS)
            {
                URHO3D_LOGERROR("Failed to create Vulkan texture image");
                return false;
            }
        }
        else
        {
            URHO3D_LOGERROR("Failed to create Vulkan texture image");
            return false;
        }
    }

    // Log VMA stats after allocation
    vmaCalculateStatistics(impl->GetAllocator(), &stats);
    URHO3D_LOGDEBUG(String("VMA AFTER Create: allocations=") + String(stats.total.statistics.allocationCount) +
                   ", blocks=" + String(stats.total.statistics.blockCount) +
                   ", used=" + String((unsigned long long)stats.total.statistics.allocationBytes / 1024) + "KB");

    object_.ptr_ = (void*)image;
    // NOTE: Cannot use object_.name_ for Vulkan! It's a union that shares memory with ptr_!
    // Store VMA allocation in dedicated member variable
    vmaAllocation_ = allocation;

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = vkFormat;

    // Component swizzle for single-channel textures (fonts/alpha maps)
    // R8 format: Map red channel to all RGB channels for grayscale, and to alpha for font rendering
    if (vkFormat == VK_FORMAT_R8_UNORM || vkFormat == VK_FORMAT_R8_SRGB)
    {
        // For single-channel textures, replicate R to all channels: (R,R,R,R)
        // This makes grayscale textures work correctly and fonts use alpha from red channel
        viewInfo.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R,
                                VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R};
    }
    else
    {
        // For multi-channel textures, use identity mapping
        viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    }
    viewInfo.subresourceRange.aspectMask = (usage_ == TEXTURE_DEPTHSTENCIL)
        ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = levels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(impl->GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create Vulkan image view");
        vmaDestroyImage(impl->GetAllocator(), image, allocation);
        object_.ptr_ = nullptr;
        return false;
    }

    shaderResourceView_ = imageView;

    // Note: Image layout transitions are handled in SetData_Vulkan during upload
    // Initial layout is VK_IMAGE_LAYOUT_UNDEFINED as specified in imageInfo.initialLayout

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    // Map filter mode (handle FILTER_DEFAULT by using graphics default)
    TextureFilterMode effectiveFilterMode = filterMode_;
    if (effectiveFilterMode == FILTER_DEFAULT)
        effectiveFilterMode = graphics_->GetDefaultTextureFilterMode();

    URHO3D_LOGDEBUG("Create_Vulkan sampler: filterMode_=" + String((int)filterMode_) +
                    " effective=" + String((int)effectiveFilterMode));

    switch (effectiveFilterMode)
    {
    case FILTER_NEAREST:
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        break;
    case FILTER_BILINEAR:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        break;
    case FILTER_TRILINEAR:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        break;
    case FILTER_ANISOTROPIC:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        {
            unsigned maxAniso = anisotropy_ ? anisotropy_ : graphics_->GetDefaultTextureAnisotropy();
            samplerInfo.anisotropyEnable = VK_TRUE;
            samplerInfo.maxAnisotropy = static_cast<float>(maxAniso);
        }
        break;
    case FILTER_NEAREST_ANISOTROPIC:
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        {
            unsigned maxAniso = anisotropy_ ? anisotropy_ : graphics_->GetDefaultTextureAnisotropy();
            samplerInfo.anisotropyEnable = VK_TRUE;
            samplerInfo.maxAnisotropy = static_cast<float>(maxAniso);
        }
        break;
    default:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
    }

    // Map address modes (must match order of TextureAddressMode enum)
    // ADDRESS_WRAP=0, ADDRESS_MIRROR=1, ADDRESS_CLAMP=2, ADDRESS_BORDER=3
    VkSamplerAddressMode addressModes[] = {
        VK_SAMPLER_ADDRESS_MODE_REPEAT,          // ADDRESS_WRAP = 0
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, // ADDRESS_MIRROR = 1
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,   // ADDRESS_CLAMP = 2
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER  // ADDRESS_BORDER = 3
    };

    // Clamp address mode indices to valid range
    unsigned uMode = addressModes_[COORD_U] < MAX_ADDRESSMODES ? addressModes_[COORD_U] : ADDRESS_WRAP;
    unsigned vMode = addressModes_[COORD_V] < MAX_ADDRESSMODES ? addressModes_[COORD_V] : ADDRESS_WRAP;

    samplerInfo.addressModeU = addressModes[uMode];
    samplerInfo.addressModeV = addressModes[vMode];
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.compareEnable = shadowCompare_ ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = shadowCompare_ ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_NEVER;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = (float)levels_;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkSampler vkSampler;
    if (vkCreateSampler(impl->GetDevice(), &samplerInfo, nullptr, &vkSampler) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create Vulkan sampler");
        vkDestroyImageView(impl->GetDevice(), imageView, nullptr);
        vmaDestroyImage(impl->GetAllocator(), image, allocation);
        object_.ptr_ = nullptr;
        return false;
    }

    sampler_ = vkSampler;
    dataPending_ = true;

    // RTT: Create framebuffer attachment view
    if (renderSurface_ && (usage_ == TEXTURE_RENDERTARGET || usage_ == TEXTURE_DEPTHSTENCIL))
    {
        // Create MSAA image for multisampled render targets
        if (multiSample_ > 1 && usage_ == TEXTURE_RENDERTARGET)
        {
            VkImageCreateInfo msaaInfo{};
            msaaInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            msaaInfo.imageType = VK_IMAGE_TYPE_2D;
            msaaInfo.format = vkFormat;
            msaaInfo.extent = {(uint32_t)width_, (uint32_t)height_, 1};
            msaaInfo.mipLevels = 1;
            msaaInfo.arrayLayers = 1;
            msaaInfo.samples = (VkSampleCountFlagBits)multiSample_;
            msaaInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            msaaInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            msaaInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            msaaInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VmaAllocationCreateInfo msaaAllocInfo{};
            msaaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

            VkImage msaaImg;
            VmaAllocation msaaAlloc;
            if (vmaCreateImage(impl->GetAllocator(), &msaaInfo, &msaaAllocInfo, &msaaImg, &msaaAlloc, nullptr) == VK_SUCCESS)
            {
                msaaImage_ = (void*)msaaImg;
                msaaAllocation_ = msaaAlloc;

                // Create image view for MSAA image
                VkImageViewCreateInfo msaaViewInfo{};
                msaaViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                msaaViewInfo.image = msaaImg;
                msaaViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                msaaViewInfo.format = vkFormat;
                msaaViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                msaaViewInfo.subresourceRange.baseMipLevel = 0;
                msaaViewInfo.subresourceRange.levelCount = 1;
                msaaViewInfo.subresourceRange.baseArrayLayer = 0;
                msaaViewInfo.subresourceRange.layerCount = 1;

                VkImageView msaaView;
                if (vkCreateImageView(impl->GetDevice(), &msaaViewInfo, nullptr, &msaaView) == VK_SUCCESS)
                {
                    msaaImageView_ = msaaView;
                    // Render into MSAA image; resolve to single-sample for shader reading
                    renderSurface_->renderTargetView_ = msaaView;
                }
                else
                {
                    URHO3D_LOGWARNING("Failed to create MSAA image view, falling back to single-sample");
                    vmaDestroyImage(impl->GetAllocator(), msaaImg, msaaAlloc);
                    msaaImage_ = nullptr;
                    msaaAllocation_ = nullptr;
                    renderSurface_->renderTargetView_ = imageView;
                }
            }
            else
            {
                URHO3D_LOGWARNING("Failed to create MSAA image, falling back to single-sample");
                renderSurface_->renderTargetView_ = imageView;
            }
        }
        else if (levels_ == 1)
        {
            renderSurface_->renderTargetView_ = imageView;
        }
        else
        {
            VkImageViewCreateInfo rtViewInfo{};
            rtViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            rtViewInfo.image = image;
            rtViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            rtViewInfo.format = vkFormat;
            rtViewInfo.subresourceRange.aspectMask = (usage_ == TEXTURE_DEPTHSTENCIL)
                ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            rtViewInfo.subresourceRange.baseMipLevel = 0;
            rtViewInfo.subresourceRange.levelCount = 1;
            rtViewInfo.subresourceRange.baseArrayLayer = 0;
            rtViewInfo.subresourceRange.layerCount = 1;

            VkImageView rtView;
            if (vkCreateImageView(impl->GetDevice(), &rtViewInfo, nullptr, &rtView) == VK_SUCCESS)
                renderSurface_->renderTargetView_ = rtView;
            else
                renderSurface_->renderTargetView_ = imageView;  // Fallback
        }
    }

    return true;
}

bool Texture2D::SetData_Vulkan(unsigned level, int x, int y, int width, int height, const void* data)
{

    if (!data)
        return false;

    // If texture not created yet, just mark data as pending
    // This can happen when SetData is called before SetSize/Create
    if (levels_ == 0 || width_ == 0 || height_ == 0)
    {
        dataPending_ = true;
        return true;
    }

    if (level >= levels_)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - invalid mipmap level " + String(level) + " (max: " + String(levels_ - 1) + ")");
        return false;
    }

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    VulkanStagingBufferManager* stagingMgr = impl->GetStagingBufferManager();
    if (!stagingMgr)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - staging buffer manager not available");
        return false;
    }

    // Calculate data size for the region
    unsigned dataSize = GetDataSize(width, height);
    if (dataSize == 0)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - invalid data size (format=" + String(format_) +
                       ", width=" + String(width_) + ", height=" + String(height_) + ")");
        return false;
    }

    VkImage image = (VkImage)(void*)object_.ptr_;
    if (!image)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - image not created yet");
        return false;
    }

    // Begin upload command buffer for transfer commands
    VkCommandBuffer commandBuffer = impl->BeginUploadCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - failed to begin upload command buffer");
        return false;
    }

    // Transition image layout: UNDEFINED -> TRANSFER_DST_OPTIMAL
    // Note: Using UNDEFINED as source means we don't care about previous contents,
    // which is correct for texture uploads (we're overwriting the data anyway)
    VkFormat vkFormat = GetVulkanFormat(format_);
    impl->TransitionImageLayout(commandBuffer, image, vkFormat,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1);

    // Create staging buffer and copy data
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;  // CRITICAL: Initialize to avoid garbage
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;  // CRITICAL: Initialize to avoid garbage
    VmaAllocationInfo allocationInfo{};

    if (vmaCreateBuffer(impl->GetAllocator(), &bufferInfo, &allocInfo,
                       &stagingBuffer, &stagingAllocation, &allocationInfo) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - failed to create staging buffer");
        return false;
    }

    // Copy texture data to staging buffer
    memcpy(allocationInfo.pMappedData, data, dataSize);

    // DEBUG: Print first few pixels of RGBA textures to verify color data
    // Debug: pixel data logging removed

    // Record buffer-to-image copy command
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // Tightly packed
    region.bufferImageHeight = 0;  // Tightly packed
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = level;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {x, y, 0};
    region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition image layout: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    impl->TransitionImageLayout(commandBuffer, image, vkFormat,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               1);

    // End and submit upload command buffer (waits for completion)
    impl->EndUploadCommandBuffer(commandBuffer);

    // Safe to destroy staging buffer now since upload command has completed
    vmaDestroyBuffer(impl->GetAllocator(), stagingBuffer, stagingAllocation);

    dataPending_ = false;
    return true;
}

bool Texture2D::SetData_Vulkan(Image* image, bool useAlpha)
{
    if (!image)
        return false;

    // Get image dimensions and validate
    int width = image->GetWidth();
    int height = image->GetHeight();
    unsigned components = image->GetComponents();

    if (width <= 0 || height <= 0 || components == 0)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - invalid image dimensions");
        return false;
    }

    // Determine format based on image components
    unsigned format = 0;
    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    if (!image->IsCompressed())
    {
        switch (components)
        {
        case 1:
            format = useAlpha ? Graphics::GetAlphaFormat() : Graphics::GetLuminanceFormat();
            break;
        case 2:
            format = Graphics::GetLuminanceAlphaFormat();
            break;
        case 3:
            format = Graphics::GetRGBFormat();
            break;
        case 4:
            format = Graphics::GetRGBAFormat();
            break;
        default:
            URHO3D_LOGERROR("Texture2D::SetData_Vulkan - unsupported component count");
            return false;
        }
    }
    else
    {
        format = graphics->GetFormat(image->GetCompressedFormat());
        if (!format)
        {
            URHO3D_LOGERROR("Texture2D::SetData_Vulkan - unsupported compressed format");
            return false;
        }
    }

    // Log the determined format
    URHO3D_LOGDEBUG("SetData_Vulkan: determined format=" + String(format) + " from " + String(components) + " components");

    // WORKAROUND: Graphics::Get*Format() methods not yet implemented for Vulkan
    // Use actual VkFormat values based on component count (NOT placeholders!)
    // GetRowDataSize_Vulkan() expects real VkFormat enums to calculate byte sizes correctly
    // Using UNORM formats instead of SRGB for better driver compatibility
    if (format == 0)
    {
        switch (components)
        {
        case 1:
            format = VK_FORMAT_R8_UNORM; // R8 UNORM (value=9)
            break;
        case 2:
            format = VK_FORMAT_R8G8_UNORM; // RG8 UNORM (value=16)
            break;
        case 3:
            format = VK_FORMAT_R8G8B8_UNORM; // RGB8 UNORM (value=23)
            break;
        case 4:
            format = VK_FORMAT_R8G8B8A8_UNORM; // RGBA8 UNORM (value=37)
            break;
        }
        URHO3D_LOGDEBUG("SetData_Vulkan: using VkFormat=" + String(format) + " for " + String(components) + " components (Graphics::Get*Format not implemented for Vulkan)");
    }

    // For compressed textures, limit mip levels to what's actually in the file
    if (image->IsCompressed())
    {
        unsigned numCompressedLevels = image->GetNumCompressedLevels();
        if (numCompressedLevels > 0)
        {
            requestedLevels_ = numCompressedLevels;
            URHO3D_LOGDEBUG("SetData_Vulkan: Compressed texture, setting requestedLevels_=" + String(numCompressedLevels));
        }
    }

    // Set texture size if not already set (this sets format_)
    if (!width_ || !height_)
    {
        // Use SetSize to properly initialize all texture parameters including format_
        if (!SetSize(width, height, format))
        {
            URHO3D_LOGERROR("Texture2D::SetData_Vulkan - SetSize failed");
            return false;
        }
    }
    else
    {
        // Texture already sized (Create_Vulkan was called), but we still need to set the format
        URHO3D_LOGDEBUG("SetData_Vulkan: texture already sized, setting format_=" + String(format));
        format_ = format;
    }

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    VkImage vkImage = (VkImage)(void*)object_.ptr_;
    if (!vkImage)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - image not created");
        return false;
    }

    URHO3D_LOGDEBUG("SetData_Vulkan: image=" + String((unsigned long long)(uintptr_t)vkImage) +
                   ", size=" + String(width) + "x" + String(height) +
                   ", levels=" + String(levels_) +
                   ", format=" + String((unsigned)format_));

    URHO3D_LOGDEBUG("SetData_Vulkan: About to get image data");

    // Vulkan doesn't support 3-component (RGB) texture formats on most hardware.
    // GetRGBFormat_Vulkan() returns VK_FORMAT_R8G8B8A8_UNORM, so the VkImage is RGBA.
    // If the source image has fewer than 4 components, convert it to RGBA so the data
    // matches the texture format. Without this, memcpy reads past the source buffer.
    SharedPtr<Image> convertedImage;
    if (!image->IsCompressed() && components == 3)
    {
        URHO3D_LOGDEBUG("SetData_Vulkan: Converting " + String(components) + "-component image to RGBA");
        convertedImage = image->ConvertToRGBA();
        if (convertedImage)
            image = convertedImage;
        else
            URHO3D_LOGWARNING("SetData_Vulkan: ConvertToRGBA failed, proceeding with original data");
    }

    // Get image data
    unsigned char* data = image->GetData();
    if (!data)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - no image data");
        return false;
    }

    URHO3D_LOGDEBUG("SetData_Vulkan: Got image data, calculating sizes");

    // Calculate total data size for all mip levels
    unsigned totalSize = 0;
    int levelWidth = width;
    int levelHeight = height;

    for (unsigned level = 0; level < levels_; ++level)
    {
        totalSize += GetDataSize(levelWidth, levelHeight);
        levelWidth = Max(levelWidth >> 1, 1);
        levelHeight = Max(levelHeight >> 1, 1);
    }

    URHO3D_LOGDEBUG("SetData_Vulkan: Calculated totalSize=" + String(totalSize) + ", beginning upload command buffer");

    // Begin upload command buffer for transfer commands
    VkCommandBuffer commandBuffer = impl->BeginUploadCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE)
    {
        URHO3D_LOGERROR("Texture2D::SetData_Vulkan - failed to begin upload command buffer");
        return false;
    }

    URHO3D_LOGDEBUG("SetData_Vulkan: Command buffer created, creating staging buffer");

    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;  // CRITICAL: Initialize to avoid garbage
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;  // CRITICAL: Initialize to avoid garbage
    VmaAllocationInfo allocationInfo{};

    URHO3D_LOGDEBUG(String("SetData_Vulkan: About to call vmaCreateBuffer, size=") + String(totalSize / 1024) + "KB");

    VkResult result = vmaCreateBuffer(impl->GetAllocator(), &bufferInfo, &allocInfo,
                       &stagingBuffer, &stagingAllocation, &allocationInfo);

    URHO3D_LOGDEBUG(String("SetData_Vulkan: vmaCreateBuffer returned, result=") + String((int)result));

    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR(String("Texture2D::SetData_Vulkan - failed to create staging buffer, VkResult=") + String((int)result));
        return false;
    }

    URHO3D_LOGDEBUG("SetData_Vulkan: Staging buffer created, copying data");

    // Copy all mip levels to staging buffer
    unsigned char* stagingData = (unsigned char*)allocationInfo.pMappedData;
    unsigned offset = 0;
    levelWidth = width;
    levelHeight = height;

    SharedPtr<Image> currentLevel(image);
    unsigned numCompressedLevels = image->GetNumCompressedLevels();
    bool isCompressed = image->IsCompressed();

    URHO3D_LOGDEBUG("SetData_Vulkan: Loading " + String(levels_) + " mip levels, image has " +
                   String(numCompressedLevels) + " compressed levels, isCompressed=" + String(isCompressed));

    for (unsigned level = 0; level < levels_; ++level)
    {
        unsigned levelSize = GetDataSize(levelWidth, levelHeight);

        if (level == 0)
        {
            // Copy base level from image data
            URHO3D_LOGDEBUG("  Level " + String(level) + ": Copying base level, size=" + String(levelSize));
            memcpy(stagingData + offset, data, levelSize);
        }
        else if (numCompressedLevels > level)
        {
            // Use precomputed mip level from compressed image
            CompressedLevel compLevel = image->GetCompressedLevel(level);
            URHO3D_LOGDEBUG("  Level " + String(level) + ": Using compressed level, has_data=" +
                           String(compLevel.data_ != nullptr) + ", size=" + String(levelSize));
            if (compLevel.data_)
                memcpy(stagingData + offset, compLevel.data_, levelSize);
        }
        else if (!isCompressed && currentLevel)
        {
            // Generate mip level by downsampling (only for uncompressed textures)
            URHO3D_LOGDEBUG("  Level " + String(level) + ": Generating mip level from previous");
            currentLevel = currentLevel->GetNextLevel();
            if (currentLevel && currentLevel->GetData())
                memcpy(stagingData + offset, currentLevel->GetData(), levelSize);
        }
        else
        {
            URHO3D_LOGWARNING("  Level " + String(level) + ": No mip data available, leaving blank");
        }

        offset += levelSize;
        levelWidth = Max(levelWidth >> 1, 1);
        levelHeight = Max(levelHeight >> 1, 1);
    }

    // Transition image layout: UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkFormat vkFormat = GetVulkanFormat(format_);
    URHO3D_LOGDEBUG("About to call TransitionImageLayout with levels_=" + String(levels_) + ", format_=" + String(format_) + ", vkFormat=" + String((unsigned)vkFormat));
    impl->TransitionImageLayout(commandBuffer, vkImage, vkFormat,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               levels_);
    URHO3D_LOGDEBUG("TransitionImageLayout returned successfully");

    // Record buffer-to-image copy for all mip levels
    Vector<VkBufferImageCopy> regions;
    offset = 0;
    levelWidth = width;
    levelHeight = height;

    for (unsigned level = 0; level < levels_; ++level)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = level;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {(uint32_t)levelWidth, (uint32_t)levelHeight, 1};

        regions.Push(region);

        offset += GetDataSize(levelWidth, levelHeight);
        levelWidth = Max(levelWidth >> 1, 1);
        levelHeight = Max(levelHeight >> 1, 1);
    }

    URHO3D_LOGDEBUG("About to call vkCmdCopyBufferToImage: cmdBuf=" + String((unsigned long long)commandBuffer) +
                   ", stagingBuf=" + String((unsigned long long)stagingBuffer) +
                   ", image=" + String((unsigned long long)vkImage) +
                   ", regions=" + String(regions.Size()));
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, vkImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          regions.Size(), &regions[0]);
    URHO3D_LOGDEBUG("vkCmdCopyBufferToImage completed successfully");

    // Transition image layout: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    impl->TransitionImageLayout(commandBuffer, vkImage, vkFormat,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               levels_);

    // End and submit upload command buffer (waits for completion)
    impl->EndUploadCommandBuffer(commandBuffer);

    // VMA handles deferred cleanup - won't free until GPU operations complete
    // Note: For async uploads, use VulkanStagingBufferManager with explicit fences
    vmaDestroyBuffer(impl->GetAllocator(), stagingBuffer, stagingAllocation);

    dataPending_ = false;
    return true;
}

bool Texture2D::GetData_Vulkan(unsigned level, void* dest) const
{
    // Vulkan doesn't support easy GPU-to-CPU readback without staging buffers
    URHO3D_LOGWARNING("GetData not supported on Vulkan - use Image resource instead");
    return false;
}

// ============================================================================
// Texture Helper Functions (Phase 10 - Staging Buffer Support)
// ============================================================================

unsigned Texture::GetRowDataSize_Vulkan(int width) const
{
    // For Vulkan, format_ stores VkFormat values directly
    switch (format_)
    {
    // 1 byte per pixel formats
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8_SNORM:
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8_SRGB:
        return (unsigned)width;

    // 2 bytes per pixel formats
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R8G8_SNORM:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8_SRGB:
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R16_SNORM:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT:
        return (unsigned)(width * 2);

    // 3 bytes per pixel formats
    case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_R8G8B8_SNORM:
    case VK_FORMAT_R8G8B8_UINT:
    case VK_FORMAT_R8G8B8_SINT:
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_B8G8R8_UNORM:
    case VK_FORMAT_B8G8R8_SRGB:
        return (unsigned)(width * 3);

    // 4 bytes per pixel formats
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SNORM:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT:
        return (unsigned)(width * 4);

    // 8 bytes per pixel formats
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SNORM:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32_SFLOAT:
        return (unsigned)(width * 8);

    // 16 bytes per pixel formats
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return (unsigned)(width * 16);

    // Compressed formats (BC/DXT) - 4x4 blocks
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
        return ((unsigned)(width + 3) >> 2u) * 8;  // 8 bytes per 4x4 block

    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return ((unsigned)(width + 3) >> 2u) * 16;  // 16 bytes per 4x4 block

    // ETC2/EAC compressed formats
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
        return ((unsigned)(width + 3) >> 2u) * 8;

    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
        return ((unsigned)(width + 3) >> 2u) * 16;

    default:
        // Default: assume RGBA 4 bytes per pixel
        return (unsigned)(width * 4);
    }
}

bool Texture::IsCompressed_Vulkan() const
{
    // Check if format is a compressed block format
    switch (format_)
    {
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
        return true;
    default:
        return false;
    }
}

void Texture::UpdateParameters_Vulkan()
{
    if (!parametersDirty_)
        return;

    parametersDirty_ = false;

    // Recreate sampler with current parameters (filter mode, address mode, shadow compare, etc.)
    // Vulkan samplers are immutable — must destroy and recreate when parameters change
    auto* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl || !impl->GetDevice())
        return;

    // Destroy old sampler
    if (sampler_)
    {
        vkDestroySampler(impl->GetDevice(), (VkSampler)sampler_, nullptr);
        sampler_ = nullptr;
    }

    // Recreate sampler with current parameters
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    TextureFilterMode effectiveFilterMode = filterMode_;
    if (effectiveFilterMode == FILTER_DEFAULT)
        effectiveFilterMode = graphics->GetDefaultTextureFilterMode();

    switch (effectiveFilterMode)
    {
    case FILTER_NEAREST:
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        break;
    case FILTER_BILINEAR:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        break;
    case FILTER_TRILINEAR:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        break;
    case FILTER_ANISOTROPIC:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        {
            unsigned maxAniso = anisotropy_ ? anisotropy_ : graphics->GetDefaultTextureAnisotropy();
            samplerInfo.anisotropyEnable = VK_TRUE;
            samplerInfo.maxAnisotropy = static_cast<float>(maxAniso);
        }
        break;
    case FILTER_NEAREST_ANISOTROPIC:
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        {
            unsigned maxAniso = anisotropy_ ? anisotropy_ : graphics->GetDefaultTextureAnisotropy();
            samplerInfo.anisotropyEnable = VK_TRUE;
            samplerInfo.maxAnisotropy = static_cast<float>(maxAniso);
        }
        break;
    default:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
    }

    VkSamplerAddressMode addressModes[] = {
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
    };

    unsigned uMode = addressModes_[COORD_U] < MAX_ADDRESSMODES ? addressModes_[COORD_U] : ADDRESS_WRAP;
    unsigned vMode = addressModes_[COORD_V] < MAX_ADDRESSMODES ? addressModes_[COORD_V] : ADDRESS_WRAP;

    samplerInfo.addressModeU = addressModes[uMode];
    samplerInfo.addressModeV = addressModes[vMode];
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.compareEnable = shadowCompare_ ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = shadowCompare_ ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_NEVER;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = (float)levels_;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkSampler vkSampler;
    if (vkCreateSampler(impl->GetDevice(), &samplerInfo, nullptr, &vkSampler) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("UpdateParameters_Vulkan: Failed to recreate sampler");
        return;
    }

    sampler_ = vkSampler;
}

bool Texture::GetParametersDirty_Vulkan() const
{
    return parametersDirty_;
}

void Texture::RegenerateLevels_Vulkan()
{
    if (!object_.ptr_ || levels_ < 2)
        return;

    auto* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return;

    auto* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return;

    VkImage image = (VkImage)object_.ptr_;
    VkCommandBuffer commandBuffer = impl->GetFrameCommandBuffer();

    // Transition base level to TRANSFER_SRC for reading
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = width_;
    int32_t mipHeight = height_;

    for (unsigned i = 1; i < levels_; i++)
    {
        // Transition previous level to TRANSFER_SRC
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Transition current level to TRANSFER_DST
        barrier.subresourceRange.baseMipLevel = i;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Blit from previous level to current level
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
                              mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // Transition previous level back to SHADER_READ_ONLY
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Update dimensions for next iteration
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition last level to SHADER_READ_ONLY
    barrier.subresourceRange.baseMipLevel = levels_ - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    levelsDirty_ = false;
}

// ============================================================================
// TextureCube Vulkan Implementation
// ============================================================================

// Helper: transition a single cube face (array layer) between image layouts
static void TransitionCubeFaceLayout(VkCommandBuffer cmd, VkImage image,
    uint32_t face, uint32_t mipLevels,
    VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = face;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage, dstStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        barrier.srcAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else
    {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        barrier.dstAccessMask = 0;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void TextureCube::OnDeviceLost_Vulkan()
{
}

void TextureCube::OnDeviceReset_Vulkan()
{
    if (!object_.ptr_ || dataPending_)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        if (cache && cache->Exists(GetName()))
            dataLost_ = !cache->ReloadResource(this);

        if (!object_.ptr_)
        {
            Create_Vulkan();
            dataLost_ = true;
        }
    }

    dataPending_ = false;
}

void TextureCube::Release_Vulkan()
{
    if (!object_.ptr_ && !shaderResourceView_ && !sampler_)
        return;

    Graphics* graphics = GetSubsystem<Graphics>();

    // Clear any Graphics texture slots referencing this texture (matches D3D11 pattern)
    if (graphics)
    {
        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        {
            if (graphics->GetTexture(i) == this)
                graphics->SetTexture(i, nullptr);
        }
    }

    VulkanGraphicsImpl* impl = graphics ? graphics->GetImpl_Vulkan() : nullptr;
    VkDevice device = (impl && impl->GetDevice()) ? impl->GetDevice() : VK_NULL_HANDLE;
    VmaAllocator allocator = (impl && impl->GetAllocator()) ? impl->GetAllocator() : VK_NULL_HANDLE;

    // Release MSAA resources
    if (msaaImageView_)
    {
        if (device)
            vkDestroyImageView(device, (VkImageView)msaaImageView_, nullptr);
        msaaImageView_ = nullptr;
    }
    if (msaaImage_)
    {
        if (allocator)
            vmaDestroyImage(allocator, (VkImage)msaaImage_, (VmaAllocation)msaaAllocation_);
        msaaImage_ = nullptr;
        msaaAllocation_ = nullptr;
    }

    if (shaderResourceView_)
    {
        if (device)
            vkDestroyImageView(device, (VkImageView)shaderResourceView_, nullptr);
        shaderResourceView_ = nullptr;
    }

    if (sampler_)
    {
        if (device)
            vkDestroySampler(device, (VkSampler)sampler_, nullptr);
        sampler_ = nullptr;
    }

    if (object_.ptr_)
    {
        if (allocator)
        {
            VkImage image = (VkImage)(void*)object_.ptr_;
            VmaAllocation allocation = (VmaAllocation)vmaAllocation_;
            vmaDestroyImage(allocator, image, allocation);
        }
        object_.ptr_ = nullptr;
    }
}

bool TextureCube::Create_Vulkan()
{
    Release_Vulkan();

    if (!width_ || !height_)
        return false;

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    levels_ = CheckMaxLevels(width_, height_, requestedLevels_);

    VkFormat vkFormat = GetVulkanFormat(format_);

    // Create 6-layer cube-compatible image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = vkFormat;
    imageInfo.extent = {(uint32_t)width_, (uint32_t)height_, 1};
    imageInfo.mipLevels = levels_;
    imageInfo.arrayLayers = MAX_CUBEMAP_FACES;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage_ == TEXTURE_RENDERTARGET)
        imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage image;
    VmaAllocation allocation;

    if (vmaCreateImage(impl->GetAllocator(), &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create Vulkan cubemap image");
        return false;
    }

    object_.ptr_ = (void*)image;
    vmaAllocation_ = allocation;

    // Create cube image view (all 6 faces)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = vkFormat;
    viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = levels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = MAX_CUBEMAP_FACES;

    VkImageView imageView;
    if (vkCreateImageView(impl->GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create Vulkan cubemap image view");
        vmaDestroyImage(impl->GetAllocator(), image, allocation);
        object_.ptr_ = nullptr;
        return false;
    }

    shaderResourceView_ = imageView;

    // Create sampler — cubemaps use CLAMP_TO_EDGE on all axes
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    TextureFilterMode effectiveFilterMode = filterMode_;
    if (effectiveFilterMode == FILTER_DEFAULT)
        effectiveFilterMode = graphics_->GetDefaultTextureFilterMode();

    switch (effectiveFilterMode)
    {
    case FILTER_NEAREST:
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case FILTER_BILINEAR:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case FILTER_ANISOTROPIC:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        {
            unsigned maxAniso = anisotropy_ ? anisotropy_ : graphics_->GetDefaultTextureAnisotropy();
            samplerInfo.anisotropyEnable = VK_TRUE;
            samplerInfo.maxAnisotropy = static_cast<float>(maxAniso);
        }
        break;
    case FILTER_NEAREST_ANISOTROPIC:
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        {
            unsigned maxAniso = anisotropy_ ? anisotropy_ : graphics_->GetDefaultTextureAnisotropy();
            samplerInfo.anisotropyEnable = VK_TRUE;
            samplerInfo.maxAnisotropy = static_cast<float>(maxAniso);
        }
        break;
    default:
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        break;
    }

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = (float)levels_;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkSampler vkSampler;
    if (vkCreateSampler(impl->GetDevice(), &samplerInfo, nullptr, &vkSampler) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create Vulkan cubemap sampler");
        vkDestroyImageView(impl->GetDevice(), imageView, nullptr);
        vmaDestroyImage(impl->GetAllocator(), image, allocation);
        object_.ptr_ = nullptr;
        return false;
    }

    sampler_ = vkSampler;
    dataPending_ = true;

    // Create MSAA cube image for multisampled render targets
    if (multiSample_ > 1 && usage_ == TEXTURE_RENDERTARGET)
    {
        VkImageCreateInfo msaaInfo{};
        msaaInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        msaaInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        msaaInfo.imageType = VK_IMAGE_TYPE_2D;
        msaaInfo.format = vkFormat;
        msaaInfo.extent = {(uint32_t)width_, (uint32_t)height_, 1};
        msaaInfo.mipLevels = 1;
        msaaInfo.arrayLayers = MAX_CUBEMAP_FACES;
        msaaInfo.samples = (VkSampleCountFlagBits)multiSample_;
        msaaInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        msaaInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        msaaInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        msaaInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo msaaAllocInfo{};
        msaaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkImage msaaImg;
        VmaAllocation msaaAlloc;
        if (vmaCreateImage(impl->GetAllocator(), &msaaInfo, &msaaAllocInfo, &msaaImg, &msaaAlloc, nullptr) == VK_SUCCESS)
        {
            msaaImage_ = (void*)msaaImg;
            msaaAllocation_ = msaaAlloc;

            // Create cube image view for MSAA image (all 6 faces)
            VkImageViewCreateInfo msaaViewInfo{};
            msaaViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            msaaViewInfo.image = msaaImg;
            msaaViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            msaaViewInfo.format = vkFormat;
            msaaViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            msaaViewInfo.subresourceRange.baseMipLevel = 0;
            msaaViewInfo.subresourceRange.levelCount = 1;
            msaaViewInfo.subresourceRange.baseArrayLayer = 0;
            msaaViewInfo.subresourceRange.layerCount = MAX_CUBEMAP_FACES;

            VkImageView msaaView;
            if (vkCreateImageView(impl->GetDevice(), &msaaViewInfo, nullptr, &msaaView) == VK_SUCCESS)
                msaaImageView_ = msaaView;
            else
            {
                URHO3D_LOGWARNING("Failed to create cubemap MSAA image view, falling back to single-sample");
                vmaDestroyImage(impl->GetAllocator(), msaaImg, msaaAlloc);
                msaaImage_ = nullptr;
                msaaAllocation_ = nullptr;
            }
        }
        else
            URHO3D_LOGWARNING("Failed to create cubemap MSAA image, falling back to single-sample");
    }

    return true;
}

bool TextureCube::SetData_Vulkan(CubeMapFace face, unsigned level, int x, int y, int width, int height, const void* data)
{
    if (!data || !object_.ptr_)
        return false;

    if (level >= levels_)
        return false;

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    unsigned dataSize = GetDataSize(width, height);
    if (dataSize == 0)
        return false;

    VkImage image = (VkImage)(void*)object_.ptr_;

    VkCommandBuffer commandBuffer = impl->BeginUploadCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE)
        return false;

    // Transition this face to TRANSFER_DST
    TransitionCubeFaceLayout(commandBuffer, image, face, levels_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo{};

    if (vmaCreateBuffer(impl->GetAllocator(), &bufferInfo, &allocInfo,
                       &stagingBuffer, &stagingAllocation, &allocationInfo) != VK_SUCCESS)
        return false;

    memcpy(allocationInfo.pMappedData, data, dataSize);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = level;
    region.imageSubresource.baseArrayLayer = face;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {x, y, 0};
    region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition back to SHADER_READ_ONLY
    TransitionCubeFaceLayout(commandBuffer, image, face, levels_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    impl->EndUploadCommandBuffer(commandBuffer);
    vmaDestroyBuffer(impl->GetAllocator(), stagingBuffer, stagingAllocation);

    return true;
}

bool TextureCube::SetData_Vulkan(CubeMapFace face, Deserializer& source)
{
    SharedPtr<Image> image(new Image(context_));
    if (!image->Load(source))
        return false;

    return SetData_Vulkan(face, image);
}

bool TextureCube::SetData_Vulkan(CubeMapFace face, Image* image, bool useAlpha)
{
    if (!image)
        return false;

    int width = image->GetWidth();
    int height = image->GetHeight();
    unsigned components = image->GetComponents();

    if (width <= 0 || height <= 0 || components == 0)
        return false;

    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

    // Determine format
    unsigned format = 0;
    if (!image->IsCompressed())
    {
        switch (components)
        {
        case 1: format = useAlpha ? Graphics::GetAlphaFormat() : Graphics::GetLuminanceFormat(); break;
        case 2: format = Graphics::GetLuminanceAlphaFormat(); break;
        case 3: format = Graphics::GetRGBFormat(); break;
        case 4: format = Graphics::GetRGBAFormat(); break;
        default: return false;
        }
    }
    else
    {
        format = graphics->GetFormat(image->GetCompressedFormat());
        if (!format)
            return false;
    }

    // Fallback format assignment if Get*Format returns 0
    if (format == 0)
    {
        switch (components)
        {
        case 1: format = VK_FORMAT_R8_UNORM; break;
        case 2: format = VK_FORMAT_R8G8_UNORM; break;
        case 3: format = VK_FORMAT_R8G8B8A8_UNORM; break;
        case 4: format = VK_FORMAT_R8G8B8A8_UNORM; break;
        }
    }

    // For compressed textures, limit mip levels to what's actually in the file
    if (image->IsCompressed())
    {
        unsigned numCompressedLevels = image->GetNumCompressedLevels();
        if (numCompressedLevels > 0)
            requestedLevels_ = numCompressedLevels;
    }

    // Create texture on first face
    if (!width_ || !height_)
    {
        if (!SetSize(width, format))
            return false;
    }

    VulkanGraphicsImpl* impl = graphics->GetImpl_Vulkan();
    if (!impl)
        return false;

    VkImage vkImage = (VkImage)(void*)object_.ptr_;
    if (!vkImage)
        return false;

    // Convert 3-component to RGBA (Vulkan doesn't support RGB8 on most hardware)
    SharedPtr<Image> convertedImage;
    if (!image->IsCompressed() && components == 3)
    {
        convertedImage = image->ConvertToRGBA();
        if (convertedImage)
            image = convertedImage;
    }

    unsigned char* data = image->GetData();
    if (!data)
        return false;

    // Calculate total staging size for all mip levels of this face
    unsigned totalSize = 0;
    int levelWidth = width;
    int levelHeight = height;
    for (unsigned level = 0; level < levels_; ++level)
    {
        totalSize += GetDataSize(levelWidth, levelHeight);
        levelWidth = Max(levelWidth >> 1, 1);
        levelHeight = Max(levelHeight >> 1, 1);
    }

    VkCommandBuffer commandBuffer = impl->BeginUploadCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE)
        return false;

    // Create staging buffer for all mip levels
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo{};

    if (vmaCreateBuffer(impl->GetAllocator(), &bufferInfo, &allocInfo,
                       &stagingBuffer, &stagingAllocation, &allocationInfo) != VK_SUCCESS)
    {
        impl->EndUploadCommandBuffer(commandBuffer);
        return false;
    }

    // Copy mip levels to staging buffer
    unsigned char* stagingData = (unsigned char*)allocationInfo.pMappedData;
    unsigned offset = 0;
    levelWidth = width;
    levelHeight = height;

    SharedPtr<Image> currentLevel(image);
    unsigned numCompressedLevels = image->GetNumCompressedLevels();
    bool isCompressed = image->IsCompressed();

    for (unsigned level = 0; level < levels_; ++level)
    {
        unsigned levelSize = GetDataSize(levelWidth, levelHeight);

        if (level == 0)
            memcpy(stagingData + offset, data, levelSize);
        else if (numCompressedLevels > level)
        {
            CompressedLevel compLevel = image->GetCompressedLevel(level);
            if (compLevel.data_)
                memcpy(stagingData + offset, compLevel.data_, levelSize);
        }
        else if (!isCompressed && currentLevel)
        {
            currentLevel = currentLevel->GetNextLevel();
            if (currentLevel && currentLevel->GetData())
                memcpy(stagingData + offset, currentLevel->GetData(), levelSize);
        }

        offset += levelSize;
        levelWidth = Max(levelWidth >> 1, 1);
        levelHeight = Max(levelHeight >> 1, 1);
    }

    // Transition this face to TRANSFER_DST
    TransitionCubeFaceLayout(commandBuffer, vkImage, face, levels_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Record copy commands for all mip levels of this face
    Vector<VkBufferImageCopy> regions;
    offset = 0;
    levelWidth = width;
    levelHeight = height;

    for (unsigned level = 0; level < levels_; ++level)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = level;
        region.imageSubresource.baseArrayLayer = face;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {(uint32_t)levelWidth, (uint32_t)levelHeight, 1};

        regions.Push(region);

        offset += GetDataSize(levelWidth, levelHeight);
        levelWidth = Max(levelWidth >> 1, 1);
        levelHeight = Max(levelHeight >> 1, 1);
    }

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, vkImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          regions.Size(), &regions[0]);

    // Transition back to SHADER_READ_ONLY
    TransitionCubeFaceLayout(commandBuffer, vkImage, face, levels_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    impl->EndUploadCommandBuffer(commandBuffer);
    vmaDestroyBuffer(impl->GetAllocator(), stagingBuffer, stagingAllocation);

    faceMemoryUse_[face] = totalSize;
    dataPending_ = false;
    return true;
}

bool TextureCube::GetData_Vulkan(CubeMapFace face, unsigned level, void* dest) const
{
    URHO3D_LOGWARNING("TextureCube::GetData not supported on Vulkan");
    return false;
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
