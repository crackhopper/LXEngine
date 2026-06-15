# REQ-071 Material, Resource, Rendering Pipeline Master Design

## Goal

Implement the full `REQ-071` family as one continuous migration:

```text
source scene / package
  -> SceneResourceTable
  -> technique validation + FrameGraph
  -> GPUResourceTable upload/preload
  -> bindless + indirect realtime/offline execution
  -> helmet/BMW direct-lighting validation
```

The work covers:

- `071-a`: Material v2 PBRT surface contract.
- `071-b`: technique, pass, RenderEffect, and FrameGraph contract.
- `071-c`: SceneResourceTable parser split and resource abstraction.
- `071-d`: GPUResourceTable, pipeline cache, upload tasks, bindless, and indirect draw.
- `071-e`: single-file scene package fast load and MaterialTemplate grouping.
- `071-f`: helmet/BMW direct-lighting validation and bridge audit.

This is not a compatibility migration. Old material contracts, old runtime PBR
parameter truth, legacy per-material descriptor submission, and offline-only
scene packing are removed or isolated as explicit debug-only paths before the
line is considered complete.

## Non-Goals

This design does not validate shadows, IBL quality, transparent/glass sorting,
Fourier BSDF evaluation accuracy, full spectral rendering, OIT, mesh batching,
GPU culling, GPU-generated indirect commands, package compression, mmap,
cross-platform package compatibility, or Vulkan hardware ray tracing. Those
can become later requirements, but they must extend the new contracts rather
than revive removed compatibility paths.

## Existing Context

The repository already contains useful pieces:

- `SceneResourceTable` owns geometry, mesh, material, texture, light, skeleton,
  object, and camera handles.
- `SceneResourceTableUploadView` exports compact GPU-facing records.
- `RenderWorkQueue`, `RenderWorkItem`, and `RenderUploadPlan` provide a shared
  realtime/offline work vocabulary.
- `MaterialTemplate` currently still owns pass definitions and shader-facing
  material bindings.
- `MaterialInstance` currently stores parameter buffers and texture bindings
  keyed by shader reflection binding names.
- `VulkanResourceManager` and pipeline cache own backend upload and pipeline
  objects.
- Existing material loader and editor scene runtime still contain old PBR
  parameters and material-name/path-driven behavior.

The migration should reuse the existing resource table and render-work
concepts where they already match the target model, but replace their current
legacy material and backend coupling points.

## Architecture

The authoritative CPU scene state is `SceneResourceTable`. It owns canonical
URI identity, typed handles, resource lifecycle, dependency graph, parser
diagnostics, typed arrays, package snapshot state, and package-ready hashes.

Parsers are format adapters. They parse one resource type, call the table for
URI resolution and dependency loading, and return typed handles. They do not
own resources or backend objects.

The authoritative material authoring state is the Material v2 PBRT envelope.
`MaterialTemplate` describes one BSDF type schema and reflected layout rules.
`MaterialTechniqueSet` describes techniques and passes. `MaterialInstance`
stores concrete envelope values, typed resource handles, technique-set
reference, dirty/version state, and override state.

The authoritative backend state is `IGpuResourceTable`. It maps
`SceneResourceTable` snapshots and resource handles to GPU buffers, images,
samplers, global bindless slots, material storage, object/draw records,
indirect draw buffers, and pipelines. Core/RHI types expose only platform-
independent handles and cache metadata; Vulkan types remain backend details.

FrameGraph is built from explicit technique/effect pass declarations and a
standard source/target registry. Logical targets use SSA-style versioned names
for dependency construction. Backend aliasing can map multiple logical targets
to one physical resource after graph compile, but dependency logic never relies
on physical attachment overwrite order.

## Milestones

### A. Material v2 PBRT Surface Contract

Material v2 makes PBRT surface material semantics the only runtime material
parameter contract for migrated assets.

Required outcomes:

- `.material` v2 uses envelope parameters for `matte`, `glass`, `uber`,
  `metal`, `substrate`, `fourier`, and `mix`.
