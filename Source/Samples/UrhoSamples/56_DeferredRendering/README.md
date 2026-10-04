# 56 - Deferred Rendering

This sample demonstrates the deferred rendering pipeline in Urho3D, including how to:

- Set up and switch between forward and deferred rendering paths
- Create multiple point lights in a scene
- Configure render path XML files
- Manage viewports with custom rendering pipelines
- Compare rendering performance with many lights

## Overview

**Deferred Rendering** is a rendering technique that decouples lighting calculations from geometry rendering. Instead of computing lighting per-vertex or per-pixel during the forward pass, deferred rendering:

1. **G-Buffer Pass**: Renders scene geometry to multiple render targets (albedo, normals, depth)
2. **Light Volume Pass**: Reconstructs lighting by reading from G-Buffers
3. **Transparency Pass**: Renders transparent objects using forward lighting

This approach scales better with many lights compared to forward rendering.

## Key Concepts

### Render Paths

Urho3D provides several pre-configured render paths:

- **Forward.xml** - Traditional forward rendering (one light pass per object)
- **Deferred.xml** - Standard deferred rendering with 3 G-Buffers
- **PBRDeferred.xml** - PBR deferred rendering with 4 G-Buffers (higher quality)

### G-Buffer Structure

The deferred render path creates multiple render targets:

```xml
<rendertarget name="albedo" sizedivisor="1 1" format="rgba" />    <!-- Diffuse color -->
<rendertarget name="normal" sizedivisor="1 1" format="rgba" />    <!-- World normals -->
<rendertarget name="depth" sizedivisor="1 1" format="lineardepth" /> <!-- Linear depth -->
```

### Light Volumes

Lights are rendered as geometric volumes (spheres for point lights, cones for spot lights). Only pixels inside the light volume are affected, improving performance.

## Controls

| Key | Action |
|-----|--------|
| **W/A/S/D** | Move camera forward/left/back/right |
| **Mouse Right-Click + Move** | Look around |
| **1** | Switch to Forward Rendering |
| **2** | Switch to Deferred Rendering |
| **3** | Switch to PBR Deferred Rendering |
| **Space** | Toggle light animation (placeholder) |
| **ESC** | Exit |

## Code Example

### Setting Up a Viewport with Deferred Rendering

```cpp
// Get the resource cache
auto* cache = GetSubsystem<ResourceCache>();
auto* renderer = GetSubsystem<Renderer>();

// Create a viewport
SharedPtr<Viewport> viewport(new Viewport(context_, scene_, camera));

// Load the deferred render path
XMLFile* deferredPath = cache->GetResource<XMLFile>("RenderPaths/Deferred.xml");
viewport->SetRenderPath(deferredPath);

// Set as the active viewport
renderer->SetViewport(0, viewport);
```

### Alternative: Using Default Render Path

```cpp
renderer->SetDefaultRenderPath(cache->GetResource<XMLFile>("RenderPaths/Deferred.xml"));
```

### Creating Point Lights

```cpp
// Create a light node
Node* lightNode = scene_->CreateChild("PointLight");
lightNode->SetPosition(Vector3(x, y, z));

// Create and configure the light component
auto* light = lightNode->CreateComponent<Light>();
light->SetLightType(LIGHT_POINT);
light->SetRange(20.0f);  // Light influence radius
light->SetColor(Color::RED);
light->SetBrightness(1.5f);
```

### Switching Render Paths at Runtime

```cpp
void DeferredRendering::HandleRenderPathToggle()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* renderer = GetSubsystem<Renderer>();

    XMLFile* renderPathFile = nullptr;

    switch (currentRenderPath_)
    {
    case 0:
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/Forward.xml");
        break;
    case 1:
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/Deferred.xml");
        break;
    case 2:
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/PBRDeferred.xml");
        break;
    }

    if (renderPathFile)
    {
        Viewport* viewport = renderer->GetViewport(0);
        if (viewport)
        {
            viewport->SetRenderPath(renderPathFile);
        }
    }
}
```

## Understanding Render Path XML

### G-Buffer Pass

