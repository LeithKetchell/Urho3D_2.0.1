//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan GLSL to SPIR-V shader compiler (Phase 6)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanShaderCompiler.h"
#include "../../Graphics/Graphics.h"
#include "../../GraphicsAPI/ShaderVariation.h"
#include "../../IO/Log.h"
#include <sstream>
#include <algorithm>
#include <fstream>
#include <cstdlib>

#include "../../DebugNew.h"

// Try to include shaderc if available (preferred compiler)
#ifdef URHO3D_SHADERC
    #include <shaderc/shaderc.hpp>
#endif

// Try to include glslang if available (fallback compiler)
#ifdef URHO3D_GLSLANG
    #include <glslang/Public/ShaderLang.h>
    #include <glslang/Public/ResourceLimits.h>
    #include <glslang/SPIRV/GlslangToSpv.h>
#endif

namespace Urho3D
{

// Enable verbose shader compilation debugging (WARNING: Severely impacts performance)
#define VULKAN_SHADER_DEBUG_LOGGING 0

/// \brief Convert Urho3D shader type to Vulkan shader stage
/// \param type Urho3D ShaderType enum (VS, PS, etc.)
/// \returns Corresponding VkShaderStageFlagBits for use in VkPipelineShaderStageCreateInfo
///
/// \details Maps Urho3D's shader type naming (VS/PS) to Vulkan's stage naming.
/// Currently supports vertex (VS) and pixel/fragment (PS) shaders.
/// Geometry, tessellation, and compute shaders return vertex stage as placeholder.
VkShaderStageFlagBits VulkanShaderCompiler::GetShaderStage(ShaderType type)
{
    switch (type)
    {
    case VS:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case PS:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case CS:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    default:
        return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

/// \brief Main entry point: compile GLSL shader source to SPIR-V bytecode
/// \param source GLSL shader source code as UTF-8 string
/// \param defines Preprocessor defines string (format: "DEFINE1 DEFINE2=value")
/// \param type Shader type (VS, PS, etc.)
/// \param spirvBytecode Output: compiled SPIR-V bytecode as uint32_t vector
/// \param compilerOutput Output: error/warning messages from compiler
/// \returns true if compilation successful, false on any error
///
/// \details Implements two-tier compilation strategy:
///   1. Validate shader source (reject if empty)
///   2. Preprocess shader (handle #version, #include directives)
///   3. Try shaderc compiler (preferred, faster, more modern)
///   4. Fall back to glslang if shaderc fails or unavailable
///   5. Return error if both compilers fail or neither available
///
/// **Preprocessing:**
/// - Adds #version 450 if not present (GLSL 4.50, compatible with Vulkan)
/// - Processes #include directives (extracts filenames for later expansion)
/// - Handles both #include "file" and #include <file> syntax
///
/// **Compiler Selection:**
/// - Preferred: shaderc (Google's compiler, optimized, faster)
/// - Fallback: glslang (Khronos reference compiler, always available if enabled)
/// - If both fail, returns detailed error message
///
/// **Output Handling:**
/// - spirvBytecode: Contains SPIR-V bytecode ready for vkCreateShaderModule()
/// - compilerOutput: Contains human-readable diagnostics (errors and warnings)
///
/// **Typical Error Cases:**
/// - Empty source -> "Error: Empty shader source"
/// - Preprocessing failure -> "Shader preprocessing failed: <details>"
/// - Compilation failure -> Compiler-specific error message
/// - No compiler available -> "Error: No shader compiler available..."

// Forward declaration (defined below)
static String AddExplicitLayoutQualifiers(const String& source,
    HashMap<StringHash, ShaderVariation::CustomUniformInfo>& customUniformMap);

bool VulkanShaderCompiler::CompileGLSLToSPIRV(
    const String& source,
    const String& defines,
    ShaderType type,
    Vector<uint32_t>& spirvBytecode,
    String& compilerOutput,
    HashMap<StringHash, ShaderVariation::CustomUniformInfo>* customUniformMap)
{
    if (source.Empty())
    {
        compilerOutput = "Error: Empty shader source";
        return false;
    }

    // Preprocess shader to handle includes
    String preprocessed;
    if (!PreprocessShader(source, preprocessed, compilerOutput))
    {
        compilerOutput = "Shader preprocessing failed: " + compilerOutput;
        return false;
    }

    // Strip dead preprocessor branches that Vulkan always skips.
    // GL3 and USE_CBUFFERS are always defined for Vulkan (added in PreprocessShader preamble).
    // Uniforms.glsl has: #if !defined(GL3) || !defined(USE_CBUFFERS) ... #else ... #endif
    // The dead branch contains bare uniforms that confuse AddExplicitLayoutQualifiers
    // (it creates phantom CustomVS/CustomPS blocks with wasted binding numbers).
    {
        Vector<String> lines = preprocessed.Split('\n');
        int state = 0;   // 0=normal, 1=stripping dead branch, 2=keeping live branch (after #else)
        int ifDepth = 0;  // nesting depth within the tracked #if block

        for (unsigned i = 0; i < lines.Size(); ++i)
        {
            String trimmed = lines[i].Trimmed();

            if (state == 0)
            {
                if (trimmed.Contains("!defined(GL3)") || trimmed.Contains("!defined(USE_CBUFFERS)"))
                {
                    state = 1;
                    ifDepth = 1;
                    lines[i] = "";
                }
            }
            else if (state == 1)  // Stripping dead branch
            {
                if (trimmed.StartsWith("#if"))
                    ifDepth++;
                else if (trimmed.StartsWith("#endif"))
                {
                    ifDepth--;
                    if (ifDepth == 0)
                    {
                        // Reached end of outer block with no #else — remove and done
                        state = 0;
                    }
                }
                else if ((trimmed.StartsWith("#else") || trimmed.StartsWith("#elif")) && ifDepth == 1)
                {
                    // Switch to live branch — keep content but track for final #endif
                    // #elif is treated same as #else: the dead branch ends here, live code follows
                    state = 2;
                }
                lines[i] = "";  // Strip everything in dead branch (including #else line)
            }
            else if (state == 2)  // Keeping live branch, waiting for closing #endif
            {
                if (trimmed.StartsWith("#if"))
                    ifDepth++;
                else if (trimmed.StartsWith("#endif"))
                {
                    ifDepth--;
                    if (ifDepth == 0)
                    {
                        // This is the outer #endif that closes the original #if — remove it
                        lines[i] = "";
                        state = 0;
                    }
                    // else: nested #endif inside live branch — keep it
                }
                // All other lines in live branch — keep as-is
            }
        }

        preprocessed.Clear();
        for (unsigned i = 0; i < lines.Size(); ++i)
        {
            preprocessed += lines[i];
            if (i < lines.Size() - 1)
                preprocessed += "\n";
        }
    }

    // Add explicit layout qualifiers for Vulkan compatibility AFTER includes are expanded
    HashMap<StringHash, ShaderVariation::CustomUniformInfo> localCustomMap;
    preprocessed = AddExplicitLayoutQualifiers(preprocessed, localCustomMap);
    if (customUniformMap)
        *customUniformMap = localCustomMap;

#if VULKAN_SHADER_DEBUG_LOGGING
#endif
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
    const char* prep = preprocessed.CString();
    for (int i = 0; i < 100 && i < (int)preprocessed.Length(); ++i) {
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
    }
#if VULKAN_SHADER_DEBUG_LOGGING
#endif

    // Try shaderc first (preferred, faster, more modern)
#ifdef URHO3D_SHADERC
    if (CompileWithShaderc(preprocessed, defines, type, spirvBytecode, compilerOutput))
    {
        return true;
    }
    // If shaderc fails, log and continue to glslang fallback
    URHO3D_LOGDEBUG("shaderc compilation failed, trying glslang fallback");
#endif

    // Fallback to glslang
#ifdef URHO3D_GLSLANG
    if (CompileWithGlslang(preprocessed, defines, type, spirvBytecode, compilerOutput))
    {
        return true;
    }
#endif

    // Both compilers failed or not available
    if (compilerOutput.Empty())
    {
        compilerOutput = "Error: No shader compiler available (neither shaderc nor glslang found)";
    }
    return false;
}

/// \brief Add explicit layout(location=N) qualifiers to shader interface variables
/// AND layout(binding=N, set=0) to uniform samplers/buffers for Vulkan
/// \param source Shader source code
/// \returns Modified source with layout qualifiers
///
/// \details Adds layout(location=N) to all 'in' and 'out' variable declarations
/// to fix glslang auto-location mapping failures that generate invalid location 1073741823.
/// Also adds layout(binding=N, set=0) to uniform samplers to fix descriptor binding collisions.
/// Skips built-in variables (gl_Position, gl_FragColor, etc.) and already-qualified variables.
/// Extract sampler name from a "uniform samplerXXX name;" declaration line
static String ExtractSamplerName(const String& line)
{
    // Find the last token before the semicolon
    unsigned semi = line.Find(';');
    if (semi == String::NPOS) return String::EMPTY;
    // Walk backwards from semicolon to find the name
    String beforeSemi = line.Substring(0, semi).Trimmed();
    unsigned lastSpace = beforeSemi.FindLast(' ');
    if (lastSpace == String::NPOS) return String::EMPTY;
    return beforeSemi.Substring(lastSpace + 1);
}

/// Fixed sampler name → binding map for Vulkan (must match Graphics_Vulkan.cpp unitToBinding[])
static int GetSamplerBinding(const String& name)
{
    // PS samplers: fixed bindings matching TU_* → binding in Graphics_Vulkan.cpp
    if (name == "sDiffMap")             return 100;
    if (name == "sDiffCubeMap")         return 101;
    if (name == "sNormalMap")           return 102;
    if (name == "sSpecMap")             return 103;
    if (name == "sSpecCubeMap")         return 103;
    if (name == "sEmissiveMap")         return 104;
    if (name == "sEnvMap")              return 105;
    if (name == "sEnvCubeMap")          return 106;
    if (name == "sLightRampMap")        return 107;
    if (name == "sLightSpotMap")        return 108;
    if (name == "sLightCubeMap")        return 109;
    if (name == "sVolumeMap")           return 110;
    if (name == "sAlbedoBuffer")        return 111;
    if (name == "sNormalBuffer")        return 112;
    if (name == "sDepthBuffer")         return 113;
    if (name == "sLightBuffer")         return 114;
    if (name == "sShadowMap")           return 115;
    if (name == "sFaceSelectCubeMap")   return 116;
    if (name == "sIndirectionCubeMap")  return 117;
    if (name == "sZoneCubeMap")         return 118;
    if (name == "sZoneVolumeMap")       return 119;
    // Terrain shader aliases (same texture units as standard samplers)
    if (name == "sWeightMap0")          return 100;  // TU_DIFFUSE
    if (name == "sDetailMap1")          return 102;  // TU_NORMAL
    if (name == "sDetailMap2")          return 103;  // TU_SPECULAR
    if (name == "sDetailMap3")          return 104;  // TU_EMISSIVE
    if (name == "sWaterMap4")           return 105;  // TU_ENVIRONMENT
    if (name == "sTramplingMap5")       return 120;  // Trampling overlay (unique binding)
    return -1;  // Unknown sampler — will use sequential fallback
}

/// Compute std140 alignment for a GLSL type declaration string
static void GetStd140Layout(const String& typeStr, unsigned& alignment, unsigned& size)
{
    if (typeStr == "float" || typeStr == "int" || typeStr == "uint" || typeStr == "bool")
    { alignment = 4; size = 4; }
    else if (typeStr == "vec2" || typeStr == "ivec2" || typeStr == "uvec2")
    { alignment = 8; size = 8; }
    else if (typeStr == "vec3" || typeStr == "ivec3" || typeStr == "uvec3")
    { alignment = 16; size = 12; }
    else if (typeStr == "vec4" || typeStr == "ivec4" || typeStr == "uvec4")
    { alignment = 16; size = 16; }
    else if (typeStr == "mat3")
    { alignment = 16; size = 48; }  // 3 × vec4 columns
    else if (typeStr == "mat4" || typeStr == "mat3x4")
    { alignment = 16; size = 64; }  // 4 × vec4 columns
    else
    { alignment = 16; size = 16; }  // Conservative default
}

/// Extract GLSL type and variable name from a declaration like "float cBloomThreshold;"
static bool ParseUniformDecl(const String& decl, String& type, String& name)
{
    String trimmed = decl.Trimmed();
    // Remove trailing semicolon
    if (trimmed.EndsWith(";"))
        trimmed = trimmed.Substring(0, trimmed.Length() - 1).Trimmed();
    // Split into type and name
    unsigned spacePos = trimmed.Find(' ');
    if (spacePos == String::NPOS)
        return false;
    type = trimmed.Substring(0, spacePos).Trimmed();
    name = trimmed.Substring(spacePos + 1).Trimmed();
    // Remove array suffix if present (e.g., "cFoo[4]" → "cFoo")
    unsigned bracketPos = name.Find('[');
    if (bracketPos != String::NPOS)
        name = name.Substring(0, bracketPos);
    return !type.Empty() && !name.Empty();
}

static String AddExplicitLayoutQualifiers(const String& source,
    HashMap<StringHash, ShaderVariation::CustomUniformInfo>& customUniformMap)
{
    String result = source;
    int inputLocation = 0;
    int outputLocation = 0;
    int uniformBlockBinding = 0;  // Uniform blocks start at binding 0
    int samplerBinding = 100;     // Fallback for unknown samplers

    // Process line by line
    Vector<String> lines = result.Split('\n');
#if VULKAN_SHADER_DEBUG_LOGGING
#endif

    bool insideInstanced = false;  // Track #ifdef INSTANCED blocks

    for (unsigned i = 0; i < lines.Size(); ++i)
    {
        String& line = lines[i];
        String trimmed = line.Trimmed();

        // Track #ifdef INSTANCED blocks
        if (trimmed.StartsWith("#ifdef INSTANCED"))
            insideInstanced = true;
        else if (insideInstanced && (trimmed.StartsWith("#endif") || trimmed.StartsWith("#elif") || trimmed.StartsWith("#else")))
            insideInstanced = false;

        // Debug: show lines that contain "attribute" or "varying"
        if (trimmed.Contains("attribute") || trimmed.Contains("varying") || trimmed.Contains("fragData"))
        {
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
        }

        // Skip comments, empty lines, and lines with existing layout qualifiers
        if (trimmed.Empty() || trimmed.StartsWith("//") || trimmed.StartsWith("/*"))
        {
            continue;
        }

        // For lines with existing layout(location=N), advance our counters past N
        // to avoid assigning duplicate locations to subsequent auto-numbered variables
        // (e.g. iTexCoord4 at location 10 inside #ifdef INSTANCED vs iObjectIndex after #endif)
        if (trimmed.Contains("layout(location"))
        {
            unsigned pos = trimmed.Find("location");
            if (pos != String::NPOS)
            {
                unsigned eqPos = trimmed.Find("=", pos);
                if (eqPos != String::NPOS)
                {
                    unsigned numStart = eqPos + 1;
                    while (numStart < trimmed.Length() && trimmed[numStart] == ' ')
                        ++numStart;
                    int loc = atoi(trimmed.CString() + numStart);
                    bool lineIsInput = (trimmed.Contains(" in ") || trimmed.StartsWith("in "));
                    bool lineIsOutput = (trimmed.Contains(" out ") || trimmed.StartsWith("out "));
                    if (lineIsInput && loc >= inputLocation)
                        inputLocation = loc + 1;
                    if (lineIsOutput && loc >= outputLocation)
                        outputLocation = loc + 1;
                }
            }
            continue;
        }

        // Check for 'in' or 'out' variable declarations (not in function parameters)
        // 'varying' is treated as INPUT for location assignment because:
        //   - In PS: #define varying in → becomes input, uses inputLocation
        //   - In VS: #define varying out → becomes output, but VS output locations
        //     must match PS input locations, so using inputLocation is correct
        // This ensures 'out vec4 fragData[N]' gets outputLocation=0 for MRT rendering
        bool isInput = (trimmed.StartsWith("in ") || trimmed.Contains(" in ") || trimmed.StartsWith("attribute ") || trimmed.StartsWith("varying "));
        bool isOutput = (trimmed.StartsWith("out ") || trimmed.Contains(" out "));

        if (trimmed.Contains("attribute") || trimmed.Contains("varying") || trimmed.Contains("fragData"))
        {
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
        }

        if ((isInput || isOutput) && trimmed.Contains(";"))
        {
            // Skip built-in variables and uniforms (but NOT user-defined attributes/varyings)
            if (trimmed.Contains("gl_") || trimmed.Contains("uniform"))
            {
                if (trimmed.Contains("attribute") || trimmed.Contains("varying") || trimmed.Contains("fragData"))
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
                continue;
            }

            // Add layout qualifier
            if (isInput && !isOutput)
            {
                line = "layout(location = " + String(inputLocation++) + ") " + trimmed;
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
            }
            else if (isOutput && !isInput)
            {
                line = "layout(location = " + String(outputLocation++) + ") " + trimmed;
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
            }
        }

        // Handle uniform sampler declarations (add binding qualifiers)
        // Uses fixed name→binding map so #ifdef branches don't shift binding numbers
        if (trimmed.StartsWith("uniform sampler") && trimmed.Contains(";") &&
            !trimmed.Contains("layout(binding"))
        {
            String samplerName = ExtractSamplerName(trimmed);
            int binding = GetSamplerBinding(samplerName);
            if (binding < 0)
            {
                // Unknown sampler — use sequential fallback
                binding = samplerBinding++;
            }
            line = "layout(binding = " + String(binding) + ", set = 0) " + trimmed;
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
        }

        // Handle uniform block declarations (add binding qualifier)
        // Named blocks from Uniforms.glsl get FIXED binding numbers to match GetUniformBlockInfo().
        // Custom blocks (from CUSTOM_MATERIAL_CBUFFER shaders) get sequential bindings starting at 11.
        if (trimmed.StartsWith("uniform ") && !trimmed.Contains("sampler") &&
            !trimmed.Contains(";") && !trimmed.Contains("layout(binding"))
        {
            // Extract block name: "uniform BlockName" or "uniform BlockName\n{"
            String blockName = trimmed.Substring(8).Trimmed();
            // Strip trailing '{' if on same line
            if (blockName.EndsWith("{"))
                blockName = blockName.Substring(0, blockName.Length() - 1).Trimmed();

            // Fixed binding map — must match GetUniformBlockInfo() in Graphics_Vulkan.cpp
            int fixedBinding = -1;
            if (blockName == "FrameVS")    fixedBinding = 0;
            else if (blockName == "CameraVS")   fixedBinding = 1;
            else if (blockName == "ZoneVS")     fixedBinding = 2;
            else if (blockName == "LightVS")    fixedBinding = 3;
            else if (blockName == "MaterialVS") fixedBinding = 4;
            else if (blockName == "ObjectVS")   fixedBinding = 5;
            else if (blockName == "FramePS")    fixedBinding = 6;
            else if (blockName == "CameraPS")   fixedBinding = 7;
            else if (blockName == "ZonePS")     fixedBinding = 8;
            else if (blockName == "LightPS")    fixedBinding = 9;
            else if (blockName == "MaterialPS") fixedBinding = 10;

            if (fixedBinding >= 0)
            {
                line = "layout(binding = " + String(fixedBinding) + ", set = 0) " + trimmed;
                // Keep counter above all fixed bindings so custom blocks start at 11+
                if (fixedBinding >= uniformBlockBinding)
                    uniformBlockBinding = fixedBinding + 1;
            }
            else
            {
                // Unknown block (custom) — assign next available binding
                if (uniformBlockBinding < 11)
                    uniformBlockBinding = 11;  // Ensure custom blocks start at 11
                line = "layout(binding = " + String(uniformBlockBinding++) + ", set = 0) " + trimmed;
            }
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
        }
    }

    // Wrap any remaining bare uniforms (not samplers, not in blocks, not layout-qualified)
    // into uniform blocks per #ifdef section. Required for Vulkan GLSL where standalone uniforms
    // are illegal. Source still has #ifdef COMPILEVS/#ifdef COMPILEPS guards (not yet resolved),
    // so we must create separate blocks per section to keep them inside the correct guards.
    // IMPORTANT: each separate #ifdef COMPILEVS/COMPILEPS section gets its OWN block, because
    // they may be in different preprocessor branches (#if/#else) and consolidating would move
    // uniforms into a dead branch.
    {
        struct UniformGroup
        {
            Vector<String> uniforms;
            Vector<unsigned> lineNums;
            char type; // 'V' = VS, 'P' = PS, 'O' = other
        };
        Vector<UniformGroup> groups;
        UniformGroup currentVS, currentPS, currentOther;
        currentVS.type = 'V'; currentPS.type = 'P'; currentOther.type = 'O';

        int ifdefDepth = 0;
        bool inCompileVS = false;
        bool inCompilePS = false;

        for (unsigned i = 0; i < lines.Size(); ++i)
        {
            String trimmed = lines[i].Trimmed();

            // Track #ifdef/#if/#ifndef nesting for COMPILEVS/COMPILEPS
            if (trimmed.StartsWith("#ifdef COMPILEVS"))
            { inCompileVS = true; ifdefDepth = 1; }
            else if (trimmed.StartsWith("#ifdef COMPILEPS"))
            { inCompilePS = true; ifdefDepth = 1; }
            else if ((inCompileVS || inCompilePS) &&
                     (trimmed.StartsWith("#ifdef") || trimmed.StartsWith("#if ") || trimmed.StartsWith("#ifndef")))
            { ifdefDepth++; }
            else if ((inCompileVS || inCompilePS) && trimmed.StartsWith("#endif"))
            {
                ifdefDepth--;
                if (ifdefDepth <= 0)
                {
                    // Section ended — flush accumulated uniforms as a group
                    if (inCompileVS && currentVS.uniforms.Size() > 0)
                    { groups.Push(currentVS); currentVS = UniformGroup(); currentVS.type = 'V'; }
                    if (inCompilePS && currentPS.uniforms.Size() > 0)
                    { groups.Push(currentPS); currentPS = UniformGroup(); currentPS.type = 'P'; }
                    inCompileVS = false; inCompilePS = false; ifdefDepth = 0;
                }
            }

            // Match bare uniforms
            if (trimmed.StartsWith("uniform ") && trimmed.Contains(";") &&
                !trimmed.Contains("sampler") && !trimmed.Contains("layout("))
            {
                String decl = trimmed.Substring(8); // skip "uniform "
                if (inCompileVS)
                { currentVS.uniforms.Push(decl); currentVS.lineNums.Push(i); }
                else if (inCompilePS)
                { currentPS.uniforms.Push(decl); currentPS.lineNums.Push(i); }
                else
                { currentOther.uniforms.Push(decl); currentOther.lineNums.Push(i); }
            }
        }

        // Flush any remaining groups
        if (currentVS.uniforms.Size() > 0) groups.Push(currentVS);
        if (currentPS.uniforms.Size() > 0) groups.Push(currentPS);
        if (currentOther.uniforms.Size() > 0) groups.Push(currentOther);

        // Wrap each group into its own uniform block at its first line position
        int vsBlockNum = 0, psBlockNum = 0, otherBlockNum = 0;
        for (unsigned g = 0; g < groups.Size(); ++g)
        {
            UniformGroup& grp = groups[g];
            if (grp.uniforms.Size() == 0) continue;

            String blockName;
            if (grp.type == 'V')
                blockName = (vsBlockNum++ == 0) ? "CustomVS" : "CustomVS" + String(vsBlockNum);
            else if (grp.type == 'P')
                blockName = (psBlockNum++ == 0) ? "CustomPS" : "CustomPS" + String(psBlockNum);
            else
                blockName = (otherBlockNum++ == 0) ? "CustomUniforms" : "CustomUniforms" + String(otherBlockNum);

            for (unsigned i = 0; i < grp.lineNums.Size(); ++i)
                lines[grp.lineNums[i]] = "";
            unsigned blockBinding = uniformBlockBinding++;
            String block = "layout(binding = " + String(blockBinding) + ", set = 0) uniform " + blockName + "\n{";
            for (unsigned i = 0; i < grp.uniforms.Size(); ++i)
                block += "\n    " + grp.uniforms[i];
            block += "\n};";
            lines[grp.lineNums[0]] = block;

            // Compute std140 offsets for custom block members and store in map
            unsigned currentOffset = 0;
            for (unsigned i = 0; i < grp.uniforms.Size(); ++i)
            {
                String type, name;
                if (ParseUniformDecl(grp.uniforms[i], type, name))
                {
                    unsigned alignment, size;
                    GetStd140Layout(type, alignment, size);
                    // Align current offset
                    currentOffset = (currentOffset + alignment - 1) & ~(alignment - 1);
                    // Store with 'c' prefix stripped for Urho3D parameter name convention
                    // Urho3D uses "BloomMix" internally, shader has "cBloomMix"
                    String paramName = name.StartsWith("c") ? name.Substring(1) : name;
                    ShaderVariation::CustomUniformInfo info;
                    info.binding = blockBinding;
                    info.offset = currentOffset;
                    info.blockSize = 0;  // Will be set after loop
                    customUniformMap[StringHash(paramName)] = info;
                    currentOffset += size;
                }
            }
            // Round up to 16-byte boundary (std140 block size rule) and set on all members
            unsigned blockSize = (currentOffset + 15) & ~15u;
            for (auto it = customUniformMap.Begin(); it != customUniformMap.End(); ++it)
            {
                if (it->second_.binding == blockBinding)
                    it->second_.blockSize = blockSize;
            }

        }
    }

    // Reconstruct source
    result.Clear();
    for (unsigned i = 0; i < lines.Size(); ++i)
    {
        result += lines[i];
        if (i < lines.Size() - 1)
            result += "\n";
    }

    return result;
}

/// \brief Preprocess GLSL shader source before compilation
/// \param source Original GLSL shader source code
/// \param preprocessed Output: preprocessed source after expansion
/// \param compilerOutput Output: preprocessing diagnostics (usually empty on success)
/// \returns true if preprocessing successful, false on error (currently always true)
///
/// \details Handles GLSL preprocessing tasks:
///   1. Adds #version 450 directive if not present
///      - Vulkan requires explicit GLSL version >= 4.50
///      - Allows older code without version directive to work
///   2. Processes #include directives
///      - Extracts filenames from #include "file" and #include <file> syntax
///      - Preserves #include statements for compiler handling
///      - Logs include directives for debugging
///   3. Preserves shader layout and comments
///
/// **#version Directive:**
/// Vulkan shaders MUST have #version >= 450 for SPIR-V compilation.
/// If source lacks version directive, "#version 450" is prepended automatically.
/// This allows legacy GLSL code to work without modification.
///
/// **#include Handling:**
/// Currently preserves #include directives as-is for compiler handling.
/// Future enhancement: Load included files via ResourceCache for inline expansion.
/// Handles both quoted and angled bracket syntax:
///   - #include "common.glsl"  -> extracted as "common.glsl"
///   - #include <math>         -> extracted as "math"
///
/// **Current Status:**
/// Always returns true (no fatal preprocessing errors detected).
/// compilerOutput only populated if errors occur (none currently defined).
bool VulkanShaderCompiler::PreprocessShader(
    const String& source,
    String& preprocessed,
    String& compilerOutput)
{
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
#if VULKAN_SHADER_DEBUG_LOGGING
#endif

    // Start with base preprocessing
    preprocessed = source;

    // Add default version if not present OR replace existing version with Vulkan-compatible one
    // Use GLSL 4.50 (Vulkan 1.0) - required for proper location auto-mapping in glslang
    size_t versionPos = preprocessed.Find("#version");
    if (versionPos != String::NPOS)
    {
        // Replace existing #version with #version 450
        size_t lineEnd = preprocessed.Find("\n", versionPos);
        if (lineEnd == String::NPOS)
            lineEnd = preprocessed.Length();
        preprocessed = "#version 450\n#extension GL_EXT_spec_constant_composites : enable\n" + preprocessed.Substring(lineEnd + 1);
    }
    else
    {
        // No version directive, add #version 450
        preprocessed = "#version 450\n#extension GL_EXT_spec_constant_composites : enable\n" + preprocessed;
    }

    // Inject defines that must be present BEFORE #include expansion,
    // since Uniforms.glsl checks these with #if !defined(GL3) || !defined(USE_CBUFFERS)
    {
        String preamble;
        preamble += "#define GL3\n";
        preamble += "#define URHO_VULKAN\n";
        preamble += "#define USE_CBUFFERS\n";
        preamble += "#define MAXBONES " + String(Graphics::GetMaxBones()) + "\n";

        // Insert after #version and #extension lines (find second newline)
        size_t firstNL = preprocessed.Find("\n");
        size_t secondNL = (firstNL != String::NPOS) ? preprocessed.Find("\n", firstNL + 1) : String::NPOS;
        if (secondNL != String::NPOS)
            preprocessed = preprocessed.Substring(0, secondNL + 1) + preamble + preprocessed.Substring(secondNL + 1);
        else
            preprocessed = preamble + preprocessed;
    }

    // Process #include directives by expanding them inline
    // This allows shaders to be split across multiple files
    // NOTE: Must do this BEFORE adding layout qualifiers so we catch all in/out declarations
    String result;
    size_t pos = 0;
    size_t includePos = preprocessed.Find("#include");
#if VULKAN_SHADER_DEBUG_LOGGING
#endif

    while (includePos != String::NPOS)
    {
        // Add everything before the #include
        result += preprocessed.Substring(pos, includePos - pos);

        // Find the end of the #include line
        size_t lineEnd = preprocessed.Find('\n', includePos);
        if (lineEnd == String::NPOS)
            lineEnd = preprocessed.Length();

        // Extract the #include statement
        String includeLine = preprocessed.Substring(includePos, lineEnd - includePos);

        // Parse the filename from #include "filename" or #include <filename>
        size_t quoteStart = includeLine.Find('"');
        size_t quoteEnd = (quoteStart != String::NPOS) ? includeLine.Find('"', quoteStart + 1) : String::NPOS;

        if (quoteStart == String::NPOS)
        {
            quoteStart = includeLine.Find('<');
            quoteEnd = (quoteStart != String::NPOS) ? includeLine.Find('>', quoteStart + 1) : String::NPOS;
        }

        if (quoteStart != String::NPOS && quoteEnd != String::NPOS)
        {
            String includeFilename = includeLine.Substring(quoteStart + 1, quoteEnd - quoteStart - 1);
            URHO3D_LOGDEBUG("Processing #include directive: " + includeFilename);

            // Load include file from bin/CoreData/Shaders/GLSL/ directory
            String includePath = "bin/CoreData/Shaders/GLSL/" + includeFilename;
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
            std::ifstream includeFile(includePath.CString());
            if (includeFile.is_open())
            {
                std::string includeContent((std::istreambuf_iterator<char>(includeFile)),
                                          std::istreambuf_iterator<char>());
                result += String(includeContent.c_str());
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
                includeFile.close();
            }
            else
            {
                // Fallback: preserve #include if file not found
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
                result += includeLine;
            }
        }
        else
        {
            // Malformed #include, keep as-is
            result += includeLine;
        }

        // Move to next include
        pos = lineEnd;
        includePos = preprocessed.Find("#include", pos);
    }

    // Add remaining source after last include
    result += preprocessed.Substring(pos);
    preprocessed = result;

#if VULKAN_SHADER_DEBUG_LOGGING
#endif

    // NOTE: AddExplicitLayoutQualifiers is called by CompileGLSLToSPIRV after this function returns,
    // so custom uniform metadata can be captured there.

#if VULKAN_SHADER_DEBUG_LOGGING
#endif



    compilerOutput.Clear();
    return true;
}

#ifdef URHO3D_SHADERC
bool VulkanShaderCompiler::CompileWithShaderc(
    const String& source,
    const String& defines,
    ShaderType type,
    Vector<uint32_t>& spirvBytecode,
    String& compilerOutput)
{
    try
    {
        // Create shader compiler
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        // Set optimization level
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        // Add MAXBONES for skeletal animation shaders
        String maxBonesStr = String(Graphics::GetMaxBones());
        options.AddMacroDefinition("MAXBONES", maxBonesStr.CString());

        // Add GL3 and USE_CBUFFERS defines (matches glslang path)
        options.AddMacroDefinition("GL3", "1");
        options.AddMacroDefinition("USE_CBUFFERS", "1");

        // Add shader type define (COMPILEVS, COMPILEPS, or COMPILECS)
        if (type == VS)
            options.AddMacroDefinition("COMPILEVS", "1");
        else if (type == PS)
            options.AddMacroDefinition("COMPILEPS", "1");
        else if (type == CS)
            options.AddMacroDefinition("COMPILECS", "1");

        // Add shader defines
        if (!defines.Empty())
        {
            // Parse defines string (format: "DEFINE1=value1 DEFINE2=value2")
            Vector<String> defineList = defines.Split(' ');
            for (const String& define : defineList)
            {
                if (!define.Empty())
                {
                    if (define.Contains('='))
                    {
                        Vector<String> parts = define.Split('=');
                        options.AddMacroDefinition(parts[0].CString(), parts[1].CString());
                    }
                    else
                    {
                        options.AddMacroDefinition(define.CString(), "1");
                    }
                }
            }
        }

        // Determine shader kind
        shaderc_shader_kind kind = shaderc_vertex_shader;
        if (type == PS)
            kind = shaderc_fragment_shader;
        else if (type == CS)
            kind = shaderc_compute_shader;

        // Compile GLSL to SPIR-V
        shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
            source.CString(),
            kind,
            "shader.glsl",
            "main",
            options
        );

        // Check for compilation errors
        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            compilerOutput = String(result.GetErrorMessage().c_str());
            URHO3D_LOGERROR("shaderc compilation failed: " + compilerOutput);
            return false;
        }

        // Extract SPIR-V bytecode
        const uint32_t* spirvData = result.cbegin();
        size_t spirvSize = result.cend() - result.cbegin();

        spirvBytecode.Clear();
        spirvBytecode.Reserve(spirvSize);
        for (size_t i = 0; i < spirvSize; ++i)
        {
            spirvBytecode.Push(spirvData[i]);
        }

        if (spirvBytecode.Empty())
        {
            compilerOutput = "Error: SPIR-V compilation produced empty bytecode";
            return false;
        }

        // DEBUG: Dump INSTANCED vertex shaders for location verification
        if (type == VS && defines.Contains("INSTANCED"))
        {
            String dumpPath = "/tmp/instanced_vs.spv";
            FILE* f = fopen(dumpPath.CString(), "wb");
            if (f)
            {
                fwrite(&spirvBytecode[0], sizeof(uint32_t), spirvBytecode.Size(), f);
                fclose(f);
                URHO3D_LOGINFO("[SPIRV_DUMP] Wrote " + String(spirvBytecode.Size() * 4) +
                               " bytes to " + dumpPath + " (defines: " + defines + ")");
            }
        }

        compilerOutput = "Success: Compiled to " + String(spirvBytecode.Size() * 4) + " bytes";
        return true;
    }
    catch (const std::exception& e)
    {
        compilerOutput = "Exception in shaderc: " + String(e.what());
        URHO3D_LOGERROR(compilerOutput);
        return false;
    }
}
#else
bool VulkanShaderCompiler::CompileWithShaderc(
    const String& source,
    const String& defines,
    ShaderType type,
    Vector<uint32_t>& spirvBytecode,
    String& compilerOutput)
{
    compilerOutput = "shaderc not available (not compiled with URHO3D_SHADERC)";
    return false;
}
#endif

#ifdef URHO3D_GLSLANG
bool VulkanShaderCompiler::CompileWithGlslang(
    const String& source,
    const String& defines,
    ShaderType type,
    Vector<uint32_t>& spirvBytecode,
    String& compilerOutput)
{

    try
    {
        // Initialize glslang library
        static bool glslangInitialized = false;
        if (!glslangInitialized)
        {
            glslang::InitializeProcess();
            glslangInitialized = true;
        }

        // Determine shader stage
        EShLanguage stage = EShLangVertex;
        if (type == PS)
            stage = EShLangFragment;
        else if (type == CS)
            stage = EShLangCompute;

        // Create shader object
        glslang::TShader shader(stage);

        // Set environment FIRST (before setting strings)
        // Must use Vulkan client for correct SPIR-V (VertexIndex, OriginUpperLeft)
        // Standalone uniforms (CUSTOM_MATERIAL_CBUFFER) will fail — that's a known limitation
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 450);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

        // DISABLE auto-location mapping since we now add explicit layout(location=N) qualifiers
        shader.setAutoMapLocations(false);

        // Override resource limits — default OpenGL limits are too low for our binding scheme (100+)
        TBuiltInResource resources = *GetDefaultResources();
        resources.maxCombinedTextureImageUnits = 256;
        resources.maxTextureUnits = 256;
        resources.maxTextureImageUnits = 256;

        // Auto-binding control:
        // - true: Compiler assigns bindings automatically (may or may not be unique)
        // - false: Use our explicit layout(binding=N) qualifiers (but pipeline layout must match!)
        shader.setAutoMapBindings(true);  // TEST: Check what auto-binding actually produces
        // shader.setAutoMapBindings(false);  // BLOCKED: Pipeline layout doesn't match our custom bindings

        // Prepend defines directly to source instead of using setPreamble
        String sourceWithDefines = source;

        // Build defines block
        String definesBlock;

        // Add GL3 define for GLSL 330+ (enables modern GLSL features)
        definesBlock += "#define GL3\n";
        definesBlock += "#define URHO_VULKAN\n";

        // Add USE_CBUFFERS to force uniform blocks (required for Vulkan SPIR-V)
        definesBlock += "#define USE_CBUFFERS\n";

        // Add shader type define (COMPILEVS, COMPILEPS, or COMPILECS)
        if (type == VS)
            definesBlock += "#define COMPILEVS\n";
        else if (type == PS)
            definesBlock += "#define COMPILEPS\n";
        else if (type == CS)
            definesBlock += "#define COMPILECS\n";

        // Add MAXBONES for skeletal animation shaders
        definesBlock += "#define MAXBONES " + String(Graphics::GetMaxBones()) + "\n";

        // Add user-provided defines
        if (!defines.Empty())
        {
            Vector<String> defineList = defines.Split(' ');
            for (const String& define : defineList)
            {
                if (!define.Empty())
                {
                    definesBlock += "#define " + define + "\n";
                }
            }
        }

        // Insert defines right after #version 330 line (which PreprocessShader adds at the start)
        // The source from PreprocessShader will be: "#version 330\n<original source>"
        // We want: "#version 330\n<defines>\n<original source>"
        if (!definesBlock.Empty())
        {
            size_t versionEnd = sourceWithDefines.Find("#version");
            if (versionEnd != String::NPOS)
            {
                size_t lineEnd = sourceWithDefines.Find("\n", versionEnd);
                if (lineEnd != String::NPOS)
                {
                    sourceWithDefines = sourceWithDefines.Substring(0, lineEnd + 1) + definesBlock + sourceWithDefines.Substring(lineEnd + 1);
                }
            }
            else
            {
                // No #version found, prepend defines at the start
                sourceWithDefines = definesBlock + sourceWithDefines;
            }
#if VULKAN_SHADER_DEBUG_LOGGING
#endif
        }

        // Set shader source strings
        const char* shaderCString = sourceWithDefines.CString();

        shader.setStrings(&shaderCString, 1);

        // Compile shader with relaxed rules for OpenGL-style shaders
        // EShMsgRelaxedErrors allows auto-assignment of input/output locations
        // Note: Removed EShMsgVulkanRules to allow OpenGL-style standalone uniforms (not in blocks)
        EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgSuppressWarnings | EShMsgRelaxedErrors);
        if (!shader.parse(&resources, 330, false, messages))
        {
            compilerOutput = String(shader.getInfoLog()) + "\n" + String(shader.getInfoDebugLog());
            // Dump failing source for debugging
            static int failDump = 0;
            if (failDump < 3)
            {
                String dumpFile = "/tmp/shader_fail_" + String(failDump) + ".glsl";
                std::ofstream dump(dumpFile.CString());
                if (dump.is_open()) { dump << shaderCString; dump.close(); }
                failDump++;
            }
            URHO3D_LOGERROR("glslang shader parsing failed: " + compilerOutput);
            return false;
        }

