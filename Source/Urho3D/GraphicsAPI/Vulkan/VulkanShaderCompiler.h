//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan GLSL to SPIR-V shader compiler (Phase 6)

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/Vector.h"
#include "../../Container/HashMap.h"
#include "../../Container/Str.h"
#include "../GraphicsDefs.h"
#include "../ShaderVariation.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class ShaderVariation;

/// \brief GLSL to SPIR-V compiler for Vulkan shaders
/// \details Provides shader compilation services to convert GLSL source code to SPIR-V
/// binary format required by Vulkan. Implements a two-tier compiler strategy: primary
/// compilation via shaderc (Google's GLSL compiler), with graceful fallback to glslang
/// (Khronos reference compiler) if shaderc is unavailable. Supports shader parameter
/// defines and includes automatic SPIR-V reflection for descriptor set generation.
///
/// **Compiler Support:**
/// - Primary: shaderc (Google's GLSL compiler, recommended)
/// - Fallback: glslang (Khronos reference compiler)
/// - Detection: CheckCompilerAvailability() validates at initialization
/// - Graceful Degradation: Continues with available compiler or reports critical error
///
/// **Supported Shader Types:**
/// - VS: Vertex shaders (VK_SHADER_STAGE_VERTEX_BIT)
/// - FS: Fragment/pixel shaders (VK_SHADER_STAGE_FRAGMENT_BIT)
/// - GS: Geometry shaders (VK_SHADER_STAGE_GEOMETRY_BIT)
/// - HS: Tessellation control shaders (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
/// - DS: Tessellation evaluation shaders (VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
/// - CS: Compute shaders (VK_SHADER_STAGE_COMPUTE_BIT)
///
/// **Typical Usage:**
/// 1. Call CheckCompilerAvailability() once during Graphics initialization
/// 2. For each shader: CompileGLSLToSPIRV(source, defines, type, spirv, output)
/// 3. Use resulting SPIR-V bytecode for VkShaderModule creation
/// 4. Optionally use VulkanSPIRVReflect on SPIR-V for descriptor inference
///
/// **Error Reporting:**
/// - CompileGLSLToSPIRV() returns false and populates compilerOutput on error
/// - compilerOutput contains compiler-specific error messages and warnings
/// - FormatCompilerOutput() provides human-readable error reporting
/// - Critical errors logged if no compiler available (prevents silent failures)
class VulkanShaderCompiler
{
public:
    /// \brief Compile GLSL shader source to SPIR-V bytecode
    /// \param source GLSL shader source code as string
    /// \param defines Preprocessor defines (e.g., "#define FOO 1")
    /// \param type Shader type (VS, FS, GS, HS, DS, CS)
    /// \param spirvBytecode Output parameter: compiled SPIR-V bytecode
    /// \param compilerOutput Output parameter: compiler error/warning messages
    /// \returns True if compilation successful, false on error
    /// \details Attempts compilation via shaderc first, falls back to glslang if unavailable.
    /// The compilerOutput parameter contains human-readable compiler diagnostics on failure.
    /// Requires at least one shader compiler available (verified via CheckCompilerAvailability()).
    static bool CompileGLSLToSPIRV(
        const String& source,
        const String& defines,
        ShaderType type,
        Vector<uint32_t>& spirvBytecode,
        String& compilerOutput,
        HashMap<StringHash, ShaderVariation::CustomUniformInfo>* customUniformMap = nullptr
    );

    /// \brief Get Vulkan shader stage from ShaderType
    /// \param type Shader type (VS, FS, GS, HS, DS, CS)
    /// \returns Corresponding VkShaderStageFlagBits value
    /// \details Maps Urho3D ShaderType enum to Vulkan shader stage flags for use in
    /// VkPipelineShaderStageCreateInfo and descriptor set binding.
    static VkShaderStageFlagBits GetShaderStage(ShaderType type);

    /// \brief Preprocess shader source (handle includes and defines)
    /// \param source Original GLSL shader source code
    /// \param preprocessed Output parameter: preprocessed source after expansion
    /// \param compilerOutput Output parameter: preprocessor diagnostics
    /// \returns True if preprocessing successful, false on error
    /// \details Handles #include directives and applies preprocessor definitions.
    /// Used as intermediate step before SPIR-V compilation.
    static bool PreprocessShader(
        const String& source,
        String& preprocessed,
        String& compilerOutput
    );

    /// \brief Check which shader compilers are available and log status
    /// \returns True if at least one compiler (shaderc or glslang) is available
    /// \details Performs initialization-time availability check for shaderc and glslang.
    /// Logs warnings if no compiler found (prevents silent shader compilation failures).
    /// Must be called during Graphics initialization before attempting shader compilation.
    static bool CheckCompilerAvailability();

    /// \brief Get human-readable list of available compilers
    /// \returns String containing comma-separated list of available compilers
    /// \details Returns "shaderc" if available, "glslang" if available, or "none" if neither found.
    /// Used for logging and diagnostics (e.g., "Available compilers: shaderc, glslang").
    static String GetAvailableCompilers();

private:
    /// \brief Try to compile using shaderc library (preferred)
    /// \param source GLSL shader source code
    /// \param defines Preprocessor defines string
    /// \param type Shader type (VS, FS, etc.)
    /// \param spirvBytecode Output: compiled SPIR-V bytecode
    /// \param compilerOutput Output: error/warning messages
    /// \returns True if compilation successful, false if shaderc unavailable or error occurred
    /// \details Primary compilation method. Uses Google's shaderc compiler if available.
    /// Falls back to glslang via CompileWithGlslang() if shaderc unavailable.
    static bool CompileWithShaderc(
        const String& source,
        const String& defines,
        ShaderType type,
        Vector<uint32_t>& spirvBytecode,
        String& compilerOutput
    );

    /// \brief Try to compile using glslang library (fallback)
    /// \param source GLSL shader source code
    /// \param defines Preprocessor defines string
    /// \param type Shader type (VS, FS, etc.)
    /// \param spirvBytecode Output: compiled SPIR-V bytecode
    /// \param compilerOutput Output: error/warning messages
    /// \returns True if compilation successful, false if glslang unavailable or error occurred
    /// \details Fallback compilation method. Uses Khronos reference compiler (glslang) if shaderc unavailable.
    /// Returns false if glslang not available (no further fallback).
    static bool CompileWithGlslang(
        const String& source,
        const String& defines,
        ShaderType type,
        Vector<uint32_t>& spirvBytecode,
        String& compilerOutput
    );

    /// \brief Convert compiler output to human-readable format
    /// \param rawOutput Raw compiler output from shaderc or glslang
    /// \returns Formatted error/warning string for logging
    /// \details Transforms compiler-specific output format into consistent,
    /// human-readable diagnostics with proper line numbers and error locations.
    static String FormatCompilerOutput(const String& rawOutput);
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
