# MeshGenerator

Generates Urho3D `.mdl` model files from simple XML mesh descriptions.

## Usage

```bash
MeshGenerator <input.xml> <output.mdl>
```

## Supported Shapes

### Box
```xml
<mesh type="box" sizeX="1.0" sizeY="1.0" sizeZ="1.0" />
```
- `sizeX`, `sizeY`, `sizeZ` (float, default=1.0) — dimensions along each axis
- Centered at origin, extends from `-size/2` to `+size/2`
- 24 vertices (4 per face), 36 indices

### Sphere
```xml
<mesh type="sphere" radius="0.5" segments="16" rings="8" />
```
- `radius` (float, default=0.5)
- `segments` (int, default=16) — longitude subdivisions
- `rings` (int, default=8) — latitude subdivisions

### Capsule
```xml
<mesh type="capsule" radius="0.5" height="2.0" segments="16" rings="8" />
```
- `radius` (float, default=0.5) — hemisphere radius
- `height` (float, default=2.0) — **total height including caps**
- `segments` (int, default=16) — circumference subdivisions
- `rings` (int, default=8) — vertical subdivisions per hemisphere
- Centered at origin, extends from `-height/2` to `+height/2` along Y
- Cylinder body height = `height - 2*radius` (0 if height <= diameter, becomes a sphere)

### Cylinder
```xml
<mesh type="cylinder" radius="0.5" height="1.0" segments="16" />
```
- `radius` (float, default=0.5)
- `height` (float, default=1.0)
- `segments` (int, default=16)
- Base at `y=0`, top at `y=height`
- Includes top and bottom caps

### Cone
```xml
<mesh type="cone" radius="0.5" height="1.0" segments="16" />
```
- `radius` (float, default=0.5) — base radius
- `height` (float, default=1.0)
- `segments` (int, default=16)
- Base at `y=0`, apex at `y=height`
- Includes base cap

## XML Format

Single self-closing `<mesh>` element. All attributes are optional — missing values use defaults shown above.

```xml
<mesh type="sphere" radius="1.0" segments="32" rings="16" />
```

## Output Format

UMDL (legacy bitmask format). Vertex elements:
- Position (3 floats)
- Normal (3 floats)
- TexCoord0 (2 floats)

Element mask: `0x0B` (Position | Normal | TexCoord1)

16-bit indices (max 65535 vertices per mesh).

## Examples

See the `examples/` subdirectory for ready-to-use XML files:

```bash
# Generate all primitives
MeshGenerator examples/box.xml Box.mdl
MeshGenerator examples/sphere.xml Sphere.mdl
MeshGenerator examples/capsule.xml Capsule.mdl
MeshGenerator examples/cylinder.xml Cylinder.mdl
MeshGenerator examples/cone.xml Cone.mdl
```

## Limitations

- No tangent generation (needed for normal mapping)
- No per-vertex colors
- No LOD variants
- No skeletal/morph animation
- Static geometry only
- Output is geometry only — materials must be created separately
