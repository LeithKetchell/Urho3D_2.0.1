# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Urho3D** is a free, lightweight, cross-platform 2D and 3D game engine implemented in C++, released under the MIT license. It supports multiple scripting languages (AngelScript, Lua) and multiple rendering backends (OpenGL, Direct3D 11).

- **Current Version**: 2.0.1
- **Build System**: CMake 3.15+
- **Supported Platforms**: Windows, Linux, macOS, iOS, tvOS, Android, Web (Emscripten), Raspberry Pi
- **Documentation**: https://urho3d.io/documentation
- **Contributing**: https://urho3d.io/documentation/HEAD/_contribution_checklist.html

## Session Startup Protocol

Claude Code instances in this project coordinate via network sockets and a WorkboardManager application. Every instance is a coder — there are no other roles.

On session start:

1. Registration is automatic — Claudette (which hosts every instance) holds the persistent connection to Manager and registers on your behalf. The `SessionStart` hook (`claude_ipc.sh announce`) does **not** contact Manager; it only surfaces the role Manager assigns
2. Check in with Leith for your assignment
3. Manager can reach you between prompts — react to incoming messages and keep listening

**Instance lifecycle is managed by WorkboardManager — NOT by you.** Never read, write, delete, or check PID files. Never check `/proc/` to determine if processes are alive. Never run `pgrep`, `kill`, or any process management commands on WorkboardManager or other Claude instances. If something looks wrong with instance state, tell Leith — don't try to fix it yourself.

**Registration is Claudette's job, and it self-heals.** Claudette owns the persistent relay connection to Manager: it registers at startup (`StartIPCListener` sends `__HELLO__`) and, if Manager drops the connection, a ~5s health check (`CheckSocketHealth`) reconnects and re-sends `__HELLO__` automatically. This is NOT gated on user activity — `UserPromptSubmit` runs `claude_ipc.sh check`, which is a no-op, and hooks never contact Manager. So if Manager restarts, Claudette reconnects on its own within a few seconds; you don't need to send a message or do anything, and neither hooks nor you should poke process/registry state to "help" it along.

### Spawning Coders

**NEVER use the Task tool to spawn coder instances.** Task subagents are API calls inside your process — they cannot run Bash, cannot register with the network, and are not real processes.

To spawn a new Coder instance, use the dedicated command:

```bash
.claude/hooks/claude_ipc.sh spawn-coder
```

This opens a real terminal with its own `claude` process and own permissions. The new instance registers with Manager via network socket and is auto-assigned the next available coder slot.

## Build Commands

### Quick Start (Linux/macOS)

```bash
# Configure CMake build
script/cmake_generic.sh build

# Build the project
cd build
make -j$(nproc)
```

### Using Rake (Recommended)

The `rakefile` provides high-level build tasks:

```bash
# Configure and build
rake build

# Build with specific options
rake build URHO3D_ANGELSCRIPT=1 URHO3D_LUA=1 URHO3D_PHYSICS=1

# Release build
rake build CMAKE_BUILD_TYPE=Release

# Run tests
rake test

# Generate documentation
rake doc

# Clean build artifacts
rake clean
```

### Cross-Compilation

```bash
# Raspberry Pi / ARM
script/cmake_rpi.sh build

# Web (Emscripten)
script/cmake_emscripten.sh build

# iOS
script/cmake_ios.sh build

# Android (uses Gradle)
rake build PLATFORM=android

# MinGW (Windows)
script/cmake_mingw.sh build
```

### Build Configuration Options

Key CMake options are stored in `script/.build-options`. Common ones:

```bash
-DURHO3D_ANGELSCRIPT=1          # Enable AngelScript support
-DURHO3D_LUA=1                  # Enable Lua support
-DURHO3D_PHYSICS=1              # Enable Bullet physics
-DURHO3D_PHYSICS2D=1            # Enable Box2D physics
-DURHO3D_NAVIGATION=1           # Enable pathfinding (Recast/Detour)
-DURHO3D_URHO2D=1               # Enable 2D engine
-DURHO3D_NETWORK=1              # Enable networking
-DURHO3D_SAMPLES=1              # Build example projects
-DURHO3D_TOOLS=1                # Build development tools
-DURHO3D_TESTING=1              # Enable tests
-DCMAKE_BUILD_TYPE=Release      # Release vs Debug
-DURHO3D_LIB_TYPE=SHARED        # Shared library (default: STATIC)
```

## Multi-Coder Build Safety

