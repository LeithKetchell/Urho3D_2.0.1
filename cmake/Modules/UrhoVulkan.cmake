# Copyright (c) 2008-2024 the Urho3D project
# License: MIT
#
# Vulkan SDK detection and configuration for Urho3D

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. Please install Vulkan SDK or set VULKAN_SDK environment variable.")
endif()

message(STATUS "Vulkan found: ${Vulkan_LIBRARIES}")
message(STATUS "Vulkan include: ${Vulkan_INCLUDE_DIRS}")

# Validate Vulkan SDK version (require 1.0+)
if(Vulkan_VERSION)
    message(STATUS "Vulkan SDK version: ${Vulkan_VERSION}")
    if(Vulkan_VERSION VERSION_LESS "1.0.0")
        message(FATAL_ERROR "Vulkan SDK version ${Vulkan_VERSION} is too old. Require 1.0.0 or later.")
    endif()
else()
    message(WARNING "Could not determine Vulkan SDK version. Proceeding anyway.")
endif()

# Add Vulkan include directories
include_directories(${Vulkan_INCLUDE_DIRS})

# Find and configure VMA (Vulkan Memory Allocator)
set(VMA_PATH "${CMAKE_SOURCE_DIR}/Source/ThirdParty/VMA")
if(EXISTS "${VMA_PATH}/include/vk_mem_alloc.h")
    message(STATUS "Found VMA (Vulkan Memory Allocator) at: ${VMA_PATH}")
    include_directories(${VMA_PATH}/include)
    set(VMA_FOUND_MSG "FOUND")
    set(VMA_FOUND TRUE)
    # VMA should be built as part of the main Urho3D library CMakeLists.txt
else()
    message(WARNING "VMA (Vulkan Memory Allocator) not found at ${VMA_PATH}")
    set(VMA_FOUND_MSG "NOT FOUND")
    set(VMA_FOUND FALSE)
endif()

# Link Vulkan library
link_libraries(${Vulkan_LIBRARIES})

# Find shader compilation tools (shaderc or glslang)
# These are optional but strongly recommended for runtime shader compilation
find_package(ShaderC QUIET)

if(ShaderC_FOUND)
    message(STATUS "Found shaderc: ${ShaderC_LIBRARY}")
    set(URHO3D_SHADERC TRUE)
    set(SHADER_COMPILER_FOUND TRUE)
    add_definitions(-DURHO3D_SHADERC)
else()
    # Try to find glslang as fallback
    find_package(glslang QUIET)
    if(glslang_FOUND)
        message(STATUS "Found glslang for shader compilation (fallback)")
        set(URHO3D_GLSLANG TRUE CACHE BOOL "glslang shader compiler available" FORCE)
        set(SHADER_COMPILER_FOUND TRUE)
        # Note: URHO3D_GLSLANG define is added via generated Urho3D.h, not command line
    else()
        message(WARNING "Neither shaderc nor glslang found. Runtime shader compilation will be disabled.")
        message(WARNING "Install Vulkan SDK (includes shaderc) or glslang library for shader compilation support.")
        set(SHADER_COMPILER_FOUND FALSE)
    endif()
endif()

# Link shader compiler libraries if found
if(ShaderC_FOUND)
    list(APPEND LIBS ${ShaderC_LIBRARY})
    if(ShaderC_INCLUDE_DIR)
        include_directories(${ShaderC_INCLUDE_DIR})
    endif()
endif()

