# 073-g Environment HDR Async IBL Bake And Runtime Lighting Design

Date: 2026-06-16

## Goal

Turn the environment map introduced by `REQ-073-f` into reusable baked IBL
lighting assets, then activate them in the current scene without scene reload or
pipeline rebuild. This implements `REQ-073-g`.

Reflection probes are deliberately excluded and move to `REQ-073-h`.

## Current Facts

- `feature.environmentLighting.parameters.environmentMap.uri` is now the
  realtime environment source.
- `IblBakeRenderer::bakeStaticEnvironment()` still hardcodes bake shaders and is
  called from the realtime renderer.
- Forward and Deferred currently have old IBL concepts such as `HAS_IBL`,
  `EnvironmentUBO`, `PrefilteredEnvMap`, and `BrdfLut`.
- Existing editor command hooks use `std::function`; `core/task/TaskGraph` is
  synchronous and is not a general async job system.

## Decision

Add a narrow async bake job service for environment IBL:

```text
current Scene
  -> bake ibl start
  -> async BakeJobId + event stream
  -> collect environment keys and one standard-pbr material key
  -> cache check, or force rebake
  -> bake environment HDR into SH + prefiltered cubemap
  -> ensure standard-pbr BRDF LUT once per material type
  -> write manifest and payloads atomically
  -> completion callback on the editor/render thread
  -> two-phase activate baked resources in the current SceneResourceTable
  -> existing Forward pass uses them next frame through common IBL helpers
```

The pipeline and shader ABI exist before baking. Bake completion changes live
resources, descriptor data, upload generation, and shader-visible bake facts; it
does not reload the scene or rebuild pipelines.

## Asset Layout

Environment-adjacent outputs:

- `diffuse_sh9.yaml`: `lxe.sh9.v1`, world-space real SH, order 2, exactly
  9 RGB coefficients;
- `specular_prefilter.ktx2`: cubemap mip chain, RGBA16Float, default resolution
  256, roughness maps by alpha squared, mip count derived and recorded;
- `manifest.yaml`: strict source / bake / outputs contract.

Material/BRDF output:

- `standard-pbr` BRDF LUT next to the material asset, keyed by material URI/hash,
  GGX/Smith model, RG16Float format, and size 256.

The BRDF LUT is not scene-local and should not be rebaked for each environment,
but it is material-scoped rather than a global light asset.

Manifest shape separates input facts, bake parameters, and output files:

```yaml
schema: lxe.environment-ibl-bake.v1
source:
  uri: assets/env/khronos/neutral/ggx/specular.ktx2
  hash: sha256:...
bake:
  diffuse:
    basis: sh9
  specular:
    format: RGBA16Float
    resolution: 256
    mips: 9
    roughness: alpha-squared
    layout: cubemap
    faces: 6
outputs:
  diffuse:
    file: diffuse_sh9.yaml
  specular:
    file: specular_prefilter.ktx2
```

```yaml
schema: lxe.material-ibl-bake.v1
material:
  uri: assets/materials/standard-pbr/standard-pbr.material.yaml
  type: standard-pbr
  hash: sha256:...
bake:
  brdf:
    model: ggx-smith
    format: RG16Float
    size: 256
outputs:
  brdf:
    file: brdf_lut.ktx2
```

## Bake Render Paths

Bake work is graph-authored too. This requirement adds at least:

```text
assets/render_paths/bake_environment_ibl.render-path.yaml
assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml
```

These graphs declare bake shader URIs, sources, targets, intermediate resources,
formats, sizes, and payload outputs. RenderWorkCompiler turns them into graph
work and `FrameGraphExecutor` executes the compiled work. `IblBakeJobService`
owns job state, cache checks, logs, manifests, file writes, atomic commit, and
hot activation. The backend does not invent missing bake passes or fallback
resources.

## Runtime Lighting Shape

Forward keeps IBL inside the existing Forward surface pass:

```text
Forward surface pass:
  material IBL helper
PostProcess / Bloom screen-space passes
```

`feature.surfaceLighting` is the shared surface feature that provides
shader-visible IBL switches and parameters:

```text
enableIblLighting
diffuseIblIntensity
specularIblIntensity
environmentIblReady
standardPbrIblReady
```

The Forward shader reads these pass-uniform facts and calls common helper
functions for `standard-pbr` material IBL. This creates uniform branches, not
per-material or per-fragment divergent policy.

RenderWorkCompiler still validates graph/resource facts. If IBL is enabled, the
graph must resolve `scene.environmentBake`, `scene.materialIblBake`, and the
material BRDF LUT facts. C++ does not pick a second Forward path.

`assets/shaders/glsl/common/ibl_lighting.glsl` owns the shared IBL formula.
Forward and DeferredLighting both include it. Deferred has structural parity in
this slice, but Forward is the image-producing acceptance path.

Skybox/background direct rendering remains owned by `REQ-073-f`. Bloom remains a
screen-space effect; this slice only requires that enabling bloom after Forward
IBL activation does not corrupt the output.

## Async Events

Bake jobs expose thread-safe events:

```text
job, phase, severity, progress, message, fix, sequence
```

Editor subscribes by polling or draining events and writes them to both
`editor.log` and the command prompt. Worker threads do not touch UI or mutate
the scene resource table directly.

There is no concurrent bake queue in the first version. A running job blocks
another `bake ibl start`; a duplicate call returns the running job id or a clear
diagnostic. `bake ibl start --force` is allowed only when no job is running.

Default `bake ibl start` uses a valid cache by jumping directly to activation.
`--force` ignores valid cache and rebakes. Invalid cache logs the reason and
rebakes.

## Failure Model

Failures are isolated:

- environment bake failure keeps the current active IBL resources unchanged;
- BRDF LUT failure only skips IBL for materials that require that LUT;
- activation is a two-phase commit: temporary live resources are prepared first,
  then the active SceneResourceTable IBL generation is swapped atomically;
- activation failure leaves baked files on disk but does not switch resources;
- every failure logs a fix and allows `bake ibl start` to retry from scratch.

Manifest and payload writes use temporary files and atomic commit so partial
results are never loaded as valid bake assets.

## Required Rejections

- scene-side environment fields satisfying IBL bake input;
- metadata-only bake records satisfying runtime resources;
- missing source hash, mip layout, SH coefficient layout, or payload path;
- material-instance-level BRDF LUT duplication for multiple standard-pbr
  objects;
- private `bakeStaticEnvironment()` as a public/default bake path;
- bake work whose pass order or shader sources are hidden in C++ instead of
  render-path YAML;
- Forward/Deferred writing separate IBL formulas instead of using common GLSL;
- default positive paths using `HAS_IBL`, `EnvironmentUBO`, or hardcoded
  `iblIntensity` as the IBL truth.

## Acceptance

- `bake ibl start` and `bake ibl start --force` produce inspectable SH,
  prefiltered cubemap mips, environment manifest, material manifest, and
  standard-pbr BRDF LUT.
- Output validation checks file existence, format, mip sizes, SH coefficient
  count and nonzero sanity.
- Cache hit hot-activates resources without GPU rebake.
- Completion hot-activates resources in the current scene without scene reload
  through two-phase commit.
- Forward render/debug dump changes after activation.
- Forward uses `feature.surfaceLighting` switches and has no default
  `ForwardIblLighting` additive pass.
- Deferred compiles and reflects the shared IBL contract.
- Failure logs include repair guidance and retry remains possible.
