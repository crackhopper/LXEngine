# 074-h OfflineRT FrameGraphExecutor Hard Cut Design

Date: 2026-06-20

## Goal

Hard-cut OfflineRT onto the same render architecture used by realtime render
paths:

```text
RenderPathGraph asset
  -> FrameGraph
  -> FrameGraph::compile()
  -> CompiledFrameGraph
  -> RenderWorkCompiler
  -> PreparedFramePassWork[]
  -> FrameGraphExecutionRequest
  -> FrameGraphExecutor
  -> FrameGraphExecutionResult.outputs
```

There is no compatibility phase. The completed positive path must not keep
`OfflineRenderJob`, `OfflineRenderJob::offlineShader`,
`createOfflineRenderFrameGraph()`, `OfflineRenderGraphExecutor`, or
`techniques/OfflineRT/...` as successful/default paths.

This design implements the active scope of `REQ-074-h` after `REQ-073-g`.

## Current Facts

- `FrameGraphExecutionRequest` is the current core execution boundary. It holds
  `FrameGraph`, `CompiledFrameGraph`, and `PreparedFramePassWork[]`.
- `VulkanFrameGraphExecutor` validates graph/prepared work and records
  graphics/compute inputs, but currently does not collect readback outputs.
- IBL bake already depends on the `FrameGraphExecutor` interface and consumes
  `FrameGraphExecutionResult.outputs`, but its actual outputs are currently
  produced by fake/test executors or cache plumbing, not by Vulkan readback.
- IBL bake dimensions are currently scattered across bake render-path
  attachment declarations, manifest defaults, cache fixtures, and synthetic
  executor tests. The bake render-path payloads describe format/kind but do not
  provide a single execution-time extent source.
- RenderPathGraph pass parser already supports `stage: compute`,
  `dispatch: compute`, and `input.kind: compute-dispatch`.
- RenderPathGraph pass parser still accepts/requires `renderState` for compute
  passes and has no first-class `compute` block.
- `PipelineBuildType::RayTracing` exists in core, but the current shader stage
  enum, shader compiler, Vulkan pipeline cache, and executor path do not build
  ray tracing pipelines. `VulkanFrameGraphExecutor` currently rejects
  `PipelineBuildType::RayTracing` inputs.
- Material v2 currently allows only `schema`, `bsdf`, `renderClass`, `tags`,
  and `metadata` at the root. It does not yet allow a `hit` map that points a
  material at ray hit shader URIs.
- `RenderPathPayloadContract` currently models bake artifacts with
  `name/target/format/kind`; these fields are preserved through `RenderPassNode`,
  `FramePass`, and `CompiledFrameGraphPass`.
- `RenderWorkCompiler::buildInputs()` currently hardcodes offline compute
  dispatch from `OfflineRenderJob.output` and hardcodes readback binding
  `OutputPixels`.
- `RenderWorkBuildContext` exposes pass-feature specialization only through
  `hasRealtimeScene()` / `realtimeScene()`. Offline cannot consume pass-level
  RenderFeature facts through the same path.
- The current CLI constructs `OfflineRenderJob` and passes a shader provider for
  `techniques/OfflineRT/offline_pbr_direct_ray`.
- `assets/shaders/glsl/techniques/OfflineRT/offline_pbr_direct_ray.comp` is the
  current shader location. The target location is
  `assets/shaders/glsl/render_paths/OfflineRT/offline_pbr_direct_ray.comp`.
- The Helmet positive target uses Material v2 `standard-pbr`:
  `bsdf.type: standard-pbr`, source
  `assets://shaders/glsl/common/materials/standard_pbr.contract.glsl`, and
  albedo/metallic-roughness/normal/AO/emissive textures.
- `standard_pbr.contract.glsl` owns the source-material ABI:
  `SceneMaterialRefs`, `SceneSourceMaterialRecords`, and `SceneTextures`.
  OfflineRT must use the same source-material storage ABI as realtime for the
  `standard-pbr` positive path.

## Hard-Cut Invariants

The completed positive path must use one public architecture:

```text
RenderPathGraph asset
  -> FrameGraph
  -> CompiledFrameGraph
  -> RenderWorkCompiler
  -> PreparedFramePassWork[]
  -> FrameGraphExecutionRequest
  -> FrameGraphExecutor
  -> FrameGraphExecutionResult.outputs
```

The slice does not keep compatibility routes:

- no positive `OfflineRenderJob` execution concept;
- no positive `OfflineRenderJob::offlineShader` shader side channel;
- no positive `createOfflineRenderFrameGraph()` graph factory;
- no positive `OfflineRenderGraphExecutor`;
- no positive `techniques/OfflineRT/...` shader URI;
- no pass-name special behavior for `OfflinePrimaryRay`.

Hard cut means delete the superseded production implementation in the same
implementation window. Old code may survive only as named negative tests/audits
or historical docs that prove rejection.

Existing classes are the default extension points. This spec must not create
parallel DTOs, request wrappers, or services when an existing class already
owns the same responsibility:

- extend `RenderWorkBuildContext` instead of introducing a separate
  `RenderWorkBuildFacts`;
- extend `RenderPassNode`, `FramePass`, `CompiledFrameGraphPass`, and the
  existing payload/readback declaration surface instead of adding a second graph
  model;
- extend `PreparedFramePassWork`, `RenderInput`, `RenderInputDesc`, and
  `FrameGraphExecutionRequest` at their existing boundaries instead of adding
  offline/bake execution wrappers;
- keep `IblBakeJobService`, `IblBakeItem`, `IblBakeCacheStore`,
  `IblBakeActivationSink`, and `FrameGraphExecutionResult.outputs` as the IBL
  bake workflow and post-processing boundary.

New small structs/classes are allowed only where there is no current owner,
for example software/hardware acceleration producer facts or ray hit table
preparation data. They must be consumed by the existing preparation/executor
flow and must not become a second public renderer pipeline.

The only positive OfflineRT shader URI is
`render_paths/OfflineRT/offline_pbr_direct_ray`.

The positive material scope is Helmet `standard-pbr`. PBRT/uber/metal/
substrate/glass/mix/fourier are not accepted as positive OfflineRT coverage in
this slice.

## Execution Terminology

`CompiledFrameGraph` is the execution plan. It contains the compiled pass order,
graph dependencies, stage/dispatch choices, and declared target/readback
contracts.

`PreparedFramePassWork` is the per-pass prepared input to that execution plan.
It contains concrete `RenderInput` objects and `RenderInputDesc` facts built
from the active scene/resource table, material variants, render features,
descriptor resources, runtime extent, and readback contracts.

The owner that already drives a workflow stores the source graph, execution
plan, and `std::vector<PreparedFramePassWork>` at the same lifetime level. That
owner can be realtime renderer state, offline CLI state, or IBL bake job state.
Do not add a public prepared-graph job wrapper unless implementation proves the
same member group is duplicated across multiple owners.

`FrameGraphExecutionRequest` is the per-call package passed to
`FrameGraphExecutor`. It borrows the graph, execution plan, and prepared inputs
for one synchronous `execute(...)` call.

## OfflineRT Rendering Logic Flow

The OfflineRT positive path is one compute pass over a selected scene subset,
with all setup routed through RenderPathGraph, RenderFeature, material source
contracts, `RenderWorkCompiler`, and `FrameGraphExecutor`.

The CPU-side render flow is:

1. `lxe_offline_render` reads CLI/profile values: scene path, camera/output
   profile, samples, max bounce, seed, compare mode, and output path.
2. The offline scene loader builds the same `Scene` model used by the renderer.
   `Scene::resources()` owns meshes, objects, materials, textures, cameras,
   lights, RenderFeatures, and shader/material payloads. The loader no longer
   returns an offline shader side channel.
3. The CLI selects `assets/render_paths/offline_ray_tracer.render-path.yaml`.
   That graph declares one `OfflinePrimaryRay` compute pass, its scene/resource
   reads, `feature.offlineRayTracer`, `offline.output`, dispatch source, and
   readback contract. The graph does not list `scene.bvh` directly; BVH is a
   feature-driven acceleration resource.
4. The graph loader resolves `effects/offline_ray_tracer.render-feature.yaml`
   and the `render_paths/OfflineRT/offline_pbr_direct_ray` shader. The feature
   declares pass-level ray tracing controls, the acceleration requirement, and
   the `hitShaderTable`. Material hit shader URIs come from
   selected material `hit.radiance.uri` fields. Acceleration is requested
   through a feature resource API/function; this slice uses the software BVH
   implementation of that API. The shader is compiled as a `standard-pbr`
   material-source variant, so its reflection and descriptor ABI include the
   same standard-pbr source-material buffers and `SceneTextures` array used by
   realtime.
5. `FrameGraph` is built from the RenderPathGraph asset and compiled to
   `CompiledFrameGraph`. The compiled graph is the execution plan: pass order,
   resource dependencies, compute dispatch declaration, and output/readback
   contracts.
6. Scene participation filtering selects the objects that satisfy the
   OfflineRT pass input contract, such as mesh render class, triangle-list
   geometry, and `standard-pbr` material type. This is the same filter concept
   used by draw paths; only the consumed output differs.
7. Ray program preparation resolves the primary ray program from the graph pass
   shader, resolves selected material `hit.radiance.uri` values, and matches
   those URIs against the RenderFeature `hitShaderTable`. The result is a
   derived `RayProgramTable` whose `hitGroupIndex` values come from that table.
   The software path lowers that table into function calls or static dispatch
   inside the compute shader variant; a future hardware RT path lowers the same
   table into raygen/miss/hit shader groups and SBT records.
8. Derived RenderFeature resource preparation runs before `RenderWorkCompiler`
   prepares pass inputs. The referenced `feature.offlineRayTracer` is handled as
   an ordinary RenderFeature, but its derived volatile acceleration resource
   calls the feature resource API/function. For software BVH this creates SSBO
   descriptors such as `SceneBvhNodes`; for future hardware RT this stage may
   create backend acceleration structures and PipelineBuildDesc extras.
9. OfflineRT scene data preparation follows the resource requirements declared
   by `feature.offlineRayTracer` and turns the selected scene objects into GPU
   compute descriptors: positions, attribute streams, attribute values, indices,
   meshes, primitives, objects, source-material records, material refs, texture
   array, software BVH nodes or hardware acceleration descriptors, frame params,
   OfflineRT runtime UBO, and the output storage buffer.
10. `RenderWorkBuildContext` borrows the `Scene`, domain label, runtime extents
   such as `offline.output.resolution`, and per-pass preparation facts. It does
   not own a render job and does not contain output file paths.
11. `RenderWorkCompiler::buildInputs()` creates one `RenderComputeInput` for
   `OfflinePrimaryRay`. Group counts come from
   `compute.dispatchFrom = offline.output.resolution` and `localSize`.
12. `RenderWorkCompiler::prepare()` resolves the compute pipeline facts,
    pipeline key, shader reflection, descriptor binding plan, resource
    dependencies, pass RenderFeature specialization facts, and concrete readback
    facts into `RenderInputDesc`. Any PipelineBuildDesc extras produced by
    feature resource preparation are also attached to the pipeline build
    description.
13. The caller stores `FrameGraph`, `CompiledFrameGraph`, and
    `PreparedFramePassWork[]` at the same owner/lifetime level, then creates a
    borrowed `FrameGraphExecutionRequest`.
14. `VulkanFrameGraphExecutor` runs in explicit immediate-submit/readback mode.
    It syncs descriptor resources, creates/looks up the compute pipeline, binds
    descriptors, dispatches the compute input, inserts GPU-to-host barriers, and
    reads the declared output storage buffer into `FrameGraphExecutionPayload`.
15. The compute shader executes one invocation per output pixel. Each invocation
    generates one or more jittered camera primary rays, traverses the BVH, loads
    interpolated hit attributes, loads the `standard-pbr` material surface via
    the source-material ABI, applies compare mode or direct-lighting shading
    with optional shadow rays, averages samples, and writes one `vec4` into
    `OutputPixels`.
16. `lxe_offline_render` receives `FrameGraphExecutionResult.outputs`, selects
    `offline.output`, converts the self-describing payload to EXR/PNG data, and
    writes files. File paths remain outside `FrameGraphExecutionRequest` and
    `FrameGraphExecutor`.

IBL bake shares the graph preparation, executor, readback, and output
post-processing parts of this flow in `074-h`. It remains driven by explicit
bake items plus the selected bake RenderPathGraph `bake` block; scene filter
unification for bake is a later refactor.

## Spec Section Order

The pre-implementation sections are `Goal`, `Current Facts`,
`Hard-Cut Invariants`, `Execution Terminology`, and
`OfflineRT Rendering Logic Flow`. After those sections, implementation sections
are ordered by dependency: authored inputs first, contract boundaries next,
future hardware target before current software lowering, then preparation,
execution, output, cleanup, and validation.

