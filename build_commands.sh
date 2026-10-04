#!/bin/bash
# Build script for Urho3D with Vulkan, CLI11, and spdlog

# Create and enter build directory
mkdir -p build && cd build

# Configure with CMake (enable Vulkan, CLI11, spdlog)
cmake .. \
    -DURHO3D_VULKAN=1 \
    -DCMAKE_BUILD_TYPE=Debug

# Build the project
cmake --build . -j$(nproc)

# Return to root directory
cd ..
