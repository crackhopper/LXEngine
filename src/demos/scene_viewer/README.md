# demo_scene_viewer

The default playground demo. Loads `DamagedHelmet.gltf`, renders it through
the project's Vulkan backend with the existing Blinn-Phong material, adds a
ground plane, a directional light, and a minimal ImGui overlay with
Orbit / FreeFly camera modes.

## Purpose

- Collapse the "does the engine actually run end-to-end?" question into a
  single executable
- Provide the default integration target for future scene features (Sponza,
  shadows, IBL, post-processing)
- Keep a human-friendly UI surface so camera / light tweaks are observable
  without editing source

This demo is **not** a tutorial and **not** a CI test.

## Upstream requirements

| REQ | Provides |
|-----|----------|
| REQ-010 | Asset layout + `cdToWhereAssetsExist` |
| REQ-011 | Real `GLTFLoader` (used on `DamagedHelmet`) |
| REQ-012 | `IInputState` interface |
| REQ-013 | `Sdl3InputState` and SDL event pump |
| REQ-014 | `Clock` and smoothed delta time |
| REQ-015 | `OrbitCameraController` |
| REQ-016 | `FreeFlyCameraController` |
| REQ-017 | ImGui overlay on `VulkanRenderer::setDrawUiCallback` |
| REQ-018 | `LX_infra::debug_ui` helpers (Stats/Camera/Light panels) |
| REQ-020 | `EngineLoop` driving the frame pump |

## Build & run

### Build

```sh
cmake --build build --target demo_scene_viewer
```

`LX_BUILD_DEMOS` is `ON` by default, so a plain `cmake --build build` also
produces the demo.

### Run

The vendored SDL3 ships as a shared library; point the loader at it:

```sh
export LD_LIBRARY_PATH=build/_deps/sdl3-build:$LD_LIBRARY_PATH
./build/src/demos/scene_viewer/demo_scene_viewer
```

The demo now initializes an explicit runtime asset root and fails fast
with a non-zero exit code if the `assets/` tree cannot be found.

## Controls

| Key / Mouse | Effect |
|-------------|--------|
| `F1` | Toggle the Help panel |
| `F2` | Switch between Orbit and FreeFly camera modes |

### Orbit mode (default)

- Left-drag — rotate around the target
- Right-drag — pan the target
- Wheel — zoom in / out

### FreeFly mode

- Right-button held — look around
- `W` / `A` / `S` / `D` — translate forward / left / back / right
- `Space` — ascend
- `LShift` — descend
- `LCtrl` — hold to accelerate

Switching modes preserves the current view direction so the framing stays
continuous.

## Known limitations

- **Material bridging is transitional glue, not full PBR.** The demo uses the
  existing `blinnphong_0` shader. `baseColorTexture` is bridged into the
  `albedoMap` binding; `metallicRoughnessTexture`, `normalTexture`,
  `occlusionTexture`, and `emissiveTexture` are read from glTF but not wired
  to the shader. Full PBR is a downstream REQ.
- **DamagedHelmet.gltf in this repository does not declare TANGENT.** The
  demo uses a placeholder tangent value and keeps `enableNormal=0` so the
  placeholder is never sampled.
- **ImGui is a swapchain overlay, not a FrameGraph pass.** This matches
  REQ-017's design and is intentional.
- **SDL is the primary backend.** GLFW builds may compile but are not
  validated through this demo.
- **First release scene is DamagedHelmet + ground only.** Sponza and other
  scenes are downstream extension targets.

## Manual acceptance checklist

Reviewers: run through these after a successful build. The demo is not
registered with CTest.

1. `demo_scene_viewer` launches and shows a window.
2. DamagedHelmet and the ground plane are visible in the viewport.
3. Orbit mode allows left-drag rotate, right-drag pan, and wheel zoom.
4. Pressing `F2` switches to FreeFly; `W`/`A`/`S`/`D`/`Space`/`LShift`/`LCtrl`
   all move the camera as described above.
5. The Stats, Camera, Directional Light, and Help panels are visible and
   interactive.
6. Dragging values in the Camera or Directional Light panels produces
   visible changes in the next rendered frame.
7. Closing the window exits the process cleanly (no crash, no hanging
   validation errors in the console).