When multiple Claude Code instances (coders) are working simultaneously, **never use raw `make` directly**. Use the safe build wrapper:

```bash
# ALWAYS use this instead of raw make:
.claude/hooks/safe_build.sh <TargetName>

# Examples:
.claude/hooks/safe_build.sh 60_TerrainNode
.claude/hooks/safe_build.sh WorkboardManager
.claude/hooks/safe_build.sh AuthServer
```

This acquires a per-target `flock` before running `make -j4`. If another coder is building the same target, the second coder blocks until the first finishes, then benefits from the freshly built artifacts.

**File I/O locking** is handled automatically via PreToolUse/PostToolUse hooks on Read/Write/Edit operations. No manual action needed.

If locks get stuck, use the **Clear File Locks** button in WorkboardManager, or manually: `rm -rf /tmp/urho_claude/locks/*`

**Chain of command:** Leith (boss) → Coders (all Claudettes). Every Claudette is a coder that can plan when asked. Manager brokers all communication via Unix domain sockets.

**Trust chain and noise filtering:**

The elder coder (unnumbered "coder") sits closest to Leith. Junior coders (coder2, coder3, ...) sit further away. Messages and claims flow upward through this chain:

1. **Junior → Elder**: A junior coder's claims, corrections, or pushback are **not trusted at face value**. The elder questions them, verifies against the rules, and rejects anything that doesn't hold up. A junior cannot override the elder's decisions.
2. **Elder → Leith**: Only the elder escalates to Leith — and only questions that survive the filter. If the elder is unsure whether a junior's pushback is valid, the elder asks Leith. The elder never caves to a junior's objection without verification.
3. **Leith → Elder**: Only Leith overrides the elder. No junior coder gets to change a decision the elder has made.
4. **Attribution**: No coder may attribute actions to Leith ("Leith said", "Leith bumped") unless Leith actually said or did it. Misattribution is a lie.

This filters noise. Juniors challenge each other freely. Only verified, important questions reach Leith.

**Workboard hygiene — MANDATORY for ALL coders:**

1. **Claim a task → run `wb-assign` BEFORE doing anything else.** This atomically moves the task from Planned to In Progress and sets the owner. No manual moves, no "I'll update it later." Use `wb-add-inprogress` + `wb-remove` from Planned if needed. Either way: **workboard updated BEFORE the first line of work. Non-negotiable.**
2. **Complete ALL phases of a task → IMMEDIATELY move it from In Progress to Done.** A task is not done until every phase in its plan is finished. When it is, move it right then — not later, not after the next task.
3. **No duplicates.** Moving a task means removing it from the old table and adding it to the new one. Never leave stale entries behind.
4. **No orphans.** Every task you're working on must be visible In Progress. Every task you've finished must be visible in Done. Nothing dangling in Ready/Planned that you've already started or completed.
5. **Review status.** When you finish, mark your own Review column as `Pending`.
6. **Task notes.** All task state lives in SQL, managed by WorkboardManager. No plaintext state files.

Failure to follow these rules creates confusion about what's being worked on, causes duplicate work, and wastes everyone's time. This is not optional.

**Task assignment protocol — MANDATORY:**

When assigning or claiming tasks, **ALWAYS use `wb-assign`** (or `wb-add-inprogress` + `wb-remove` when self-assigning). No exceptions.

1. Use `wb-assign <task-name> <role>` — atomic claim + notify
2. NEVER send a task without updating the workboard FIRST
3. NEVER broadcast a task to multiple Coders — assign to ONE, notify ONE
4. Coders self-assign from the workboard when idle. Check Ready/Planned for available work.

Bypassing the workboard caused a race condition (Mar 29, 2026) where two Coders worked the same task simultaneously. File locks prevented data corruption but the duplicate work was entirely avoidable. These failures are preventable — use the tools.

**Pipeline cache after shader edits:** When a local Coder modifies any GLSL shader file and rebuilds Sample 60, they MUST delete `~/.local/share/urho3d/pipeline_cache.bin` after the build completes. Stale pipeline cache entries will serve old shader bytecode — deleting it ensures the next run recompiles all shaders fresh.

**Claudette version bumps:** If you are going to build Claudette, increment the sub-version number in `CLAUDETTE_VERSION` (Claudette.h) FIRST. Odd sub-version numbers indicate a beta version, even sub-version numbers indicate a Release Candidate.

## Repository Structure

