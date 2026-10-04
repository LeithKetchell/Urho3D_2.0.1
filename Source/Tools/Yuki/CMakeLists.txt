# Yuki — public domain LLM data curation and training tool

if (NOT URHO3D_NETWORK OR NOT URHO3D_DATABASE_SQLITE)
    return ()
endif ()

set (TARGET_NAME Yuki)

define_source_files ()

setup_main_executable (NOBUNDLE)
