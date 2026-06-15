# Reflection Probe Capture And Lighting Design

Date: 2026-06-15

## Stage

Stage 3 of 3.

This stage adds local reflection probes, scene capture, probe blending, and
Forward/DeferredLighting consumption. It builds on:

- `2026-06-15-reflection-filter-brdf-graph-design.md`
- `2026-06-15-scene-bake-cache-design.md`

## Decision

`ReflectionProbeComponent` is a scene component with capture settings and an
influence volume. It behaves like a panorama capture source, but it is not a
normal `CameraComponent` and must not be collected by ordinary view-camera
rendering.

The third render path is:

```text
ReflectionCapture  # scene geometry from probe capture camera -> RadianceMap
```

The local probe bake flow is:

```text
ReflectionProbeComponent
  -> ReflectionCapture
       produces capture.radiance
  -> ReflectionFilter
       produces ReflectionProbeBakeAsset
  -> scene-adjacent cache
  -> SceneResourceTable probe set
  -> common environment lighting shader
  -> Forward and DeferredLighting
```

Stage 3 also adds:

```text
assets/render_paths/reflection_filter_cubemap.render-path.yaml
```

This graph uses `renderPath: ReflectionFilter`, imports `capture.radiance`, and
runs `PrefilterSpecular` plus `ProjectDiffuseSH9`. It does not include the
spherical-map conversion pass from stage 1.

## Scene Component

Add a component with this scene YAML shape:

```yaml
nodes:
  - name: garage_probe
    transform:
      translate: [0.0, 1.5, 0.0]
    components:
      reflectionProbe:
        global: false
        capture:
          projection: cubemap
          resolution: 256
          nearClip: 0.1
          farClip: 50.0
          includeSky: true
        influence:
          shape: sphere
          radius: 12.0
          blendDistance: 2.0
        projection:
          mode: sphere
```

Global sky environment is represented in the runtime probe set as a global
probe record, not as a different lighting system.

## ReflectionCapture Graph

Add:

```text
assets/render_paths/reflection_capture.render-path.yaml
```

Graph:

```yaml
schema: lxe.render-path-graph.v1
name: ReflectionCapture
renderPath: ReflectionCapture
resources:
  imports:
    scene.renderables:
      type: scene-renderables
  outputs:
    capture.radiance:
      type: cubemap
      binding: RadianceMap
      format: RGBA16Float
      extent: settings.captureSize
      mips: 1
passes:
  - id: CaptureRadiance
    stage: raster
    dispatch: draw
    shader: render_paths/ReflectionCapture/capture_radiance
    input:
      kind: cube-face-capture
      capture:
        target: capture.radiance
        faces: 6
        camera: probe-capture
    sources: [scene.renderables]
    targets: [capture.radiance]
```

The executor expands `cube-face-capture` into six render iterations with probe
view-projection matrices. The pass must not include the probe's own helper mesh
or editor-only debug objects unless the probe capture mask explicitly asks for
them.

## Probe Runtime Resource Set

Add scene-level GPU resources:

```cpp
struct ReflectionProbeGpuRecord final {
  Vec4f positionAndRadius;
  Vec4f blendAndFlags;
  u32 prefilteredEnvMapIndex = 0;
  u32 diffuseSh9Index = 0;
  u32 brdfLutIndex = 0;
  u32 _padding = 0;
};

struct ReflectionProbeSetResource final : public IGpuResource {
  std::vector<ReflectionProbeGpuRecord> records;
};
```

The resource table also owns bindless indices or descriptor resources for:

- `PrefilteredEnvMap` cubemaps;
- `DiffuseSH9` coefficient buffers;
- `BrdfLut` textures.

No metadata-only probe record may satisfy `scene.reflectionProbes`.

## Lighting Integration

Add:

```text
assets/shaders/glsl/common/environment_lighting.glsl
```

Both runtime lighting paths include it:

- `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- `assets/shaders/glsl/render_paths/Deferred/deferred_lighting.frag`

The common shader:

- selects the global probe and any local probes whose influence volume contains
  the shaded world position;
- blends local probes by influence weight;
- evaluates diffuse lighting from `DiffuseSH9`;
- evaluates specular lighting from `PrefilteredEnvMap` plus `BrdfLut`;
- returns zero environment lighting when the probe set count is zero.

Deferred is the default path, so `DeferredLighting` must declare
`scene.reflectionProbes` in its graph sources. Forward declares the same source
on the surface pass. Pipeline preparation validates the shader reflection
against the live resource set.

## Scene Bake Orchestration

`bakeScene(scene, options)` extends stage 2:

```text
load scene
scan material contracts for BRDF LUT requirements
bake missing BRDF LUT assets
bake global sky ReflectionFilter asset when environment exists
for each ReflectionProbeComponent:
  run ReflectionCapture
  run ReflectionFilter
  write probe manifest under .lxe-bake/<scene-stem>/probes/<node-id>/
register all live assets into SceneResourceTable
```

After a successful bake, the active scene can render environment lighting in the
same session without reopening the file.

## Tests And Audits

Required tests:

- Scene parser accepts valid `reflectionProbe` components and rejects unknown
  fields.
- Ordinary camera collection ignores `ReflectionProbeComponent`.
- `ReflectionCapture` parser requires `scene.renderables` and
  `capture.radiance`.
- `RenderWorkCompiler` expands `cube-face-capture` into six draw inputs.
- `bakeScene()` writes one local probe asset per reflection probe.
- Forward and DeferredLighting reject `scene.reflectionProbes` when live
  resources are missing.
- Forward and DeferredLighting render with zero environment contribution when
  the probe set is empty.
- A Vulkan smoke scene bakes a tiny local probe, updates `SceneResourceTable`,
  and renders through DeferredLighting with the probe resource set bound.

Required audits:

```bash
rg -n "ReflectionProbeComponent|ReflectionCapture|scene.reflectionProbes|DiffuseSH9|PrefilteredEnvMap" src assets notes docs
rg -n "IrradianceMap" src assets/shaders/glsl/render_paths/Forward assets/shaders/glsl/render_paths/Deferred
```

After this stage, Forward and DeferredLighting must use the common environment
lighting include instead of duplicated IBL math.

## Out Of Scope

- Parallax-corrected box projection beyond storing projection metadata.
- Probe update scheduling during gameplay.
- Probe debug UI.
- Lightmaps and irradiance volumes.