1. CLI scene/profile input;
2. RenderPathGraph asset;
3. graph resources;
4. OfflineRT RenderFeature;
5. standard-pbr material/shader scope;
6. material hit shader contract;
7. acceleration resource ownership;
8. parser/schema validation;
9. hardware RT target architecture;
10. scene participation filter;
11. ray program assembly;
12. acceleration feature-resource precondition;
13. software BVH lowering;
14. domain-neutral `RenderWorkBuildContext`;
15. caller-side preparation responsibilities;
16. compute dispatch contract;
17. readback/output contracts;
18. `RenderWorkCompiler` preparation;
19. prepared-work ownership and invalidation;
20. `FrameGraphExecutionRequest` packaging;
21. realtime/offline execution mode;
22. `FrameGraphExecutor` execution/readback;
23. offline image writing;
24. IBL bake parameter config, output migration, and post-processing;
25. legacy deletion;
26. validation, task slicing, and open design points.

Each section describes one design decision so later implementation tasks can
reference it directly.

## Offline CLI Scene And Profile Input

`lxe_offline_render` is the user-facing orchestration entry, but it is not a
renderer architecture boundary. It owns only CLI/profile selection and output
paths.

The CLI stage prepares these facts before graph compilation/preparation:

- scene document path and selected camera path;
- output resolution/background/output file path;
- OfflineRT runtime parameters such as samples, max bounce, seed, shadow toggle,
  and compare mode;
- selected RenderPathGraph asset: `offline_ray_tracer.render-path.yaml`;
- a `Scene` loaded from the scene document, with resources registered through
  `Scene::resources()`.

The offline loader must not return an `offlineShader` side channel. Shader
selection comes from the RenderPathGraph, RenderFeature, and material source
contracts. The output file path also stays outside graph/executor state and is
used only after `FrameGraphExecutionResult.outputs` is produced.

## RenderPathGraph Asset

Add `assets/render_paths/offline_ray_tracer.render-path.yaml`:

```yaml
schema: lxe.render-path-graph.v1
name: OfflineRayTracer
renderPath: OfflineRT

features:
  offlineRayTracer:
    uri: effects/offline_ray_tracer.render-feature.yaml

passes:
  - id: OfflinePrimaryRay
    stage: compute
    dispatch: compute
    shader: render_paths/OfflineRT/offline_pbr_direct_ray
    input:
      kind: compute-dispatch
      object:
        renderClass: [mesh]
      material:
        type: [standard-pbr]
        required: true
      geometry:
        vertex: position-only
        topology: triangle-list
    sources:
      - scene.camera
      - scene.geometry
      - scene.materials
      - scene.textures
      - scene.lights
      - feature.offlineRayTracer
    targets: [offline.output]
    compute:
      dispatchFrom: offline.output.resolution
      localSize: [8, 8, 1]
    readbacks:
      - name: offline.output
        target: offline.output
        extentFrom: offline.output.resolution
        binding: OutputPixels
        format: RGBA32Float
        kind: image2d
        mediaType: application/x-lxe-rgba32f
```

`OfflinePrimaryRay` is graph identity only. Compiler and executor behavior must
come from `stage`, `dispatch`, `input`, `compute`, shader reflection, and graph
resources, not from pass-name special cases.

`input.geometry` follows the same render-path contract used by realtime
Forward/Deferred passes. It filters/selects compatible scene geometry and
topology; it does not describe the full shading attribute storage used by
OfflineRT. The positive mesh path should use `vertex: position-only` plus
`topology: triangle-list`, matching the existing realtime mesh passes.
Normals, UVs, tangents, and any other hit-time attributes come from the
engine-owned scene/material storage ABI: shader reflection, system-owned binding
names, material source contracts, and `SceneResourceTable::buildUploadView()`.
They are not authored as RenderFeature `sceneStorage` schema and they are not a
new vertex layout contract in the OfflineRT graph.

## Graph Resources

`GraphResourceRegistry::makeDefault()` must recognize the resources the
OfflineRT graph declares. At minimum this slice needs:

- `scene.camera`;
- `scene.geometry`;
- `scene.materials`;
- `scene.textures`;
- `scene.lights`;
- `offline.output`;
- `feature.offlineRayTracer`.

These names are graph dependency names, not placeholders. A dependency is ready
only when the relevant loader/upload path registers live typed payloads or
domain facts. Metadata-only declarations must not satisfy renderable graph
dependencies.

`scene.bvh` is intentionally not a graph source. BVH is generated data owned by
`feature.offlineRayTracer`'s acceleration resource requirement. Referencing the
feature from the RenderPathGraph is what causes the BVH data to be collected,
validated against shader reflection, and attached to the pass descriptor plan
and PipelineBuildDesc extras when needed.

Ray hit shader resources are reached through selected material
`hit.radiance.uri` fields, not through extra graph source names. The graph
should not list `scene.hitShaders` or `offline.hitShaders`.
`feature.offlineRayTracer` declares the pass hit shader table that maps allowed
hit shader URIs to stable `hitGroupIndex` values. A missing material hit shader
URI, missing table entry, or mismatched table entry fails pass preparation.

## Unified RenderFeature Schema

`RenderFeature` has one global schema and one core C++ model. OfflineRT,
Forward pass flags, environment lighting, tone mapping, bloom, and future RT
features all use the same `RenderFeature` class; only `level` and field
validation decide which fields are legal. Do not add a second OfflineRT feature
schema, feature registry, or parser-local public DTO.

The target YAML shape is:

```yaml
schema: lxe.render-feature.v1
name: FeatureName
feature: stableFeatureKey
level: shader | pass
shader:
  uri: features/or/render_path_shader
parameters: {}
resources: {}
hitShaderTable: {}
```

Top-level fields map to core types as follows:

| YAML field | Core target | Meaning |
|---|---|---|
| `schema` | parser validation only | Schema version. Unsupported versions fail. |
| `name` | `RenderFeature::name` | Human-readable name. |
| `feature` | `RenderFeature::feature` | Stable key used by graph resources such as `feature.surfaceLighting`. |
| `level` | `RenderFeature::level` | `shader` or `pass`; controls parameter/resource/table validation. |
| `shader.uri` | `RenderFeature::shader.uri` | Shader ABI/reflection target for parameters and pass feature validation. |
| `parameters` | `RenderFeature::parameters` | Scalar/vector/resource parameters and volatile runtime UBO members. |
| `resources` | `RenderFeature::resources` | Feature-declared derived resources only. |
| `hitShaderTable` | `RenderFeature::hitShaderTable` | Ray hit table owned by a pass-level ray feature. |

Existing core fields are extended, not replaced:

```cpp
struct RenderFeatureParameter final {
  std::string kind;
  std::string value;
  ResourceUri uri;
  std::string sourceHash;
  std::string valueType;
  std::string binding;
  std::string member;
  bool required = false;
  bool volatileRuntime = false;
  std::vector<std::string> allowedValues;
  std::string requiredWhenParameter;
  std::string requiredWhenEquals;
};

struct RenderFeature final {
  std::string name;
  std::string feature;
  RenderFeatureLevel level = RenderFeatureLevel::Unknown;
  std::optional<RenderFeatureShaderContract> shader;
  std::unordered_map<std::string, RenderFeatureParameter> parameters;
  std::unordered_map<std::string, RenderFeatureResourceRequirement> resources;
  std::optional<RenderFeatureHitShaderTable> hitShaderTable;
};
```

`RenderFeatureParameter` remains the only parameter model:

- `kind` is the feature parameter type, such as `bool`, `u32`, `i32`, `float`,
  `vec3`, `vec4`, `enum`, `texture2D`, or `textureCube`.
- `value` is for non-volatile inline values.
- `uri` / `sourceHash` are for resource-like parameters, such as environment
  maps.
- `valueType` is semantic metadata, for example `linear-radiance`; it is not a
  Material parameter type and must not route through Material parameter
  handling.
- `binding` / `member` identify shader ABI placement.
- `volatile: true` means runtime data. A volatile parameter must be pass-level,
  must have `binding` / `member`, and must not author `value`, `valueType`, or
  specialization metadata.
- All volatile parameter values are supplied through
  `RenderWorkBuildContext::Options::featureValues` as
  `RenderFeatureVolatileValue`. Existing forward-pass volatile fields such as
  `enableIblLighting`, `environmentIblReady`, and `standardPbrIblReady` migrate
  to this same path; OfflineRT must not add a separate volatile-value channel.

`level` validation is fixed:

- `level: shader`: parameters may bind to feature UBOs or texture bindings and
  may author static `value` / `uri` data. This covers `environmentLighting`,
  `surfaceLighting`, `toneMapping`, `bloom`, and similar shared shader
  features.
- `level: pass`: non-volatile parameters are static pass facts /
  specialization constants and must not use `binding` / `member`; volatile
  parameters are runtime UBO members and must use `binding` / `member`.

`resources` is not a general descriptor list. It contains only
feature-declared derived resources that require explicit producer code. The
RenderFeature declares the requirement, the producer builds the resource, and
`SceneResourceTable` owns the actual runtime resource lifetime/registration. In
`074-h`, the only positive resource shape is scene acceleration:

```cpp
enum class RenderFeatureResourceApi { SceneAcceleration };
enum class RenderFeatureResourceImplementation {
  SoftwareBvh,
  HardwareRayTracing,
};

struct RenderFeatureResourceOutput final {
  std::string kind;
  std::string binding;
  std::string layout;
  std::string elementType;
};

struct RenderFeatureResourceRequirement final {
  RenderFeatureResourceApi api;
  std::string function;
  RenderFeatureResourceImplementation implementation;
  bool derived = true;
  bool volatileRuntime = true;
  std::string source;
  RenderFeatureResourceOutput output;
  bool required = false;
};
```

The `resources` field must reject ordinary system-owned scene/material
bindings. `SceneObjects`, `SceneDraws`, `SceneMaterials`, `SceneTextures`,
`SceneMaterialRefs`, normal/UV/tangent storage, and similar ABI resources are
prepared from `SceneResourceTable`, shader reflection, and fixed system binding
rules. They are not authored in RenderFeature YAML.

IBL converges into this same RenderFeature boundary:

- `environmentLighting`, `surfaceLighting`, `skybox`, tone mapping, bloom, and
  similar runtime shader features stay as shader-level `RenderFeature` assets
  with `parameters` for texture, UBO, and scalar bindings.
- Forward-pass IBL switches and readiness flags stay as pass-level volatile
  `parameters`, supplied by `RenderFeatureVolatileValue`; they must not remain
  as ad hoc `RenderWorkBuildContext` booleans.
- IBL bake products such as `diffuse_sh9`, `specular_prefilter`, and `brdf_lut`
  are graph outputs: `RenderPathReadbackContract` resolves them into
  `RenderInputDesc::Readback`, `FrameGraphExecutor` returns them as
  `FrameGraphExecutionPayload`, and `IblBakeJobService` writes/cache-activates
  them. They are not `RenderFeature::resources`.
- Scene-level baked IBL resources are registered in `SceneResourceTable` and
  referenced by graph resources such as `scene.environmentBake` or
  `scene.materialIblBake`. The feature authoring file does not declare those
  scene-owned resources.
- If a future IBL feature declares a derived GPU resource producer with an
  explicit engine API/function, that producer may use
  `RenderFeature::resources`; current IBL bake outputs do not.

`hitShaderTable` is also a `RenderFeature` extension, not a separate ray program
asset:

```cpp
struct RenderFeatureHitShaderTableEntry final {
  u32 index = 0;
  std::string materialType;
  ResourceUri uri;
  std::string function;
};

struct RenderFeatureHitShaderTable final {
  std::string payload;
  std::string dispatchFunction;
  std::vector<RenderFeatureHitShaderTableEntry> entries;
};
```

`hitShaderTable` is legal only for pass-level ray features in this slice. It
defines stable hit group indices and allowed material hit shader URIs. It does
not define per-primitive assignment; `PrimitiveHitGroups` is derived during
preparation from selected scene primitives and the resolved table.

All RenderFeature assets use this schema. Parser allowlists must be exact: a
field accepted by `RenderFeatureResourceParser` must be stored into
`RenderFeature` and consumed by preparation, or the asset must fail with a
diagnostic. Legacy/alternate fields such as `rayPrograms`, ad hoc
scene-storage lists, descriptor resources under `parameters`, or
OfflineRT-only feature schemas are rejected.

## OfflineRT Render Feature

Add `assets/effects/offline_ray_tracer.render-feature.yaml`:

```yaml
schema: lxe.render-feature.v1
name: OfflineRayTracer
feature: offlineRayTracer
level: pass
shader:
  uri: render_paths/OfflineRT/offline_pbr_direct_ray
hitShaderTable:
  payload: radiance
  dispatchFunction: lxDispatchRadianceHit
  entries:
    - index: 0
      materialType: standard-pbr
      uri: assets://shaders/glsl/common/materials/hits/standard_pbr_radiance.glsl
      function: lxHitStandardPbrRadiance
parameters:
  enableDirectLighting:
    kind: bool
    value: true
    required: true
  enableTextureSampling:
    kind: bool
    value: true
    required: true
  samples:
    kind: u32
    volatile: true
    binding: OfflineRayTracerUBO
    member: samples
    required: true
  maxBounce:
    kind: u32
    volatile: true
    binding: OfflineRayTracerUBO
    member: maxBounce
    required: true
  seed:
    kind: u32
    volatile: true
    binding: OfflineRayTracerUBO
    member: seed
    required: true
  compareMode:
    kind: enum
    volatile: true
    binding: OfflineRayTracerUBO
    member: compareMode
    required: true
    allowedValues: [beauty, albedo]
resources:
  acceleration:
    api: scene-acceleration
    function: buildSceneAcceleration
    implementation: software-bvh
    derived: true
    volatile: true
    source: scene.selection
    output:
      kind: storage-buffer
      binding: SceneBvhNodes
      layout: struct-array
      elementType: LxSceneBvhNode
    required: true
```

