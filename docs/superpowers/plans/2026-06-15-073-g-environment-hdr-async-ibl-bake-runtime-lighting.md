# 073-g Environment HDR Async IBL Bake And Runtime Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking. Also use
> `render-agent-guardrails` before changing rendering architecture code.

**Goal:** Add async environment HDR IBL baking, cache validation, hot activation,
and inline Forward `standard-pbr` IBL runtime lighting without scene reload,
pipeline rebuild, or a separate Forward IBL geometry pass.

**Architecture:** Bake work is authored as RenderPathGraph YAML, compiled by
RenderWorkCompiler, and executed by a general `FrameGraphExecutor`.
`IblBakeJobService` owns job state, logs, cache decisions, manifests, file
commit, retry, and main-thread hot activation. Runtime shading uses
`feature.surfaceLighting` pass-uniform readiness facts and
`common/ibl_lighting.glsl` shared by Forward and Deferred. Environment bake
input format is explicit data: `EnvironmentIblBakeKey::sourceKind` comes from
`environmentMap.kind`, and the first bake pass normalizes either equirect 2D or
textureCube input into the common `bake.environment.cubemap` resource. The
strict acceptance scene uses
`assets/env/khronos/neutral/ggx/specular.ktx2` as a `textureCube` source.

**Tech Stack:** C++20, Vulkan, RenderPathGraph/RenderWorkCompiler,
SceneResourceTable, YAML resource parsers, GLSL, KTX2 assets, CMake/Ninja tests.

---

## File Map

Expected new files:

- `src/core/scene/ibl_bake_manifest.hpp`: manifest value types for environment,
  material, and SH9 YAML payloads.
- `src/core/scene/ibl_bake_manifest.cpp`: strict manifest validation helpers and
  derived mip calculation.
- `src/core/scene/ibl_bake_job.hpp`: `BakeJobId`, job state, event records,
  cache key records, and service-facing interfaces.
- `src/core/scene/ibl_bake_job.cpp`: event stream, status transitions, cache-key
  deduplication, and retry-safe state machine.
- `src/infra/resource_parsers/ibl_bake_manifest_parser.hpp`: parser/writer API
  for manifests and SH9 YAML.
- `src/infra/resource_parsers/ibl_bake_manifest_parser.cpp`: strict YAML
  parsing/writing and unknown-field rejection.
- `src/core/frame_graph/frame_graph_executor.hpp`: backend-neutral compiled
  graph execution interface.
- `src/backend/vulkan/vulkan_frame_graph_executor.hpp`: Vulkan implementation
  facade for compiled graph work.
- `src/backend/vulkan/vulkan_frame_graph_executor.cpp`: initial execution path
  for bake graph work, reusing existing Vulkan pass helpers where practical.
- `src/core/scene/ibl_bake_service.hpp`: `IblBakeJobService` public API.
- `src/core/scene/ibl_bake_service.cpp`: async orchestration, cache hit/force,
  file commit, activation callback dispatch.
- `assets/render_paths/bake_environment_ibl.render-path.yaml`: graph-authored
  environment IBL bake work.
- `assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml`: graph-authored
  `standard-pbr` BRDF LUT bake work.
- `assets/effects/surface_lighting.render-feature.yaml`: shader-visible IBL
  switches/readiness facts.
- `assets/shaders/glsl/common/ibl_lighting.glsl`: shared IBL formula.
- `src/test/integration/test_scene_bake_cache.cpp`: manifest, cache-key, and job
  state tests.
- `src/test/integration/test_frame_graph_executor.cpp`: executor boundary tests.
- `src/test/integration/test_vulkan_ibl_bake.cpp`: Vulkan bake/output smoke.

Expected modified files:

