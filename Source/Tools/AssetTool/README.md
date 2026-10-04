# AssetTool

AssetTool is the command-line asset conversion utility for Urho3D. It imports models, animations, and scenes from a wide range of source formats, and can also export back to FBX as a means to recover or edit assets when the original source files have been lost.

## Features

### Import (Source Format to Urho3D)

Converts 3D assets from any format supported by Assimp 6.0.4 into native Urho3D resources:

- **model** — Import a single model to `.mdl` format
- **anim** — Extract animations to `.ani` format
- **scene** — Import a complete scene with node hierarchy, lights, and materials
- **node** — Import a node and its children as a reusable prefab
- **lod** — Combine multiple Urho3D models as LOD levels of a single output model

Supported source formats include FBX, glTF 2.0, OBJ, Collada (DAE), Blender, 3DS, and 40+ others. See the [Assimp format list](http://assimp.sourceforge.net/main_features_formats.html) for the complete set.

### Export (Urho3D to FBX)

Converts native Urho3D `.mdl` models back to FBX 7.5 format:

```bash
AssetTool export input.mdl output.fbx [-anim Walk.ani -anim Run.ani ...]
```

This is a recovery and interoperability feature — when original source files (Blender, Maya, Max) are lost or unavailable, the export path reconstructs an editable FBX from the compiled Urho3D model. Animations can be bundled into the export. This capability is unique to this fork of Urho3D.

### Inspection

- **info** — Display model or animation metadata: bounding box diagonal, bone count, vertex count, animation duration and channels. Works on both native `.mdl`/`.ani` and all Assimp-supported formats.
- **dump** — Print the full scene node hierarchy for debugging. No output file generated.

## Usage

```bash
# Import a model from FBX
AssetTool model character.fbx character.mdl

# Import with scale correction
AssetTool model character.fbx character.mdl -scale 0.01

# Import animations only
AssetTool anim character.fbx character

# Export an Urho3D model back to FBX with animations
AssetTool export character.mdl character.fbx -anim Walk.ani -anim Run.ani

# Inspect a model
AssetTool info character.mdl

# Import a full scene
AssetTool scene level.fbx level.xml

# Combine LOD levels
AssetTool lod 0 high.mdl 10 medium.mdl 50 low.mdl output.mdl
```

## Common Options

| Flag | Description |
|------|-------------|
| `-scale <f>` | Apply uniform scale to imported geometry |
| `-normalize` | Auto-scale model to a normalized bounding box |
| `-b` | Save scene in binary format (default: XML) |
| `-j` | Save scene in JSON format (default: XML) |
| `-h` | Generate hard normals if input has none |
| `-na` | Do not output animations |
| `-nm` | Do not output materials |
| `-nt` | Do not output material textures |
| `-o` | Optimize redundant submeshes (loses hierarchy and animations) |
| `-mb <n>` | Maximum bones per submesh (default: 64) |
| `-s <filter>` | Include non-skinning bones matching filter |
| `-f <freq>` | Animation tick frequency (default: 4800) |

## Build

```bash
# Built as part of the Urho3D tools suite
cmake -DURHO3D_TOOLS=1 ..
make AssetTool
```

## Formerly Known As

AssetTool was previously named AssetImporter. The rename reflects the tool's bidirectional capability — it imports from 40+ formats and exports to FBX.