Static pass-level bools may become specialization constants and enter pipeline
identity. Volatile values are resolved from
`RenderWorkBuildContext::Options` runtime feature values during preparation and
enter a UBO; they must not enter `PipelineKey`.

Runtime scalar controls are not graph `sources`. They are pass-level
RenderFeature volatile parameters. The graph references
`feature.offlineRayTracer`; preparation then fills the feature's volatile UBO
members from `RenderWorkBuildContext::Options` runtime feature values. This
keeps graph sources limited to resource dependencies and prevents
profile/settings objects from becoming fake FrameGraph resources.

`parameters` and `resources` are disjoint schema surfaces:

- `parameters` holds scalar/vector values only. A non-volatile parameter may
  become a specialization constant or static pipeline fact. A volatile
  parameter is materialized into a named UBO binding/member, such as
  `OfflineRayTracerUBO.samples`.
- `resources` holds feature-declared derived resources only in this slice. It
  does not list the ordinary system-owned scene/material storage bindings that
  are already implied by shader reflection and SceneResourceTable upload views.
- A storage buffer is never modeled as a `parameter`. Acceleration data such as
  `SceneBvhNodes` is a derived resource output with `kind: storage-buffer`.
- Future hardware RT keeps the same split: scalar controls remain
  `parameters`, while acceleration output changes from `kind: storage-buffer`
  to `kind: acceleration-structure` plus PipelineBuildDesc extras returned by the
  producer.

RenderFeature data has four categories:

- static parameters: authored values that may enter pipeline identity, such as
  bool specialization constants;
- volatile parameters: runtime scalar/vector values materialized into UBOs,
  such as samples, seed, max bounce, and compare mode;
- derived volatile resources: feature-declared resources such as acceleration
  data whose content is generated by a named feature resource API/function
  before execution;
- hit shader tables: pass-level dispatch tables that assign stable
  `hitGroupIndex` values to allowed material hit shader URIs.

BVH belongs to the third category. The feature YAML defines the resource shape,
source, API/function, implementation, and output descriptor contract, not the
concrete data. `derived: true` means the resource is computed, `volatile: true`
means the resource must be refreshed when its inputs change,
`api: scene-acceleration` / `function: buildSceneAcceleration` define the
stable feature-resource interface, and `implementation: software-bvh` selects
the current producer that converts selected scene primitives into the
`SceneBvhNodes` storage buffer.

`hitShaderTable` and `PrimitiveHitGroups` are intentionally different layers:

| Name | Authored Where | Meaning | Lifetime / Invalidation | Software BVH Lowering | Hardware RT Lowering |
|---|---|---|---|---|---|
| `hitShaderTable` | RenderFeature YAML | Pass-level table of allowed hit shader programs: stable `index`, material type, hit shader URI, function name, payload contract. | Stable with the RenderFeature/pass contract; changes when the graph/feature asset changes. | Supplies `hitGroupIndex` constants and the software dispatch function/switch target. | Supplies hit groups and metadata for future SBT construction. |
| `PrimitiveHitGroups` | Not authored; derived during pass preparation | Scene-specific lookup from selected primitive to a `hitShaderTable.index`. | Rebuilt when selected primitives, materials, material hit URIs, or the hit shader table change. | Optional system-owned SSBO only if the software shader reflects that binding. | Lowered into primitive/SBT record association or equivalent backend metadata, not necessarily shader-visible. |

The RenderFeature owns `hitShaderTable` because it describes the ray program
contract for the pass. `PrimitiveHitGroups` is prepared data because it depends
on the concrete selected scene and material instances. Authoring
`PrimitiveHitGroups` in YAML would duplicate scene-derived data and would not
map cleanly to hardware RT.

Per-primitive hit group data is generated after ray program assembly has
matched selected material `hit.radiance.uri` values against the RenderFeature
`hitShaderTable`. It is a preparation artifact derived from selected
primitives and the hit table. If the software shader needs a
`PrimitiveHitGroups` descriptor, that binding should be treated like other
engine-owned system bindings: prepared from `SceneResourceTable` upload data
and shader reflection, not authored as a RenderFeature `sceneStorage` entry. A
future hardware RT path can use the same per-primitive hit group information to
build SBT record offsets instead of exposing a shader-visible SSBO.

`PrimitiveHitGroups` is not a ray-type dispatch table. It answers this
question after traversal: "which material hit group should shade the primitive
that was hit?" Primary rays, bounce rays, and shadow rays may all hit the same
primitive and therefore see the same primitive hit group. The difference
between primary/beauty rays and shadow/visibility rays is owned by the primary
ray program and payload policy in this slice. For example, a shadow ray can use
the same traversal path and either stop at any accepted occluder or call a
minimal visibility branch without adding a second hit table. A future hardware
RT implementation can introduce explicit ray types, miss groups, any-hit
programs, SBT record variants, or ray flags, but `074-h` keeps that as a future
lowering detail.

Concrete hit shader implementation is not stored in this RenderFeature. The
RenderFeature owns the table and dispatch contract: payload kind, table indices,
allowed hit shader URIs, and the software dispatch function name. The material
owns the hit shader URI it needs for a payload. Preparation joins the two:
selected material `hit.radiance.uri` must appear in the
`feature.offlineRayTracer.hitShaderTable`, and the matching table entry supplies
the primitive `hitGroupIndex`.

For future hardware RT, the same feature resource API can switch to a backend
implementation:

```yaml
resources:
  acceleration:
    api: scene-acceleration
    function: buildSceneAcceleration
    implementation: hardware-rt
    derived: true
    volatile: true
    source: scene.selection
    output:
      kind: acceleration-structure
      binding: SceneAcceleration
    required: true
```

The hardware implementation does not need the software `LxSceneBvhNode`
structure-array output. It calls backend acceleration-structure build code and
returns a descriptor/handle compatible with the hardware RT shader contract.
The RenderPathGraph still references `feature.offlineRayTracer`; only the
feature resource implementation and pipeline lowering change.

The current `RenderFeatureResourceParser` already allows pass-level static
parameters and volatile runtime UBO parameters. This slice extends the
RenderFeature schema with feature-declared derived `resources` and
`hitShaderTable` data.
The missing pieces are:

- parser support for storing feature resource requirements;
- parser support for storing hit shader table entries and stable indices;
- validation that feature resource bindings match shader reflection;
- validation that hit shader table entries have unique indices and URIs;
- preparation logic that dispatches named feature resource API/functions,
  collects selected scene data, computes generated resources, and appends
  descriptors because the RenderPathGraph references
  `feature.offlineRayTracer`;
- preparation logic that validates selected material hit shader URIs against the
  feature hit shader table and emits primitive hit group data;
- offline contexts exposing pass feature data to `RenderWorkCompiler`.

## Material And Shader Scope

This slice targets `standard-pbr` only:

- shader source: `common/materials/standard_pbr.contract.glsl`;
- material resource schema: `lxe.material.v2`;
- expected source material buffers: `SceneMaterialRefs` and
  `SceneSourceMaterialRecords`;
- expected texture array: `SceneTextures`;
- expected surface accessor: `lxLoadMaterialSurface(...)`;
- expected BSDF functions: `lxEvaluateBsdf(...)` and `lxSampleBsdf(...)`.

The OfflineRT shader must move to `render_paths/OfflineRT` and be compiled as a
standard-pbr material-source variant. Its descriptor contract must include the
same standard-pbr source-material bindings as realtime. It must not declare a
private incompatible `SceneTextures` binding or skip source-local material
records.

The `standard-pbr` material source contract supplies BSDF/surface functions,
not a second material-specific render path. Material v2 is extended with a
`hit` map that points payload kinds to hit shader source URIs:

```yaml
schema: lxe.material.v2
bsdf:
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
hit:
  radiance:
    uri: assets://shaders/glsl/common/materials/hits/standard_pbr_radiance.glsl
```

The referenced hit shader lives under the normal shader tree and can include or
reuse the material source contract ABI. The initial `standard-pbr` radiance hit
shader wraps the existing material ABI:

```text
standard-pbr radiance hit shader
  -> interpolate hit attributes
  -> lxLoadMaterialSurface(...)
  -> lxEvaluateBsdf(...) / lxSampleBsdf(...)
  -> return payload contribution or next-ray sample
```

Multiple bounces use the same trace/hit/miss contract repeatedly inside the
primary ray program. They are not modeled as separate FrameGraph passes in this
slice. A later wavefront/path-queue tracer may split bounces into multiple
passes, but that would still consume the same ray program and acceleration
contracts.

Non-`standard-pbr` materials in the Helmet smoke are rejected with diagnostics
that name the object/material and explain that this slice supports
`standard-pbr` only. Future support for PBRT/uber/metal/substrate/glass/mix is
outside this spec.

## Material Hit Shader Contract

OfflineRT should expose the same high-level shader model for software BVH and
future hardware RT, but material hit behavior starts from material schema:

```text
primary ray program
  -> trace scene through acceleration API
  -> miss program when no hit exists
  -> hit group selected from RenderFeature hitShaderTable
  -> closest-hit/any-hit material shading program
```

The RenderPathGraph still selects the pass and primary/raygen shader URI. The
`offlineRayTracer` RenderFeature declares acceleration, pass-level runtime
controls, and the hit shader table. Each selected material declares which hit
shader URI it needs for the `radiance` payload.

The important user-facing abstraction is stable:

- primary ray code defines camera rays, sampling, bounce loop, and payload
  policy;
- material hit code defines how an intersection becomes a shaded surface
  response;
- acceleration implementation defines how rays are intersected, but not the
  material shading policy.

Material schema should extend the current root allowlist with `hit`. For this
slice the only positive payload key is `radiance`:

```yaml
hit:
  radiance:
    uri: assets://shaders/glsl/common/materials/hits/standard_pbr_radiance.glsl
```

The hit shader source should carry a lightweight documentation marker so the
hand-written software dispatch table can stay readable:

```glsl
// LX_HIT_SHADER_BEGIN
// payload: radiance
// function: lxHitStandardPbrRadiance
// LX_HIT_SHADER_END
```

Hit shader source files live under:

```text
assets/shaders/glsl/common/materials/hits/
```

The `standard-pbr` positive path uses:

```text
assets/shaders/glsl/common/materials/hits/standard_pbr_radiance.glsl
```

This marker is not a second reflection system and should not be required to
drive runtime behavior. The authoritative runtime facts are the material
`hit.radiance.uri` and the RenderFeature `hitShaderTable` entry. The marker is
there to document the source-level function expected by the table; the actual
compile will fail if the dispatch function calls a missing hit shader function.
`074-h` also adds a lightweight source-text audit: for every
`hitShaderTable.entries[].uri` / `function` pair, the referenced hit shader
source must contain the function name. The audit is intentionally shallow; it
guards table/source drift without creating a new shader reflection system.

The ownership is fixed:

- `.material` instances select a material type/source and declare
  `hit.radiance.uri`;
- hit shader source lives under `assets/shaders/glsl/...` like other shader
  source;
- RenderFeature declares the pass hit shader table and stable indices;
- selected scene primitives derive a hit group from the material hit URI matched
  against the RenderFeature table;
- a selected material without required `hit.radiance.uri`, or with a URI missing
  from the table, fails pass preparation.

The initial `standard-pbr` hit program may wrap the existing material ABI:

```text
standard-pbr hit program
  -> interpolate hit attributes
  -> lxLoadMaterialSurface(...)
  -> lxEvaluateBsdf(...) / lxSampleBsdf(...)
  -> return payload contribution
```

For the `074-h` positive path there is only one accepted material hit shader:
`standard-pbr` for `radiance`. The table and SSBO shape still support multiple
entries so the software path stays structurally compatible with future hardware
RT. Additional material types become positive paths only when their material
schema declares compatible hit shader URIs, their table entries exist, and their
source material storage ABI is prepared.

## Acceleration Resource Ownership

BVH is scene-derived data, but it should not be modeled as a concrete
`SceneNode`.

Scene nodes represent authored scene entities with identity, transform,
hierarchy, visibility/layer behavior, editor selection, serialization, and
component lifecycle. A BVH is different:

- it is derived from many scene objects, not owned by one object;
- it has no meaningful local transform or parent-child scene hierarchy;
- it may be pass/filter/feature specific, for example standard-pbr mesh subset
  for OfflineRT versus another subset for picking, shadows, physics, or future
  GPU ray tracing;
- it is invalidated by scene geometry, object transform, material/filter, and
  feature configuration changes;
- multiple acceleration structures can be valid for the same `Scene` at the
  same time.

The recommended model is a scene-level derived acceleration resource:

```text
Scene + scene participation filter + RenderFeature acceleration requirement
  -> SceneResourceTable derived-resource cache
  -> descriptor resource, e.g. SceneBvhNodes
```

`SceneResourceTable` is the right owner for lifetime, dirty tracking, cache
keys, and delayed GPU resource release. `RenderFeature` is the right owner for
the demand and configuration: whether the pass needs acceleration data, whether
that resource is derived/volatile, which feature resource API/function computes
it (`buildSceneAcceleration` in this slice), which implementation is selected
(`software-bvh` now, `hardware-rt` later), which scene selection it uses, and
which shader binding receives the generated resource.