```
Source/
├── Urho3D/              # Core engine library (24 modules)
├── Samples/             # 60+ example projects
├── Tools/               # Development tools (AssetImporter, BindingGenerator, etc.)
└── ThirdParty/          # 30+ external dependencies

cmake/                   # CMake configuration modules
script/                  # Build automation scripts
bin/                     # Data files and game resources
Docs/                    # Doxygen documentation configuration
```

## Core Architecture

### Engine Modules

The engine is divided into 24 specialized modules in `/Source/Urho3D/`:

**Foundation**: Core, Container, Math (event system, data structures, math)
**Engine**: Engine, IO, Resource, Scene (application lifecycle, resource management, scene graph)
**Graphics**: Graphics, GraphicsAPI, UI (rendering pipeline, materials, UI components)
**Specialized**: Input, Audio, Physics, Physics2D, Navigation, Urho2D, IK, Network, Database
**Scripting**: AngelScript, LuaScript (2.35.1 and Lua/LuaJIT bindings)

### Key Architectural Patterns

1. **Event System**: Core event-driven architecture via `Object` and `Context` classes
2. **Component Architecture**: Node/Component pattern for scene graph (`Node`, `Component`, `LogicComponent`)
3. **Resource Management**: Centralized `ResourceCache` for asset loading and caching
4. **Module Layering**: Foundation → Engine → Subsystems → Application
5. **Serialization**: XML-based scene/component serialization via pugixml

### Important Files

| File | Purpose |
|------|---------|
| `/Source/Urho3D/Core/Context.h` | Core engine context and event system |
| `/Source/Urho3D/Core/Object.h` | Base object model and event handling |
| `/Source/Urho3D/Scene/Scene.h` | Scene management and node hierarchy |
| `/Source/Urho3D/Engine/Engine.h` | Main engine class and lifecycle |
| `/CMakeLists.txt` | Root build configuration |
| `/script/.build-options` | All available build configuration options |

## Testing

```bash
# Run all tests
rake test

# Or with CMake
cd build
ctest --verbose

# Tests are located in /Source/Tools/Tests/
```

Tests cover Container and Math modules. Enable with `-DURHO3D_TESTING=1`.

## Code Style and Conventions

Follow the project's [Coding Conventions](https://urho3d.io/documentation/HEAD/_coding_conventions.html):

- Use clang-tidy for static analysis (integrated in CI/CD)
- Use clang-format for code formatting (config in `.clang-format`)
- Follow C++ naming conventions: PascalCase for classes, camelCase for functions/variables
- Doxygen comments for public APIs (C++ style: `///`)

## Documentation

### Generate Doxygen Documentation

```bash
rake doc
# Output: Docs/html/
```

Or manually:
```bash
cd Docs
doxygen Doxyfile
```

### Online Documentation

- Full API Reference: https://urho3d.io/documentation/HEAD/index.html
- GitHub Pages: https://urho3d.github.io

## Working with Samples

The `/Source/Samples/` directory contains 60+ example projects demonstrating engine features:

- **01-10**: Foundational features (rendering, UI, animation)
- **11-20**: Physics, audio, networking, character control
- **21-59**: Scripting, advanced rendering, 2D engine

Each sample is a standalone CMake project. Build all samples with:

```bash
rake build URHO3D_SAMPLES=1
```

## Development Workflow

1. **Locate the relevant module**: Code changes typically belong in one of the 24 modules in `/Source/Urho3D/`
2. **Check examples**: Review `/Source/Samples/` for examples demonstrating similar features
3. **Build and test**: Use `rake build` and `rake test` for validation
4. **Follow conventions**: Use clang-format and clang-tidy for code quality
5. **Test on platforms**: Use cross-compilation scripts for multi-platform validation

## Common Development Tasks

### Build a Single Optimization

```bash
# Debug build for development
rake build CMAKE_BUILD_TYPE=Debug

# Release build with optimizations
rake build CMAKE_BUILD_TYPE=Release

# Static library (default)
rake build URHO3D_LIB_TYPE=STATIC

# Shared library
rake build URHO3D_LIB_TYPE=SHARED
```

### Enable Profiling

```bash
# Tracy profiler support
rake build URHO3D_TRACY_PROFILING=1
```

### Generate Scripting Bindings

```bash
# Regenerate AngelScript/Lua bindings
rake build URHO3D_GENERATEBINDINGS=1
```

## Third-Party Dependencies

Over 30 external libraries integrated in `/Source/ThirdParty/`:

