# REQ-073-d RenderPath Shader URI And Material Source Path Hard Cut Design

Date: 2026-06-14

## Decision

`REQ-073-d` is expanded from a URI-only migration into the hard cut that makes
the completed `REQ-073-a/b/c` material source path impossible to bypass from
the realtime default path.

The requirement still owns the `techniques/...` to `render_paths/...` shader URI
migration, but it also owns the immediate hard cuts that are necessary for that
migration to be meaningful:

- default RenderPathGraph assets use `render_paths/...` pass shader URIs;
- shader URI resolution accepts `render_paths/...` as the positive realtime
  RenderPath shader namespace and rejects `techniques/...`;
- default/material-source realtime preparation does not inject old
  `techniques/Forward/pbr` material passes;
- material-source passes must enter rendering through final source variant
  reflection, RenderPathNode signatures, and typed SceneResourceTable indices;
- legacy implementation code, old shader source directories, old compatibility
  aliases, and old positive tests are deleted instead of preserved as alternate
  paths;
- the pipeline preparation phase happens explicitly after scene resources and
  material source variants are complete.

`REQ-076-b` remains the broader realtime clean gate. It still owns Helmet/BMW
smoke validation, final global fallback cleanup, debug/default material
rejection, and end-to-end proof that unsupported sources or skipped draws cannot
look like success.

## Context

`REQ-073-c` established:

- material `type` owns source variant identity;
- pass shaders consume a final material-source variant rather than branching on
  material type at runtime;
- `PipelineKey = MaterialTypeVariant + RenderPathNodeSignature`;
- pipeline build inputs must use final shader reflection.

The current repository still has positive paths that can obscure those facts:

- default graph assets under `assets/render_paths/` still reference
  `techniques/Forward/...` and `techniques/Deferred/...`;
- `resolveShaderSourceUris()` in
  `src/infra/resource_parsers/render_resource_scene_parser_adapters.cpp`
  still accepts `techniques/...` and maps legacy deferred aliases;
- `scene_runtime.cpp` still has a helper that compiles
  `assets/shaders/glsl/techniques/Forward/pbr.vert|frag` and injects a
  Forward pass for materials that do not already expose one;
- ordinary parser, shader, and material source variant tests still use
  `techniques/...` as positive fixture data.

A plain directory rename would not be enough. If the runtime can still generate
old material passes or accept old shader URIs, later indirect batching and smoke
tests can pass through a mixed path instead of proving the source-reflected
material architecture.

## Scope

This design includes:

- realtime Forward and Deferred shader source tree migration from
  `assets/shaders/glsl/techniques/...` to
  `assets/shaders/glsl/render_paths/...`;
- default RenderPathGraph asset shader URI migration;
- resolver diagnostics for `render_paths/...`, missing shader source, legacy
  URI, and unsupported stage/shape;
- removal or explicit rejection of runtime material-source pass injection that
  relies on old `techniques/Forward/pbr` shaders;
- deletion of old shader source directories, resolver aliases, runtime helper
  branches, and positive tests that existed only to support `techniques/...`;
- strict preparation gates for default/migrated material-source passes;
- an explicit post-load pipeline preparation phase;
- tests and audits that keep ordinary positive fixtures off old technique
  terminology.

This design excludes:

- indirect batching design beyond validating that batchable material-source work
  items have complete typed indices;
- Helmet and BMW low-resolution smoke success gates;
- broad cleanup of every realtime descriptor/submission helper that is not on
  the 073-a/b/c material-source positive path;
- OfflineRT RenderPathGraph compute entry migration;
- pipeline cache package serialization or BC7 packaging.

This exclusion does not permit keeping old realtime material-source code around
as a named compatibility path. If a legacy branch, shader tree, alias, or test
exists only for the old `techniques/...` route, this requirement deletes it.

## Architecture

### Shader URI Resolver

The RenderPath shader resolver is the single boundary that translates pass
shader URIs into source files.

Positive realtime RenderPath shader URIs use the `render_paths/...` namespace,
for example:

- `render_paths/Forward/pbr`;
- `render_paths/Forward/shadow_depth_only`;
- `render_paths/Deferred/pbr_gbuffer`;
- `render_paths/Deferred/deferred_lighting`.

The resolver must reject:

- `techniques/...`;
- legacy aliases such as `techniques/Deferred/gbuffer`;
- silent redirects from old URIs to new URIs.

The old resolver code path is removed. Rejection is implemented as a current
schema validation/error branch, not as a preserved compatibility resolver that
first understands the old route and then redirects or adapts it.