- `src/core/CMakeLists.txt`: add core bake/job/executor sources.
- `src/infra/CMakeLists.txt`: add manifest parser sources.
- `src/backend/vulkan/CMakeLists.txt`: add Vulkan executor sources.
- `src/test/CMakeLists.txt`: add new test targets.
- `src/core/frame_graph/render_work_compiler.hpp`
- `src/core/frame_graph/render_work_compiler.cpp`
- `src/core/frame_graph/render_work_build_context.hpp`
- `src/core/frame_graph/render_work_build_context.cpp`
- `src/infra/resource_parsers/render_path_graph_resource_parser.cpp`
- `src/infra/resource_parsers/render_pass_node_parser.cpp`
- `src/infra/resource_parsers/render_feature_resource_parser.cpp`
- `src/core/scene/scene_resource_table.hpp`
- `src/core/scene/scene_resource_table.cpp`
- `src/core/scene/scene_resource_table_upload_view.hpp`
- `src/core/scene/scene_resource_handles.hpp`
- `src/editor/commands/lxe_editor_commands.cpp`
- `src/editor/commands/lxe_editor_commands.hpp`
- `src/editor/app/editor_session.cpp`
- `assets/render_paths/forward_main.render-path.yaml`
- `assets/render_paths/forward_bloom.render-path.yaml`
- `assets/render_paths/deferred_main.render-path.yaml`
- `assets/render_paths/deferred_bloom.render-path.yaml`
- `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- `assets/shaders/glsl/render_paths/Deferred/deferred_lighting.frag`
- `assets/shaders/CMakeLists.txt`

## Tasks

### Task 1: Baseline Audits And Negative Test Anchors

**Files:**
- Modify: `src/test/integration/test_lxe_editor_source_boundary.cpp`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] Add an audit test that identifies current default positive references to
  `IblBakeRenderer::bakeStaticEnvironment`, `HAS_IBL`, `EnvironmentUBO`,
  `iblIntensity`, and `ForwardIblLighting`.
- [ ] Mark allowed hits narrowly: historical docs and named negative audits are
  allowed; default production paths are not.
- [ ] Add a shader source audit that fails if Forward or Deferred introduces a
  second IBL formula outside `common/ibl_lighting.glsl`.
- [ ] Run:

```bash
cmake --build build --target test_lxe_editor_source_boundary test_shader_compiler
ctest --test-dir build --output-on-failure -R "(test_lxe_editor_source_boundary|test_shader_compiler)"
```

Expected before implementation: new audits fail on existing legacy hits. Keep
the exact failing output in the task notes before moving to implementation.

### Task 2: Manifest And SH9 Value Types

**Files:**
- Create: `src/core/scene/ibl_bake_manifest.hpp`
- Create: `src/core/scene/ibl_bake_manifest.cpp`
- Modify: `src/core/CMakeLists.txt`
- Test: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] Add value types for:
  - `EnvironmentIblBakeManifest`
  - `MaterialIblBakeManifest`
  - `Sh9IrradiancePayload`
  - `IblBakeOutputPaths`
- [ ] Add `deriveMipCount(u32 resolution)` with rule
  `floor(log2(resolution)) + 1`.
- [ ] Add validation helpers:
  - environment schema must be `lxe.environment-ibl-bake.v1`;
  - material schema must be `lxe.material-ibl-bake.v1`;
  - SH schema must be `lxe.sh9.v1`;
  - specular format must be `RGBA16Float`;
  - BRDF format must be `RG16Float`;
  - BRDF size must be `256`;
  - SH coefficient count must be exactly 9 RGB triples.
- [ ] Write failing tests for derived mips, invalid resolution, wrong schema,
  missing output file, wrong SH count, and wrong BRDF size.
- [ ] Run:

```bash
cmake --build build --target test_scene_bake_cache
ctest --test-dir build --output-on-failure -R test_scene_bake_cache
```

Expected: tests pass after minimal value-type implementation.

### Task 3: Strict Manifest Parser And Writer

**Files:**
- Create: `src/infra/resource_parsers/ibl_bake_manifest_parser.hpp`
- Create: `src/infra/resource_parsers/ibl_bake_manifest_parser.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Test: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] Add parser/writer functions for:
  - environment manifest YAML;
  - material manifest YAML;
  - SH9 YAML payload.