**Graphics**: GLEW, Assimp, ETCPACK
**Physics**: Bullet, Box2D, StanHull
**Scripting**: AngelScript, Lua, LuaJIT
**Audio**: SDL 2.0
**UI/Text**: FreeType, pugixml, Mustache
**Navigation**: Recast, Detour, DetourCrowd
**Networking**: SLikeNet, Civetweb
**Database**: SQLite, nanodbc
**Images**: stb_image, stb_vorbis, WebP
**Other**: RapidJSON, LZ4, boost, Tracy

License information for all dependencies is in `/licenses/`.

## CI/CD Pipeline

Located in `.github/workflows/`:
- **main.yml**: Primary CI/CD across all platforms
- **nightly.yml**: Scheduled nightly builds
- **style.yml**: Code style and formatting checks
- **website.yml**: Documentation website deployment

Uses ccache for build caching and Docker for cross-platform builds.

## Vulkan Graphics Backend (NEW)

### Overview

Urho3D 2.0.1 now includes **Vulkan** as a modern graphics backend alongside legacy OpenGL and Direct3D 11 support. Vulkan is available on:

- **Linux/Windows**: Native Vulkan support
- **macOS/iOS/tvOS**: Via MoltenVK (Vulkan → Metal translation layer)
- **Android**: Native Vulkan support (API 24+)

The engine automatically selects the best available backend with fallback to OpenGL/D3D11 on platforms where Vulkan is unavailable.

### Building with Vulkan

```bash
# Configure with Vulkan backend (Linux)
script/cmake_generic.sh build -DURHO3D_VULKAN=1

# Build with samples
script/cmake_generic.sh build -DURHO3D_VULKAN=1 -DURHO3D_SAMPLES=1

# Or using Rake
rake build URHO3D_VULKAN=1 URHO3D_SAMPLES=1
```

### Graphics API Priority

The engine automatically selects the best available graphics backend in this order:

1. **Vulkan** (if available and `-DURHO3D_VULKAN=1` enabled)
2. **OpenGL** (Linux/macOS default, always available)
3. **Direct3D 11** (Windows fallback)

Only one backend can be enabled per build for safety and clarity.

### Vulkan Architecture

#### Core Components

- **VulkanGraphicsImpl** (`/Source/Urho3D/GraphicsAPI/Vulkan/VulkanGraphicsImpl.h`)
  - Instance, device, and queue management
  - Swapchain creation and presentation
  - Memory allocator integration (VMA)
  - Command buffer management (triple-buffered)
  - Render pass and framebuffer handling
  - Synchronization primitives (fences, semaphores)

- **Graphics_Vulkan.cpp** (`/Source/Urho3D/Graphics/Graphics_Vulkan.cpp`)
  - Frame lifecycle (BeginFrame, EndFrame)
  - Draw call submission
  - State management (blend, cull, depth, etc.)
  - Viewport and scissor configuration

#### Key Features Implemented

✅ **Initialization & Lifecycle**
- Vulkan instance creation with validation layer support
- Physical device selection (prefers dedicated GPUs)
- Queue family management (graphics + present)
- Surface creation via SDL2
- Swapchain with intelligent format/mode selection

✅ **Memory Management**
- Vulkan Memory Allocator (VMA) integration
- Optimal memory type selection
- GPU resource lifecycle management

✅ **Synchronization**
- Per-frame fencing for CPU-GPU sync
- Semaphores for image acquisition/presentation
- Triple-buffering for smooth rendering

✅ **Rendering Pipeline**
- Single render pass with color + depth attachments
- Per-image framebuffers
- Clear value configuration
- Dynamic viewport and scissor

✅ **Command Recording**
- Per-frame command buffers
- Batch command recording
- Proper synchronization between frames

✅ **Buffers & Textures**
- Vertex buffer creation and binding (vkCmdBindVertexBuffers)
- Index buffer creation and binding (vkCmdBindIndexBuffer)
- Dynamic and static buffer support
- 16-bit and 32-bit index sizes
- Texture creation with VkImageView and VkSampler
- Filter modes: NEAREST, BILINEAR, TRILINEAR, ANISOTROPIC
- Address modes: CLAMP, REPEAT, MIRROR

