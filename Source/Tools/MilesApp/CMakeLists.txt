# MilesApp: Urho3D tool integrating Miles as an AI subsystem

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../NewGuy/mistral_tools.cpp")
    return ()
endif ()

# Define target name
set(TARGET_NAME MilesApp)

# Pull Miles' source files from NewGuy
define_source_files(
    EXTRA_CPP_FILES
        ../../../../NewGuy/mistral_tools.cpp
        ../../../../NewGuy/mistral_client.cpp
        ../../../../NewGuy/exec_tools.cpp
        ../../../../NewGuy/fs_tools.cpp
        ../../../../NewGuy/hooks.cpp
        MilesApp.cpp
    EXTRA_H_FILES
        ../../../../NewGuy/mistral_tools.hpp
        ../../../../NewGuy/mistral_client.hpp
        ../../../../NewGuy/exec_tools.hpp
        ../../../../NewGuy/fs_tools.hpp
        ../../../../NewGuy/hooks.hpp
        MilesApp.h
)

# Include NewGuy directory for Miles' headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../../NewGuy)

# Setup target
setup_main_executable(NOBUNDLE)