- [ ] Reject unknown top-level fields and unknown nested fields in `source`,
  `material`, `bake`, and `outputs`.
- [ ] Reject manifests where `bake.specular.mips` does not match derived mip
  count for `bake.specular.resolution`.
- [ ] Write tests that parse the exact spec examples and reject:
  - unknown `brdf` in environment manifest;
  - unknown `specular` in material manifest;
  - missing `outputs.brdf.file`;
  - metadata-only manifest with no payload output.
- [ ] Run:

```bash
cmake --build build --target test_scene_bake_cache
ctest --test-dir build --output-on-failure -R test_scene_bake_cache
```

Expected: parser tests pass and diagnostics name the rejected field/path.

### Task 4: Atomic File Commit Helpers

**Files:**
- Modify: `src/core/scene/ibl_bake_job.hpp`
- Modify: `src/core/scene/ibl_bake_job.cpp`
- Test: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] Add small file-commit helpers used by the bake service:
  - write to sibling temporary file;
  - fsync/close where available;
  - atomically rename into final path;
  - leave old valid payload intact on write failure.
- [ ] Add tests using a temporary directory:
  - successful commit creates final manifest and removes temp file;
  - simulated invalid manifest does not replace existing final manifest;
  - partial payload is not considered valid by the parser.
- [ ] Run:

```bash
cmake --build build --target test_scene_bake_cache
ctest --test-dir build --output-on-failure -R test_scene_bake_cache
```

Expected: old final content remains after failed commit.

### Task 5: Bake Job State And Event Stream

**Files:**
- Create/modify: `src/core/scene/ibl_bake_job.hpp`
- Create/modify: `src/core/scene/ibl_bake_job.cpp`
- Test: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] Define `BakeJobId`, `IblBakeJobPhase`, `IblBakeJobSeverity`,
  `IblBakeJobEvent`, `IblBakeJobStatus`.
- [ ] Implement a thread-safe event stream with monotonically increasing
  `sequence`.
- [ ] Implement status transitions for:
  - `queued`
  - `cache-check`
  - `filter`
  - `write-cache`
  - `activate`
  - `complete`
  - `failed`
  - `activation-failed`
  - `cancel-pending`
- [ ] Add tests for incremental log reads with `since`, cancel transition, and
  fix-message preservation on failure.
- [ ] Run:

```bash
cmake --build build --target test_scene_bake_cache
ctest --test-dir build --output-on-failure -R test_scene_bake_cache
```

Expected: concurrent readers observe ordered events without duplicate sequence
numbers.

### Task 6: Bake Key Collection And Deduplication