        // Link into program
        glslang::TProgram program;
        program.addShader(&shader);

        if (!program.link(messages))
        {
            compilerOutput = String(program.getInfoLog()) + "\n" + String(program.getInfoDebugLog());
            URHO3D_LOGERROR("glslang program linking failed: " + compilerOutput);
            return false;
        }

        // Translate to SPIR-V with optimization enabled
        std::vector<uint32_t> spirvTemp;
        glslang::SpvOptions options;
        options.disableOptimizer = false;  // Enable dead code elimination
        glslang::GlslangToSpv(*program.getIntermediate(stage), spirvTemp, &options);

        if (spirvTemp.empty())
        {
            compilerOutput = "Error: SPIR-V generation produced empty bytecode";
            return false;
        }

        // Copy to Urho3D Vector
        spirvBytecode.Clear();
        spirvBytecode.Reserve(spirvTemp.size());
        for (uint32_t word : spirvTemp)
        {
            spirvBytecode.Push(word);
        }

        compilerOutput = "Success: Compiled to " + String(spirvBytecode.Size() * 4) + " bytes";
        return true;
    }
    catch (const std::exception& e)
    {
        compilerOutput = "Exception in glslang: " + String(e.what());
        URHO3D_LOGERROR(compilerOutput);
        return false;
    }
}
#else
bool VulkanShaderCompiler::CompileWithGlslang(
    const String& source,
    const String& defines,
    ShaderType type,
    Vector<uint32_t>& spirvBytecode,
    String& compilerOutput)
{
    compilerOutput = "glslang not available (not compiled with URHO3D_GLSLANG)";
    return false;
}
#endif