The producer is independently encapsulated behind an internal engine API, not
embedded in the offline renderer or hardcoded in `RenderWorkCompiler`. Use an
interface plus a closed engine-owned registry. YAML may select only validated
`api` / `implementation` values; YAML must not name or call arbitrary C++
functions.

```cpp
enum class RenderFeatureResourceApi {
  SceneAcceleration,
};

enum class RenderFeatureResourceImplementation {
  SoftwareBvh,
  HardwareRayTracing,
};

struct RenderFeatureDerivedResourceRequest final {
  RenderFeatureResourceApi api;
  RenderFeatureResourceImplementation implementation;
  StringID pass;
  StringID feature;
  StringID binding;
  std::span<const RenderSceneParticipant> selectedScene;
  std::reference_wrapper<const Scene> scene;
  std::reference_wrapper<const SceneResourceTable> resources;
};

struct RenderFeatureDerivedResourceResult final {
  DescriptorResourceList descriptors;
  std::vector<GpuResourceRef> resourceDependencies;
  std::vector<PipelineBuildDescExtra> pipelineBuildDescExtras;
};

class RenderFeatureDerivedResourceProducer {
public:
  virtual ~RenderFeatureDerivedResourceProducer() = default;

  [[nodiscard]] virtual RenderFeatureDerivedResourceResult
  produce(const RenderFeatureDerivedResourceRequest &request) = 0;
};
```

The producer boundary is fixed:

- RenderFeature declares the API/function and selected implementation.
- A closed registry maps that API/implementation to engine code. Registry
  construction is C++ owned by core render-work preparation and/or
  scene-resource-table integration; it is not an asset and not an OfflineRT
  renderer helper.
- The authored `function` string is a validated semantic operation name, such
  as `buildSceneAcceleration`; it is not a dynamic symbol, script hook, or
  reflection target.
- The software BVH producer builds CPU BVH records and registers a storage
  buffer descriptor such as `SceneBvhNodes`.
- A future hardware RT producer calls backend acceleration-structure build code
  and returns an acceleration-structure descriptor/handle plus any pipeline
  PipelineBuildDesc extras required by the backend instead of a software struct array.
- Unsupported API/implementation pairs fail validation; missing producer output
  fails pass preparation.

The preparation contract is:

```text
RenderPathGraph pass reads feature.offlineRayTracer
  -> selected materials provide hit.radiance.uri
  -> RenderFeature hitShaderTable maps URI to hitGroupIndex/function
  -> prepare assembles RayProgramTable
  -> selected primitives receive/derive hitGroupIndex
  -> RenderFeature declares derived volatile scene-acceleration resource
  -> scene participation filter selects objects/primitives
  -> buildSceneAcceleration producer runs selected implementation
  -> software-bvh: SceneResourceTable caches/registers SceneBvhNodes buffer
  -> hardware-rt: backend builds/caches acceleration structure descriptor
  -> PassPreparationFacts receives produced descriptors and PipelineBuildDesc extras
  -> RenderWorkCompiler copies them into RenderInputDesc / PipelineBuildDesc
```

Derived resource producers are explicit engine code selected by a closed enum or
validated API/function name. YAML cannot point at arbitrary code. Unsupported
API/function/implementation names fail parser/validation; missing generated
descriptors fail pass preparation.

A future editor-facing visualization/debug node may display acceleration stats
or bounds, but that node must not be the source of truth for the BVH resource.
The source of truth is always the selected scene data plus the feature
requirement.

## Parser And Schema Validation

The RenderPathGraph parser must be strict:

- every allowlisted field is consumed into `RenderPassNode` or rejected;
- unsupported fields fail with diagnostics;
- compute-specific fields must not be accepted and ignored;
- the old shader URI under `techniques/OfflineRT/...` is rejected;
- `OfflinePrimaryRay` does not unlock hidden parser behavior.

Current parser support for `stage: compute`, `dispatch: compute`, and
`input.kind: compute-dispatch` remains valid. The missing schema surface is the
first-class compute/readback contract described below.

The RenderFeature parser must also be strict:

- all `.render-feature.yaml` assets use the same `RenderFeature` schema and
  core C++ model. Parser behavior must not branch into an OfflineRT-only public
  feature type;
- `resources` and `hitShaderTable` are either fully parsed into the feature
  model or rejected;
- `parameters` accepts only scalar/vector values and optional UBO or
  specialization mapping; descriptor resources authored under `parameters` are
  rejected;
- `resources` accepts only feature-declared derived resource declarations in
  this slice; scalar runtime controls and ordinary scene/material system
  bindings authored under `resources` are rejected;
- IBL bake output names such as `diffuse_sh9`, `specular_prefilter`, `brdf_lut`,
  `scene.environmentBake`, and `scene.materialIblBake` authored under
  `RenderFeature::resources` are rejected. They belong to graph readbacks and
  `SceneResourceTable` registration, not feature-declared derived resources;
- `rayPrograms` is not a supported RenderFeature field in this design and must
  be rejected if authored there;
- unsupported acceleration API/function/implementation names fail validation;
- feature resource bindings must match shader reflection and produced
  descriptor resources;
- hit shader table entries must have unique `index` values, unique `uri`
  values for a given payload, a required `function`, and payload key `radiance`
  in this slice;
- table function names are stored and consumed into the software dispatch
  contract. The hand-written dispatch shader must stay consistent with the
  table; this is documented with source comments, checked by a shallow
  source-text audit, and finally validated by shader compilation rather than
  SPIR-V reflection.

The MaterialResource parser must be strict:

- root field `hit` is fully parsed into `MaterialInstance` or rejected;
- unsupported `hit` payload keys fail diagnostics; `074-h` accepts `radiance`;
- `hit.radiance.uri` must be a live shader resource URI if the material
  participates in OfflineRT;
- selected materials whose hit URI is absent from the RenderFeature
  `hitShaderTable` fail pass preparation.

## Hardware RT Target Architecture

The software BVH implementation in `074-h` must be shaped as a lowering of the
future hardware RT architecture. The hardware target flow is:

1. RenderPathGraph declares an OfflineRT ray tracing pass. The current graph
   still uses `stage: compute`; a future graph can switch to
   `stage: ray-tracing` / `dispatch: ray-trace` without changing material
   contracts.
2. The pass shader supplies the raygen/primary program. This is the same role
   as `offline_pbr_direct_ray` today: camera rays, sample loop, bounce loop,
   payload initialization, and output write.
3. Scene participation filtering selects mesh/material/geometry primitives.
   Each selected material declares a `hit.radiance.uri`.
4. Preparation assembles `RayProgramTable`: one raygen program, miss programs,
   and one hit group per RenderFeature `hitShaderTable` entry that is used by
   selected materials. The table entry's index becomes the primitive
   `hitGroupIndex`.
5. Acceleration preparation uses the same selected primitives and hit group
   indices to build backend acceleration structures. Hardware RT maps those
   indices to instance/geometry SBT record offsets or equivalent backend
   metadata.
6. `PipelineBuildDesc::rayTracing(...)` or equivalent PipelineBuildDesc extras
   carry raygen/miss/hit shader stages, hit group records, max recursion depth,
   SBT layout, descriptor bindings, and specialization constants.
7. The backend creates a Vulkan ray tracing pipeline, builds the shader binding
   table, binds the top-level acceleration structure descriptor, dispatches
   rays, and collects readbacks through the same `FrameGraphExecutor` output
   path.

The future hardware path must extend the existing `PipelineBuildDesc`,
`RenderInputDesc`, descriptor/resource, and `FrameGraphExecutor` boundaries. It
must not introduce a second OfflineRT renderer/job/pipeline abstraction.

In the current codebase `PipelineBuildType::RayTracing` exists, but
`ShaderStage` and Vulkan pipeline construction do not yet support raygen,
miss, closest-hit, any-hit, intersection, callable stages, or SBT construction.
Therefore `074-h` lowers this contract to `PipelineBuildType::Compute`.
Hardware RT is a future lowering of the same graph/material/feature contract,
not a second OfflineRT frontend.

The future hardware path should extend `PipelineBuildDesc` rather than bypass
it. The likely extension is a `PipelineBuildDesc::rayTracing(...)` constructor
or PipelineBuildDesc extras carrying:

- raygen/miss/material-hit shader stage code;
- hit group records;
- SBT layout/build inputs;
- max recursion depth;
- acceleration-structure descriptor requirements.

The software path should keep its API shaped similarly by returning a resolved
`RayProgramTable` and acceleration descriptor facts, even though the backend
pipeline remains a compute pipeline.

## Scene Participation Filter

Object/material/geometry filtering is not realtime-specific and is not owned by
the raster draw path. It answers a domain-neutral question: which scene objects
participate in this pass?

`input.kind` selects the prepared execution payload:

- `scene-renderables` builds `RenderDrawInput` work;
- `fullscreen-triangle` builds fullscreen draw work;
- `compute-dispatch` builds `RenderComputeInput` work.

The scene filter is orthogonal to that choice. The existing
`RenderPassInputContract.object`, `.material`, and `.geometry` fields should be
usable by both `scene-renderables` and scene-consuming `compute-dispatch`
passes. The parser must stop rejecting these fields solely because
`input.kind == compute-dispatch`.

Compiler-side scene selection should be factored into a shared helper, for
scene-consuming passes. This must not introduce a second filter schema. The
filter source of truth remains the existing `RenderPassInputContract`:

- `RenderPassInputContract::object` stores `input.object.renderClass`;
- `RenderPassInputContract::material` stores `input.material.type` and
  `required`;
- `RenderPassInputContract::geometry` stores the pass geometry contract.

The current draw path already materializes selected scene participants while
building `RenderDrawInput` entries from `IRenderable::getValidatedPassData()`,
including object, mesh, material, primitive index, render type, material type
signature, vertex/index resources, and draw commands. `074-h` should extract
the common selected-participant data from `buildSceneRenderableInputs(...)`
instead of defining parallel OfflineRT-only `SceneSelectionFilter` or
`SelectedSceneObject` public structs.

The shared helper boundary should be:

```text
Scene + RenderPassInputContract + visibility/camera facts
  -> selected scene participant records using the existing renderable pass data
```

The concrete C++ shape should be a neutral record such as
`RenderSceneParticipant`, produced by extracting the existing common
object/mesh/material/primitive fields that `RenderDrawInput` already carries.
`RenderDrawInput` then becomes the draw-submission payload built from that
selection plus draw commands. OfflineRT consumes the same selection record to
build compute-side scene storage and acceleration data. This is a refactor of
the existing draw selection data, not an independent OfflineRT-only selection
model.

The draw path consumes selected objects by creating `RenderDrawInput` entries.
OfflineRT consumes the same selected objects by building scene storage buffers,
standard-pbr source-material buffers, texture arrays, and a BVH for one compute
dispatch. That is reuse of the scene/filter/material preparation without
pretending that ray tracing is raster draw submission.

Fullscreen passes still reject object/material/geometry filters because they do
not consume scene objects.

IBL bake is a known exception in this slice. Its current flow is driven by
explicit bake items and payload contracts rather than the scene participation
filter. Architecturally it should eventually use the same filter concept, but
`074-h` does not force that refactor. This slice only moves bake output
collection onto the generic readback/executor path.

## Ray Program Assembly

`RayProgramTable` is a derived preparation artifact, not authored
as a separate graph. It is assembled from:

- the RenderPathGraph pass shader, which supplies the primary/raygen program;
- pass/output/runtime facts, which supply payload mode, bounce policy, and
  output target shape;
- selected material `hit.radiance.uri` values, which say which concrete hit
  shader each material needs;
- the RenderFeature `hitShaderTable`, which supplies stable indices, table
  entries, dispatch function name, and the allowed hit shader URIs for this
  pass;
- feature acceleration facts, which select software BVH or future hardware AS
  lowering.

The resolved contract should be represented as normal preparation facts:

```cpp
enum class RayProgramLowering {
  SoftwareCompute,
  HardwareRayTracing,
};

struct RayHitGroupProgram final {
  StringID name;
  StringID materialType;
  ResourceUri hitShaderUri;
  StringID materialSourceSignature;
  u32 hitGroupIndex = 0;
  StringID softwareFunction;
};

struct RayProgramTable final {
  RayProgramLowering lowering = RayProgramLowering::SoftwareCompute;
  StringID payloadType;
  ResourceUri primaryShader;
  StringID primaryEntry;
  StringID dispatchFunction;
  std::vector<StringID> missPrograms;
  std::vector<RayHitGroupProgram> hitGroups;
};
```

The `RayProgramTable` boundaries are fixed:

- the table is produced from graph pass shader facts, selected material hit
  shader URIs, the RenderFeature hit shader table, and feature acceleration
  facts;
- `hitGroupIndex` belongs to the RenderFeature hit shader table entry, not to
  the material instance;
- selected scene primitives can derive a `hitGroupIndex` from the table through
  a system-owned auxiliary primitive-to-hit-group lookup descriptor if the
  software shader reflects it;
- `hitGroupIndex` selects material hit behavior after a primitive is hit. It
  does not distinguish primary, bounce, or shadow ray types;
- software BVH uses the table to compile/select a compute shader variant and
  dispatch hit handling inside that shader;