**Files:**
- Modify: `src/core/scene/ibl_bake_job.hpp`
- Modify: `src/core/scene/ibl_bake_job.cpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] Add `EnvironmentIblBakeKey` keyed by environment URI and source hash.
- [ ] Add `MaterialIblBakeKey` keyed by material type; first supported type is
  exactly `standard-pbr`.
- [ ] Implement collection from the current `SceneResourceTable` facts:
  - one key per environment URI/hash;
  - one `standard-pbr` key no matter how many objects/material instances use it;
  - unsupported material types produce diagnostic events but do not fail the job.
- [ ] Add tests with a table containing multiple `standard-pbr` instances and
  multiple environment hashes.
- [ ] Run:

```bash
cmake --build build --target test_scene_bake_cache test_scene_resource_upload_view_v2
ctest --test-dir build --output-on-failure -R "(test_scene_bake_cache|test_scene_resource_upload_view_v2)"
```

Expected: material bake key count is 1 for repeated `standard-pbr`.

### Task 7: Bake Render-Path YAML Assets

**Files:**
- Create: `assets/render_paths/bake_environment_ibl.render-path.yaml`
- Create: `assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml`
- Modify: `src/infra/resource_parsers/render_path_graph_resource_parser.cpp`
- Modify: `src/infra/resource_parsers/render_pass_node_parser.cpp`
- Test: `src/test/integration/test_render_path_graph_pass_contract.cpp`
- Test: `src/test/integration/test_render_resource_parsers.cpp`

- [ ] Add graph assets with explicit pass names, shader URIs, sources, targets,
  intermediate resources, output payload names, and formats.
- [ ] Extend parser validation only as needed for bake graph semantics:
  cubemap face work, mip-chain output, readback output, and compute/fullscreen
  dispatch shape.
- [ ] Reject bake graph variants missing source, target, shader URI, format, or
  payload output.
- [ ] Add tests that parse both new assets from disk and reject malformed
  in-memory YAML variants.
- [ ] Run:

```bash
cmake --build build --target test_render_path_graph_pass_contract test_render_resource_parsers
ctest --test-dir build --output-on-failure -R "(test_render_path_graph_pass_contract|test_render_resource_parsers)"
```

Expected: bake graph assets parse through the same RenderPathGraph parser, not
a separate bake-only schema.

### Task 8: RenderWorkCompiler Bake Work

**Files:**
- Modify: `src/core/frame_graph/render_work_compiler.hpp`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Modify: `src/core/frame_graph/render_work_build_context.hpp`
- Modify: `src/core/frame_graph/render_work_build_context.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`

- [ ] Add compiled work records for bake graph passes:
  - pass kind;
  - shader URI;
  - declared sources;
  - declared targets;
  - cubemap face/mip/readback metadata;
  - payload output identifiers.
- [ ] Add validation that bake graph work cannot be compiled from metadata-only
  resources.
- [ ] Add tests that compile both bake graphs and assert the resulting work
  contains environment SH, specular cubemap, and BRDF LUT payload outputs.
- [ ] Add negative tests for graph missing `scene.environmentBake` or
  `scene.materialIblBake` when `feature.surfaceLighting.enableIblLighting` is
  true.
- [ ] Run:

```bash
cmake --build build --target test_render_work_compiler
ctest --test-dir build --output-on-failure -R test_render_work_compiler
```

Expected: compiled bake work is deterministic and contains no backend-invented
resources.

### Task 9: FrameGraphExecutor Interface

**Files:**
- Create: `src/core/frame_graph/frame_graph_executor.hpp`
- Modify: `src/core/CMakeLists.txt`
- Create: `src/test/integration/test_frame_graph_executor.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] Define a backend-neutral `FrameGraphExecutor` interface that accepts
  compiled RenderWorkCompiler output and execution inputs.
- [ ] Include result types for success, failure diagnostics, generated payload
  handles, and readback metadata.
- [ ] Keep cache, file writing, manifest writing, and activation out of this
  interface.
- [ ] Add tests using a fake executor:
  - records received compiled work;
  - rejects missing payload outputs;
  - proves `IblBakeJobService` can depend on the interface instead of a Vulkan
    concrete class.
- [ ] Run:

```bash
cmake --build build --target test_frame_graph_executor
ctest --test-dir build --output-on-failure -R test_frame_graph_executor
```

Expected: fake executor tests pass without linking Vulkan.

### Task 10: Vulkan FrameGraphExecutor Minimum Implementation

**Files:**
- Create: `src/backend/vulkan/vulkan_frame_graph_executor.hpp`
- Create: `src/backend/vulkan/vulkan_frame_graph_executor.cpp`
- Modify: `src/backend/vulkan/CMakeLists.txt`
- Modify: `src/backend/vulkan/details/ibl_bake_renderer.hpp`
- Modify: `src/backend/vulkan/details/ibl_bake_renderer.cpp`
- Test: `src/test/integration/test_vulkan_ibl_bake.cpp`

- [ ] Move or wrap the useful low-level Vulkan bake routines behind
  `VulkanFrameGraphExecutor`.
