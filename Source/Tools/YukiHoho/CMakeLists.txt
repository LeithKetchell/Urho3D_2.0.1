# YukiHoho — standalone binary removed; LLM embedded in WorkboardManager.
# This CMakeLists exists solely to build llama.cpp libraries (llama, ggml, ggml-base,
# ggml-cpu, ggml-vulkan) that WorkboardManager links against.

if (NOT URHO3D_VULKAN AND NOT Vulkan_FOUND)
    return ()
endif ()

set (GGML_VULKAN ON CACHE BOOL "" FORCE)
set (GGML_CPU ON CACHE BOOL "" FORCE)
set (GGML_NATIVE ON CACHE BOOL "" FORCE)
set (LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set (LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set (LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set (LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set (LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set (BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory (${CMAKE_CURRENT_SOURCE_DIR}/../../ThirdParty/llama.cpp llama.cpp)
