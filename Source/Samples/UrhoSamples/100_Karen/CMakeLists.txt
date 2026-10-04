# Karen — RUDP telemetry viewer.

if (NOT URHO3D_NETWORK)
    return ()
endif ()

set (TARGET_NAME 100_Karen)

define_source_files ()

setup_main_executable ()
