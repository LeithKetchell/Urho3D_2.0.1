# Copyright (c) 2008-2022 the Urho3D project
# License: MIT

# Define target name
set (TARGET_NAME 60_TerrainNode)

# Define source files
define_source_files (EXTRA_H_FILES ${COMMON_SAMPLE_H_FILES})

# Setup target with resource copying
if (EMSCRIPTEN)
    # Override the Urho3D default
    if (URHO3D_TESTING AND URHO3D_TEST_TIMEOUT LESS 15)
        set (URHO3D_TEST_TIMEOUT 15)
    endif ()
endif ()
setup_main_executable ()

# Link serialization is handled by .claude/hooks/safe_build.sh (project + target
# locks). A second flock here would deadlock against safe_build.sh's held fd —
# the parent holds the target lock via exec 200>, child flock would block on
# the same inode. safe_build.sh is the authoritative serializer now.

# Setup test cases
setup_test ()