- [ ] The public/default path must accept compiled graph work, not a private
  `bakeStaticEnvironment()` shortcut.
- [ ] Keep `IblBakeRenderer` only as an internal implementation detail if it is
  still needed for Vulkan commands.
- [ ] Add a Vulkan smoke that executes the environment bake graph and BRDF LUT
  graph through `FrameGraphExecutor`.
- [ ] Run:

```bash
cmake --build build --target test_vulkan_ibl_bake
xvfb-run -a ./build/src/test/test_vulkan_ibl_bake
```

Expected: generated GPU outputs are non-empty and execution path mentions
`FrameGraphExecutor` in the test fixture.

### Task 11: IblBakeJobService Orchestration

**Files:**
- Create: `src/core/scene/ibl_bake_service.hpp`
- Create: `src/core/scene/ibl_bake_service.cpp`
- Modify: `src/core/CMakeLists.txt`
- Test: `src/test/integration/test_scene_bake_cache.cpp`
- Test: `src/test/integration/test_frame_graph_executor.cpp`

- [ ] Implement `startBake(force=false)`, `status(id)`, `logs(id, since)`, and
  `cancel(id)`.
- [ ] Enforce a single global running job.
- [ ] Implement default cache hit path: valid manifest + valid payloads skip GPU
  bake and enter activation.
- [ ] Implement `--force` semantics: valid cache ignored only when no job is
  running.
- [ ] Implement invalid cache path: log exact invalid reason and rebake.
- [ ] Add tests using the fake executor and temporary cache directories for:
  cache hit, invalid cache, force rebake, duplicate running job, cancel, executor
  failure, and retry.
- [ ] Run:

```bash
cmake --build build --target test_scene_bake_cache test_frame_graph_executor
ctest --test-dir build --output-on-failure -R "(test_scene_bake_cache|test_frame_graph_executor)"
```

Expected: job phases include `cache-check`, `filter`, `write-cache`,
`activate`, and `complete` in the expected order.

### Task 12: Two-Phase Hot Activation

**Files:**
- Modify: `src/core/scene/scene_resource_handles.hpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Modify: `src/core/scene/ibl_environment.hpp`
- Test: `src/test/integration/test_scene_resource_upload_view_v2.cpp`
- Test: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] Add live resource records for:
  - environment diffuse SH payload;
  - specular prefiltered cubemap payload;
  - `standard-pbr` BRDF LUT payload;
  - active IBL generation id.
- [ ] Implement two-phase activation:
  - prepare temporary live handles;
  - validate descriptor/upload readiness;
  - swap active generation only when all payloads are ready.
- [ ] Ensure activation failure leaves old active IBL resources unchanged.
- [ ] Add upload-view tests proving generation changes only on successful
  activation.
- [ ] Run:

```bash
cmake --build build --target test_scene_resource_upload_view_v2 test_scene_bake_cache
ctest --test-dir build --output-on-failure -R "(test_scene_resource_upload_view_v2|test_scene_bake_cache)"
```

Expected: failed activation preserves the previous active generation.

### Task 13: Editor Commands And Logs

**Files:**
- Modify: `src/editor/commands/lxe_editor_commands.hpp`
- Modify: `src/editor/commands/lxe_editor_commands.cpp`
- Modify: `src/editor/app/editor_session.cpp`
- Test: `src/test/integration/test_lxe_editor_render_debug_dump.cpp`

- [ ] Register commands:
  - `bake ibl start`
  - `bake ibl start --force`
  - `bake job status <id>`
  - `bake job logs <id> [since]`
  - `bake job cancel <id>`
- [ ] Return structured JSON including job id, phase, progress, sequence range,
  and latest fix message when present.
- [ ] Pipe bake events into editor command prompt history and `editor.log`
  through existing logging hooks.
- [ ] Add command tests for usage errors, successful start, duplicate running
  job, status, logs since sequence, and cancel.
- [ ] Run:

```bash
cmake --build build --target test_lxe_editor_render_debug_dump lxe_editor
ctest --test-dir build --output-on-failure -R test_lxe_editor_render_debug_dump
```

Expected: command output is machine-readable and logs include bake phase lines.

### Task 14: Surface Lighting Feature Asset

**Files:**
- Create: `assets/effects/surface_lighting.render-feature.yaml`
- Modify: `src/infra/resource_parsers/render_feature_resource_parser.cpp`
- Test: `src/test/integration/test_render_resource_parsers.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`

- [ ] Define `feature.surfaceLighting` fields:
  - `enableIblLighting`
  - `diffuseIblIntensity`
  - `specularIblIntensity`
  - `environmentIblReady`
  - `standardPbrIblReady`
- [ ] Include binding/member schema compatible with shader reflection.
- [ ] Add parser tests rejecting unknown fields, missing required fields, and
  wrong numeric/bool types.
- [ ] Add graph/compiler tests proving Forward and Deferred can depend on the
  same feature payload.
- [ ] Run:

```bash
cmake --build build --target test_render_resource_parsers test_render_work_compiler
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler)"
```

Expected: `surfaceLighting` is a live feature payload, not C++ hardcoded UBO
data.

### Task 15: Shared IBL GLSL

**Files:**
- Create: `assets/shaders/glsl/common/ibl_lighting.glsl`
- Modify: `assets/shaders/glsl/common/pbr.glsl`
- Modify: `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- Modify: `assets/shaders/glsl/render_paths/Deferred/deferred_lighting.frag`
- Modify: `assets/shaders/CMakeLists.txt`
- Test: `src/test/integration/test_shader_compiler.cpp`

