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
- The latest Forward pass feature specialization keeps pass-flow switches in
  `feature.forwardPass` and explicitly avoids adding `feature.surfaceLighting`.
  This slice therefore extends the pass-feature model with volatile runtime
  fields instead of creating a second surface-lighting feature.
- Current scene YAML is a node tree with mesh/material, camera, and light
  components. It does not yet have a first-class environment-map node; older
  `scene.environment` fields exist in some scenes but cannot satisfy the
  RenderFeature-owned environment dependency.

## Decision

Add a narrow async bake job service for environment IBL:

```text
current Scene
  -> bake ibl start
  -> async BakeJobId + worker event queue
  -> scan scene nodes that explicitly opt in to IBL bake
  -> collect environment nodes and their attached environmentLighting feature
  -> collect object nodes and their attached material asset
  -> deduplicate environment items by render-feature input URI/hash
  -> deduplicate material items by material type / BSDF bake model
  -> cache check, or force rebake
  -> select the bake render path for each distinct bake item
  -> execute the compiled bake graph with a FrameGraphExecutor
  -> export light assets or BRDF assets beside the source asset
  -> write manifest and payloads atomically
  -> item-complete messages consumed on the editor/render thread
  -> load each baked item's assets from adjacent output locations
  -> register each item in the current SceneResourceTable and upload to GPU
  -> after all scene bake items activate, mark scene/resource IBL readiness tags
  -> per-frame runtime policy fills volatile pass-feature control facts
  -> existing Forward pass uses them next frame through common IBL helpers
```

The pipeline and shader ABI exist before baking. Bake completion changes live
resources, descriptor data, upload generation, and shader-visible bake facts; it
does not reload the scene or rebuild pipelines.

The spec sections and implementation plan should follow the same order as the
runtime flow. Foundational work comes first:

1. extend scene nodes so the scene can express environment-map nodes and
   per-node IBL bake participation;
2. introduce the compiled graph executor boundary needed by bake graphs;
3. add the async job id, worker thread, and thread-safe event queue;
4. implement scene-node scanning, asset analysis, deduplication, and cache
   checks;
5. define adjacent asset output layouts and strict manifests;
6. author bake render paths and execute them through the executor;
7. export payloads atomically;
8. consume completion on the main thread, load resources, register them in
   `SceneResourceTable`, upload them to GPU through a focused activation/policy
   boundary, and mark scene/resource readiness only after all required items are
   ready;
9. make Forward and Deferred consume the activated IBL facts through common
   shader helpers.

## Scene Node Extension And Bake Participation

The bake job does not invent global bake keys and does not let assets decide by
themselves whether the current scene should bake them. Bake opt-in is a scene
node property:

- an environment-map node opts its attached environment feature into light bake;
- an object node opts its material into BRDF bake and marks that object as
  participating in baked IBL evaluation.

This slice extends the scene schema with a first-class environment-map node. The
node owns scene placement, editor operability, bake participation, and the link
to its `environmentLighting` RenderFeature. A render path enables environment
lighting only when the scene contains an environment-map node. Baked IBL
contribution becomes visible only after that node opted into bake, all required
environment/material bake items completed, and the main thread activated their
runtime resources.

The environment node is also the unified scene entry for visible skybox /
background and IBL bake. If the scene has no environment node, positive runtime
paths must treat the scene as having no environment source. If the node exists
with `environment.bake.enabled: false`, the same environment source may still
drive skybox/background rendering, but it does not request baked surface IBL.
Legacy `scene.environment` fields do not satisfy this node requirement.

The environment scene node schema is fixed for this design:

```yaml
- nodeName: studio_env
  name: studio_env
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  environment:
    feature:
      uri: assets/effects/environment_lighting.render-feature.yaml
    bake:
      enabled: true
```

`environment.feature.uri` points to the RenderFeature asset that owns the
environment map URI, color, intensity, rotation, shader ABI, and bake-method
facts. `environment.bake.enabled` is scene-local opt-in. A feature asset may
describe how it can be baked, but it must not start bake work unless a scene
environment node opts in.

Object nodes use their existing material binding to identify material bake
inputs. The bake marker is still scene-local and must not be written into the
material asset merely to control this scene:

```yaml
- nodeName: damaged_helmet
  name: damaged_helmet
  mesh:
    uri: assets/models/damaged_helmet/DamagedHelmet.gltf
  material:
    uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material
  bake:
    ibl:
      enabled: true
```

