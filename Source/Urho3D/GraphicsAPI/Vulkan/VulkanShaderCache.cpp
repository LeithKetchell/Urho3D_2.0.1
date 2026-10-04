//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan shader compilation result caching implementation (Quick Win #5)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanShaderCache.h"
#include "VulkanShaderCompiler.h"
#include "../ShaderVariation.h"
#include "../Shader.h"
#include "../../IO/Log.h"
#include "../../IO/File.h"
#include "../../IO/FileSystem.h"

#include "../../DebugNew.h"
#include "../../Math/StringHash.h"
#include "../../Core/Context.h"

namespace Urho3D
{

VulkanShaderCache::VulkanShaderCache(Context* context)
    : Object(context)
{
}

VulkanShaderCache::~VulkanShaderCache()
{
    Release();
}

bool VulkanShaderCache::Initialize()
{
    // Cache directory will be set by Graphics subsystem
    URHO3D_LOGDEBUG("VulkanShaderCache initialized");
    return true;
}

void VulkanShaderCache::Release()
{
    spirvCache_.Clear();
    URHO3D_LOGDEBUG("VulkanShaderCache released (hits: " + String(cacheHits_) +
                   ", misses: " + String(cacheMisses_) + ")");
}

String VulkanShaderCache::GetCacheFilePath(const String& cacheKey) const
{
    if (cacheDir_.Empty())
        return String::EMPTY;

    return cacheDir_ + cacheKey + ".spv";
}

bool VulkanShaderCache::LoadSPIRVFromDisk(
    const String& cacheKey,
    Vector<uint32_t>& spirvBytecode)
{
    if (!diskCacheEnabled_ || cacheDir_.Empty())
        return false;

    String filePath = GetCacheFilePath(cacheKey);
    File cacheFile(context_, filePath, FILE_READ);

    if (!cacheFile.IsOpen())
    {
        URHO3D_LOGDEBUG("Shader cache file not found: " + filePath);
        return false;
    }

    // Read and validate magic number (SPIR-V: 0x07230203)
    uint32_t magic;
    if (cacheFile.Read(&magic, sizeof(uint32_t)) != sizeof(uint32_t) || magic != 0x07230203)
    {
        URHO3D_LOGWARNING("Invalid SPIR-V cache file: " + filePath);
        return false;
    }

    // Read bytecode size
    uint32_t size;
    if (cacheFile.Read(&size, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        URHO3D_LOGWARNING("Failed to read SPIR-V cache size: " + filePath);
        return false;
    }

    // Read SPIR-V bytecode
    spirvBytecode.Clear();
    spirvBytecode.Reserve(size / 4);

    Vector<unsigned char> buffer(size);
    if (cacheFile.Read(!buffer.Empty() ? (void*)&buffer[0] : nullptr, size) != (int)size)
    {
        URHO3D_LOGWARNING("Failed to read SPIR-V bytecode from cache: " + filePath);
        return false;
    }

    // Convert unsigned char back to uint32_t
    const uint32_t* spirvWords = reinterpret_cast<const uint32_t*>(!buffer.Empty() ? &buffer[0] : nullptr);
    for (unsigned i = 0; i < size / 4; ++i)
    {
        spirvBytecode.Push(spirvWords[i]);
    }

    URHO3D_LOGDEBUG("Loaded SPIR-V from cache: " + filePath);
    return true;
}

bool VulkanShaderCache::SaveSPIRVToDisk(
    const String& cacheKey,
    const Vector<uint32_t>& spirvBytecode)
{
    if (!diskCacheEnabled_ || cacheDir_.Empty() || spirvBytecode.Empty())
        return false;

    String filePath = GetCacheFilePath(cacheKey);
    File cacheFile(context_, filePath, FILE_WRITE);

    if (!cacheFile.IsOpen())
    {
        URHO3D_LOGWARNING("Failed to open shader cache file for writing: " + filePath);
        return false;
    }

    // Write magic number (SPIR-V module magic)
    uint32_t magic = 0x07230203;
    if (cacheFile.Write(&magic, sizeof(uint32_t)) != sizeof(uint32_t))
        return false;

    // Write bytecode size
    uint32_t size = spirvBytecode.Size() * sizeof(uint32_t);
    if (cacheFile.Write(&size, sizeof(uint32_t)) != sizeof(uint32_t))
        return false;

    // Write SPIR-V bytecode
    if (cacheFile.Write(!spirvBytecode.Empty() ? (const void*)&spirvBytecode[0] : nullptr, size) != (int)size)
        return false;

    URHO3D_LOGDEBUG("Saved SPIR-V to cache: " + filePath);
    return true;
}

bool VulkanShaderCache::GetOrCompileSPIRV(
    ShaderVariation* variation,
    Vector<uint32_t>& spirvBytecode,
    String& errorOutput)
{
    if (!variation)
    {
        errorOutput = "Invalid shader variation";
        return false;
    }

    // Generate cache key from variation (shader name + defines hash)
    StringHash keyHash = StringHash(variation->GetOwner()->GetName() + variation->GetDefines());
    uint64_t cacheKey = keyHash.Value();

    // Check in-memory cache first
    auto cached = spirvCache_.Find(cacheKey);
    if (cached != spirvCache_.End())
    {
        spirvBytecode = cached->second_;
        ++cacheHits_;
        URHO3D_LOGDEBUG("SPIR-V cache hit for: " + variation->GetFullName());
        return true;
    }

    // Check disk cache as fallback
    String diskCacheKey = variation->GetOwner()->GetName() + "_" + String(cacheKey);
    if (LoadSPIRVFromDisk(diskCacheKey, spirvBytecode))
    {
        // Restore to in-memory cache
        spirvCache_[cacheKey] = spirvBytecode;
        ++cacheHits_;
        URHO3D_LOGDEBUG("SPIR-V cache hit (disk) for: " + variation->GetFullName());
        return true;
    }

    // Cache miss - caller must compile
    ++cacheMisses_;
    errorOutput = "Not in cache - compilation required";
    return false;
}

bool VulkanShaderCache::CacheSPIRV(ShaderVariation* variation, const Vector<uint32_t>& spirvBytecode)
{
    if (!variation || spirvBytecode.Empty())
        return false;

    // Generate cache key
    StringHash keyHash = StringHash(variation->GetOwner()->GetName() + variation->GetDefines());
    uint64_t cacheKey = keyHash.Value();

    // Store in memory cache
    spirvCache_[cacheKey] = spirvBytecode;

    // Store on disk cache
    String diskCacheKey = variation->GetOwner()->GetName() + "_" + String(cacheKey);
    SaveSPIRVToDisk(diskCacheKey, spirvBytecode);

    return true;
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
