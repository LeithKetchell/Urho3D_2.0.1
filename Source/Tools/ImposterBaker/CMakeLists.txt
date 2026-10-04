# ImposterBaker — offline billboard texture generator
# Loads a .mdl OR generates an L-system tree, orbits camera, saves N transparent PNG snapshots

# Define target name
set (TARGET_NAME ImposterBaker)

# Pull LTreeGenerator from Sample 60 so L-system trees can be generated in-tool
define_source_files (
    EXTRA_CPP_FILES ../../Samples/UrhoSamples/60_TerrainNode/LTreeGenerator.cpp
    EXTRA_H_FILES   ../../Samples/UrhoSamples/60_TerrainNode/LTreeGenerator.h
)

# LTreeGenerator.cpp uses #include "LTreeGenerator.h"
include_directories (${CMAKE_CURRENT_SOURCE_DIR}/../../Samples/UrhoSamples/60_TerrainNode)

# Setup target — needs graphics subsystem so use setup_main_executable
setup_main_executable (NOBUNDLE)