When the marker is absent or false, the object can still render with direct
lighting, but it does not request BRDF bake and does not participate in baked
IBL evaluation for this slice.

The object marker is fixed as top-level `bake.ibl.enabled`. It is not
`material.bake`, because the marker belongs to this scene node's participation
in IBL, not to the material asset itself.

## Compiled Graph Executor Foundation

The runtime flow remains the same pipeline used by ordinary rendering. Scene is
the input source. Parsed scene resources are stored in `SceneResourceTable`.
The active RenderPathGraph is the program: its pass schema filters the
renderable/resource facts, forms the FrameGraph DAG, and gives
`RenderWorkCompiler` enough information to package per-pass `RenderInput` and
`RenderInputDesc`. The executor then walks the compiled graph order, combines
each pass contract with prepared inputs and pipeline state, and records backend
work. IBL bake graphs must fit this flow; they must not bypass it by hardcoding
shader order or resource payloads in C++.

`FrameGraphExecutor` is therefore a prerequisite for this slice. It executes a
compiled `FrameGraph` using the same pass contract and prepared input model that
ordinary rendering uses. The first implementation may be Vulkan-backed, but the
public boundary is "execute compiled graph work", not "call or wrap a private
IBL bake renderer". Any capability that only exists in the old private bake
renderer must be reimplemented in the new bake render-path shaders, graph
executor, payload exporter, or activation code. The old private bake renderer is
hard-cut rather than fixed. The executor must reject missing sources, targets,
shaders, formats, or typed payloads before backend execution.

This slice also adds rebuild gating around the ordinary per-frame path. A static
scene with a static RenderPathGraph must not rebuild the FrameGraph DAG,
repackage identical `RenderInput` / `RenderInputDesc`, rebuild descriptor
preparation, or re-run unchanged upload preparation every frame. Scene node
changes, material/resource generation changes, render-feature changes,
render-path graph changes, swapchain/target shape changes, and bake activation
are the invalidation points that can mark the cached graph/work/descriptor/upload
state dirty. When no dirty bit is set, the frame reuses the prepared graph,
prepared inputs, descriptor plans, GPU descriptor resources, and upload state.

Descriptor sets and uploaded GPU resources are expected to be stable across
frames unless their source generation changes. Upload work is therefore
generation-driven: if the CPU-side resource payload, descriptor-visible binding
plan, or backend target shape has not changed, the backend must not re-upload or
rebuild descriptor state simply because a new frame started.

The remaining per-frame logic should be lightweight policy, not structural
preparation. For IBL this means checking scene/resource state tags such as
environment node presence, bake activation readiness, and feature enablement
before a pass executes. Those tags can fill small pass-control data, enable or
disable IBL contribution, or skip an optional pass, but they must not cause a
full graph/input/descriptor/upload rebuild unless the underlying scene,
resource, render-path, or target facts changed.

Pass-feature YAML remains the schema for pass runtime control. Static,
coarse-grained fields from the asset file may drive specialization constants and
pipeline identity. `073-g` adds volatile pass-feature fields for facts that are
computed before each frame, such as IBL enablement and readiness. Volatile
fields are part of the pass-feature schema and shader ABI, but their values are
runtime-filled, not persisted authoring values. They must default to uniform /
pass-control buffer data, not specialization constants, and must not enter
`PipelineKey` or force pipeline rebuild by themselves.

IBL runtime control belongs to the pass feature for the pass that consumes it.
Forward adds these volatile fields to `feature.forwardPass`. `073-g` does not
require a full `feature.deferredPass` implementation; Deferred keeps structural
parity by sharing the common GLSL and reserving the same volatile field names and
uniform ABI for the Deferred pass feature when that pass feature is introduced.
This slice must not add `feature.surfaceLighting`, `feature.iblLighting`, or any
other cross-pass runtime-lighting feature. Shared lighting math lives in common
GLSL; runtime control stays pass-local.

The pass-feature schema shape is:

```yaml
parameters:
  enableIblLighting:
    kind: bool
    volatile: true
    binding: PassRuntimeUBO
    member: enableIblLighting
    required: true
  environmentIblReady:
    kind: bool
    volatile: true
    binding: PassRuntimeUBO
    member: environmentIblReady
    required: true
  standardPbrIblReady:
    kind: bool
    volatile: true
    binding: PassRuntimeUBO
    member: standardPbrIblReady
    required: true
```