```xml
<command type="scenepass" pass="deferred" marktostencil="true" vertexlights="true" metadata="gbuffer">
    <output index="0" name="viewport" />    <!-- Final frame buffer -->
    <output index="1" name="albedo" />      <!-- Diffuse color -->
    <output index="2" name="normal" />      <!-- World-space normals -->
    <output index="3" name="depth" />       <!-- Linear depth -->
</command>
```

### Light Volume Pass

```xml
<command type="lightvolumes" vs="DeferredLight" ps="DeferredLight">
    <texture unit="albedo" name="albedo" />    <!-- Read G-Buffers -->
    <texture unit="normal" name="normal" />
    <texture unit="depth" name="depth" />
</command>
```

The `DeferredLight` shader:
1. Reconstructs world position from depth
2. Reads normal and albedo from G-Buffers
3. Computes lighting contribution
4. Accumulates to the frame buffer

## Performance Considerations

### When to Use Deferred Rendering

✅ **Good for:**
- Scenes with 10+ dynamic lights
- Static geometry with many lights
- Performance-critical applications

❌ **Not ideal for:**
- Scenes with few lights (forward rendering is simpler)
- Heavy transparency (deferred + transparency = expensive)
- Highly varied materials (forward rendering blends better)

### G-Buffer Memory

Each G-Buffer render target uses video memory:

```
Frame Buffer:     4 channels × 8 bits = 32 bits per pixel
Albedo:           4 channels × 8 bits = 32 bits per pixel
Normal:           4 channels × 8 bits = 32 bits per pixel
Depth:            1 channel  × 32 bits = 32 bits per pixel
───────────────────────────────────────────────────
Total per pixel: 128 bits = 16 bytes per pixel

1920×1080 resolution: ~32 MB additional memory
```

## Advanced Configurations

### PBR Deferred Rendering

Use higher-precision G-Buffers for physically-based rendering:

```cpp
XMLFile* pbrPath = cache->GetResource<XMLFile>("RenderPaths/PBRDeferred.xml");
viewport->SetRenderPath(pbrPath);
```

Features:
- `rgba16f` format for G-Buffers (higher precision)
- Separate specular G-Buffer for material properties
- `PBRDeferred` shader with physically-based lighting

### Hardware Depth Optimization

For platforms with efficient hardware depth sampling:

```cpp
XMLFile* hwDepthPath = cache->GetResource<XMLFile>("RenderPaths/DeferredHWDepth.xml");
viewport->SetRenderPath(hwDepthPath);
```

### Post-Processing Effects

Attach post-processing to deferred render path:

```cpp
SharedPtr<RenderPath> deferredPath = viewport->GetRenderPath()->Clone();
deferredPath->Append(cache->GetResource<XMLFile>("PostProcess/FXAA2.xml"));
deferredPath->Append(cache->GetResource<XMLFile>("PostProcess/Tonemap.xml"));
viewport->SetRenderPath(deferredPath);
```

## Debugging Deferred Rendering

### Visualizing G-Buffers

The `RenderPath` class allows you to visualize G-Buffers:

```cpp
SharedPtr<RenderPath> debugPath = viewport->GetRenderPath()->Clone();

// Enable debug visualization of specific G-Buffer
debugPath->SetShaderParameter("DebugMode", 1);  // Visualize albedo
debugPath->SetShaderParameter("DebugMode", 2);  // Visualize normals
debugPath->SetShaderParameter("DebugMode", 3);  // Visualize depth

viewport->SetRenderPath(debugPath);
```

### Profiling

Use the Vulkan profiler to measure deferred rendering performance:

```cpp
// Enable Vulkan profiling during build
rake build URHO3D_VULKAN=1 URHO3D_TRACY_PROFILING=1
```

## Related Resources

- **Render Path Reference**: `bin/CoreData/RenderPaths/`
- **Deferred Shaders**: `bin/CoreData/Shaders/GLSL/DeferredLight.glsl`
- **Material System**: See sample 42_PBRMaterials
- **Post-Processing**: See sample 09_MultipleViewports

## Further Reading

- [GPU Gems 3: Deferred Rendering](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-19-deferred-rendering)
- [Urho3D Documentation](https://urho3d.io/documentation/)
- [Khronos Vulkan Specification](https://www.khronos.org/vulkan/)
