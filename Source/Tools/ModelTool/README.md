# ModelTool

ModelTool is the primary asset workstation for Urho3D. It combines model inspection, animation editing, audio production, and event authoring into a single interactive application.

## Features

### Model Preview
- Load and inspect any Urho3D `.mdl` model with real-time 3D rendering
- Orbit, pan, and zoom camera controls
- Bind pose display and bone hierarchy inspector
- Skinned vertex overlay for rig debugging
- Bounding box visualization (F5 debug toggle)
- Material discovery and display

### Animation Editing
- Full animation catalogue browser — cycle through all `.ani` files associated with a model
- Scrub timeline with playback controls (play, pause, step, speed)
- Animation trimming — extract sub-ranges and save as new `.ani` files
- Animation splitting — divide an animation at the scrub point into two separate files
- Folder browsing with Next/Previous/Flag/Reject workflow for batch asset review

### Text Key Authoring
- Create custom event keys associated with animation keyframes
- Text keys are stored as JSON sidecars alongside `.ani` files
- Keys fire as Urho3D events during animation playback — usable for footstep sounds, particle triggers, gameplay callbacks, or any per-frame event
- Keys support the full Urho3D Variant hierarchy (strings, floats, vectors, nested maps)
- Schema template loader with 9 built-in templates for common event types
- ResourceRef pickers with browse dialogs and type dropdowns

### Audio Recording
- Built-in audio capture for recording sound effects directly within the tool
- Selectable audio input device (microphone, virtual device, network stream, or any source the OS exposes)
- Mono 16-bit PCM at 44.1 kHz — ideal for 3D positional audio (SoundSource3D)
- Pre-opened capture device keeps the hardware codec hot for zero-latency recording
- Save captured audio as `.wav` files directly into the project's `Sounds/` directory
- Preview playback of recorded audio before saving
- Combined with text key authoring, enables a complete audio event pipeline: record a sound, scrub to the animation frame, place the text key, all without leaving the tool

### Asset Pipeline Integration
- Invokes AssetTool internally for format conversion
- FBX export support — recover editable source files from compiled `.mdl` models (a capability unique to this Urho3D fork)
- Model info display (bounding box diagonal, bone count, vertex count)
- Auto-scale normalization via `-normalize` flag

### Network and Multi-Coder Support
- Network sockets for remote control from Claude Code instances or other tools
- Commands: load, browse, keep, reject — enables automated asset review pipelines
- PID registration for process discovery
- Bracketed-paste responses to calling instances

## Usage

```bash
# Launch interactively
./ModelTool

# The tool opens with a 3D viewport, animation timeline, and audio panel.
# Use File > Open to load a model, or drag-and-drop.
```

## Build

```bash
# Built as part of the Urho3D tools suite
cmake -DURHO3D_TOOLS=1 ..
make ModelTool
```

## Formerly Known As

ModelTool was previously named ModelViewer. The rename reflects the tool's evolution from a simple model inspector into a full asset workstation.