Volatile pass-feature fields must reject persisted authoring values such as
`value` and specialization-constant metadata such as `constantId`. A volatile
field declares schema, binding, member, and required-ness only; runtime policy
must supply its value before draw/dispatch. Concrete passes may choose
pass-specific binding names, but the fields remain volatile uniform data. If the
policy does not supply a required volatile value, validation must fail-fast or
force a conservative false value with a diagnostic; it must not read a fake YAML
fallback.

## Async Job And Event Queue

Bake jobs receive a thread-safe event queue when started and publish events into
that queue from the worker thread:

```text
job, item, phase, severity, progress, message, fix, sequence
```

The editor/render main thread drains the queue once per frame and writes events
to both `editor.log` and the command prompt. Worker threads do not touch UI,
mutate the scene resource table, or upload GPU resources directly.

When the main thread receives an item-complete event, it resolves that finished
bake item, loads its baked payloads from the predeclared adjacent output paths,
registers temporary live resources in `SceneResourceTable`, and uploads them to
GPU. Uploading an item alone must not enable IBL contribution. The main thread
tracks every required bake item for the job; only after all required items have
loaded, uploaded, and reached active-resource readiness does it mark
scene/resource IBL readiness state. The job and activation path do not directly
enable pass contribution; the per-frame runtime policy reads the state tags and
fills volatile pass-feature control facts used by Forward and Deferred.

There is no concurrent bake queue in the first version. A running job blocks
another `bake ibl start`; a duplicate call returns the running job id or a clear
diagnostic. `bake ibl start --force` is allowed only when no job is running.

## Bake Item Collection, Deduplication, And Cache Check

The scene scan produces bake items, not work commands. Each bake-enabled node is
resolved to the asset contract it references:

- environment nodes resolve to their attached `environmentLighting`
  RenderFeature, then to the environment map input asset declared by that
  feature;
- object nodes resolve to their material asset, then to the material type /
  BSDF bake model declared by that material's source/type contract.

The referenced asset contract supplies the bake method, including the render
path graph and algorithm parameters. The service normalizes those facts to cache
keys, deduplicates them, then maps each distinct bake item to the graph that
knows how to generate that asset class. Environment keys are distinct when their
render-feature input URI/hash differs. Material keys are distinct when their
material type or BSDF bake model differs. Light bake items export diffuse SH and
a prefiltered specular cubemap. BRDF bake items export a BRDF LUT.

Default `bake ibl start` uses a valid disk cache by skipping GPU bake and file
export for that item, then still entering the same activation path as a freshly
baked item. Cache hit does not imply that the live resource table, GPU upload,
or descriptors are already ready. Activation must inspect the resource layer:
if the live baked payload exists and its generation is clean, it can be reused;
if it is missing, dirty, stale, or descriptor-visible state changed, the main
thread still loads/registers/uploads as needed. `--force` ignores valid disk
cache and rebakes. Invalid cache logs the reason and rebakes.

## Asset Layout

Environment-adjacent outputs:

- `diffuse_sh9.yaml`: `lxe.sh9.v1`, world-space real SH, order 2, exactly
  9 RGB coefficients;
- `specular_prefilter.ktx2`: cubemap mip chain, RGBA16Float, default resolution
  256, roughness maps by alpha squared, mip count derived and recorded;
- `manifest.yaml`: strict source / bake / outputs contract.

Material/BRDF output:

- `standard-pbr` BRDF LUT next to the canonical material source/type asset,
  keyed by material type / BSDF bake model, GGX/Smith model, RG16Float format,
  and size 256.

The BRDF LUT is not scene-local and should not be rebaked for each environment,
material instance, or object node, but it is material-family-scoped rather than
a global light asset. Baked assets always follow their source asset: environment
light outputs follow the environment map input asset referenced by the feature;
BRDF outputs follow the material type/source asset. They never follow scene
nodes.

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
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
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

The concrete bake method belongs to the referenced asset contract, not to the
scene bake marker. Environment bake method facts live in the
`environmentLighting` render-feature YAML. Material bake method facts live in
the material YAML or the material source/type definition. The scene node marker
only says whether this scene instance participates in bake/IBL; it does not
name shaders, bake passes, executor details, or output files.

## Baked Asset Loading, Upload, And Runtime Policy

Bake graph execution only produces files. It does not directly mutate the live
scene, upload GPU resources, or enable rendering features. The activation path
is a separate main-thread workflow:

```text
item-complete event
  -> resolve bake item source asset
  -> locate adjacent baked manifest and payloads
  -> strictly validate manifest, source hash, and payload files
  -> load SH / cubemap / BRDF payloads as temporary live resources
  -> register resources in SceneResourceTable
  -> upload new/dirty GPU resources and descriptors
  -> mark that bake item active-resource ready
  -> when every required item is ready, mark scene/resource readiness tags
  -> per-frame policy fills volatile pass-feature control facts
```