✅ **Shader Compilation**
- GLSL to SPIR-V compilation via shaderc (Google's compiler)
- Fallback to glslang (Khronos reference compiler)
- Shader parameter/define support
- Error reporting with formatted output
- Graceful degradation if compilers unavailable
- Pipeline layout creation framework

✅ **Descriptors & Pipelines (Framework)**
- Descriptor pool with layout caching
- Support for 8 descriptor types (uniform buffers, textures, samplers, etc.)
- SPIR-V reflection framework for descriptor inference
- Graphics pipeline state caching by state hash
- Full mapping of Urho3D graphics states to Vulkan equivalents
- Shader module creation from SPIR-V bytecode
- Complete integration roadmap documented

#### Architecture Patterns

**Dispatch Pattern**: All Graphics methods use runtime dispatch:
```cpp
void Graphics::SetTexture(unsigned index, Texture* texture) {
    if (gapi == GAPI_VULKAN)
        return SetTexture_Vulkan(index, texture);
    else if (gapi == GAPI_OPENGL)
        return SetTexture_OGL(index, texture);
    // ... etc
}
```

**State Caching**: Pipelines and samplers are cached by hash for performance.

**Frame Pipelining**: Triple-buffering ensures GPU and CPU don't stall:
- Frame 0: GPU renders while CPU prepares frame 1
- Frame 1: GPU renders while CPU prepares frame 2
- Frame 2: GPU renders while CPU prepares frame 0

### Current Implementation Status

**Phases Complete:**
- ✅ Phase 1: CMake integration with Vulkan SDK detection
- ✅ Phase 2: API abstraction layer (GAPI_VULKAN enum)
- ✅ Phase 3: Core graphics initialization and frame lifecycle
- ✅ Phase 4: Vertex/Index buffers with VMA allocation
- ✅ Phase 5: Texture loading with sampler management
- ✅ Phase 6: Shader compilation with shaderc/glslang fallback
- ✅ Phase 7: SPIR-V reflection and descriptor pool management
- ✅ Phase 8: Graphics pipeline creation and binding
- ✅ Phase 9: Constant buffers and shader parameters (framework complete)
- ✅ Phase 30: MSAA color image support and depth buffer management
- ✅ Phase 31: G-Buffer attachment creation (4 color + shared depth for deferred rendering)
- ✅ Phase 32: GPU state application - Graphics state to pipeline state conversion
- ✅ Phase 33: Shader module integration - Vertex/fragment shader compilation and binding
- ✅ Phase 34: Deferred Rendering Framework (render target binding, G-Buffer lifecycle, framebuffer selection)
- ✅ Phase 35: Extended Render Pass Descriptor (multi-attachment, multi-subpass support)

**Phases In Progress:**
- 🔄 Phase 36+: Advanced deferred rendering features (geometry shaders, compute, lighting passes)

**Supported Platforms:**
- ✅ Linux (X11/Wayland) - Native Vulkan
- ✅ Windows (Win32) - Native Vulkan
- ✅ macOS - MoltenVK (Vulkan → Metal translation)
- ✅ iOS - MoltenVK (Vulkan → Metal translation)
- ✅ tvOS - MoltenVK (Vulkan → Metal translation)
- ✅ Android - Native Vulkan (API 24+)

**Not Yet Implemented:**
- Full material parameter reflection
- Deferred lighting pass implementation
- Input attachments for lighting
- Instancing optimization

### Known Limitations (v2.0)

- **Texture descriptor sets not yet bound**: `SetTexture_Vulkan()` stores texture pointers but doesn't create/bind Vulkan descriptor sets. Only solid color rendering works (logo), text rendering fails (requires font atlas texture)
- Deferred rendering framework complete, lighting pass not yet implemented
- MSAA support framework exists, full integration pending
- Compute shaders ARE supported: runtime GLSL→SPIR-V compute compilation, `Graphics::DispatchCompute()`, SSBO storage buffers (`SetStorageBuffer`, bound as `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`), batched dispatch (`BeginComputeBatch`/`EndComputeBatch`), and inter-dispatch memory barriers all work (`Graphics_Vulkan.cpp:2323`). Remaining caveats, not gaps: storage buffers are typed `VertexBuffer*` (wrap tensor data in a VertexBuffer — verify it carries `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`); the per-dispatch descriptor write loop binds up to 4 SSBOs; one-shot dispatch does a blocking fence-wait on the graphics queue (no dedicated async-compute queue yet); `SetStorageBuffer_Vulkan`'s body has a stale TODO but binding genuinely happens at dispatch time
- Full shader parameter reflection not yet implemented
- No timeline semaphore support
- Staging buffers not yet optimized (optional Phase 10)
- Input attachments for deferred lighting not yet bound in draw calls

### Debugging with Vulkan

#### Enable Validation

```bash
export VK_LAYER_PATH=/path/to/vulkan-sdk/lib
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
./your_app
```

#### Common Issues

1. **"No Vulkan-capable devices found"**
   - Install Vulkan drivers for your GPU
   - Check NVIDIA/AMD driver versions

2. **"Failed to create swapchain"**
   - Window may not support presentation
   - Try running in windowed mode

3. **"Failed to compile shader"**
   - Ensure glslang or shaderc is installed
   - Check shader source for syntax errors

### Building with MoltenVK (macOS/iOS/tvOS)

**Prerequisites:**
```bash
# macOS: Install MoltenVK via Homebrew
brew install molten-vk

# Or download Vulkan SDK for macOS from:
# https://vulkan.lunarg.com/sdk/home#mac
```

**Building for macOS:**
```bash
# Configure with Vulkan/MoltenVK support
script/cmake_generic.sh build -DURHO3D_VULKAN=1

# Build
cd build && make -j$(nproc)
```

**Building for iOS:**
```bash
# Configure iOS build with Vulkan/MoltenVK
script/cmake_ios.sh build-ios -DURHO3D_VULKAN=1

# Build
cd build-ios && make -j$(nproc)
```

**MoltenVK Limitations:**
- No geometry shaders (not supported by Metal)
- Limited tessellation support
- Some Vulkan 1.1+ features unavailable
- Compute shader support varies by device
- Best results with forward rendering paths

### Building for Android with Vulkan

**Prerequisites:**
- Android NDK with API level 24+ support
- Vulkan is natively supported (no additional libraries needed)

**Building:**
```bash
# Configure Android build with Vulkan
rake build PLATFORM=android URHO3D_VULKAN=1

# Or manually with Gradle
cd android
./gradlew assembleRelease -PANDROID_ABI=arm64-v8a -PURHO3D_VULKAN=1
```

**Android Manifest Requirements:**
Your AndroidManifest.xml should include:
```xml
<uses-feature android:name="android.hardware.vulkan.version"
              android:version="0x400003"
              android:required="false"/>
<uses-sdk android:minSdkVersion="24"/>
```

**Testing Android Vulkan:**
- Not all Android devices support Vulkan (check device specs)
- Vulkan drivers vary in quality across vendors
- Test on multiple devices (Qualcomm Adreno, ARM Mali, etc.)
- Use validation layers during development

### Future Enhancements

- Compute shader support
- Advanced descriptor management
- Timeline semaphore support
- Deferred rendering optimization
- Ray tracing support (desktop only)

### Vulkan Resources

- [Vulkan Official Registry](https://www.khronos.org/vulkan/)
- [Vulkan Specification](https://www.khronos.org/registry/vulkan/)
- [Vulkan Samples](https://github.com/KhronosGroup/Vulkan-Samples)
- [VMA Documentation](https://gpuopen.com/vulkan-memory-allocator/)

## ProfilerUI in Examples (STANDARD PATTERN)

All 55+ examples now follow a consistent pattern for displaying the Vulkan profiler UI overlay with proper styling.

### Required Setup in Example Headers (.h)

```cpp
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class YourExample : public Sample
{
    // ... class definition ...

private:
    /// Profiler UI overlay
    SharedPtr<ProfilerUI> profilerUI_;
};
```

### Required Setup in Start() Method

```cpp
void YourExample::Start()
{
    // Execute base class startup
    Sample::Start();

    // Load UI style for ProfilerUI (must be before creating UI elements)
    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    // ... create scene, UI, setup viewport, subscribe to events ...

    // Set the mouse mode to use in the sample
    Sample::InitMouseMode(MM_FREE);

    // Initialize profiler UI
    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler());
    profilerUI_->SetVisible(true);
}
```

### Required Setup in HandleUpdate() Method

```cpp
void YourExample::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // ... your update code ...

    // Update profiler display
    if (profilerUI_)
    {
        GetSubsystem<Graphics>()->GetVulkanProfiler()->RecordFrame(timeStep);
        profilerUI_->Update();
    }
}
```

### Key Points

1. **UI style must be loaded FIRST** - before any UI element creation
2. **ProfilerUI must be initialized LAST** - after InitMouseMode() and all other setup
3. **HandleUpdate must call both RecordFrame() and Update()** - to properly display metrics
4. **All examples use the same pattern** - ensures consistency across the codebase

### Current Status

✅ **All 55 numbered examples + benchmark properly configured**
✅ **Complete build verification successful**
✅ **See EXAMPLES_UI_FIXES_SUMMARY.md for detailed changes**

## Deferred Rendering (Phase 36 - ✅ CORE COMPLETE)

### Overview

Urho3D's deferred rendering system is fully implemented and working on OpenGL and Direct3D 11. The system uses a two-pass approach:

1. **G-Buffer Pass**: Geometry is rendered to multiple render targets (position, normal, albedo, specular)
2. **Lighting Pass**: Light volumes are rendered, reading from G-Buffer textures to compute lighting

**Vulkan Status:** ✅ Core implementation complete, ready for end-to-end testing

### Architecture

**Complete Flow (OpenGL/D3D11 - Reference Implementation)**:
1. `View::Define()` - Detects `CMD_LIGHTVOLUMES` in render path, sets `deferred_ = true`
2. `View::GetLightBatches()` - Creates light volume batches with appropriate geometry (sphere/cone/quad)
3. `Renderer::SetLightVolumeBatchShaders()` - Selects shader variations based on light type, shadows, etc.
4. `View::ExecuteRenderPathCommands()` - Executes lighting pass:
   - Binds G-Buffer textures as shader inputs
   - Sets up graphics state (additive blend, depth test, stencil)
   - Renders light volume geometry
5. `DeferredLight.glsl` - Shader reads G-Buffer, computes lighting, outputs to framebuffer

**Key Components**:
- **Light Geometries**: Fullscreen quad (directional), sphere (point), cone (spot)
- **Shader Variations**: 64 pixel shader variants for different light types/features
- **Stencil Optimization**: Mark-to-stencil prevents redundant lighting calculations
- **State Management**: Custom depth test (CMP_GREATER inside light volume, CMP_LESSEQUAL outside)

### Vulkan Implementation Status (Phase 36)

**✅ Completed Infrastructure** (December 2024):
- ✅ G-Buffer creation (4 attachments: position, normal, albedo, specular)
- ✅ Render pass framework with multi-subpass support
- ✅ Full-screen quad geometry for lighting pass
- ✅ Shader compilation (GLSL → SPIR-V)
- ✅ Graphics pipeline creation and binding
- ✅ State management (blend, depth, stencil, cull)
- ✅ Render target switching
- ✅ Material descriptor binding

**✅ Phase 36 Complete Features**:
- ✅ SetTexture_Vulkan() - Stores G-Buffer textures for descriptor binding
- ✅ SetShaderParameter_Vulkan() - Batches shader parameters for upload
- ✅ CalculateParameterBufferSize() - Computes std140 buffer layout
- ✅ PackShaderParameters() - Packs parameters with std140 alignment
- ✅ UploadPendingShaderParameters_Vulkan() - Uploads constant buffers to GPU
- ✅ CreateConstantBufferDescriptorSet_Vulkan() - Creates descriptor sets
- ✅ BindConstantBufferDescriptors_Vulkan() - Binds descriptors to pipeline
- ✅ Multi-set pipeline layouts - 4 sets: materials, textures, constants, input attachments
- ✅ VulkanConstantBufferPool integration - Efficient GPU memory allocation
- ✅ Full build verification - All 55+ samples compile successfully

**⏳ Pending Testing**:
1. **End-to-End Runtime Testing**:
   - Run `56_DeferredRendering` sample with Vulkan backend
   - Verify G-Buffer pass + lighting pass execute correctly
   - Test with multiple light types (point, spot, directional)
   - Validate with Vulkan validation layers

2. **Performance Profiling**:
   - Compare frame times vs. OpenGL backend
   - Profile descriptor set allocations with RenderDoc
   - Measure constant buffer upload overhead

**⚠️ Future Optimizations** (Phase 37+):
- Descriptor set caching (avoid reallocations per frame)
- Dynamic descriptor sets with buffer offsets
- Parameter change tracking (skip unchanged uploads)
- Push constants for small parameters (<128 bytes)

### Code Locations

| Component | File | Lines | Status |
|-----------|------|-------|--------|
| Texture binding API | `Graphics_Vulkan.cpp` | 708 | ✅ Complete |
| Shader parameters API | `Graphics_Vulkan.cpp` | 1140-1196 | ✅ Complete |
| Buffer size calculation | `Graphics_Vulkan.cpp` | 1202-1260 | ✅ Complete |
| Parameter packing | `Graphics_Vulkan.cpp` | 1262-1408 | ✅ Complete |
| Constant buffer upload | `Graphics_Vulkan.cpp` | 1645-1710 | ✅ Complete |
| Descriptor set creation | `Graphics_Vulkan.cpp` | 1518-1600 | ✅ Complete |
| Descriptor set binding | `Graphics_Vulkan.cpp` | 1602-1628 | ✅ Complete |
| VulkanGraphicsImpl binding | `VulkanGraphicsImpl.cpp` | 3146-3174 | ✅ Complete |
| G-Buffer creation | `VulkanGraphicsImpl.cpp` | 503 | ✅ Complete |
| Full-screen quad | `VulkanGraphicsImpl.cpp` | 1506 | ✅ Complete |
| Render pass subpasses | `VulkanGraphicsImpl.cpp` | 1867 | ✅ Complete |
| Material descriptors | `Graphics_Vulkan.cpp` | 256 | ✅ Complete |
| Multi-set pipeline layouts | `VulkanShaderProgram.cpp` | 111 | ✅ Complete |
| Constant buffer pool | `VulkanConstantBufferPool.h/cpp` | Full file | ✅ Complete |

### Testing Deferred Rendering

Once implementation is complete, test with:

```bash
# Run sample with deferred render path
./01_HelloWorld -renderpath RenderPaths/Deferred.xml

# Enable profiling
./01_HelloWorld -renderpath RenderPaths/Deferred.xml -profile
```

**Expected Behavior**:
- G-Buffer pass renders geometry to 4 render targets
- Lighting pass composites lights using light volume geometry
- Final output shows fully lit scene with shadows (if enabled)
- ProfilerUI shows G-Buffer pass + lighting pass timings

## Vulkan Backend Selection (December 2025)

### Status: ✅ Infrastructure Complete, Constructor Implementation Pending

The Vulkan backend can now be selected at runtime. When built with `-DURHO3D_VULKAN=1`, Vulkan becomes the default graphics API.

### Changes Implemented

1. **Engine Parameter** (`EngineDefs.h:18`):
   ```cpp
   static const String EP_VULKAN = "Vulkan";
   ```

2. **Engine Initialization** (`Engine.cpp:175-177, 193-197`):
   - Vulkan set as default GAPI when `URHO3D_VULKAN=1`
   - Command-line override: `-Vulkan=true` or `-OpenGL=true`

3. **Graphics Constructor Dispatch** (`Graphics.cpp:607-613`):
   - `Constructor_Vulkan()` called when `gapi == GAPI_VULKAN`

### Usage

```bash
# Vulkan is default when URHO3D_VULKAN=1
./your_app

# Force OpenGL even when Vulkan is available
./your_app -OpenGL=true

# Explicitly request Vulkan
./your_app -Vulkan=true
```

### Remaining Work

**Constructor_Vulkan() Implementation** (`Graphics_Vulkan.cpp:27-30`):
- Currently a stub that only logs a message
- Needs to initialize VulkanGraphicsImpl, SDL window, and Vulkan instance
- See `VULKAN_BACKEND_INTEGRATION.md` for detailed implementation plan

### Documentation

- Full details: `/VULKAN_BACKEND_INTEGRATION.md`
- Phase 36 status: `/PHASE_36_STATUS.md`

---

## Known Build Options and Platforms

- **32/64-bit**: `-DURHO3D_64BIT=1` (default: auto-detect)
- **Graphics API**: `-DURHO3D_VULKAN=1` (Linux/Windows, new), `-DURHO3D_OPENGL=1` (Linux/macOS default), or `-DURHO3D_D3D11=1` (Windows)
- **CPU Features**: `-DURHO3D_SSE=1`, `-DURHO3D_MMX=1`, `-DURHO3D_3DNOW=1`
- **Android**: Uses Gradle build system (separate from CMake)
- never revert to urho3d 1.9 github - this is not the same animal
- always ask before assuming old code is bad code
- in future, you have access to older working code, compare them
- older code can be found one folder up, check the datestamps, use the most recent one as a reference
- older code is stored in zip format
- before removing or replacing any code, enumerate what it does and what will be lost — even when explicitly told to remove it. Raise objections if the code has functional value that may have been overlooked. Be objective, not eager to please.
- all new .md and .csv files will be stored in the Claude folder
- URHO3D_GLSLANG flag must be specified to build urho3d lib when vulkan backend is specified, otherwise no support for glsl to spirv runtime compilation
- Always use Urho3D engine functionality where possible (FileSystem, File, Log, ResourceCache, etc.) — do not use raw platform-specific APIs (fopen, std::ifstream, POSIX calls) out of laziness. The engine has cross-platform support for file I/O, logging, threading, and more. Check before reaching for platform APIs.