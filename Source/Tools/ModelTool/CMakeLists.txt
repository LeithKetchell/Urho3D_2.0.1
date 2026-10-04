# ModelTool — interactive model inspection and asset workstation
# Loads .mdl files, displays mesh info, skeleton, animations, materials

# Define target name
set (TARGET_NAME ModelTool)

# Define source files
define_source_files ()

# Setup target — needs graphics subsystem
setup_main_executable (NOBUNDLE)