Uploading resources is not the same as enabling IBL. A resource upload may make
SH, prefiltered cubemaps, or BRDF LUTs available to descriptors, but the render
path must continue treating IBL as disabled until the whole scene-level bake set
is ready. The final switch is not issued by the job. After all required
environment and material bake items are active-resource ready, activation marks
scene/resource readiness tags. The per-frame runtime policy then reads those
tags and fills shader-visible volatile control facts on the pass feature, such
as `enableIblLighting`, `environmentIblReady`, and `standardPbrIblReady`.

Cache-hit activation follows this same path. A cache hit proves only that the
adjacent baked manifest and payloads match the current bake key. It does not
prove that `SceneResourceTable` already has the payload, that the backend has
uploaded it, or that descriptors are clean for the current target. The
activation component checks live resource existence and dirty generations before
deciding whether to reuse, load, upload, or refresh descriptors.

Activation is all-or-nothing in this slice. If any required bake item fails to
bake, validate, load, register, or upload, activation must not mark the new
scene/resource IBL readiness tags. Existing active IBL resources and state tags
remain in place. Partial per-material or per-BRDF activation is a follow-up
extension, not part of `073-g`.

This policy logic should live behind a small, explicit runtime boundary rather
than being scattered through the bake worker, renderer loop, or shader binding
helpers. The implementation plan should introduce a focused component such as
`EnvironmentIblActivation` / `EnvironmentLightingRuntimePolicy` that owns:

- tracking required bake items for the current job;
- loading and validating adjacent baked assets on the main thread;
- registering loaded payloads into `SceneResourceTable`;
- requesting dirty GPU upload/descriptor refresh for newly active payloads;
- deciding when scene-level IBL readiness tags may be marked;
- applying per-frame pass-feature policy for environment lighting.

Most pass-level feature logic follows the same pattern: each frame may inspect
small scene/resource tags and fill pass-control data, but only dirty source
generations should trigger upload or descriptor refresh. For `073-g`, the
per-frame policy checks whether an environment node exists, whether the relevant
bake set is active-resource ready, and whether the render path/feature wants
environment lighting. It may write tiny feature-control data each frame, but it
must not rebuild graph/work/descriptor/upload state when those source facts have
not changed.

## Runtime Lighting Shape

Forward keeps IBL inside the existing Forward surface pass:

```text
Forward surface pass:
  material IBL helper
Bloom screen-space pass
```

`feature.environmentLighting` remains the environment source feature. It owns
the environment source radiance, environment map URI, color, intensity,
rotation, and bake-method facts. Runtime IBL control belongs to the active
pass feature as volatile uniform data. For Forward, these fields are on
`feature.forwardPass`:

```text
enableIblLighting
environmentIblReady
standardPbrIblReady
```

IBL contribution does not have a separate surface-lighting intensity in this
slice. Surface IBL uses the same `environmentLighting.color` and
`environmentLighting.intensity` values that define the environment radiance for
skybox/background rendering. The only additional runtime controls are volatile
pass-feature fields derived each frame from scene/resource readiness tags.

Environment lighting is scene-driven. If the current scene has no environment
node, both skybox/background environment source and surface IBL are disabled
regardless of graph defaults. If an environment node exists but its bake marker
is disabled or its baked assets are not yet activated, the environment can still
provide direct skybox/background radiance where the render path supports that,
but surface IBL remains disabled.
After all required environment and material bake resources are activated, the
per-frame runtime policy observes the readiness tags and the same Forward
surface pass starts evaluating IBL.

The Forward shader reads these volatile pass-uniform facts and calls common
helper functions for `standard-pbr` material IBL. This creates uniform branches,
not per-material or per-fragment divergent policy.

RenderWorkCompiler still validates graph/resource facts. If IBL is enabled, the
graph must resolve `scene.environmentBake`, `scene.materialIblBake`, and the
material BRDF LUT facts. C++ does not pick a second Forward path.

`assets/shaders/glsl/common/ibl_lighting.glsl` owns the shared IBL formula.
Forward and DeferredLighting both include it. Deferred has structural parity in
this slice through the shared GLSL/ABI contract, but `073-g` does not require a
complete Deferred pass-feature runtime path or Deferred image smoke. Forward is
the image-producing acceptance path.