- hardware RT uses the same table to build raygen/miss/hit shader groups and
  shader binding table records;
- a material selected by the pass filter without a matching hit group is a pass
  preparation error.

## Acceleration Feature-Resource Precondition

Acceleration handling is a precondition for preparing passes that reference a
RenderFeature with a derived acceleration resource. It is still treated as
normal RenderFeature processing: the RenderPathGraph references
`feature.offlineRayTracer`, the feature payload declares a resource
API/function, and preparation resolves the resource before
`RenderWorkCompiler::prepare()` finalizes the pass input description.

Acceleration preparation consumes the selected primitive set after material hit
program assembly has assigned or made derivable each primitive's
`hitGroupIndex`. It does not own the hit shading policy. This keeps the
software and hardware APIs aligned: software traversal can carry or derive that
index through `traceScene(...)`, and a future hardware path can use the same
index/table to build SBT records.

This stage has two outputs:

- descriptor resources for `RenderInputDesc.bindingPlan`;
- optional PipelineBuildDesc extras needed by the pipeline/cache/backend.

For the current software-BVH implementation, the descriptor output is enough:

```text
implementation: software-bvh
  -> builds LxSceneBvhNode[]
  -> carries/derives primitive hitGroupIndex through scene storage
  -> registers a storage buffer in SceneResourceTable
  -> appends DescriptorResourceRef(binding = SceneBvhNodes)
  -> no special PipelineBuildDesc extra is required
```

For future hardware RT, the same API/function can produce a different output:

```text
implementation: hardware-rt
  -> calls backend acceleration-structure build code
  -> returns an acceleration-structure descriptor/handle
  -> uses RayProgramTable to build hit groups/SBT record metadata
  -> may append PipelineBuildDesc extras required by RT pipeline creation
  -> may not create any shader-visible SSBO struct array
```

The implementation contract is that downstream code sees already prepared
facts. `RenderWorkCompiler` should not know how to build a BVH. It only copies
produced descriptors and PipelineBuildDesc extras into `RenderInputDesc` /
`PipelineBuildDesc` after validating that the referenced RenderFeature and
shader reflection agree.

`PipelineBuildDescExtra` is a supplement to the existing `PipelineBuildDesc`,
not a replacement pipeline description. If an extra becomes stable, broadly
used, and understood by the pipeline cache/backend, it should graduate into a
typed `PipelineBuildDesc` field or constructor. `074-h` may specify the type for
hardware-RT compatibility, but the software-BVH positive path should not require
one.

## Software BVH Lowering

`074-h` implements the same architecture with a compute shader and software BVH.
The implementation priority is migration compatibility with future hardware RT,
not optimizing the software dispatch branch.

The software flow is:

1. The graph pass stays `stage: compute` / `dispatch: compute`.
2. The pass shader remains the primary/raygen program and owns camera rays,
   sample loop, bounce loop, compare mode, and output write.
3. Selected materials provide `hit.radiance.uri`; the RenderFeature
   `hitShaderTable` maps that URI to `hitGroupIndex` and the software hit
   function name.
4. Scene storage preparation derives each primitive's `hitGroupIndex` from the
   resolved hit table. If the software shader reflects a `PrimitiveHitGroups`
   binding, preparation writes that auxiliary system-owned SSBO.
5. Software BVH preparation builds `SceneBvhNodes` from the same selected
   primitive set.
6. The compute shader traces the software BVH, returns hit attributes plus
   `hitGroupIndex`, then dispatches material hit handling with a simple branch
   or static function switch.
7. The readback/output path is identical to the future hardware path:
   `FrameGraphExecutor` returns `FrameGraphExecutionResult.outputs`.

Do not spend this slice optimizing the software hit dispatch. A plain branch is
acceptable because the long-term architecture assumes hardware RT will replace
the traversal and SBT dispatch path. The important constraint is that software
data layout preserves the same conceptual inputs hardware RT will need:
selected primitives, material contract source signatures, material hit shader
URIs, hit shader table indices, payload ABI, acceleration descriptor, and
output readback contract.

## Domain-Neutral RenderWorkBuildContext

`RenderWorkBuildContext` must stop being realtime-shaped, but that does not
mean realtime has a scene and offline does not. OfflineRT also renders a scene.
The shared scene owner should be `Scene`; `SceneResourceTable` remains the
resource lifetime center inside `Scene`.

The actual split is:

- scene/resource/material/texture/RenderFeature access is common and goes
  through `Scene::resources()`;
- scene participation filtering is common for scene-consuming passes, including
  both `scene-renderables` and OfflineRT `compute-dispatch`;
- OfflineRT uses `input.kind: compute-dispatch`, so it usually prepares scene
  storage descriptors from the same `Scene::resources()` instead of producing
  per-renderable draw inputs.

`RenderDomain` must not be used as "has scene" vs "does not have scene". It is
only an explicit domain label for diagnostics, validation defaults, and runtime
facts where graph contracts genuinely need to distinguish realtime/offline.

The target is an in-place evolution of the existing
`RenderWorkBuildContext`, not a new parallel `RenderWorkBuildFacts` public
entry point. `RealtimeOptions` should be renamed/generalized into common
options, the `OfflineRenderJob` variant should be removed, and both realtime
and offline callers should construct the same context class.

Target header shape:

```cpp
class RenderWorkBuildContext final {
public:
  struct RuntimeExtent final {
    StringID key;     // e.g. offline.output.resolution
    Vec3u extent{1u, 1u, 1u};
  };

  struct FeatureValue final {
    StringID key;     // e.g. feature.offlineRayTracer.samples
    RenderFeatureVolatileValue value;
  };

  struct PassPreparationFacts final {
    StringID pass;
    StringID pipelineVariantKey;
    ShaderProgramSet shaderProgram;
    IShaderSharedPtr shaderInfo;
    RenderState renderState;
    std::optional<RayProgramTable> rayProgramTable;
    DescriptorResourceList descriptorResources;
    std::vector<PipelineBuildDescExtra> pipelineBuildDescExtras;
  };

  struct Options final {
    std::vector<RuntimeExtent> runtimeExtents;
    std::vector<FeatureValue> featureValues;
    std::optional<RenderTarget> sceneResourceTarget;
    std::optional<CameraResource> cameraResource;
    std::optional<VisibilityLayerMask> visibleMask;
    std::vector<PassPreparationFacts> passPreparationFacts;
  };

  [[nodiscard]] static RenderWorkBuildContext realtimeEmpty();
  [[nodiscard]] static RenderWorkBuildContext realtime(const Scene &scene);
  [[nodiscard]] static RenderWorkBuildContext realtime(const Scene &scene,
                                                       Options options);
  [[nodiscard]] static RenderWorkBuildContext offline(const Scene &scene,
                                                      Options options);

  [[nodiscard]] RenderDomain domain() const;
  [[nodiscard]] const Scene &scene() const;
  [[nodiscard]] const SceneResourceTable &resourceTable() const;
  [[nodiscard]] std::optional<VisibilityLayerMask> visibleMask() const;
  [[nodiscard]] std::optional<Vec3u> findRuntimeExtent(StringID key) const;
  [[nodiscard]] std::optional<RenderFeatureVolatileValue>
  findFeatureValue(StringID key) const;
  [[nodiscard]] std::optional<std::reference_wrapper<const PassPreparationFacts>>
  findPassPreparationFacts(StringID pass) const;
};
```

If tests need an empty context, use a separate explicit test/empty constructor
that cannot satisfy graph dependencies requiring scene resources.

The concrete names can differ, but these rules are fixed:

- `collectPassFeatureSpecializationConstants()` must not depend on
  `hasRealtimeScene()`. It uses `context.resourceTable()`.
- Scene/resource access needed for descriptor construction goes through
  `context.scene().resources()` / `context.resourceTable()` for every domain.
- Realtime and offline both pass their chosen RenderPathGraph into the same
  graph/build/compile/prepare/execute flow.
- scene-consuming passes reuse the same scene selection filter, regardless of
  whether they later produce draw inputs or compute inputs.
- `compute-dispatch` does not produce draw inputs. It may still use scene
  selection to build compute descriptors such as scene buffers and BVH.
- Offline scene data transfer must use the same descriptor/upload plan model as
  realtime: `RenderInputDesc.bindingPlan`, `resourceDependencies`, and accepted
  desc filtering.

The old `hasRealtimeScene()`, `realtimeScene()`, `realtimeOptions()`,
`offline(OfflineRenderJob&)`, and `offlineJob()` can exist only as temporary
call-site migration helpers inside the implementation slice. They are not part
of the completed architecture.

## RenderWorkBuildContext Caller Responsibilities

Realtime callers build the common facts from the live scene:

- the live `Scene` is passed to `RenderWorkBuildContext::realtime(...)`;
- camera/light/environment descriptors are appended to per-pass
  `PassPreparationFacts.descriptorResources` before compiler preparation;
- material/fullscreen shader facts continue to use
  `PassPreparationFacts.shaderProgram`, `shaderInfo`, and `renderState`;
- passes using `input.kind: scene-renderables` traverse
  `context.scene().getRenderables()`.

Offline callers build the same common facts from an offline-loaded `Scene`:

- `OfflineSceneLoader` no longer returns an offline shader side channel;
- `OfflineSceneLoader` should return/populate a `Scene`, not only a
  `SceneResourceTable`;
- the loaded `Scene` is passed to `RenderWorkBuildContext::offline(...)`;
- offline storage descriptors are built from
  `scene.resources().buildUploadView()`, BVH, output/runtime facts, and standard-pbr
  source-material data;
- feature-declared derived volatile resources, including software
  `SceneBvhNodes`, are generated through their feature resource API/function
  implementations during pass preparation rather than by an offline-only side
  channel;
- selected material `hit.radiance.uri` fields are resolved against the
  RenderFeature `hitShaderTable` into a `RayProgramTable`; for this slice the
  table has one `standard-pbr` material hit shader table entry and
  software-compute lowering;
- OfflineRT volatile RenderFeature values such as samples, max bounce, seed, and
  compare mode are projected into
  `RenderWorkBuildContext::Options::featureValues` and
  materialized into a UBO descriptor for the `feature.offlineRayTracer`
  parameters;
- the OfflineRT shader is resolved from RenderPathGraph/RenderFeature/material
  source facts;
- all of those shader and descriptor facts are appended to
  `PassPreparationFacts` for `OfflinePrimaryRay`;
- `offline.output.resolution` is registered in
  `RenderWorkBuildContext::Options::runtimeExtents`.

After this step, realtime and offline call the same `RenderWorkCompiler` API.
The compiler consumes prepared facts; it does not know how the caller produced
them.

`RenderWorkBuildContext::Options` runtime vectors are neutral projections inside
the existing build context, not new owners for profile state. OfflineRT
projects CLI/profile fields into runtime facts. IBL bake projects parsed bake
RenderPathGraph parameters into runtime facts. Realtime may project
swapchain/target dimensions and editor/runtime toggles into the same structure
when graph preparation needs them. File paths, cache roots, and manifest output
paths stay outside this structure.

## Compute Dispatch Contract

Compute dispatch is declarative graph data, not an `OfflineRenderJob` side
channel. The implementation should add the missing compute/readback fields to
the existing `RenderPassNode` and `FramePass` path, then preserve them through
`CompiledFrameGraphPass`. It should not introduce a second graph/pass contract
beside those classes. The compute data is a small field on the pass model:

```cpp
struct RenderPassNode final {
  struct Compute final {
    std::string dispatchFrom;
    Vec3u localSize;
  };

  std::optional<Compute> compute;
};
```

For OfflineRT:

```yaml
compute:
  dispatchFrom: offline.output.resolution
  localSize: [8, 8, 1]
```

`RenderWorkCompiler` resolves `dispatchFrom` using domain-neutral output facts
and writes concrete group counts into the prepared compute input. The executor
receives concrete dispatch work; it does not know how to interpret
`offline.output.resolution`.

## Payload And Readback Semantics

`payloads` currently means "a pass produces a named artifact". That is close to
readback but mixes two responsibilities:

- generic executor output collection;
- bake-specific file/cache/activation behavior.

This design separates them:

```text
pass-level readbacks
  -> generic graph output collection
  -> FrameGraphExecutionResult.outputs

bake payload policy
  -> validates expected output names/formats
  -> writes manifests/files
  -> activates cached or freshly written resources
```

`payloads` should stop being a separate executor output system. Existing bake
payload meaning is preserved by migrating the output-characterization part into
readback contracts, and keeping bake file/cache/activation as post-processing.

## Generic Readback Contract

The generic readback contract should be pass-stage agnostic enough for buffers,
textures, cubemap layers, and later render attachments. `074-h` must cover both
OfflineRT and the current IBL bake payload shapes:

- OfflineRT output storage buffer: `offline.output`, interpreted as image2d
  RGBA float pixels by the offline image writer;
- IBL bake storage-buffer readback: `diffuse_sh9`, interpreted by bake
  post-processing as SH9 coefficients and optionally written as YAML;
- IBL bake texture/image readback: `brdf_lut`, interpreted by bake
  post-processing as a 2D LUT and encoded as KTX2;
- IBL bake cubemap/layer readback: `specular_prefilter`, interpreted by bake
  post-processing as cubemap mip/layer data and encoded as KTX2.

