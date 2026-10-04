# Copyright (c) 2008-2022 the Urho3D project
# License: MIT

if (NOT URHO3D_ANGELSCRIPT)
    return ()
endif ()

# Define target name
set (TARGET_NAME ScriptCompiler)

# Define source files
define_source_files ()

# Setup target
setup_executable (TOOL)
