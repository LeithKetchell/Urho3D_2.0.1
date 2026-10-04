# Claudia -- cross-platform Anthropic API client with Urho3D UI.
# Requires URHO3D_NETWORK for HttpRequest (libcurl-backed HTTPS).

if (NOT URHO3D_NETWORK)
    return ()
endif ()

set (TARGET_NAME Claudia)
define_source_files ()
setup_main_executable (NOBUNDLE)
