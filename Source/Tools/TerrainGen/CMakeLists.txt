# Copyright (c) 2008-2022 the Urho3D project
# License: MIT
#
# World Expansion — CLI wrapper around AuthServer's TerrainGenerator class.

# Define target name
set (TARGET_NAME TerrainGenerator)

# Pull AuthServer's TerrainGenerator implementation in via EXTRA_CPP_FILES so
# we share the canonical Perlin/ridged/erosion machinery instead of duplicating it.
define_source_files (
    EXTRA_CPP_FILES ../AuthServer/TerrainGenerator.cpp
    EXTRA_H_FILES   ../AuthServer/TerrainGenerator.h ../AuthServer/PerlinNoise.h
)

# AuthServer's TerrainGenerator.cpp uses #include "TerrainGenerator.h" — add the
# AuthServer dir to its include search path so the relative include resolves.
include_directories (${CMAKE_CURRENT_SOURCE_DIR}/../AuthServer)

# Setup target
setup_executable (TOOL)