- `MaterialTemplate` no longer owns technique/pass/shader/render-state data.
  It owns BSDF type schema, validation, template id, debug name, and layout
  mapping rules.
- `MaterialInstance` owns concrete envelope values, typed resource handles,
  technique-set reference, override state, and dirty/version state.
- `MaterialResourceParser` parses `.material` v2, validates required
  parameters, resolves texture/spectrum/bsdf table/material-ref dependencies
  through the minimum resource table API, and fails fast on missing contracts.
- PBRT converter output explicitly writes defaults from configuration. Runtime
  parser does not fill defaults.
- Old PBR parameter files and loader paths are removed from migrated runtime
  material truth.
- Helmet material smoke uses Material v2 and renders non-black through the
  current transitional render path if needed.

The transitional path may exist during A only to keep smoke tests alive. It
must consume Material v2 data and must be recorded for deletion in D.

### B. Technique, Effect, and FrameGraph Contract

Technique and render-flow data moves out of `MaterialTemplate`.

Required outcomes:

- `.material` declares `defaultTechnique` and explicit `techniques`.
- BMW and helmet converted materials declare `Forward`, `Deferred`, and
  `OfflineRT`.
- Every pass declares shader URI, stage, dispatch, sources, targets, write
  mode when needed, and full render state. There is no code inference from pass
  names, material names, or shader file names.
- `RenderEffect` assets describe camera `pre` and `post` effects with the same
  pass field contract as material passes.
- FrameGraph builds in three phases: camera pre effects, material passes,
  camera post effects.
- Source/target registry rejects unknown names and missing producers.
- Fixed system ABI moves toward SSBO arrays with C++ mirror structs and GLSL
  common files. Material/effect-owned bindings remain reflection driven.
- Active technique validation distinguishes missing technique warnings from
  invalid technique fatal errors.
- Helmet Forward smoke still renders non-black through Material v2,
  technique validation, and FrameGraph.

Any pass/effect bridge kept for smoke must be documented and scheduled for D
cleanup.

### C. SceneResourceTable Resource Abstraction

The resource table becomes the CPU-side owner of all scene resources.

Required outcomes:

- Resource identity is canonical URI plus resource type.
- Handles include type, index, and generation.
- Resource metadata records state, version/content generation, dependencies,
  diagnostics, content hash, and canonical URI.
- Parsers are split for material, mesh, texture, camera, light, render effect,
  spectrum, and BSDF table resources.
- Scene/node material override creates or reuses a concrete
  `MaterialInstance` resource; it never mutates source material files,
  templates, techniques, shaders, passes, or render state.
- Dependency graph can diagnose missing dependencies, drive dirty propagation,
  and export package resource lists.
- Geometry storage is prepared for bindless and indirect rendering:
  position/index are stable streams, normal/uv/tangent/color/skinning are
  attribute streams, and missing attributes fail validation when required by a
  shader.
- Upload view exports stable typed arrays and handle-to-typed-index mappings
  for textures, samplers, spectrum, bsdf tables, geometry streams, meshes,
  material instances, objects, cameras, lights, and effects.
- Helmet smoke loads through split parsers and the resource table.

C may keep a transitional per-object draw path, but it must read the new table
data and must be removed from default rendering in D.

### D. GPUResourceTable, Bindless, and Indirect Draw

D is the point where transitional default rendering paths are removed.

Required outcomes:

- Core/RHI defines `IGpuResourceTable` and platform-independent GPU handles.
- Vulkan implements the table for buffers, images, samplers, bindless tables,
  pipeline cache, cache blobs, material storage, object/draw records, and
  indirect draw buffers.
- Pipeline cache has typed `find` and `getOrCreate`, observable misses,
  preload without warning noise, and import/export metadata.
- Upload/preload task model reports progress, dependencies, diagnostics, and
  task phases.
- Editor scene load, package restore, and technique switch enter loading state,
  show task progress/logs, and do not render half-initialized scenes.
- Bindless tables are global by resource kind. GPU slots are allocated from
  CPU resource handles and typed arrays, not from material names or paths.
- Material parameter storage is grouped by `MaterialTemplate`/BSDF type and
  technique/pass reflected layout.