Metadata files such as `manifest.yaml`, and encoded asset payloads such as
`.ktx2`, are not executor payload kinds. They are post-processing products
created from self-describing readback bytes.

Implementation should hard-cut rename/evolve the existing
`RenderPathPayloadContract` surface rather than create a side-by-side output
contract. Today `RenderPathPayloadContract` already carries `name`, `target`,
`format`, and `kind` through `RenderPassNode`, `FramePass`, and
`CompiledFrameGraphPass`. `074-h` renames that surface to
`RenderPathReadbackContract` and adds the missing generic readback facts. The
logical shape is:

```cpp
enum class RenderPathOutputKind {
  Buffer,
  Image2D,
  Cubemap,
  Sh9,
  Texture2D,
};

struct RenderPathReadbackContract final {
  std::string name;       // output name in FrameGraphExecutionResult
  std::string target;     // graph target, e.g. offline.output
  std::string extentFrom; // runtime extent key, e.g. offline.output.resolution
  std::string binding;    // shader binding, e.g. OutputPixels
  std::string format;     // e.g. RGBA32Float, SH9RgbFloat, RG16Float
  RenderPathOutputKind kind;
  std::string mediaType;  // e.g. application/x-lxe-rgba32f
};

// Logical shape of RenderPassNode::Compute / FramePass::Compute /
// CompiledFrameGraphPass::Compute.
struct Compute final {
  std::string dispatchFrom;
  Vec3u localSize;
};
```

The names above are logical roles, not permission to add a parallel contract
tree. `RenderPassNode`, `FramePass`, and `CompiledFrameGraphPass` should carry
the evolved existing payload/readback contract as a pass-level `readbacks`
vector. Their `compute` field should carry only compute dispatch facts: extent
key and local size. Parser allowlisted fields must be stored; unsupported fields
fail.

The executor returns bytes and metadata only. Format-specific conversion belongs
to the caller:

```text
FrameGraphExecutionPayload(diffuse_sh9 bytes)
  -> IBL bake post-process writes diffuse_sh9.yaml

FrameGraphExecutionPayload(specular_prefilter cubemap bytes)
  -> IBL bake post-process writes specular_prefilter.ktx2

FrameGraphExecutionPayload(brdf_lut texture bytes)
  -> IBL bake post-process writes brdf_lut.ktx2

FrameGraphExecutionPayload(offline.output image2d bytes)
  -> offline image writer writes EXR/PNG
```

The only positive path for producing these payloads is:

```text
RenderPathReadbackContract
  -> RenderWorkCompiler resolves binding/extent into RenderInputDesc::Readback
  -> RenderInputDesc.readbacks
  -> FrameGraphExecutor reads bytes
  -> FrameGraphExecutionPayload
```

IBL bake does not get a separate payload-generation path. Synthetic executors
may remain in tests only if they are clearly test doubles for
`FrameGraphExecutor`; production cache writers must not infer readback shape
from manifests or hardcoded defaults.

## Execution Payload Format

`FrameGraphExecutionPayload` must be self-describing enough for offline image
writing and bake cache writing:

```cpp
struct FrameGraphExecutionPayload final {
  std::string name;
  std::string target;
  std::string mediaType;
  std::string format;
  std::string kind;
  u32 width = 0;
  u32 height = 0;
  u32 depthOrLayers = 1;
  std::vector<u8> bytes;
};
```

The executor must reject a readback if:

- the target is not in pass targets;
- the binding is not reflected by the shader;
- the binding has no live descriptor resource;
- the descriptor is metadata-only or placeholder;
- the output extent/format cannot be derived from graph/runtime facts.

## RenderWorkCompiler Preparation

`RenderWorkCompiler` is responsible for turning graph declarations plus
domain-neutral context facts into concrete prepared inputs:

- compute dispatch groups from the compute dispatch fields added to/evolved on
  `FramePass`;
- pass RenderFeature static specialization constants;
- volatile RenderFeature UBO bindings and descriptor resources;
- derived RenderFeature resource descriptors and PipelineBuildDesc extras already
  produced by the feature-resource precondition stage;
- resolved ray program table facts derived from graph pass shader, material hit
  shader URIs, and RenderFeature `hitShaderTable`, which enter shader
  variant/pipeline identity for software compute and future ray tracing
  pipeline/SBT build facts for hardware RT;
- standard-pbr material/source-material descriptor plans;
- graph resource dependencies into `RenderInputDesc.resourceDependencies`;
- readback declarations into resolved readback facts.

It must not read `OfflineRenderJob.output`, `OfflineRenderJob.offline`, or
`OfflineRenderJob.offlineShader`. It must not branch on `OfflinePrimaryRay` or
shader path substrings.

Concrete call-site replacements in `render_work_compiler.cpp`:

- `collectPassFeatureSpecializationConstants()` uses
  `context.resourceTable().findPassFeatureDataByFeatureName(...)`.
- `findPassPreparationFacts()` no longer checks `domain() == Realtime`.
- `collectSceneLevelResourcesForPass()` is removed as compiler policy.
  Realtime callers append scene-level descriptors to `PassPreparationFacts`
  before preparation; offline callers append offline storage descriptors the
  same way.
- `collectComputePreparationFacts()` only applies `PassPreparationFacts`,
  including descriptors, ray program facts, and PipelineBuildDesc extras. The
  branch that reads `context.offlineJob()`, `job.offlineShader`, and
  `buildOfflineSceneStorageResources(job)` is deleted.
- `buildInputs()` for `ComputeDispatch` reads the evolved `FramePass` compute
  dispatch extent key and local size, resolves the named extent with
  `context.findRuntimeExtent(...)`, and computes dispatch group counts with
  ceil-div.
- `RenderComputeInput::readbackResource` is replaced by resolved readback facts
  on `RenderInputDesc`, so multiple readbacks use the same output model.
- environment/surface lighting validation reads RenderFeature state through
  `context.resourceTable()`.
- object/material/geometry filtering is extracted into a shared scene selection
  helper used by both draw preparation and OfflineRT compute scene-data
  preparation.
- `buildSceneRenderableInputs()` uses selected scene objects to create
  `RenderDrawInput` entries.
- OfflineRT compute preparation uses selected scene objects to create scene
  storage descriptors and to run feature-derived resource producers such as
  `buildSceneAcceleration`; it does not create fake `RenderDrawInput` entries.

OfflineRT must reuse the scene/material/resource preparation below the input
kind boundary, but it must not fake raster draw work to do so. The reusable
parts are:

- scene/object/material/texture enumeration from `Scene` and
  `SceneResourceTable`;
- material v2 and standard-pbr validation;
- source-material buffer construction;
- texture array descriptor construction;
- shader/material-source variant resolution;
- ray program table resolution from selected material hit URIs and the
  RenderFeature hit shader table;
- descriptor resource dependencies and upload planning.

The non-reusable part is the prepared execution payload:

- raster `scene-renderables` produces one or more `RenderDrawInput` objects,
  graphics pipeline descriptions, vertex/index draw commands, and render-target
  writes;
- OfflineRT produces `RenderComputeInput`, a compute pipeline description,
  storage-buffer/texture descriptors for the whole scene, feature-produced
  acceleration descriptors, a resolved ray program table, optional pipeline
  PipelineBuildDesc extras, a scene frame parameter block, an output storage buffer, and
  readback descriptors.

If implementation discovers duplicated scene/material extraction code inside the
draw path, factor that code into a shared scene render-data preparation helper
used by both draw and compute preparation. Do not make OfflineRT pass through
`RenderDrawInput` or graphics pipeline validation only to recover scene data.

Readback metadata is not added to `FrameGraphExecutionRequest`. It belongs to
the existing `RenderInputDesc` because it is a resolved execution fact. Extend
`RenderInputDesc` with a readback vector. The element type is the new small
`RenderInputDesc::Readback` field element because no current type represents concrete
readback extent, binding, format, kind, and media facts after graph/runtime
resolution. The name intentionally matches `RenderInputDesc`: it is a child
description of the prepared input, not a second request or graph contract:

```cpp
struct RenderInputDesc final {
  struct Readback final {
    StringID name;
    StringID target;
    StringID binding;
    std::string mediaType;
    std::string format;
    std::string kind;
    u32 width = 0;
    u32 height = 0;
    u32 depthOrLayers = 1;
  };

  ...
  std::vector<Readback> readbacks;
};
```

`RenderWorkCompiler` resolves declarative readback fields carried by the
evolved `FramePass` against `RenderWorkBuildContext` and writes concrete
extents/bindings into `RenderInputDesc.readbacks`. The executor then needs no
scene, profile, or feature access to collect outputs.

This is still an extension of the existing execution input model, not a new
execution request type:

- authored data lives in `RenderPathReadbackContract` on the pass;
- prepared readback descriptions live in `RenderInputDesc.readbacks`;
- byte output lives in `FrameGraphExecutionPayload`;
- IBL bake consumes those payloads for cache/file/activation post-processing.

The old IBL bake payload flow had a similar authored-output description, but it
mixed executor readback with bake-specific file policy. `RenderInputDesc::Readback`
is the missing generic middle step: it carries only what the executor needs to
copy bytes, while IBL-specific interpretation remains outside the executor.

## Prepared Work Ownership And Invalidation

`PreparedFramePassWork[]` should live at the same owner/lifetime level as the
`CompiledFrameGraph` it was prepared for, but it should not become a member of
`CompiledFrameGraph` while `CompiledFrameGraph` means "execution plan".

The owner can be realtime renderer state, offline CLI/integrator state, or IBL
bake service/job state. In each case prefer adding members to that existing
owner over introducing a public prepared-graph wrapper type.

The invalidation rules are:

```text
FrameGraph or CompiledFrameGraph invalidated
  -> prepared pass work invalidated

SceneResourceTable/material/render-feature/output/readback facts changed
  -> prepared pass work invalidated
  -> CompiledFrameGraph may remain valid if graph topology did not change
```

This is not a resource lifetime problem. Meshes, materials, textures, shaders,
UBOs, storage buffers, and other GPU-visible payloads remain owned by
`SceneResourceTable` and referenced through `GpuResourceRef`,
`DescriptorResourceRef`, handles, or resource-table caches. Backend GPU object
lifetime remains separate and is governed by the existing resource-manager/cache
policy after resources are no longer referenced.

What is forbidden is returning a `FrameGraphExecutionRequest` whose span points
at a local temporary prepared-work vector that has already been destroyed.

## FrameGraphExecutionRequest Packaging

`FrameGraphExecutionRequest` remains a narrow execution boundary for already
prepared graph work. It is not a scene/profile/material owner and it does not
replace preparation context. Its current shape is close to the right boundary:

```cpp
struct FrameGraphExecutionRequest final {
  const FrameGraph *graph = nullptr;
  const CompiledFrameGraph *compiled = nullptr;
  std::span<const PreparedFramePassWork> preparedPasses;
};
```

The hard cut does not make this request an owning render job. Ownership belongs
to the caller's prepared-work owner. The request only borrows those prepared
facts for the duration of `FrameGraphExecutor::execute(...)`.

The ordinary request fields are enough for execution if preparation resolves
all runtime-dependent facts before the executor runs:

```text
parse RenderPathGraph asset
  -> owned FrameGraph
  -> owned CompiledFrameGraph
  -> RenderWorkBuildContext resolves scene/profile/feature/material facts
  -> RenderWorkCompiler fills PreparedFramePassWork[]
  -> RenderInputDesc contains pipeline, descriptor, dependency, and readback facts
  -> FrameGraphExecutionRequest borrows graph/compiled/prepared work
  -> FrameGraphExecutor executes borrowed prepared work
```

`lxe_offline_render` writes files after it receives
`FrameGraphExecutionResult.outputs`. Output paths do not belong in
`FrameGraphExecutionRequest`; they belong to the CLI/image writer layer.

The completed OfflineRT path must not introduce a replacement `OfflineRenderJob`
that owns scene/profile/shader state. It builds or reuses a prepared-work owner
and executes a borrowed request.

## Realtime And Offline Execution Policy

No separate public `OfflinePolicy`, `OfflineRenderProgram`, or replacement job
class is introduced. The existing architecture already has the right separation;
it needs explicit configuration instead of realtime-only branching.

Preparation policy lives in `RenderWorkBuildContext`:

- `RenderDomain::Realtime` or `RenderDomain::Offline` selects domain-specific
  validation where the graph contract genuinely differs.
- Shared facts are exposed through neutral accessors: `Scene`,
  `SceneResourceTable`, output/runtime facts, and pass preparation
  facts.
- `scene-renderables` traverses `Scene::getRenderables()`; this is an
  input-kind requirement, not a realtime-domain requirement.
- `compute-dispatch` is domain-neutral and must not require renderable scene
  traversal.
- pass-level RenderFeature data is found through the neutral resource table, not
  through `hasRealtimeScene()`.

Execution policy lives in the backend executor target/configuration:

- realtime/editor uses record-only execution into an externally managed command
  buffer;
- offline/bake uses immediate submit plus readback collection;
- the selected mode is explicit target/config data, not a graph name, pass name,
  shader URI, or path substring heuristic.

The preferred implementation is a small enum/config on the existing backend
execution target, for example:

```cpp
enum class VulkanFrameGraphExecutionMode {
  RecordOnly,
  ImmediateSubmitReadback,
};
```