Skybox/background direct rendering remains owned by `REQ-073-f`. Bloom remains a
screen-space effect; this slice only requires that enabling bloom after Forward
IBL activation does not corrupt the output.

## Failure Model

Failures are isolated:

- environment bake failure keeps the current active IBL resources unchanged;
- BRDF LUT failure keeps the current active IBL resources unchanged and prevents
  this job from enabling new IBL facts;
- activation is a two-phase commit: temporary live resources are prepared first,
  then the active SceneResourceTable IBL generation is swapped atomically;
- activation failure leaves baked files on disk but does not switch resources;
- every failure logs a fix and allows `bake ibl start` to retry from scratch.

Manifest and payload writes use temporary files and atomic commit so partial
results are never loaded as valid bake assets.
Zero-byte manifests, zero-byte SH payloads, zero-byte KTX2 payloads, and KTX2
files whose header reports zero dimensions or zero mip levels are invalid bake
outputs. A bake job may not report item success until the generated files pass
this smoke validation.

## Required Rejections

- scene-side environment fields satisfying IBL bake input;
- metadata-only bake records satisfying runtime resources;
- missing source hash, mip layout, SH coefficient layout, payload path, or
  non-empty payload bytes;
- render-feature or material asset bake markers causing work without a
  bake-enabled scene node;
- legacy `scene.environment` satisfying the environment node requirement;
- scene render paths enabling environment lighting with no environment-map node;
- material-instance-level BRDF LUT duplication for multiple standard-pbr
  objects;
- private `bakeStaticEnvironment()` or `IblBakeRenderer` as a public, default,
  internal-wrapper, or compatibility bake path;
- bake work whose pass order or shader sources are hidden in C++ instead of
  render-path YAML;
- uploaded baked resources directly enabling IBL before the whole required bake
  set is active-resource ready;
- partial IBL activation after a required bake item fails;
- IBL activation and per-frame feature policy scattered through worker,
  renderer loop, and shader binding helpers instead of a focused runtime
  boundary;
- volatile pass-feature fields implemented as specialization constants or
  included in `PipelineKey`;
- volatile pass-feature fields accepting persisted YAML `value` fallbacks;
- static scene/render-path frames rebuilding graph, inputs, descriptor plans, or
  unchanged upload state every frame;
- Forward/Deferred writing separate IBL formulas instead of using common GLSL;
- default positive paths using `HAS_IBL`, `EnvironmentUBO`,
  `feature.surfaceLighting`, or hardcoded `iblIntensity` as the IBL truth;
- new `feature.iblLighting` or equivalent cross-pass runtime-lighting feature.

## Acceptance

- `bake ibl start` and `bake ibl start --force` produce inspectable SH,
  prefiltered cubemap mips, environment manifest, material manifest, and
  standard-pbr BRDF LUT.
- Output validation checks file existence, `file_size > 0`, format, dimensions,
  mip sizes, SH coefficient count, and nonzero sanity. Empty generated assets
  fail the bake smoke and do not count as item-complete success.
- `src/backend/vulkan/details/ibl_bake_renderer.*` is deleted. Environment and
  BRDF bake capability is covered by the graph executor/exporter smoke, not by a
  fixed legacy renderer.
- Cache hit skips GPU bake/export but still runs activation, including live
  resource checks and dirty upload/descriptor refresh when needed.
- Completion hot-activates resources in the current scene without scene reload
  through two-phase commit.
- Item-complete activation can load and upload individual baked payloads without
  enabling IBL until every required bake item is active-resource ready.
- Any required bake/validation/load/upload failure preserves the old active IBL
  state and prevents activation from marking new IBL readiness tags.
- Forward render/debug dump changes after activation.
- Forward uses volatile pass-feature IBL readiness/control facts and has no
  default `ForwardIblLighting` additive pass.
- Forward volatile IBL fields live on `feature.forwardPass`; Deferred keeps
  shared GLSL/ABI structural parity without requiring a full `feature.deferredPass`
  runtime implementation in this slice.
- IBL readiness/control fields are volatile pass-feature uniform data, not
  specialization constants, and changing them does not rebuild pipelines.
- RenderFeature parser/validator rejects `volatile: true` parameters that carry
  persisted `value` or specialization metadata.
- Deferred compiles and reflects the shared IBL contract.
- Static scene/render-path smoke proves repeated frames reuse cached
  FrameGraph/work/descriptor/upload preparation and only refresh lightweight
  pass-control policy.
- Failure logs include repair guidance and retry remains possible.
