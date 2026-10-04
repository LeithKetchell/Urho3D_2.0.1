# KarenTest — headless telemetry emitter for testing Karen.

if (NOT URHO3D_NETWORK)
    return ()
endif ()

set (TARGET_NAME KarenTest)

define_source_files ()

setup_main_executable ()