if(glslang_FOUND OR URHO3D_GLSLANG)
    # Find glslang library directory from Vulkan SDK
    if(DEFINED ENV{VULKAN_SDK})
        set(GLSLANG_LIB_DIR "$ENV{VULKAN_SDK}/lib")
    else()
        # Try to find from Vulkan package
        get_filename_component(GLSLANG_LIB_DIR "${Vulkan_LIBRARIES}" DIRECTORY)
    endif()

    # Link glslang libraries with full paths
    find_library(GLSLANG_LIB NAMES glslang PATHS ${GLSLANG_LIB_DIR})
    find_library(SPIRV_LIB NAMES SPIRV PATHS ${GLSLANG_LIB_DIR})
    find_library(GLSLANG_RESOURCE_LIB NAMES glslang-default-resource-limits PATHS ${GLSLANG_LIB_DIR})

    # Find all SPIRV-Tools libraries (required by glslang)
    find_library(SPIRV_TOOLS_LIB NAMES SPIRV-Tools PATHS ${GLSLANG_LIB_DIR})
    find_library(SPIRV_TOOLS_OPT_LIB NAMES SPIRV-Tools-opt PATHS ${GLSLANG_LIB_DIR})
    find_library(SPIRV_TOOLS_LINK_LIB NAMES SPIRV-Tools-link PATHS ${GLSLANG_LIB_DIR})
    find_library(SPIRV_TOOLS_REDUCE_LIB NAMES SPIRV-Tools-reduce PATHS ${GLSLANG_LIB_DIR})
    find_library(SPIRV_TOOLS_LINT_LIB NAMES SPIRV-Tools-lint PATHS ${GLSLANG_LIB_DIR})
    find_library(SPIRV_TOOLS_DIFF_LIB NAMES SPIRV-Tools-diff PATHS ${GLSLANG_LIB_DIR})

    # Early warning if core SPIRV-Tools is missing (critical for glslang)
    if(NOT SPIRV_TOOLS_LIB)
        message(WARNING "SPIRV-Tools library NOT FOUND in ${GLSLANG_LIB_DIR}")
        message(WARNING "This is required for glslang shader compilation!")
        message(WARNING "")
        message(WARNING "Install full Vulkan SDK from: https://vulkan.lunarg.com/")
        message(WARNING "  - Includes: Vulkan, SPIRV-Tools, shaderc, glslang, validation layers")
        message(WARNING "")
        message(WARNING "Or install SPIRV-Tools separately:")
        message(WARNING "  - Linux (Debian/Ubuntu): sudo apt-get install spirv-tools libspirv-dev")
        message(WARNING "  - Linux (Fedora/RHEL):   sudo dnf install spirv-tools spirv-tools-devel")
        message(WARNING "  - macOS (Homebrew):      brew install spirv-tools")
        message(WARNING "  - Windows:               Install Vulkan SDK (includes SPIRV-Tools)")
    endif()

    if(GLSLANG_LIB AND SPIRV_LIB AND GLSLANG_RESOURCE_LIB)
        # Link in correct order: glslang depends on SPIRV-Tools-opt, which depends on SPIRV-Tools
        list(APPEND LIBS ${GLSLANG_LIB} ${SPIRV_LIB} ${GLSLANG_RESOURCE_LIB})
        # Add all SPIRV-Tools libraries (order matters for static linking)
        foreach(_lib ${SPIRV_TOOLS_OPT_LIB} ${SPIRV_TOOLS_LINK_LIB} ${SPIRV_TOOLS_REDUCE_LIB} ${SPIRV_TOOLS_LINT_LIB} ${SPIRV_TOOLS_DIFF_LIB} ${SPIRV_TOOLS_LIB})
            if(_lib)
                list(APPEND LIBS ${_lib})
            endif()
        endforeach()
        # Cache so define_dependency_libs can propagate these to tools linking against Urho3D
        set (URHO3D_GLSLANG_LIBS ${GLSLANG_LIB} ${SPIRV_LIB} ${GLSLANG_RESOURCE_LIB} CACHE STRING "glslang+SPIRV libs needed by tools using VulkanShaderCompiler" FORCE)
        message(STATUS "Linked glslang libraries: ${GLSLANG_LIB}, ${SPIRV_LIB}, ${GLSLANG_RESOURCE_LIB}")
        if(SPIRV_TOOLS_LIB)
            message(STATUS "Linked SPIRV-Tools libraries (all components)")
        endif()
        set(SHADER_COMPILER_FOUND TRUE)
    else()
        message(WARNING "Could not find all glslang libraries in ${GLSLANG_LIB_DIR}")
    endif()

    if(glslang_INCLUDE_DIR)
        include_directories(${glslang_INCLUDE_DIR})
    endif()
endif()

# Define Vulkan-specific compiler flags based on platform
if(CMAKE_SYSTEM_NAME MATCHES "Linux|Unix")
    set(VULKAN_PLATFORM_DEFINES
        VK_USE_PLATFORM_XCLIB_KHR
        VK_USE_PLATFORM_WAYLAND_KHR
    )
