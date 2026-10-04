# Gary — LAN file transfer daemon (ZAR compressed, RUDP transport)

if (NOT URHO3D_NETWORK)
    return ()
endif ()

set (TARGET_NAME Gary)

define_source_files ()

setup_main_executable (NOBUNDLE)