- Render work build groups by technique, pass, pipeline, and template; CPU can
  generate indirect draw buffers.
- Vulkan default execution uses indirect draw for supported raster passes.
- Missing bindless/descriptor-indexing/indirect support is an unsupported
  validation error, not a fallback to legacy descriptors.
- Helmet realtime and offline direct smoke use GPUResourceTable upload,
  bindless tables, and indirect draw.
- Default rendering no longer calls legacy material loader, old PBR parameter
  truth, legacy per-material descriptor update, non-bindless draw submission,
  or A-C transitional bridges.

Debug-only legacy paths may remain only behind explicit flags, default off,
and covered by tests that prove default smoke and validation do not use them.

### E. Scene Package Fast Load

Package serialization persists the CPU resource table state and optional
backend cache metadata.

Required outcomes:

- `.lxpkg` is a single binary container with header, section table, package
  index, string/URI table, resource metadata, dependency graph, typed resource
  sections, MaterialTemplate grouping, material instances by template, objects,
  camera/light/effect state, technique/pass metadata, and optional backend
  cache blobs.
- Large sections use chunk tables. Loader can read independent sections/chunks
  in parallel and restore typed storage as chunks complete.
- Loader does not read the whole package into memory as the normal path.
- Package resolver resolves package-internal resources before asset-root files
  and does not unpack resources to temporary files for normal restore.
- Package restore reconstructs `SceneResourceTable` persisted state without
  reparsing source YAML/material/mesh files.
- Derived runtime state is recomputed after restore and excluded from the CPU
  root hash: upload view, FrameGraph compile result, pipeline desc collection,
  dirty flags, runtime generation counters, GPU handles, bindless slots, and
  backend staging state.
- Merkle hash covers persisted canonical state, persisted typed arrays,
  dependency graph, MaterialTemplate grouping, and stable mappings that are
  part of package restore.
- Source parse and package restore produce matching `SceneResourceTable` root
  hashes and matching offline direct deterministic output for the same test
  scene.
- Backend cache compatibility mismatch warns and rebuilds pipelines.
- Helmet source and package paths both render non-black through the same
  resource/render chain.

Package correctness blockers may be recorded during development, but E is not
complete until package round trip, hash, and source/package render equivalence
are verified.

### F. Helmet and BMW Direct-Lighting Validation

F validates the completed chain. It does not introduce new architecture.

Required outcomes:

- Helmet and BMW validation scenes use Material v2, explicit techniques,
  SceneResourceTable resources, package input, GPUResourceTable upload,
  bindless tables, and indirect draw.
- Validation profile fixes source mode, camera, low-resolution direct profile,
  random seed, tone mapping, sample count, debug dump flags, and disables
  shadows, IBL, and transparent/glass behavior.
- Every actual helmet/BMW `MaterialInstance` gets a 64x64 material sphere
  direct validation case for Forward, Deferred, and OfflineRT.
- Full model validation renders 128x128 helmet/BMW direct outputs through
  source and package paths.
- Diagnostics include color, materialId, object/draw id, normal,
  depth/visibility, direct input hash, and template-specific channels.
- `lxe_compare_exr` or its successor reports full-image, per-material,
  per-object/draw metrics, edge/coverage classification, input mismatch,
  BRDF mismatch, unsupported/disabled classification, and top suspicious
  samples.
- Realtime headless CLI and editor export use the same core render path and
  produce matching outputs for the same scene, technique, camera, and profile.
- GBuffer/FrameGraph debug dumps include logical target id/version metadata and
  every consumed target has a producer.
- Bridge audit report lists every temporary bridge from A-E and proves it is
  removed, final-contract rewritten, or converted into a later REQ. Default
  validation must not use old material, legacy descriptor, or non-bindless draw
  paths.

If environment limits prevent a validation command from running during an
intermediate milestone, record the exact command, failure, and suspected cause.
Final 071 completion still requires all mandatory validation gates to pass or a
user-approved scope change.

## Data Contracts

### Material Parameter Envelope