- [ ] Move standard PBR IBL functions into `common/ibl_lighting.glsl`.
- [ ] Add pass-uniform readiness branch using `feature.surfaceLighting` facts.
- [ ] Keep Forward IBL inside `Forward/pbr.frag`; do not create
  `ForwardIblLighting` shader or pass.
- [ ] Make DeferredLighting include the same common file; Deferred image output
  is not the acceptance path in this requirement.
- [ ] Add shader compile/reflection tests proving both shaders include the same
  IBL ABI and no old `HAS_IBL` / `EnvironmentUBO` truth remains.
- [ ] Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler
ctest --test-dir build --output-on-failure -R test_shader_compiler
```

Expected: Forward and Deferred shaders compile and reflect the shared surface
lighting/IBL bindings.

### Task 16: Forward And Deferred Graph Wiring

**Files:**
- Modify: `assets/render_paths/forward_main.render-path.yaml`
- Modify: `assets/render_paths/forward_bloom.render-path.yaml`
- Modify: `assets/render_paths/deferred_main.render-path.yaml`
- Modify: `assets/render_paths/deferred_bloom.render-path.yaml`
- Test: `src/test/integration/test_render_work_compiler.cpp`
- Test: `src/test/integration/test_render_resource_parsers.cpp`

- [ ] Add `feature.surfaceLighting` dependency to Forward and Deferred graph
  assets where IBL can run.
- [ ] Add graph facts for `scene.environmentBake` and `scene.materialIblBake`
  without adding a separate Forward IBL geometry/additive pass.
- [ ] Ensure skybox direct rendering remains owned by 073f graph shape.
- [ ] Add compiler tests:
  - Forward graph with IBL enabled requires environment bake facts;
  - Forward graph with IBL enabled requires `standard-pbr` material bake facts;
  - default graph contains no `ForwardIblLighting` pass.
- [ ] Run:

```bash
cmake --build build --target test_render_resource_parsers test_render_work_compiler
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler)"
```

Expected: graph-authored facts drive readiness; backend does not patch graph
inputs.

### Task 17: Bake Source-Kind And Output Validation Smoke

**Files:**
- Modify: `src/core/scene/ibl_bake_keys.hpp`
- Modify: `src/core/scene/ibl_bake_manifest.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/frame_graph/frame_graph_executor.hpp`
- Modify: `src/backend/vulkan/vulkan_frame_graph_executor.cpp`
- Modify: `src/test/integration/test_vulkan_ibl_bake.cpp`
- Test assets generated at runtime under a temporary directory.

- [ ] Add `EnvironmentIblBakeSourceKind` with values `Equirect2D` and
  `TextureCube`.
- [ ] Collect source kind from `RenderFeatureParameter::kind`; do not infer
  strictness or source kind from URI/path substrings.
- [ ] Keep one graph-authored environment bake path. The first pass is
  `NormalizeEnvironmentToCubemap`; it uses source kind to select an equirect or
  cubemap normalization variant, then all later SH/specular passes consume the
  same `bake.environment.cubemap`.
- [ ] Add an end-to-end contract test that runs `bake ibl start` through
  service + `FrameGraphExecutor` using
  `assets/env/khronos/neutral/ggx/specular.ktx2` as a `textureCube` source.
- [ ] Validate files:
  - environment `manifest.yaml`;
  - manifest source kind is `textureCube`;
  - `diffuse_sh9.yaml` with 9 nonzero-ish RGB coefficients;
  - `specular_prefilter.ktx2` exists and reports 256 base resolution with
    derived mip count 9;
  - material `manifest.yaml`;
  - `brdf_lut.ktx2` exists and reports 256 RG16Float.
- [ ] Add a cache-hit phase check by running the command twice; second run must
  activate without GPU bake.
- [ ] Add a `--force` check; forced run must rebake and atomically replace
  payloads.
- [ ] Run:

```bash
cmake --build build --target test_vulkan_ibl_bake
xvfb-run -a ./build/src/test/test_vulkan_ibl_bake
```

Expected: output validation fails if any payload is metadata-only, missing, or
written against the wrong environment source kind.

### Task 18: Strict Neutral Runtime Visual Smokes

**Files:**
- Modify: `src/test/integration/test_lxe_editor_render_debug_dump.cpp`
- Modify/Create: realtime smoke helper under `src/test/integration/` if needed.
- Modify as needed: editor smoke scene or generated test scene asset under
  `assets/scenes/`

- [ ] Add a pure-environment-light Forward scene/debug smoke using Helmet plus
  `assets/env/khronos/neutral/ggx/specular.ktx2` through
  `feature.environmentLighting`.
- [ ] The pure-environment smoke must:
  - contain no direct light nodes;
  - rely on IBL/runtime environment lighting for the Helmet surface;
  - render/dump after bake activation;
  - fail if the Helmet output is pure black or effectively black by image
    statistics.
- [ ] Add a complete Forward scene/debug smoke using Helmet plus
  `assets/env/khronos/neutral/ggx/specular.ktx2` through
  `feature.environmentLighting`.
- [ ] The complete smoke must:
  - load the neutral `textureCube` environment;
  - include the normal Helmet scene context plus neutral environment lighting;
  - starts `bake ibl start`;
  - waits for completion;
  - render/dump before and after activation;
  - write human-inspectable images under
    `artifacts/smoke/ibl-neutral/`;
  - prove PBR surface output changes after IBL activation with numeric image
    stats and active IBL generation/readiness facts.
- [ ] Add a Forward bloom compatibility smoke: enabling bloom after IBL
  activation must not produce the known wrong output path.
- [ ] Do not add a Deferred image acceptance requirement; only compile/reflection
  parity is required for Deferred in 073g.
- [ ] Run:

```bash
cmake --build build --target CompileShaders test_lxe_editor_render_debug_dump lxe_editor
ctest --test-dir build --output-on-failure -R test_lxe_editor_render_debug_dump
```

Expected: Forward output changes after activation without scene reload and
without a separate Forward IBL geometry pass. Final reporting must include the
exact before/after image paths for manual inspection.

### Task 19: Private Bake Path Hard Cut

**Files:**
- Modify: `src/backend/vulkan/details/ibl_bake_renderer.hpp`
- Modify: `src/backend/vulkan/details/ibl_bake_renderer.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: source-boundary tests from Task 1.