Root-level utility shaders may remain only when they are explicitly treated as
non-material fullscreen/debug utility shaders. They must not be used as a
material-source draw pass escape hatch.

### Legacy Deletion Policy

Old-path code and fixtures are not retained after migration. This applies to:

- shader source directories under `assets/shaders/glsl/techniques/Forward/` and
  `assets/shaders/glsl/techniques/Deferred/`;
- generated or checked-in default realtime SPIR-V outputs under the same old
  directories;
- resolver aliases and helper branches that accept `techniques/...` as a
  positive route;
- runtime material helpers that compile or attach old `techniques/Forward/pbr`
  passes;
- tests whose purpose was to prove the old route worked.

The only allowed mentions of old tokens after this requirement are:

- the active requirement/design text;
- narrowly named negative/audit tests that construct the old token inline and
  assert rejection;
- diagnostic message assertions for that rejection.

Those negative tests must not preserve old shader files, old directories, old
material assets, or reusable compatibility helpers.

### RenderPathGraph Assets

Default graph assets are the source of truth for pass shaders. Forward,
Deferred, Shadow, and DeferredLighting graph nodes use RenderPath terminology
and `render_paths/...` shader URIs.

Material-source draw passes that use a shader requiring
`LX_MATERIAL_CONTRACT_SOURCE` must declare `material.bsdf` in `sources`. A graph
that declares `material.bsdf` for a shader that does not consume a material
source, or vice versa, fails during graph dependency resolution.

### Material Source Runtime Gate

Material files declare `bsdf.type`, `bsdf.source`, parameters, and texture
dependencies. They do not define material-local techniques or pass shaders.

The runtime must not repair missing source-material pass data by compiling the
old Forward PBR shader and injecting a material pass. If a source material is
missing final variant data after graph and material resources are loaded, the
preparation phase fails with a diagnostic that points to the RenderPathGraph and
material source variant path.

### Strict Default/Migrated Pass Validation

`REQ-073-d` does not rewrite the whole Vulkan descriptor system. It does harden
the default/migrated material-source path so old material facts cannot satisfy
new passes.

For material-source raster work:

- final shader reflection must be present;
- `MaterialTypeVariant` and `RenderPathNodeSignature` must be populated;
- `PipelineKey` must be built from those two identities;
- a shader that consumes `SceneMaterials` must have a typed material index;
- a shader that consumes source-local material refs must have a typed source
  material ref index;
- draw/object data consumers must have a typed `SceneDraws`/object index;
- old material bindings such as `MaterialUBO`, `MaterialParams`, and legacy
  per-material texture binding names fail validation.

## Data Flow

The positive path is:

```text
.material
  -> bsdf.type / bsdf.source / source-reflected envelopes
  -> SceneResourceTable material and source records

.render-path.yaml
  -> pass shader: render_paths/...
  -> RenderPassNodeSignature
  -> ShaderResourceMetadata

SceneResourceTable + RenderPathGraph
  -> material source variant resolver
  -> final shader variant + final reflection
  -> material pass enabled for graph pass

loaded scene preparation
  -> FrameGraph build
  -> RenderWorkQueue build
  -> PipelineKey(MaterialTypeVariant, RenderPathNodeSignature)
  -> typed material/draw/source indices
  -> PipelinePreparation
  -> render submission
```

`techniques/...` does not enter this flow. It may appear only in the active
requirement/design text, narrowly named inline rejection/audit tests, or
diagnostic assertions. No production code, default asset, shader source file, or
ordinary positive test keeps the old route.

## Pipeline Preparation

Pipeline work must be prepared explicitly after scene resource loading is
complete and after material source variant resolution has succeeded.

The phase is intentionally named pipeline preparation rather than pipeline build
because future work may load a cache package.

```text
loaded scene resources complete
  -> material texture dependencies resolved
  -> RenderPathGraph dependencies resolved
  -> material source variants resolved
  -> FrameGraph built
  -> PipelineBuildDesc collection
  -> PipelinePreparation
       current: preload/build pipelines
       future: load cache package, validate keys/reflection/state,
               build only misses
  -> render submission
```

Constraints:

- resource-incomplete or variant-incomplete scenes must not trigger implicit
  fallback pipeline construction;
- cache hits must still validate material type variant, RenderPathNode
  signature, shader/reflection identity, render state, rendering mode, and
  attachment contracts;
- cache misses are handled inside pipeline preparation, not by old draw-time
  fallback paths;