elseif(WIN32)
    set(VULKAN_PLATFORM_DEFINES VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    # MoltenVK support for Apple platforms
    if(IOS OR TVOS)
        set(VULKAN_PLATFORM_DEFINES
            VK_USE_PLATFORM_IOS_MVK
            VK_USE_PLATFORM_METAL_EXT
        )
    else()
        set(VULKAN_PLATFORM_DEFINES
            VK_USE_PLATFORM_MACOS_MVK
            VK_USE_PLATFORM_METAL_EXT
        )
    endif()
elseif(ANDROID)
    # Native Android Vulkan support (API 24+)
    set(VULKAN_PLATFORM_DEFINES VK_USE_PLATFORM_ANDROID_KHR)
endif()

# Add Vulkan compiler definitions with proper -D prefix
foreach(_define ${VULKAN_PLATFORM_DEFINES})
    add_definitions(-D${_define})
endforeach()

# MoltenVK library detection for Apple platforms
if(APPLE AND URHO3D_VULKAN)
    # Search for MoltenVK in common installation locations
    find_library(MOLTENVK_LIBRARY
        NAMES MoltenVK
        PATHS
            /usr/local/lib
            $ENV{VULKAN_SDK}/lib
            $ENV{VULKAN_SDK}/MoltenVK/dylib/macOS
            $ENV{VULKAN_SDK}/MoltenVK/dylib/iOS
            ~/VulkanSDK/*/MoltenVK/dylib/macOS
        PATH_SUFFIXES MoltenVK
        NO_DEFAULT_PATH
    )

    # Also try system paths as fallback
    find_library(MOLTENVK_LIBRARY NAMES MoltenVK)

    if(MOLTENVK_LIBRARY)
        message(STATUS "MoltenVK found: ${MOLTENVK_LIBRARY}")
        # Link MoltenVK library
        link_libraries(${MOLTENVK_LIBRARY})

        # Add MoltenVK include path if available
        get_filename_component(MOLTENVK_LIB_DIR ${MOLTENVK_LIBRARY} DIRECTORY)
        get_filename_component(MOLTENVK_SDK_DIR ${MOLTENVK_LIB_DIR}/../.. ABSOLUTE)
        if(EXISTS "${MOLTENVK_SDK_DIR}/include")
            include_directories(${MOLTENVK_SDK_DIR}/include)
            message(STATUS "MoltenVK headers: ${MOLTENVK_SDK_DIR}/include")
        endif()
    else()
        message(WARNING "MoltenVK library not found. Vulkan support will be unavailable on this Apple platform.\n"
                        "Install via: brew install molten-vk\n"
                        "Or download from: https://github.com/KhronosGroup/MoltenVK")
        set(URHO3D_VULKAN OFF CACHE BOOL "Vulkan disabled - MoltenVK not found" FORCE)
    endif()
endif()

# Android Vulkan library linking
if(ANDROID AND URHO3D_VULKAN)
    # Android has native Vulkan support starting from API 24
    # The vulkan library is provided by the system
    link_libraries(vulkan)
    message(STATUS "Android native Vulkan support enabled (requires API 24+)")
endif()

# Function to compile GLSL shaders to SPIR-V
function(compile_glsl_to_spirv GLSL_SOURCE SPIRV_OUTPUT)
    if(ShaderC_FOUND)
        add_custom_command(
            OUTPUT ${SPIRV_OUTPUT}
            COMMAND ${ShaderC_GLSLC} ${GLSL_SOURCE} -o ${SPIRV_OUTPUT}
            DEPENDS ${GLSL_SOURCE}
            COMMENT "Compiling ${GLSL_SOURCE} to SPIR-V"
        )
    elseif(glslang_FOUND)
        add_custom_command(
            OUTPUT ${SPIRV_OUTPUT}
            COMMAND ${glslang_VALIDATOR} -V ${GLSL_SOURCE} -o ${SPIRV_OUTPUT}
            DEPENDS ${GLSL_SOURCE}
            COMMENT "Compiling ${GLSL_SOURCE} to SPIR-V (glslang)"
        )
    else()
        message(WARNING "Cannot compile shaders: shader compiler not found")
    endif()
endfunction()

# ============================================================================
# Vulkan Configuration Validation Summary
# ============================================================================
message(STATUS "")
message(STATUS "========== Vulkan Configuration Summary ==========")
message(STATUS "Vulkan SDK:              FOUND (${Vulkan_LIBRARIES})")
message(STATUS "VMA (Memory Allocator):  ${VMA_FOUND_MSG}")
message(STATUS "Shader Compiler:         ${SHADER_COMPILER_FOUND}")
if(ShaderC_FOUND)
    message(STATUS "  - Primary: shaderc (Google's GLSL compiler)")
elseif(glslang_FOUND OR URHO3D_GLSLANG)
    message(STATUS "  - Primary: glslang (Khronos reference compiler)")
    if(SPIRV_TOOLS_LIB)
        message(STATUS "  - SPIRV-Tools: FOUND")
    else()
        message(STATUS "  - SPIRV-Tools: NOT FOUND (WARNING: shader compilation may fail)")
    endif()
endif()
message(STATUS "Platform Definitions:    ${VULKAN_PLATFORM_DEFINES}")
message(STATUS "==================================================")