- [ ] Remove or make private the default/public
  `IblBakeRenderer::bakeStaticEnvironment()` shortcut.
- [ ] Ensure realtime renderer no longer starts private IBL bake during scene
  initialization.
- [ ] If low-level bake helpers remain, document them as `FrameGraphExecutor`
  internals and keep them unreachable from public/default commands.
- [ ] Run:

```bash
cmake --build build --target test_lxe_editor_source_boundary test_vulkan_ibl_bake lxe_editor
ctest --test-dir build --output-on-failure -R test_lxe_editor_source_boundary
rg -n "IblBakeRenderer|bakeStaticEnvironment|renderPath: IBLBake|ibl_prefilter_env|ibl_brdf_lut|HAS_IBL|EnvironmentUBO|iblIntensity|ForwardIblLighting" src assets docs notes
```

Expected: production positive paths no longer depend on the private shortcut or
old shader truth tokens. Historical docs and named negative audits may remain.

### Task 20: Final Verification And Documentation Sync

**Files:**
- Modify if implementation drift requires it:
  `notes/requirements/073-g-environment-hdr-async-ibl-bake-and-runtime-lighting.md`
- Modify if shader/resource docs need current-code updates:
  `notes/concepts/material/shader.md`
  `notes/subsystems/vulkan-backend.md`

