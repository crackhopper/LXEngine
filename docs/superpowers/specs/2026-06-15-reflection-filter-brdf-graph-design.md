# Reflection Filter And BRDF Graph Design

Date: 2026-06-15

## Stage

Stage 1 of 3.

This stage replaces the private `IBLBake` renderer with graph-authored bake
runtime paths for global/static environment data:

```text
ReflectionFilter  # sky spherical map -> PrefilteredEnvMap + DiffuseSH9
BrdfLutBake       # BSDF integration model -> BrdfLutAsset
```

`ReflectionCapture` for local scene probes is not implemented in this stage. It
is specified in
`docs/superpowers/specs/2026-06-15-reflection-probe-capture-lighting-design.md`.

## Decision

The engine stops treating IBL bake as one private backend feature. The graph is
the source of truth for environment filtering and BRDF LUT integration. The
backend may still provide cubemap and readback helpers, but it must consume
`RenderPathGraph`, `RenderInput`, and `RenderInputDesc` data. It must not
hardcode bake shader names, pass order, or descriptor layout.

The durable runtime flow for this stage is:

```text
scene environment spherical map or imported radiance cubemap
  -> assets/render_paths/reflection_filter_spherical.render-path.yaml
  -> ReflectionProbeBakeAsset(global = true)
       - PrefilteredEnvMap
       - DiffuseSH9
       - optional RadianceMap

material contract requiresBrdfLut
  -> assets/render_paths/brdf_lut_bake.render-path.yaml
  -> BrdfLutAsset
```

The complete environment bake asset is `ReflectionProbeBakeAsset`.
`PrefilteredEnvMap` is only the specular cubemap field inside that asset.

## Current Context

Current repository facts:

- `src/backend/vulkan/details/ibl_bake_renderer.cpp` hardcodes
  `equirect_to_cubemap`, `ibl_irradiance_convolve`, `ibl_prefilter_env`, and
  `ibl_brdf_lut`.
- `test_vulkan_ibl_bake` validates that private renderer directly.
- `RenderPathGraphResourceParser` accepts only `Forward`, `Deferred`, and
  `OfflineRT`.
- `RenderPassInputKind` accepts only `scene-renderables`,
  `fullscreen-triangle`, and `compute-dispatch`.
- Current Forward and DeferredLighting shaders use `IrradianceMap`,
  `PrefilteredEnvMap`, and `BrdfLut` under `HAS_IBL`.
- The target model uses `DiffuseSH9` for diffuse environment lighting instead
  of a generated `IrradianceMap` cubemap. Runtime shader consumption moves in
  stage 3.

## Graph Assets

Add:

```text
assets/render_paths/reflection_filter_spherical.render-path.yaml
assets/render_paths/brdf_lut_bake.render-path.yaml
```

### ReflectionFilter

`ReflectionFilter` is the render path domain. Stage 1 adds the spherical-map
graph asset for global sky filtering:

```text
assets/render_paths/reflection_filter_spherical.render-path.yaml
```

Stage 3 adds the cubemap-input companion graph for local probe capture. The two
assets share the same `renderPath: ReflectionFilter` value, shader directory,
resource schema, and executor. Each graph keeps a static pass list.

```yaml
schema: lxe.render-path-graph.v1
name: ReflectionFilter
renderPath: ReflectionFilter
resources:
  imports:
    environment.sphericalMap:
      type: texture2d
      binding: SphericalEnvironmentMap
      projection: equirectangular
  outputs:
    filter.radiance:
      type: cubemap
      binding: RadianceMap
      format: RGBA16Float
      extent: settings.radianceSize
      mips: 1
    filter.prefiltered:
      type: cubemap
      binding: PrefilteredEnvMap
      format: RGBA16Float
      extent: settings.prefilterSize
      mips: settings.prefilterMipCount
    filter.diffuseSh9:
      type: sh9
      binding: DiffuseSH9
      format: RGB32Float
      coefficients: 9
passes:
  - id: SphericalToCubemap
    stage: raster
    dispatch: draw
    shader: render_paths/ReflectionFilter/spherical_to_cubemap
    input:
      kind: cube-face-filter
      filter:
        source: environment.sphericalMap
        target: filter.radiance
        faces: 6
    sources: [environment.sphericalMap]
    targets: [filter.radiance]

  - id: PrefilterSpecular
    stage: raster
    dispatch: draw
    shader: render_paths/ReflectionFilter/prefilter_specular_env
    input:
      kind: cube-mip-face-filter
      filter:
        source: filter.radiance
        target: filter.prefiltered
        roughness: from-mip
        sampleCount: settings.specularSampleCount
    sources: [filter.radiance]
    targets: [filter.prefiltered]

  - id: ProjectDiffuseSH9
    stage: compute
    dispatch: compute
    shader: render_paths/ReflectionFilter/project_diffuse_sh9
    input:
      kind: compute-dispatch
      dispatch:
        groups: settings.shProjectionGroups
    sources: [filter.radiance]
    targets: [filter.diffuseSh9]
```

Missing `environment.sphericalMap` is a diagnostic for the spherical graph. The
stage 3 cubemap graph imports `capture.radiance` directly and does not include
the `SphericalToCubemap` pass.

### BrdfLutBake

