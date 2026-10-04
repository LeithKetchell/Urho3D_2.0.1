# Karen — standalone telemetry collector + display hub.
# The single collector every KarenClient dials into (binds KAREN_PORT / 25771).
# One Karen per system: a second instance fails the port bind and exits.

set (TARGET_NAME Karen)

# Auto-glob Karen.cpp / Karen.h in this directory.
define_source_files ()

setup_main_executable (NOBUNDLE)