- [ ] Run full targeted build:

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_render_work_compiler test_scene_bake_cache test_frame_graph_executor test_vulkan_ibl_bake test_shader_compiler test_scene_resource_upload_view_v2 test_lxe_editor_render_debug_dump lxe_editor
```

- [ ] Run targeted CTest:

```bash
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler|test_scene_bake_cache|test_frame_graph_executor|test_shader_compiler|test_scene_resource_upload_view_v2|test_lxe_editor_render_debug_dump)"
```

- [ ] Run Vulkan smoke:

```bash
xvfb-run -a ./build/src/test/test_vulkan_ibl_bake
```

- [ ] Run audit:

```bash
rg -n "ForwardIblLighting|IBLBake|IblBakeRenderer|bakeStaticEnvironment|ibl_prefilter_env|ibl_brdf_lut|HAS_IBL|EnvironmentUBO|iblIntensity" src assets docs notes
```

- [ ] Run notes build if docs changed:

```bash
scripts/notes/serve_site.sh --build
```

Expected final state:

- `bake ibl start` returns a job id and logs progress.
- Valid cache hits activate without GPU rebake.
- `bake ibl start --force` rebakes when no job is running.
- Failed bake/activation does not replace active IBL resources.
- Output files match manifest contracts.
- The accepted strict smoke uses
  `assets/env/khronos/neutral/ggx/specular.ktx2` and emits
  `artifacts/smoke/ibl-neutral/` before/after images.
- The pure-environment-light Helmet smoke contains no direct lights and still
  renders nonblack after IBL activation.
- Forward uses inline common IBL in the existing surface pass.
- Deferred compiles against the same common IBL contract.
- No default positive path uses the old private bake shortcut or a separate
  Forward IBL pass.

## Self-Review

- Spec coverage: R1 input, R2 async service, R3 manifests/output layout,
  R3.1 deduplication, R3.2 bake render paths, R4 hot activation, R5 Forward
  inline runtime, R6 failure isolation, R7 Deferred parity, and R8 hard cut each
  have implementation and verification tasks.
- Placeholder scan: no unfinished placeholder markers remain.
- Type consistency: the plan consistently uses `feature.surfaceLighting`,
  `FrameGraphExecutor`, `IblBakeJobService`, `standard-pbr`, and
  `common/ibl_lighting.glsl`.