This is not a new renderer policy hierarchy. Add a dedicated cpp class only if
Vulkan command-buffer allocation/submission/readback orchestration becomes large
enough to need a private helper owned by `VulkanFrameGraphExecutor`; that helper
must not become a public graph/job abstraction.

## FrameGraphExecutor Behavior

`VulkanFrameGraphExecutor` must support two execution modes without changing
the public executor interface:

- record-only mode for realtime/editor command buffers;
- immediate submit + readback mode for offline/bake work.

The mode is selected by explicit backend execution target/config data, not by
graph name, shader URI, or path substrings.

When collecting readbacks, the executor:

1. executes only accepted `RenderInputDesc` entries;
2. syncs `RenderInputDesc.resourceDependencies`;
3. records pipeline/descriptors/dispatch through the existing command path;
4. inserts the required GPU-to-host barrier;
5. maps the descriptor resource named by the readback binding;
6. copies bytes into `FrameGraphExecutionPayload`;
7. reports diagnostics with graph asset/pass/readback/binding names on failure.

The record/dispatch path must reuse the same prepared-input execution code that
the realtime path already validates: accepted `RenderInputDesc`, pipeline lookup
from `PipelineKey`, descriptor binding from `RenderInputBindingPlan`, resource
dependency sync, and `RenderInput` dispatch. OfflineRT differs only in graph
asset, domain facts, execution mode, and readback collection.

## Offline Image Writing

`lxe_offline_render` writes files after it receives
`FrameGraphExecutionResult.outputs`. It selects the `offline.output` payload,
checks the self-describing metadata (`mediaType`, `format`, `kind`, width,
height, layers), converts bytes to the requested image representation, and
writes EXR/PNG.

Output file paths do not enter `FrameGraphExecutionRequest`,
`CompiledFrameGraph`, `PreparedFramePassWork`, or `FrameGraphExecutor`.

## IBL Bake Parameter Config

Current IBL bake flow to preserve:

```text
IblBakeJobService::startBake(force)
  -> cacheStore->check(item)
  -> makeExecutionRequest(item)
  -> executor->execute(FrameGraphExecutionRequest)
  -> cacheStore->write(item, FrameGraphExecutionResult)
  -> activationDispatcher->dispatchActivation(...)
```

The existing class responsibilities remain:

- `IblBakeItem` identifies environment/material bake work and the bake render
  path URI;
- `IblBakeJobService` owns job lifecycle, phases, cancellation, logs, cache
  check/write ordering, executor invocation, and activation ordering;
- `IblBakeCacheStore` owns manifest/payload validation and file writing;
- `IblBakeActivationSink` / `IblBakeActivationDispatcher` own live scene
  activation after cache hit or successful write;
- `FrameGraphExecutor` owns prepared graph execution and returns
  `FrameGraphExecutionResult.outputs`.

`074-h` extends this flow by making `makeExecutionRequest(item)` build real
graph/prepared work from the selected bake render-path YAML and by making
`FrameGraphExecutor` produce real readback outputs. It does not replace the IBL
bake service, cache store, activation sink, or item model.

IBL bake should have an explicit YAML parameter block in the existing bake
RenderPathGraph assets instead of hardcoded values scattered across manifest
defaults, tests, cache writer code, and executor fixtures. That authored graph
block is the source of truth for bake execution sizes and bake runtime
constants; manifests remain artifact metadata written after execution.

Do not introduce a separate bake-profile asset in this slice. The current bake
render-path YAML files are already the authored bake program:

- `assets/render_paths/bake_environment_ibl.render-path.yaml`;
- `assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml`.

The target shape is a required top-level bake parameter block consumed by the
RenderPathGraph parser:

```yaml
schema: lxe.render-path-graph.v1
name: EnvironmentIblBake
renderPath: OfflineRT
bake:
  kind: environment-ibl
  environment:
    cubemap:
      resolution: 256
      faces: 6
    diffuse:
      kind: sh9
      order: 2
    specular:
      resolution: 256
      mips: auto
      faces: 6

passes:
  ...
```

```yaml
schema: lxe.render-path-graph.v1
name: StandardPbrBrdfLutBake
renderPath: OfflineRT
bake:
  kind: standard-pbr-brdf-lut
  brdfLut:
    resolution: 256

passes:
  ...
```

The ownership boundary is fixed:

- `IblBakeItem` selects what to bake and which bake render path to use.
- the selected bake RenderPathGraph YAML selects the execution dimensions, SH
  order, mip policy, face count, and other bake runtime constants through its
  required `bake` block.
- `RenderPathGraph` declares the pass graph, resources, formats, targets, and
  readback contracts.
- `RenderWorkBuildContext::Options::runtimeExtents` is the prepared projection
  consumed by graph preparation for dispatch/readback extents, for example:
  - `bake.environment.cubemap.resolution = 256x256x6`;
  - `bake.environment.specular_prefilter.resolution = 256x256x6`;
  - `bake.material.brdf_lut.resolution = 256x256x1`;
- bake scalar constants that are not extents, such as mip count and SH order,
  are either pass-specific preparation facts or feature volatile values when
  they correspond to a reflected RenderFeature UBO member:
  - `bake.environment.specular_prefilter.mips = 9`;
  - `bake.environment.diffuse.order = 2`.
- Manifest files record what was produced and provide cache validation. They do
  not define execution dimensions for a new bake.

Each IBL compute pass must declare its dispatch through the pass `compute` field
instead of renderer code:

```yaml
compute:
  dispatchFrom: bake.environment.specular_prefilter.resolution
  localSize: [8, 8, 1]
```

`IblBakeJobService::makeExecutionRequest(item)` projects the selected bake
graph's `bake` block into `RuntimeExtent` entries before calling
`RenderWorkCompiler`. `RenderWorkCompiler` resolves `dispatchFrom` exactly the
same way for IBL bake compute passes and OfflineRT compute passes.

The current hardcoded defaults such as specular resolution `256`, BRDF LUT
resolution `256`, and derived mip counts should move into the bake render-path
YAML `bake` block. Tests and cache writers should derive expected manifest
fields from the parsed graph bake parameters, instead of duplicating those
values independently.

`IblBakeJobService::makeExecutionRequest(item)` becomes the place where bake
item plus parsed bake graph parameters are translated into prepared graph work:

```text
IblBakeItem + RenderPathGraph.bake
  -> RenderPathGraph load
  -> FrameGraph compile
  -> RenderWorkBuildContext::Options::runtimeExtents / pass preparation facts
  -> pass preparation facts and descriptors
  -> borrowed FrameGraphExecutionRequest
```

This is an extension of the current IBL service flow, not a new bake execution
service. `IblBakeJobService` still owns job phases, cache check, cache write,
activation, logs, and cancellation. Its existing `makeExecutionRequest`
callback remains the hook that turns an `IblBakeItem` into a prepared
`FrameGraphExecutionRequest`; the change is that the callback must build real
graph/prepared work instead of returning placeholders.

The default `makeExecutionRequest` returning an empty request is no longer a
positive bake path once Vulkan readback is implemented. Empty requests may
survive only in negative tests that prove missing prepared graph work is
rejected.

Because `FrameGraphExecutionRequest` only borrows `FrameGraph`,
`CompiledFrameGraph`, and `PreparedFramePassWork[]`, IBL preparation must also
establish an owner for those objects. The preferred implementation is private
per-job prepared-work state inside `IblBakeJobService` or an existing caller
owner captured by the request factory. Do not return a request whose
`preparedPasses` span points at temporaries allocated inside
`makeExecutionRequest(item)`. If the public callback shape must change, extend
the existing IBL service/config surface narrowly; do not introduce a replacement
bake job abstraction.

## IBL Bake Payload Migration

Existing bake `payloads` are not preserved as a second output system. They are
migrated to readback contracts plus bake policy:

```yaml
readbacks:
  - name: diffuse_sh9
    target: bake.environment.diffuse_sh9
    binding: BakeDiffuseSh9
    format: SH9RgbFloat
    kind: sh9
    mediaType: application/x-lxe-sh9-rgb-float
  - name: specular_prefilter
    target: bake.environment.specular_prefilter
    binding: BakeSpecularPrefilter
    format: RGBA16Float
    kind: cubemap
    mediaType: application/x-lxe-cubemap-rgba16f
  - name: brdf_lut
    target: bake.material.brdf_lut
    binding: BakeBrdfLut
    format: RG16Float
    kind: texture2d
    mediaType: application/x-lxe-texture2d-rg16f
```

For raster bake passes, the same `readbacks` concept can later reference an
attachment target instead of a compute storage binding. If a pass output is only
an intermediate graph resource and does not need CPU/export/cache visibility, it
does not declare a readback.

IBL bake-specific code remains responsible for:

- validating the required output names for environment/material bake items;
- converting readback bytes into bake files such as `manifest.yaml`, SH9 YAML,
  and `.ktx2`;
- copying effective bake parameter config values into manifest metadata;
- cache hit/miss policy;
- activation into `SceneResourceTable`.

IBL cache writing consumes `FrameGraphExecutionPayload` objects produced from
`RenderInputDesc::Readback`. It must not construct payload dimensions, media type,
or format from manifest defaults, cache fixtures, or a bake-only renderer path.
The manifest records the produced outputs after the executor returns them.

IBL bake-specific code must not own Vulkan dispatch, barriers, descriptor
binding, or GPU readback.

This migration does not require IBL bake to adopt object/material/geometry scene
filters yet. Bake filter unification is a follow-up refactor after OfflineRT is
hard-cut onto FrameGraphExecutor.

## IBL Bake Post-Processing

IBL bake uses the same executor readback output path as OfflineRT. Its
additional responsibilities remain outside the executor:

- validate output names/formats for bake items;
- convert self-describing readback payloads into bake-specific files:
  `manifest.yaml`, SH9 YAML, `specular_prefilter.ktx2`, `brdf_lut.ktx2`, and
  future material bake files;
- write manifest metadata from the effective bake parameter config and parsed
  readback payload metadata;
- apply cache hit/miss policy;
- activate baked resources into `SceneResourceTable`.

No IBL bake path may own Vulkan dispatch, barriers, descriptor binding, or GPU
readback after this hard cut.

The executor must not write `manifest.yaml`, choose cache keys, encode KTX2, or
activate resources. Those actions are bake policy layered after
`FrameGraphExecutionResult.outputs`.

## Legacy Closure

Implementation tasks must delete or turn into negative audits the positive
production paths for:

- `OfflineRenderJob`;
- `OfflineRenderJob::offlineShader`;
- `createOfflineRenderFrameGraph`;
- `OfflineRenderGraphExecutor`;
- `techniques/OfflineRT/offline_pbr_direct_ray`;
- hardcoded offline dispatch/readback in `RenderWorkCompiler::buildInputs()`;
- realtime-only pass feature collection through `hasRealtimeScene()`.

Old tokens may remain only in named negative tests/docs that prove rejection.

This closure targets OfflineRT-specific internals, even when those files live
under `src/core/offline/`. It must not delete or bypass shared core
architecture such as `FrameGraph`, `RenderPathGraph`, `RenderWorkCompiler`,
`RenderWorkBuildContext`, `PreparedFramePassWork`, `RenderInputDesc`,
`SceneResourceTable`, Material v2, or RenderFeature. Those shared core types are
the extension points for this requirement.

The current deletion/replacement map is:

| Current path | Hard-cut outcome |
|---|---|
| `src/core/offline/offline_render_job.hpp` | Delete execution job concept. Move reusable profile/readback data into neutral profile/output facts or image writer structs. |
| `src/core/offline/offline_render_validation.*` | Delete job validation. Replace with graph/schema/context validation. |
| `src/core/offline/offline_render_work_graph.*` | Delete file-local graph factory. Replace with `assets/render_paths/offline_ray_tracer.render-path.yaml`. |
| `src/backend/vulkan/offline/offline_render_graph_executor.*` | Delete offline executor. Replace with `VulkanFrameGraphExecutor` immediate-submit/readback mode. |
| `src/backend/vulkan/offline/offline_integrator.*` | Delete public integrator selection as positive path unless all it does is private CLI orchestration with no render logic. |
| `src/backend/vulkan/offline/software_compute_offline_integrator.*` | Delete software/offline integrator path. Its Vulkan work migrates into `VulkanFrameGraphExecutor`. |
| `src/backend/vulkan/offline/vulkan_offline_renderer.*` | Delete or shrink to a CLI-facing wrapper that only builds prepared graph work and calls `FrameGraphExecutor`; it must not own a second renderer flow. |
| `src/backend/vulkan/offline/offline_compute_shader.*` | Delete shader-provider side channel; shader is resolved from RenderPathGraph/RenderFeature/material source. |
| `src/infra/offline/offline_scene_loader.*` | Remove `OfflineShaderProvider` and `offlineShader` output. Keep scene loading/resource-table population if still useful. |
| `src/infra/offline/offline_image_writer.*` | Keep only file/image writing from `FrameGraphExecutionPayload`; remove dependency on `OfflineRenderJob`. |
| `src/tools/lxe_offline_render/main.cpp` | Rewire to load graph asset, prepare graph work, execute `FrameGraphExecutor`, and write result payloads. |
| `assets/shaders/glsl/techniques/OfflineRT/offline_pbr_direct_ray.comp` | Move/replace with `assets/shaders/glsl/render_paths/OfflineRT/offline_pbr_direct_ray.comp`; old URI rejected. |