- draw submission consumes prepared pipeline facts and must not infer missing
  shader/material identity.

## Diagnostics

Failure diagnostics must distinguish:

- legacy shader URI rejected;
- missing shader source;
- unsupported shader stage or source shape;
- pass declares `material.bsdf` but shader does not consume material source;
- shader requires material source but pass omits `material.bsdf`;
- material source variant required but unresolved;
- final shader reflection missing;
- typed material, source material ref, draw, or object index missing;
- legacy material binding found in a material-source pass.

Legacy URI diagnostics include at least:

- graph URI;
- pass id;
- rejected URI;
- expected `render_paths/...` prefix;
- resolver search path.

Preparation diagnostics include enough identity to debug the failing item:

- material type;
- material source URI;
- RenderPathGraph URI;
- pass id;
- RenderPathNode signature;
- PipelineKey or the missing identity that prevented it from being built.

## Testing Strategy

### Default Asset URI Tests

Parse the default Forward, Forward bloom, Deferred, and Deferred bloom
RenderPathGraph assets and assert that realtime pass shaders use
`render_paths/...` or an explicitly allowed non-material utility shader URI.
Default assets must not contain `techniques/...`.

### Resolver Tests

Positive cases:

- `render_paths/Forward/pbr`;
- `render_paths/Forward/shadow_depth_only`;
- `render_paths/Deferred/pbr_gbuffer`;
- `render_paths/Deferred/deferred_lighting`.

Negative cases:

- `techniques/Forward/pbr`;
- `techniques/Forward/shadow_depth_only`;
- `techniques/Deferred/gbuffer`;
- missing `render_paths/...` shader.

Legacy URI failures must name the expected replacement namespace instead of
falling through to a generic missing-file error.

### Material Source Runtime Gate Tests

Add or update tests that prove source-material preparation cannot succeed by
injecting `techniques/Forward/pbr` through editor/runtime material helpers.

The positive path must rely on RenderPathGraph pass shaders and final material
source variants.

### Explicit Loaded-scene Preparation Tests

Construct a small material-source scene and run the explicit preparation phase
after resources are loaded:

- resolve material source variants;
- build FrameGraph/RenderWorkQueue;
- collect `PipelineBuildDesc`;
- run pipeline preparation.

The test asserts that pipeline descriptors consume final shader reflection and
RenderPathNode signatures. Missing variants, missing typed indices, and legacy
bindings fail during preparation.

### Positive Fixture Audit

Ordinary positive tests, default assets, and runtime default paths must not use:

- `techniques/...`;
- `defaultTechnique`;
- material-local `techniques`;
- old material pass-contract terminology as success fixtures.

Delete old positive tests instead of converting them into compatibility tests.
When old-token rejection still needs coverage, add a new narrow negative test
with an inline fixture that asserts rejection.

Run `rg` audits as part of the acceptance gate:

- production/runtime roots must have zero old-token hits for
  `techniques/Forward`, `techniques/Deferred`, `defaultTechnique`, and
  material-local technique types;
- `src/test` must have zero old-token hits except the named rejection/audit
  tests;
- any remaining hit outside historical docs and the active 073-d spec is either
  deleted or explicitly justified as a new rejection diagnostic assertion.

### Build Gate

Shader build targets compile default realtime shaders from
`render_paths/...`. Variant-only shaders still fail naked compilation unless
compiled through the material source variant route.

## Acceptance Gate

`REQ-073-d` is complete when:

- default realtime RenderPathGraph assets no longer use `techniques/...`;
- resolver positive tests use `render_paths/...`;
- resolver negative tests reject `techniques/...` with migration diagnostics;
- source-material runtime preparation cannot inject old `techniques/Forward/pbr`
  pass data;
- loaded-scene preparation runs explicitly after resources and source variants
  are complete;
- pipeline preparation consumes stable `PipelineBuildDesc` and final shader
  reflection;
- material-source passes fail on unresolved variants, missing typed indices, or
  legacy material bindings;
- old implementation code, old shader directories, old resolver aliases, and
  old positive tests are deleted rather than preserved;
- ordinary positive fixtures are migrated off old technique terminology;
- `rg` audits show old tokens are absent from production code and ordinary
  tests, with only the named negative/audit tests and documentation remaining.

This leaves `REQ-076-b` with a cleaner job: prove the complete realtime default
path with Helmet/BMW smoke and finish broad fallback cleanup without also
debugging old URI and source-variant bypass paths.