String VulkanShaderCompiler::FormatCompilerOutput(const String& rawOutput)
{
    if (rawOutput.Empty())
        return "No compiler output";

    // Simple formatting: capitalize first letter if it's a warning or error
    String formatted = rawOutput;
    if (!formatted.Empty() && formatted[0] >= 'a' && formatted[0] <= 'z')
    {
        formatted[0] = formatted[0] - ('a' - 'A');
    }

    return formatted;
}

bool VulkanShaderCompiler::CheckCompilerAvailability()
{
    String available = GetAvailableCompilers();

    if (available.Empty())
    {
        URHO3D_LOGERROR("========================================");
        URHO3D_LOGERROR("CRITICAL: No shader compiler available!");
        URHO3D_LOGERROR("========================================");
        URHO3D_LOGERROR("Shader compilation will fail. Install one of:");
        URHO3D_LOGERROR("  - shaderc (Google's GLSL compiler)");
        URHO3D_LOGERROR("  - glslang (Khronos reference compiler)");
        URHO3D_LOGERROR("");
        URHO3D_LOGERROR("Ubuntu/Debian: sudo apt-get install glslang-tools");
        URHO3D_LOGERROR("Or build from source: https://github.com/KhronosGroup/glslang");
        URHO3D_LOGERROR("========================================");
        return false;
    }

    URHO3D_LOGINFO("Shader compiler available: " + available);
    return true;
}

String VulkanShaderCompiler::GetAvailableCompilers()
{
    String result;
    bool hasCompiler = false;

#ifdef URHO3D_SHADERC
    if (!result.Empty()) result += " + ";
    result += "shaderc (primary)";
    hasCompiler = true;
#endif

#ifdef URHO3D_GLSLANG
    if (!result.Empty()) result += " + ";
    result += "glslang (fallback)";
    hasCompiler = true;
#endif

    if (!hasCompiler)
    {
        return "";  // No compilers available
    }

    return result;
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