## Validation And Tests

Required negative evidence:

- a graph using `techniques/OfflineRT/offline_pbr_direct_ray` is rejected;
- `OfflineRenderJob`, `offlineShader`, `createOfflineRenderFrameGraph`, and
  `OfflineRenderGraphExecutor` have no positive production path;
- compute pass without pass-level `readbacks` for `offline.output` is rejected for
  OfflineRT;
- readback binding missing from shader reflection is rejected;
- readback binding resolved only to placeholder/metadata resource is rejected;
- OfflineRT graph with non-`standard-pbr` material is rejected in the Helmet
  positive smoke path;
- pass-level `feature.offlineRayTracer` missing or ABI-mismatched is rejected;
- `rayPrograms` authored on RenderFeature is rejected; the supported table field
  is `hitShaderTable`;
- `PrimitiveHitGroups` authored on RenderFeature or RenderPathGraph YAML is
  rejected; it is derived prepared data, not asset schema;
- hit shader table fields accepted by the RenderFeature parser but not stored
  are impossible; unsupported table fields are rejected with diagnostics;
- material `hit` fields accepted by the MaterialResource parser but not stored
  are impossible; unsupported hit payload keys are rejected with diagnostics;
- selected `standard-pbr` material without `hit.radiance.uri`, or whose URI is
  absent from `feature.offlineRayTracer.hitShaderTable`, is rejected;
- a hit shader table entry whose `function` does not appear in the referenced
  hit shader source fails the source-text audit;
- graph/profile/backend requests for hardware RT lowering are rejected in
  `074-h` unless ray tracing pipeline/SBT backend support is implemented.

Required positive evidence:

- `assets/render_paths/offline_ray_tracer.render-path.yaml` parses and builds a
  `FrameGraph`;
- `GraphResourceRegistry::makeDefault()` accepts OfflineRT resources including
  `offline.output` and `feature.offlineRayTracer`;
- `effects/offline_ray_tracer.render-feature.yaml` parses acceleration/resource
  declarations plus `hitShaderTable`, and rejects misplaced `rayPrograms`;
- existing RenderFeature assets such as `forward_pass`, `environmentLighting`,
  `surfaceLighting`, `toneMapping`, `bloom`, and `skybox` still parse through
  the same extended `RenderFeature` class, with no legacy parser branch or
  per-feature DTO;
- RenderFeature schema validation rejects storage-buffer/descriptor resources
  under `parameters`, rejects scalar controls or ordinary scene/material system
  bindings under `resources`, and accepts `SceneBvhNodes` as a derived
  storage-buffer output;
- `standard-pbr` material parses `hit.radiance.uri`, the URI matches the
  RenderFeature table entry, and preparation produces a resolved one-hit-group
  `RayProgramTable`;
- selected primitives derive their hit group from the resolved
  `hitShaderTable`; if `PrimitiveHitGroups` is reflected by the software
  shader, preparation emits it as a system-owned auxiliary descriptor rather
  than reading it from YAML;
- source-text audit verifies the `standard_pbr_radiance.glsl` file contains
  `lxHitStandardPbrRadiance`;
- `RenderWorkCompiler` builds a compute input from the evolved `FramePass`
  compute dispatch/readback fields, not from an offline side channel;
- `FrameGraphExecutor` returns `FrameGraphExecutionResult.outputs` containing
  `offline.output`;
- `lxe_offline_render` writes EXR/PNG from that output;
- IBL bake receives `diffuse_sh9`, `specular_prefilter`, and `brdf_lut` through
  `FrameGraphExecutionResult.outputs`, then writes manifest/SH/KTX2 files in
  bake post-processing rather than in the executor;
- IBL bake execution dimensions and defaults come from an explicit bake
  parameter config projected into
  `RenderWorkBuildContext::Options::runtimeExtents` and pass preparation facts,
  not from duplicated manifest/test/cache hardcoding;
- Helmet standard-pbr offline smoke renders non-empty finite pixels.

## Task Slicing Guidance

Suggested task order follows the OfflineRT rendering flow:

1. make the offline CLI/loader produce a unified `Scene` and remove the
   `offlineShader` side channel;
2. add OfflineRT RenderPathGraph, RenderFeature, graph resources, parser/schema
   support, including the strict `parameters` versus `resources` split, compute
   contract, and readback contract;
3. extend Material v2 schema with `hit.radiance.uri`, extend RenderFeature with
   `hitShaderTable`, and build the software-compute one-hit-group
   `standard-pbr` `RayProgramTable` from selected material hit URIs plus the
   feature table;
4. add the hit shader table source-text audit that verifies listed function
   names appear in referenced hit shader sources;
5. document and enforce the hardware RT target lowering boundaries, while
   rejecting hardware RT graph/profile/backend requests until pipeline/SBT
   support exists;
6. align the OfflineRT shader URI and descriptor ABI with the standard-pbr
   material source contract;
7. extract the existing `RenderPassInputContract` / `RenderDrawInput` scene
   participation selection logic into a shared helper and allow
   scene-consuming `compute-dispatch` passes to use object/material/geometry
   filters;
8. evolve the existing `RenderWorkBuildContext` in place so it uses common
   `Scene`, common options/runtime facts, and pass preparation facts;
9. implement the feature resource API/function layer for derived volatile
   resources, with `buildSceneAcceleration` using the software-BVH
   implementation to produce `SceneBvhNodes` and a clean backend boundary for
   future hardware RT;
10. move OfflineRT scene storage descriptor/output-buffer preparation,
   including any reflected `PrimitiveHitGroups` system-owned binding, and
   feature-derived acceleration descriptors into caller-side pass preparation
   facts;
11. update `RenderWorkCompiler` to build compute inputs from graph compute
   contracts and resolved runtime extents;
12. add resolved readbacks to `RenderInputDesc` and self-describing
   `FrameGraphExecutionPayload`;
13. add IBL bake parameter config to existing bake RenderPathGraph YAML files
   and project it into runtime facts for bake graph preparation;
14. add explicit realtime/offline execution mode configuration;
15. implement `VulkanFrameGraphExecutor` immediate submit/readback using the
    same prepared-input recording path as realtime;
16. update offline image writing to consume `FrameGraphExecutionResult.outputs`;
17. wire prepared graph ownership and borrowed `FrameGraphExecutionRequest` for
    offline CLI and bake;
18. migrate IBL bake payloads to generic readback outputs, including SH9,
    cubemap, and 2D LUT payloads, then keep manifest/SH/KTX2/cache activation
    in bake post-processing without forcing bake scene-filter unification;
19. delete old OfflineRT executor/job/integrator/shader-provider paths;
20. add negative audits and Helmet standard-pbr smoke tests.

## Decided Type Plan

Small-field folding rule:

- Prefer extending existing core classes and their option/field structs.
- If a type is only an element of one owning class, define it under that owner
  and use a short local name, for example `RenderInputDesc::Readback` or
  `RenderWorkBuildContext::FeatureValue`.
- Keep standalone names only for data that crosses several owners, has an
  independent lifetime/registry, or carries large architecture meaning such as
  `RayProgramTable` or `RenderFeatureDerivedResourceProducer`.
- Do not create parallel public DTOs for OfflineRT, IBL bake, readback,
  compute, or runtime facts.

Core extensions, not parallel systems:

- `RenderWorkBuildContext`: evolve in place. Rename/generalize
  `RealtimeOptions` to `Options`, remove the `OfflineRenderJob` variant, add
  `offline(const Scene&, Options)`, and expose `scene()`,
  `resourceTable()`, `findRuntimeExtent(...)`,
  `findFeatureValue(...)`, and `findPassPreparationFacts(...)`.
- `RenderWorkBuildContext::Options`: add nested runtime facts:
  `std::vector<RuntimeExtent> runtimeExtents` and
  `std::vector<FeatureValue> featureValues`.
  These are borrowed preparation inputs, not profile ownership.
- `RenderFeatureVolatileValue`: add a small core value type for existing
  `RenderFeatureParameter::volatileRuntime` execution values. Current
  RenderFeature volatile schema and ABI validation exist, but there is no
  domain-neutral runtime value carrier from `RenderWorkBuildContext::Options`
  into the reflected UBO member. All existing and future `volatile: true`
  RenderFeature parameters use this path; do not add feature-specific volatile
  value side channels:

```cpp
enum class RenderFeatureVolatileValueKind {
  Bool,
  U32,
  I32,
  F32,
  Vec4,
  EnumToken,
};

struct RenderFeatureVolatileValue final {
  RenderFeatureVolatileValueKind kind = RenderFeatureVolatileValueKind::U32;
  bool boolValue = false;
  u32 u32Value = 0;
  i32 i32Value = 0;
  float f32Value = 0.0f;
  Vec4f vec4Value{0.0f, 0.0f, 0.0f, 0.0f};
  StringID enumToken;
};
```

- `RenderPathPayloadContract`: hard-cut rename/evolve to
  `RenderPathReadbackContract`. Existing bake `payloads` YAML fields migrate
  to pass-level `readbacks`; the parser rejects old positive `payloads` instead
  of preserving it as an alias.
  `RenderPassNode`, `FramePass`, and `CompiledFrameGraphPass` carry
  `std::vector<RenderPathReadbackContract> readbacks`.
- `RenderPassNode` / `FramePass` / `CompiledFrameGraphPass`: add a nested
  `Compute` field, `std::optional<Compute> compute`, containing only
  `dispatchFrom` and `localSize`. Readbacks stay pass-level so raster and
  compute outputs use one output contract. OfflineRT and all IBL bake compute
  passes use this same field; no bake renderer or offline renderer may compute
  dispatch dimensions from private defaults.
- `RenderDrawInput` / scene input preparation: extract a small neutral
  `RenderSceneParticipant` record from the existing common object, mesh,
  material, primitive, render-type, material-signature, and resource-reference
  fields currently assembled while building draw inputs. Draw preparation builds
  `RenderDrawInput` from those participants plus draw commands; OfflineRT
  compute preparation consumes the same participants for scene storage and
  acceleration data. This is a refactor of existing draw selection data, not an
  OfflineRT-only selection DTO.
- `RenderInputDesc`: add nested `Readback` and
  `std::vector<Readback> readbacks` for concrete binding, extent, format, kind,
  and media type after preparation.
- `FrameGraphExecutionPayload`: extend the existing result payload with
  `target`, `format`, `kind`, and dimensions. Do not add a separate output
  result type.
- `RenderWorkBuildContext::PassPreparationFacts`: extend the existing nested
  struct with `std::optional<RayProgramTable> rayProgramTable` and
  `std::vector<PipelineBuildDescExtra> pipelineBuildDescExtras`.
- `RenderFeature`: extend the existing core class with
  `std::unordered_map<std::string, RenderFeatureResourceRequirement> resources`
  and `std::optional<RenderFeatureHitShaderTable> hitShaderTable`. All
  `.render-feature.yaml` assets parse into this one model; no OfflineRT-only
  RenderFeature class, parser DTO, or registry is introduced.
- `PreparedFramePassWork`, `FrameGraphExecutionRequest`, `FrameGraphExecutor`,
  `IblBakeJobService`, `IblBakeItem`, `IblBakeCacheStore`, and
  `IblBakeActivationSink` remain the workflow boundaries. Extend their current
  fields/callers only where needed; do not add replacement offline/bake job
  wrappers.

Standalone or heavy new types that still need named design:

- `RayProgramTable` and `RayHitGroupProgram`: needed because no current core
  type represents raygen/miss/material-hit grouping or future SBT-compatible
  hit group metadata.
- Material hit URI data: extend Material v2 with a small `hit` field/map,
  currently `hit.radiance.uri`, rather than a separate material-hit registry.
- Acceleration producer facts:

```cpp
enum class RenderFeatureResourceApi { SceneAcceleration };
enum class RenderFeatureResourceImplementation { SoftwareBvh, HardwareRayTracing };

struct RenderFeatureDerivedResourceRequest final {
  RenderFeatureResourceApi api;
  RenderFeatureResourceImplementation implementation;
  StringID pass;
  StringID feature;
  StringID binding;
  std::span<const RenderSceneParticipant> selectedScene;
  std::reference_wrapper<const Scene> scene;
  std::reference_wrapper<const SceneResourceTable> resources;
};

struct RenderFeatureDerivedResourceResult final {
  DescriptorResourceList descriptors;
  std::vector<GpuResourceRef> resourceDependencies;
  std::vector<PipelineBuildDescExtra> pipelineBuildDescExtras;
};

class RenderFeatureDerivedResourceProducer {
public:
  virtual ~RenderFeatureDerivedResourceProducer() = default;

  [[nodiscard]] virtual RenderFeatureDerivedResourceResult
  produce(const RenderFeatureDerivedResourceRequest &request) = 0;
};

struct PipelineBuildDescExtra final {
  StringID kind;
  StringID key;
  std::vector<u8> data;
};
```

`RenderFeatureDerivedResourceProducer` is reached through an internal closed
registry keyed by `(RenderFeatureResourceApi, RenderFeatureResourceImplementation)`.
The registry is owned by core preparation code and/or scene-resource-table
integration. It must not live in the old OfflineRT renderer, and YAML must not
reference arbitrary producer functions.