Every Material v2 parameter is an envelope with a `kind` and either `value` or
`uri`, plus `valueType` for resource-backed logical values where needed. PBRT
parameter names remain intact. Texture parameters do not create parallel names
such as `KdTexture`.

`mix` material references use material-ref envelopes. The parser must load
target material headers/metadata to reject multi-level mix. The first
implementation does not recursively expand referenced material instances during
parse.

Normal maps are material resources. Geometry normals/tangents/UVs are geometry
attribute streams. Validation must keep those concepts separate.

### Technique and Effect Passes

Pass data is explicit. No renderer path supplies missing shader, target,
source, blend, depth, cull, or render-pass defaults. Missing required pass
fields fail fast.

Material-owned and effect-owned bindings are validated through reflection.
System-owned ABI bindings are reserved names and fixed set/binding contracts.
If a user shader declares a reserved system binding, it must exactly match the
common ABI.

### Resource Identity and Ownership

`SceneResourceTable` owns resources. Parser code and backend code use handles.
Resource de-duplication happens once at canonical URI plus type. GPU upload
does not repeat de-duplication by path string, material name, or file basename.

`ResourceHandle -> typed index` can be persisted only when it is part of the
package restore contract. Runtime generation counters, dirty flags, and
freelist details are never package identity.

### GPU Binding Model

Global bindless tables are organized by resource kind:

- texture table
- sampler table
- buffer table
- material storage by template
- object storage
- mesh/geometry storage
- camera/light storage

`MaterialInstance` GPU records store indices or handles into these tables.
They do not store URI strings or backend object pointers.

Pipeline keys include template, technique, pass, shader reflection layout, and
render target signature. They do not include concrete material parameter values.

## Execution Model

The master implementation plan should be milestone-based and continuous.
Completing one subtask or one subagent assignment is not a stop condition.

Use subagents where their work can be isolated:

- Code-reading and risk-review subagents for material loader, scene resource
  table, Vulkan resource manager, offline renderer, package IO, and validation
  tooling.
- Implementation subagents for bounded units such as parser tests, FrameGraph
  registry validation, package hash tests, task graph progress, or compare tool
  diagnostics.
- Review subagents after large patches, focused on regressions, missing tests,
  and hidden compatibility paths.

The main agent owns cross-module interfaces, final patches, integration,
conflict resolution, verification, remote editor validation, and commits.

## Testing Gates

Each milestone has mandatory gates:

- A: Material v2 parser contract, converter defaults, resource dependency
  registration, template/technique boundary, reflection-driven parameters, and
  helmet material smoke.
- B: technique validation, missing global technique warning, FrameGraph DAG,
  no hidden fallback, transparent pass rules, system ABI reflection, effect
  reflection, and helmet Forward smoke.
- C: URI de-duplication, parser ownership, material override instance,
  dependency graph, fatal missing contract, geometry attribute stream
  validation, object/material indirection, bindless upload view, and helmet
  parser/resource smoke.
- D: pipeline cache find/getOrCreate, cache blob round trip, upload task
  progress, technique-switch loading state, no backend type leak, bindless
  material arrays, global bindless slot mapping, indirect draw execution,
  unsupported bindless capability, helmet bindless smoke, and transitional draw
  cleanup.
- E: package round trip, Merkle diff, derived state exclusion, source/package
  offline equivalence, package resolver, single-binary load, chunked streaming
  restore, hash mismatch, backend cache fallback, MaterialTemplate grouping,
  and helmet package smoke.
- F: material sphere direct equivalence, full model direct similarity,
  source/package render chain, headless/editor export comparison, deferred
  GBuffer/FrameGraph dump, disabled feature diagnostics, and bridge audit.

Verification that cannot run during an intermediate step must be recorded with
the exact command and failure. The final completion claim requires all gates to
pass or an explicit approved change to the requirement.

## Documentation and Status

Implementation notes should update the active 071 requirement files as
milestones complete. Temporary bridges must be listed with:

- purpose
- owning milestone
- call site
- default-on or debug-only status
- deletion milestone
- verification proving it is unused after deletion

Final closure should move the completed 071 requirements through the
repository's requirement finish workflow and refresh affected notes.