`BrdfLutBake` is independent from scene probes. It bakes one texture for one
BSDF integration model.

```yaml
schema: lxe.render-path-graph.v1
name: BrdfLutBake
renderPath: BrdfLutBake
resources:
  outputs:
    brdf.standardGgx:
      type: texture2d
      binding: BrdfLut
      format: RGBA16Float
      extent: settings.brdfLutSize
      mips: 1
passes:
  - id: IntegrateStandardGgx
    stage: raster
    dispatch: fullscreen
    shader: render_paths/BrdfLutBake/integrate_standard_ggx
    input:
      kind: fullscreen-triangle
    sources: []
    targets: [brdf.standardGgx]
```

Material instances do not contain a manual "bake BRDF LUT" checkbox. The
material contract / BSDF model declares a requirement such as:

```text
requiresBrdfLut: standard-ggx-split-sum-v1
```

The bake system scans live material contracts and bakes each required LUT once.

## Core Contract Extensions

Extend the existing graph model:

```cpp
enum class RenderPath {
  Forward,
  Deferred,
  OfflineRT,
  ReflectionFilter,
  BrdfLutBake,
};

enum class RenderPassInputKind {
  SceneRenderables,
  FullscreenTriangle,
  ComputeDispatch,
  CubeFaceFilter,
  CubeMipFaceFilter,
};

enum class RenderGraphResourceType {
  Attachment2D,
  Cubemap,
  Texture2DImport,
  Sh9Buffer,
};

struct RenderBakeIteration final {
  StringID source;
  StringID target;
  u32 mipLevel = 0;
  u32 faceLayer = 0;
  u32 extent = 1;
  float roughness = 0.0f;
  float sourceMipCount = 1.0f;
  float sampleCount = 64.0f;
};
```

`RenderDrawInput` gains `std::optional<RenderBakeIteration> bakeIteration`.
`RenderComputeInput` must carry enough target metadata for the SH9 projection
pass to write `filter.diffuseSh9`.

The parser adds `input.filter` only for `cube-face-filter` and
`cube-mip-face-filter`. It rejects `input.filter` on fullscreen, scene
renderable, and compute passes.

Graph resource declarations are mandatory for `ReflectionFilter` and
`BrdfLutBake`. Unknown resource fields, unknown resource types, unsupported
`settings.*` keys, and undeclared sources/targets fail during parse or compile.

## Runtime Asset Types

Add runtime types without introducing a second graph system:

```cpp
struct DiffuseSH9 final {
  Vec4f coefficients[9];
};

struct ReflectionProbeBakeAsset final {
  bool global = true;
  std::unique_ptr<IGpuResource> radianceMap;
  std::unique_ptr<IGpuResource> prefilteredEnvMap;
  std::unique_ptr<IGpuResource> diffuseSh9;
};

struct BrdfLutAsset final {
  StringID bsdfModel;
  std::unique_ptr<IGpuResource> texture;
};
```

These assets can be registered in `SceneResourceTable`, but metadata alone must
not satisfy graph or shader dependencies. A live typed GPU payload is required.

## Shader Layout

Move or create shaders under render-path-owned folders:

```text
assets/shaders/glsl/render_paths/ReflectionFilter/spherical_to_cubemap.vert
assets/shaders/glsl/render_paths/ReflectionFilter/spherical_to_cubemap.frag
assets/shaders/glsl/render_paths/ReflectionFilter/prefilter_specular_env.vert
assets/shaders/glsl/render_paths/ReflectionFilter/prefilter_specular_env.frag
assets/shaders/glsl/render_paths/ReflectionFilter/project_diffuse_sh9.comp
assets/shaders/glsl/render_paths/BrdfLutBake/integrate_standard_ggx.vert
assets/shaders/glsl/render_paths/BrdfLutBake/integrate_standard_ggx.frag
```

Root shader names and direct source file paths remain rejected by the strict
shader resolver.

## Tests And Audits

Required behavior tests:

- Parser rejects legacy `renderPath: IBLBake`.
- Parser accepts `ReflectionFilter` and `BrdfLutBake` only when all referenced
  graph resources are declared.
- Parser rejects root shader URI `ibl_brdf_lut`.
- Parser rejects direct shader source paths.
- Parser rejects `input.filter` on non-filter input kinds.
- `RenderWorkCompiler` expands `cube-face-filter` into six draw inputs.
- `RenderWorkCompiler` expands `cube-mip-face-filter` into
  `6 * settings.prefilterMipCount` draw inputs.
- Vulkan smoke executes graph-owned ReflectionFilter and BrdfLutBake paths and
  readback proves `PrefilteredEnvMap`, `DiffuseSH9`, and `BrdfLut` are live.

Required audits:

```bash
rg -n "IBLBake|iblbake|IblBakeRenderer|bakeStaticEnvironment" src assets docs notes
rg -n "\"(equirect_to_cubemap|ibl_irradiance_convolve|ibl_prefilter_env|ibl_brdf_lut)\"" src assets/render_paths assets/shaders/glsl
```

After this stage, production backend code must not contain hardcoded old bake
shader names.

## Out Of Scope

- Scene-adjacent persisted bake cache.
- Local reflection probe scene components.
- Probe influence volumes and probe blending in Forward/DeferredLighting.
- Lightmaps and UV-surface baking.
