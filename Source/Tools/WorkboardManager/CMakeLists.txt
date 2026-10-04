# WorkboardManager — GUI dashboard with embedded LLM inference

set (TARGET_NAME WorkboardManager)

# Source files
define_source_files ()

setup_main_executable (NOBUNDLE)

# llama.cpp headers (targets already built by YukiHoho's CMakeLists)
target_include_directories (${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../ThirdParty/llama.cpp/include)

# llama.cpp creates circular deps that span all static archives — --start-group resolves them.
# Build the ThirdParty lib list dynamically so toggling subsystems doesn't break this.
set (_WBM_TP_LIBS "")
foreach (_t Bullet Box2D ik Recast Detour DetourCrowd DetourTileCache
             SLikeNet Civetweb FreeType PugiXml LZ4 StanHull SDL sqlite WebP ETCPACK
             VulkanMemoryAllocator AngelScript)
    if (TARGET ${_t})
        list (APPEND _WBM_TP_LIBS ${_t})
    endif ()
endforeach ()

target_link_libraries (${TARGET_NAME}
    -Wl,--start-group
    $<TARGET_FILE:Urho3D>
    llama ggml ggml-base ggml-cpu ggml-vulkan
    ${_WBM_TP_LIBS}
    ssl crypto dl pthread m rt
    -Wl,--end-group)
