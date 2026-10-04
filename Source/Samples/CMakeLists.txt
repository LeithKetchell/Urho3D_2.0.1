# Copyright (c) 2008-2022 the Urho3D project
# License: MIT

# Need at least one sample set enabled to proceed
if (NOT URHO3D_SAMPLES AND NOT URHO3D_BULLET_SAMPLES)
    return ()
endif ()

# Set project name
project (Urho3D-Samples)

# Find Urho3D library
find_package (Urho3D REQUIRED)
include_directories (${URHO3D_INCLUDE_DIRS})

# Include common to all samples
set (COMMON_SAMPLE_H_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Sample.h" "${CMAKE_CURRENT_SOURCE_DIR}/Sample.inl")

# Define dependency libs
set (INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/UrhoSamples)

# Macro for adding sample subdirectory
macro (add_sample_subdirectory SOURCE_DIR)
    if (DEFINED ENV{included_sample})
        if (DEFINED ENV{INCLUDED_SAMPLE_${SOURCE_DIR}})
            add_subdirectory (${SOURCE_DIR} ${ARGN})
        endif ()
    elseif (NOT DEFINED ENV{EXCLUDED_SAMPLE_${SOURCE_DIR}})
        add_subdirectory (${SOURCE_DIR} ${ARGN})
    endif ()
endmacro ()

# Discover and add Urho samples (controlled by URHO3D_SAMPLES)
if (URHO3D_SAMPLES)
    file (GLOB_RECURSE URHO_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} UrhoSamples/*/CMakeLists.txt)
    list (SORT URHO_DIRS)
    foreach (DIR ${URHO_DIRS})
        get_filename_component (DIR ${DIR} PATH)
        if (DIR)
            add_sample_subdirectory (${DIR})
        endif ()
    endforeach ()
endif ()

# Discover and add Bullet samples (controlled by URHO3D_BULLET_SAMPLES)
if (URHO3D_BULLET_SAMPLES)
    set (BULLET_SAMPLES_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/BulletSamples CACHE INTERNAL "")
    file (GLOB_RECURSE BULLET_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} BulletSamples/*/CMakeLists.txt)
    list (SORT BULLET_DIRS)
    foreach (DIR ${BULLET_DIRS})
        get_filename_component (DIR ${DIR} PATH)
        if (DIR)
            add_sample_subdirectory (${DIR})
        endif ()
    endforeach ()
endif ()
