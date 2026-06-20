# 074-h OfflineRT FrameGraphExecutor Hard Cut Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use `render-agent-guardrails` before changing rendering architecture code. Implement task-by-task. Keep checklist state current. This plan implements `docs/superpowers/specs/2026-06-20-074-h-offlinert-framegraph-executor-hard-cut-design.md`.

**Goal:** Hard-cut OfflineRT onto the unified `RenderPathGraph` -> `FrameGraph` -> `RenderWorkCompiler` -> `FrameGraphExecutor` flow, converge IBL bake onto generic readback outputs, and remove the old OfflineRT executor/job/integrator/shader-provider positive paths.

**Architecture:** OfflineRT is a scene-consuming compute pass prepared through the same graph/material/render-feature/resource-table machinery as realtime. IBL bake remains an `IblBakeJobService` workflow, but its GPU work and outputs are graph/readback/executor artifacts. RenderFeature is extended as the single schema for shader parameters, pass-level volatile values, hit shader tables, and feature-declared derived resources such as scene acceleration.

**Tech Stack:** C++20, YAML-CPP, Vulkan, RenderPathGraph, FrameGraph, RenderWorkCompiler, SceneResourceTable, Material v2, RenderFeature, GLSL, CMake/Ninja integration tests.

---

## Rendering Guardrail Prefix

Use current repo facts only. Read the active spec and current code before changing scope. This task is not a rename-only task.

First identify a negative test, audit, diagnostic, or code fact that proves the current legacy path, silent fallback, ignored field, placeholder resource, or mixed renderer graph path can leak through. Then implement the smallest change that closes that path.

Hard constraints:
- Every parser allowlist field must be consumed into the target model. Unknown, legacy, or not-yet-modeled fields must fail-fast with diagnostics.
- No placeholder payloads may satisfy resource dependencies. A dependency must be truly parsed/registered, or the load/upload path must fail with diagnostics.
- Do not introduce a second public graph/contract system beside `RenderPathGraph` and `RenderPassNode`.
- Runtime-only FrameGraph branches are unfinished work unless the branch is explicitly dynamic and has a named removal/follow-up target.
- Shader, RenderFeature, material, graph, and resource dependencies must resolve to live typed payloads before graph export/upload.
- Remove superseded legacy implementation and positive tests in the same slice.
- Keep verification warning-free for touched targets.

## File Map

Primary core targets:
- `src/core/asset/render_effect.hpp`
- `src/core/frame_graph/frame_graph.hpp`
- `src/core/frame_graph/frame_graph_executor.hpp`
- `src/core/frame_graph/render_input.hpp`
- `src/core/frame_graph/render_work_build_context.hpp`
- `src/core/frame_graph/render_work_build_context.cpp`
- `src/core/frame_graph/render_work_compiler.hpp`
- `src/core/frame_graph/render_work_compiler.cpp`
- `src/core/pipeline/pipeline_build_desc.hpp`
- `src/core/scene/scene_resource_table.hpp`
- `src/core/scene/scene_resource_table.cpp`
- `src/core/scene/scene_resource_table_upload_view.hpp`

Primary infra/backend targets:
- `src/infra/resource_parsers/render_path_graph_resource_parser.*`
- `src/infra/resource_parsers/render_feature_resource_parser.*`
- `src/infra/resource_parsers/material_resource_parser.*`
- `src/infra/offline/offline_scene_loader.*`
- `src/infra/offline/offline_image_writer.*`
- `src/backend/vulkan/vulkan_frame_graph_executor.*`
- `src/backend/vulkan/details/commands/command_buffer.*`
- `src/backend/vulkan/details/resource_manager.*`
- `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- `src/tools/lxe_offline_render/main.cpp`

Assets/shaders:
- `assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml`
- `assets/render_paths/bake_environment_ibl.render-path.yaml`
- `assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml`
- `assets/effects/offline_ray_tracer.render-feature.yaml`
- `assets/effects/environment_lighting.render-feature.yaml`
- `assets/effects/surface_lighting.render-feature.yaml`
- `assets/materials/**/standard-pbr*.material.yaml`
- `assets/shaders/glsl/render_paths/OfflineRT/**`
- `assets/shaders/glsl/materials/standard_pbr/**`
- `assets/shaders/glsl/common/**`

Legacy deletion targets:
- `src/core/offline/offline_render_job.*`
- `src/core/offline/offline_render_work_graph.*`
- `src/backend/vulkan/offline/offline_render_graph_executor.*`
- `src/backend/vulkan/offline/software_compute_offline_integrator.*`
- `src/backend/vulkan/offline/vulkan_offline_renderer.*` if it still owns render logic
- old positive tests or fixtures that exercise the deleted OfflineRT path

Primary tests:
- `src/test/integration/test_render_resource_parsers.cpp`
- `src/test/integration/test_render_work_compiler.cpp`
- `src/test/integration/test_frame_graph_executor.cpp`
- `src/test/integration/test_vulkan_ibl_bake.cpp`
- `src/test/integration/test_shader_compiler.cpp`
- `src/test/integration/test_lxe_editor_source_boundary.cpp`
- `src/test/integration/test_bindless_validation_contract.cpp`

---

### Task 1: Add Baseline Legacy Boundary Audits

**Files:**
- Modify: `src/test/integration/test_lxe_editor_source_boundary.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`
- Modify: `src/test/integration/test_vulkan_ibl_bake.cpp`

**Required negative test or audit:**
- Prove current code still exposes positive legacy paths: `OfflineRenderJob`, `offlineShader`, `createOfflineRenderFrameGraph`, `OfflineRenderGraphExecutor`, `software_compute_offline_integrator`, `RenderComputeInput::readbackResource`, and positive `payloads` parsing.

**Implementation constraints:**
- Audits may allow these tokens only in named negative tests, docs/specs, and deletion lists.
- Do not fix behavior in this task; establish failing or xfail-ready evidence first.

- [x] Add source-boundary audits for legacy OfflineRT and bake-output tokens.
- [x] Add parser/compiler tests proving old positive `payloads` and single `readbackResource` are still accepted or still present.
- [x] Add assertions that old pass-name special behavior for `OfflinePrimaryRay` must disappear.
- [x] Run:

```bash
cmake --build build --target test_lxe_editor_source_boundary test_render_work_compiler test_vulkan_ibl_bake
ctest --test-dir build --output-on-failure -R "(test_lxe_editor_source_boundary|test_render_work_compiler|test_vulkan_ibl_bake)"
```

Expected before later tasks: relevant audits fail or are marked as known red baseline inside this plan.

---

### Task 2: Hard-Cut RenderPathGraph Compute, Readback, And Bake Schema

**Files:**
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Modify: `src/infra/resource_parsers/render_path_graph_resource_parser.*`
- Modify: `assets/render_paths/bake_environment_ibl.render-path.yaml`
- Modify: `assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml`
- Add: `assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml`
- Test: `src/test/integration/test_render_resource_parsers.cpp`

**Required negative test or audit:**
- Old positive `payloads:` is rejected with a diagnostic that says to use `readbacks:`.
- Unknown `compute` / `readbacks` / `bake` fields fail-fast.
- `compute-dispatch` without `compute.dispatchFrom` fails.

**Implementation constraints:**
- Extend `RenderPassNode`, `FramePass`, and `CompiledFrameGraphPass`; do not add a parallel graph contract.
- `compute` is a nested pass field with only `dispatchFrom` and `localSize`.
- `readbacks` is pass-level and supports raster/compute outputs.
- Bake parameters live in the existing bake render-path YAML, not a new profile asset.

- [x] Add `RenderPassNode::Compute` / `FramePass::Compute` / compiled equivalents.
- [x] Rename/evolve `RenderPathPayloadContract` to `RenderPathReadbackContract`.
- [x] Add `RenderPathGraph::bake` parameter model for environment IBL and standard-pbr BRDF LUT bake graphs.
- [x] Migrate bake render-path YAML from `payloads` to `readbacks` and add `bake` blocks.
- [x] Add OfflineRT standard-pbr render-path graph with `feature.offlineRayTracer`, compute dispatch, and `offline.output` readback.
- [x] Run:

```bash
cmake --build build --target test_render_resource_parsers
ctest --test-dir build --output-on-failure -R test_render_resource_parsers
```

---

### Task 3: Extend FrameGraph Execution Payload And Input Readbacks

**Files:**
- Modify: `src/core/frame_graph/render_input.hpp`
- Modify: `src/core/frame_graph/frame_graph_executor.hpp`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`
- Test: `src/test/integration/test_frame_graph_executor.cpp`

**Required negative test or audit:**
- A pass with multiple `readbacks` produces multiple `RenderInputDesc::Readback` entries.
- A readback with missing binding, target, extent, or placeholder descriptor rejects the desc.
- `RenderComputeInput::readbackResource` no longer exists.

**Implementation constraints:**
- `RenderInputDesc` owns nested `Readback` and `std::vector<Readback> readbacks`.
- `FrameGraphExecutionPayload` is extended in place with `target`, `format`, `kind`, dimensions, and bytes.
- Do not add a second result/output type.

- [x] Add `RenderInputDesc::Readback`.
- [x] Remove `RenderComputeInput::readbackResource`.
- [x] Extend `FrameGraphExecutionPayload`.
- [x] Resolve readback contracts into `RenderInputDesc::readbacks` during preparation.
- [x] Update synthetic executor tests to behave as `FrameGraphExecutor` test doubles only.
- [x] Run:

```bash
cmake --build build --target test_render_work_compiler test_frame_graph_executor
ctest --test-dir build --output-on-failure -R "(test_render_work_compiler|test_frame_graph_executor)"
```

---

### Task 4: Unify RenderFeature Schema, Volatile Values, Resources, And Hit Table

**Files:**
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/infra/resource_parsers/render_feature_resource_parser.*`
- Modify: `src/core/frame_graph/render_feature_shader_validation.*`
- Modify: `assets/effects/*.render-feature.yaml`
- Add: `assets/effects/offline_ray_tracer.render-feature.yaml`
- Test: `src/test/integration/test_render_resource_parsers.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test or audit:**
- Descriptor resources under `parameters` are rejected except valid resource-like parameters such as environment map URI.
- IBL bake outputs under `RenderFeature::resources` are rejected.
- `rayPrograms` as a RenderFeature field is rejected.
- Unsupported resource API/implementation values fail validation.

**Implementation constraints:**
- One `RenderFeature` class and parser path for all features.
- Shader-level IBL features remain `parameters`.
- Pass-level volatile fields use `RenderFeatureVolatileValue` through `RenderWorkBuildContext::Options::featureValues`.
- `resources` is only for feature-declared derived resources such as scene acceleration.
- `hitShaderTable` is pass-level ray feature data.

- [x] Add `RenderFeatureVolatileValue`.
- [x] Extend `RenderFeature` with `resources` and `hitShaderTable`.
- [x] Migrate existing forward/surface/environment volatile values to `featureValues`.
- [x] Add OfflineRT render feature with ray controls, scene acceleration resource requirement, and hit shader table.
- [x] Add parser/reflection validation tests for all strictness rules.
- [x] Run:

```bash
cmake --build build --target test_render_resource_parsers test_render_work_compiler
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler)"
```

---

### Task 5: Extend Material Hit Shader Contract For Standard-PBR

**Files:**
- Modify: `src/core/asset/material.hpp`
- Modify: `src/infra/resource_parsers/material_resource_parser.*`
- Modify: `assets/materials/**/standard-pbr*.material.yaml`
- Add/modify: `assets/shaders/glsl/materials/standard_pbr/*`
- Test: `src/test/integration/test_render_resource_parsers.cpp`
- Test: `src/test/integration/test_shader_compiler.cpp`

**Required negative test or audit:**
- A selected standard-pbr material missing `hit.radiance.uri` fails OfflineRT preparation.
- Unsupported `hit` payload keys fail parser diagnostics.
- A hit shader URI missing from `feature.offlineRayTracer.hitShaderTable` fails preparation.

**Implementation constraints:**
- `hit` is a material field/map, not a RenderFeature field.
- 074-h accepts only payload key `radiance`.
- Hit shader files live under the shared shader directory, not an OfflineRT-only provider.
- Source-text audit checks the hit shader documentation marker and software dispatch switch names stay aligned.

- [x] Parse `hit.radiance.uri` into material resource/model.
- [x] Add standard-pbr hit shader source and documentation marker.
- [x] Add shader compiler coverage for standard-pbr hit shader libs.
- [x] Add parser and preparation negative tests.
- [x] Run:

```bash
cmake --build build --target test_render_resource_parsers test_shader_compiler test_render_work_compiler
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_shader_compiler|test_render_work_compiler)"
```

---

### Task 6: Neutralize RenderWorkBuildContext

**Files:**
- Modify: `src/core/frame_graph/render_work_build_context.hpp`
- Modify: `src/core/frame_graph/render_work_build_context.cpp`
- Modify callers in `src/backend/vulkan/`, `src/core/frame_graph/`, `src/core/scene/`, and tests.
- Test: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test or audit:**
- Pass feature/resource access no longer requires `hasRealtimeScene()`.
- `offline(OfflineRenderJob&)`, `offlineJob()`, `realtimeScene()`, and `realtimeOptions()` have no positive production callers after this task.

**Implementation constraints:**
- Evolve existing class in place.
- `OfflineRT` also receives a `Scene`.
- `Options` owns borrowed runtime facts: `runtimeExtents`, `featureValues`, pass prep facts, render target/camera/visibility.
- `FrameGraphExecutionRequest` does not receive scene/profile/runtime facts.

- [x] Rename/generalize `RealtimeOptions` to `Options`.
- [x] Add nested `RuntimeExtent` and `FeatureValue`.
- [x] Replace source variant with common `Scene` plus `RenderDomain`.
- [x] Add `scene()`, `resourceTable()`, `findRuntimeExtent()`, `findFeatureValue()`, and neutral pass fact access.
- [x] Remove positive callers of old realtime/offline accessors.
- [x] Run:

```bash
cmake --build build --target test_render_work_compiler test_bindless_validation_contract
ctest --test-dir build --output-on-failure -R "(test_render_work_compiler|test_bindless_validation_contract)"
```

---

### Task 7: Extract Shared Scene Participant Selection

**Files:**
- Modify: `src/core/frame_graph/render_input.hpp`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Modify: scene/renderable data helpers as needed.
- Test: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test or audit:**
- `input.kind: compute-dispatch` with object/material filters no longer fails solely because the input is compute.
- Fullscreen passes still reject object/material/geometry filters.
- OfflineRT compute does not create fake `RenderDrawInput` entries just to recover scene data.

**Implementation constraints:**
- Reuse existing `RenderPassInputContract` filter schema.
- Extract neutral `RenderSceneParticipant` from existing draw-selection data.
- Draw preparation builds `RenderDrawInput` from participants plus draw commands.
- OfflineRT preparation consumes participants for scene storage, material buffers, texture arrays, ray table resolution, and acceleration data.

- [x] Extract selected participant data from `buildSceneRenderableInputs(...)`.
- [x] Keep raster draw submission behavior unchanged.
- [x] Enable scene-consuming compute passes to use object/material filters.
- [x] Add tests for mesh/material selection parity between draw and OfflineRT compute.
- [x] Run:

```bash
cmake --build build --target test_render_work_compiler
ctest --test-dir build --output-on-failure -R test_render_work_compiler
```

---

### Task 8: Assemble RayProgramTable From Material And RenderFeature Facts

**Files:**
- Modify: `src/core/frame_graph/render_input.hpp`
- Modify: `src/core/frame_graph/render_work_build_context.hpp`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test or audit:**
- Selected material hit URI not present in the feature hit table rejects pass preparation.
- Duplicate hit table indices or duplicate URI/payload entries reject RenderFeature parsing.
- `PrimitiveHitGroups` authored in YAML is rejected.

**Implementation constraints:**
- `RayProgramTable` is a pass preparation fact.
- `hitShaderTable` is authored on RenderFeature; `PrimitiveHitGroups` is derived prepared data.
- 074-h accepts only `radiance` payload and standard-pbr hit shader positive path.

- [x] Add `RayProgramTable`, `RayHitGroupProgram`, and lowering enum.
- [x] Extend prepared input facts with optional ray table.
- [x] Build ray table from selected participants, material `hit.radiance.uri`, pass shader, and feature hit table.
- [x] Derive hit group indices for selected primitives.
- [x] Add parser/preparation negative tests.
- [x] Run:

```bash
cmake --build build --target test_render_work_compiler test_render_resource_parsers
ctest --test-dir build --output-on-failure -R "(test_render_work_compiler|test_render_resource_parsers)"
```

---

### Task 9: Add Feature-Declared Scene Acceleration Producer

**Files:**
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/frame_graph/render_work_build_context.hpp`
- Add/modify core producer files near render-work preparation or scene-resource-table integration.
- Test: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test or audit:**
- Missing producer for `(SceneAcceleration, SoftwareBvh)` rejects pass preparation.
- Unsupported `(api, implementation)` pair rejects parser/validation.
- Missing generated descriptor for required acceleration resource rejects preparation.

**Implementation constraints:**
- `RenderFeatureDerivedResourceProducer` is reached through an internal closed registry.
- YAML cannot reference arbitrary C++ functions.
- RenderFeature declares the requirement; producer builds it; `SceneResourceTable` owns lifetime/registration.
- Software path produces `SceneBvhNodes` storage buffer; hardware RT remains architecture-only.

- [x] Add `RenderFeatureResourceApi`, `RenderFeatureResourceImplementation`, resource requirement/output model.
- [x] Add `RenderFeatureDerivedResourceProducer` interface, request/result, and closed registry.
- [x] Implement `buildSceneAcceleration` software BVH producer.
- [x] Register/cache produced acceleration buffer in `SceneResourceTable`.
- [x] Return descriptors, dependencies, and optional `PipelineBuildDescExtra`.
- [x] Run:

```bash
cmake --build build --target test_render_work_compiler
ctest --test-dir build --output-on-failure -R test_render_work_compiler
```

---

### Task 10: Prepare OfflineRT Compute Inputs Through RenderWorkCompiler

**Files:**
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Modify: `src/core/frame_graph/render_input.hpp`
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test or audit:**
- `RenderWorkCompiler` has no positive branch reading `context.offlineJob()`, `job.offlineShader`, or `OfflineRenderJob.output`.
- Group counts come from `compute.dispatchFrom` + `RuntimeExtent`, not C++ width/height defaults.
- Multiple readbacks are resolved from pass contracts, not a single compute field.

**Implementation constraints:**
- Compiler does not build BVH directly; it consumes prepared producer results.
- Standard-pbr material/source-material descriptor preparation is reused below the input-kind boundary.
- OfflineRT does not fake draw submissions.

- [x] Resolve compute dispatch extents through `context.findRuntimeExtent(...)`.
- [x] Build OfflineRT scene storage, material, texture, frame parameter, output storage, ray table, and acceleration descriptors.
- [x] Fill `RenderComputeInput` group counts only after runtime extent resolution.
- [x] Fill `RenderInputDesc::Readback` from pass readback contracts.
- [x] Delete old `buildOfflineSceneStorageResources(job)` style code.
- [x] Run:

```bash
cmake --build build --target test_render_work_compiler
ctest --test-dir build --output-on-failure -R test_render_work_compiler
```

---

### Task 11: Implement Software BVH OfflineRT Shaders And Bindings

**Files:**
- Add/modify: `assets/shaders/glsl/render_paths/OfflineRT/**`
- Add/modify: `assets/shaders/glsl/materials/standard_pbr/**`
- Modify: shader build/CMake lists if needed.
- Test: `src/test/integration/test_shader_compiler.cpp`

**Required negative test or audit:**
- Old shader URI under `techniques/OfflineRT/...` is rejected.
- Software dispatch switch/table source-text audit fails if `hitShaderTable` entries and shader dispatch functions drift.
- Standard-pbr hit shader compiles with shared material/source-material ABI.

**Implementation constraints:**
- Shader source lives in the normal shader tree.
- Primary ray shader owns camera rays, sampling, bounce loop, and output write.
- Hit shader computes standard-pbr radiance and returns payload/next-ray data.
- Branch optimization is not in scope; keep architecture compatible with future hardware RT.

- [x] Add OfflineRT primary/ray compute shader and common software-BVH traversal helpers.
- [x] Add standard-pbr radiance hit shader function.
- [x] Add `SceneBvhNodes`, optional `PrimitiveHitGroups`, material/source-material, texture, frame, and output bindings matching reflection.
- [x] Add shader compiler and source-text audit tests.
- [x] Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler
ctest --test-dir build --output-on-failure -R test_shader_compiler
```

---

### Task 12: Add VulkanFrameGraphExecutor Immediate Submit And Readback Mode

**Files:**
- Modify: `src/backend/vulkan/vulkan_frame_graph_executor.hpp`
- Modify: `src/backend/vulkan/vulkan_frame_graph_executor.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.*`
- Modify: `src/backend/vulkan/details/resource_manager.*`
- Test: `src/test/integration/test_frame_graph_executor.cpp`

**Required negative test or audit:**
- Executor rejects readback when target/binding/descriptor/extent is missing.
- Executor does not infer behavior from graph name, pass name, shader URI, or path substring.
- Record-only realtime mode still ignores readback collection unless explicitly configured.

**Implementation constraints:**
- Keep public `FrameGraphExecutor` interface unchanged.
- Add explicit backend execution mode/config such as `RecordOnly` vs `ImmediateSubmitReadback`.
- Reuse existing prepared-input command recording path.
- Readback copies bytes and metadata into `FrameGraphExecutionPayload`.

- [x] Add explicit execution mode/config.
- [x] Implement immediate submit path with resource sync and GPU-to-host barrier.
- [x] Map/copy readback descriptor resources named by `RenderInputDesc::Readback`.
- [x] Preserve realtime record-only behavior.
- [x] Run:

```bash
cmake --build build --target test_frame_graph_executor
ctest --test-dir build --output-on-failure -R test_frame_graph_executor
```

---

### Task 13: Rewire Offline CLI And Image Writer

**Files:**
- Modify: `src/tools/lxe_offline_render/main.cpp`
- Modify: `src/infra/offline/offline_scene_loader.*`
- Modify: `src/infra/offline/offline_image_writer.*`
- Modify/delete old offline support files as needed.
- Test: add/update CLI smoke tests if present.

**Required negative test or audit:**
- CLI no longer constructs `OfflineRenderJob`.
- CLI no longer passes `offlineShader` or shader provider side channel.
- Image writer consumes only `FrameGraphExecutionPayload`.

**Implementation constraints:**
- CLI loads scene + render-path graph + runtime output config.
- CLI builds `RenderWorkBuildContext::offline(scene, options)`, compiles/prepares graph work, executes `FrameGraphExecutor`, then writes selected output payload.
- Output file path does not enter graph/compiled/prepared/executor objects.

- [x] Rewire scene loader to populate/return `Scene` and resource table.
- [x] Rewire CLI to build graph/prepared work and execute borrowed request.
- [x] Update image writer to validate payload metadata and write EXR/PNG.
- [x] Remove CLI dependency on old offline job/shader provider.
- [x] Run available CLI/offline tests, plus:

```bash
cmake --build build --target lxe_offline_render test_render_work_compiler
```

---

### Task 14: Converge IBL Bake Onto Generic Readbacks

**Files:**
- Modify: `src/core/scene/ibl_bake_service.hpp`
- Modify: `src/core/scene/ibl_bake_service.cpp`
- Modify: `src/core/scene/ibl_bake_manifest.*`
- Modify: IBL cache store/writer implementation.
- Modify: `assets/render_paths/bake_environment_ibl.render-path.yaml`
- Modify: `assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml`
- Test: `src/test/integration/test_vulkan_ibl_bake.cpp`
- Test: `src/test/integration/test_frame_graph_executor.cpp`

**Required negative test or audit:**
- Default empty `makeExecutionRequest` is not a positive bake path.
- Cache writer must not infer payload dimensions/media/format from manifests or fixtures.
- Old bake `payloads` does not produce executor outputs.

**Implementation constraints:**
- Keep `IblBakeJobService`, `IblBakeItem`, cache store, activation sink, and job phases.
- IBL service or caller must own graph/compiled/prepared work for the borrowed request lifetime.
- Bake YAML `bake` block projects into `RenderWorkBuildContext::Options::runtimeExtents` and pass facts.
- SH9/KTX2/manifest/cache activation remain bake post-processing.

- [x] Extend IBL request preparation so bake graph parameters produce runtime extents and prepared graph work.
- [x] Add private per-job or captured prepared-work owner for borrowed request lifetime.
- [x] Update cache writer to consume `FrameGraphExecutionPayload` metadata and bytes.
- [x] Keep activation behavior unchanged.
- [x] Run:

```bash
cmake --build build --target test_vulkan_ibl_bake test_frame_graph_executor
ctest --test-dir build --output-on-failure -R "(test_vulkan_ibl_bake|test_frame_graph_executor)"
```

---

### Task 15: Delete Legacy OfflineRT Positive Paths

**Files:**
- Delete or gut positive path:
  - `src/core/offline/offline_render_job.*`
  - `src/core/offline/offline_render_work_graph.*`
  - `src/backend/vulkan/offline/offline_render_graph_executor.*`
  - `src/backend/vulkan/offline/software_compute_offline_integrator.*`
  - old `vulkan_offline_renderer` render logic if still separate
- Modify: CMake lists and includes.
- Test: `src/test/integration/test_lxe_editor_source_boundary.cpp`

**Required negative test or audit:**
- `rg` for old OfflineRT path tokens has no production hits except named negative audits/docs:
  - `OfflineRenderJob`
  - `offlineShader`
  - `createOfflineRenderFrameGraph`
  - `OfflineRenderGraphExecutor`
  - `software_compute_offline_integrator`
  - `techniques/OfflineRT`

**Implementation constraints:**
- Do not keep aliases, wrappers, or compatibility fallback paths.
- If a file remains, it must be private CLI orchestration or negative-test-only; it must not own render logic.

- [x] Remove source files and CMake target references.
- [x] Update includes and tests.
- [x] Tighten source-boundary audit allowlists.
- [x] Run:

```bash
cmake --build build --target test_lxe_editor_source_boundary
ctest --test-dir build --output-on-failure -R test_lxe_editor_source_boundary
rg -n "OfflineRenderJob|offlineShader|createOfflineRenderFrameGraph|OfflineRenderGraphExecutor|software_compute_offline_integrator|techniques/OfflineRT" src assets
```

Expected final `rg`: no production hits.

---

### Task 16: End-To-End Standard-PBR OfflineRT Smoke

**Files:**
- Add/update: OfflineRT smoke scene plus generated Helmet scene fixture coverage.
- Modify: test targets and smoke harness as needed.
- Test: `src/test/integration/test_frame_graph_executor.cpp`
- Test: `src/test/integration/test_shader_compiler.cpp`
- Optional video-device test if Vulkan device is available.

**Required positive evidence:**
- A standard-pbr scene runs through `FrameGraphExecutor`.
- The low-resolution smoke uses a close, deterministic direct-light scene so the image cannot pass as visually black.
- The generated Helmet scene remains a complex standard-pbr asset coverage case, but is not the visibility threshold fixture.
- `offline.output` payload is self-describing and non-empty.
- Output dimensions match `offline.output.resolution`.
- Standard-pbr hit shader table resolves exactly one positive `radiance` hit group.

**Required negative evidence:**
- PBRT/uber/material tags do not satisfy the Helmet OfflineRT path.
- Missing BVH resource, missing material hit URI, or missing readback binding rejects preparation/execution.

**Implementation constraints:**
- The smoke must use the new render-path graph and render feature assets.
- No old offline job/executor/integrator path may be called.

- [x] Add a deterministic direct-light OfflineRT smoke fixture.
- [x] Keep Helmet standard-pbr as complex asset coverage rather than the visibility threshold fixture.
- [x] Verify prepared inputs include compute dispatch, scene descriptors, acceleration descriptor, ray table, and `RenderInputDesc::Readback`.
- [x] Verify executor returns `offline.output`.
- [x] Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler test_render_work_compiler test_frame_graph_executor lxe_offline_render
ctest --test-dir build --output-on-failure -R "(test_shader_compiler|test_render_work_compiler|test_frame_graph_executor)"
```

If a Vulkan device/display is available:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device -R "(offline|frame_graph|ibl)"
```

---

### Task 17: Smoke Direct-Lighting OfflineRT Image Output

**Files:**
- Add/update: direct-light-only OfflineRT smoke scene or generated Helmet fixture.
- Modify: `src/tools/lxe_offline_render/main.cpp` if CLI smoke hooks are needed.
- Modify: test harness under `src/test/integration/` as needed.
- Output artifact: deterministic direct-light OfflineRT image or payload dump in the test temp directory.

**Required positive evidence:**
- The offline ray tracer renders a standard-pbr scene with direct lighting only.
- The smoke uses `FrameGraphExecutor`, `RenderPathGraph`, `feature.offlineRayTracer`, software BVH, standard-pbr material hit shader, and `offline.output` readback.
- The produced image/payload is non-empty, has the requested dimensions, and has enough visible RGB radiance in expected pixels and the center region.

**Required negative evidence:**
- The smoke fails if it reaches `OfflineRenderJob`, old offline integrator/executor code, or `techniques/OfflineRT/...`.
- The smoke fails if direct lighting is replaced by placeholder black/constant output.

**Implementation constraints:**
- This is a direct-lighting smoke, not IBL validation.
- Keep output comparison tolerant and deterministic: use a tiny resolution and a simple light/material/camera setup.
- Store generated output only in test temp/output directories, not committed binary artifacts.

- [x] Add a minimal direct-light standard-pbr scene fixture.
- [x] Add a smoke command/test that invokes the new OfflineRT graph path and captures `offline.output` under `build/test-output/offline_rt_cli_smoke`.
- [x] Assert payload metadata, output files, visible RGB thresholds, lit-pixel count, and center-region radiance.
- [x] Audit that old OfflineRT path tokens are not reached.
- [x] Run:

```bash
cmake --build build --target CompileShaders test_render_work_compiler test_frame_graph_executor lxe_offline_render
ctest --test-dir build --output-on-failure -R "(offline|frame_graph)"
```

If a Vulkan device/display is required:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device -R "offline"
```

---

### Task 18: Smoke IBL Bake Output After OfflineRT Migration

**Files:**
- Modify: `src/test/integration/test_vulkan_ibl_bake.cpp`
- Modify: `src/test/integration/test_offline_rt_cli_smoke.py`
- Modify: IBL bake render-path assets only if final smoke reveals missing readback metadata.
- Output artifact: deterministic IBL bake payloads, summary, and direct-light Helmet OfflineRT image in the build test-output directory.

**Required positive evidence:**
- Environment IBL bake still produces `diffuse_sh9`, `specular_prefilter`, and material `brdf_lut` outputs through `FrameGraphExecutionPayload`.
- Cache write and activation still work.
- The smoke leaves inspectable bake artifacts under `build/test-output/ibl_bake_smoke/neutral/`.
- The smoke summary names the exact manifest and payload paths.
- The Helmet direct-light OfflineRT smoke writes `build/test-output/offline_rt_cli_smoke/helmet_raytrace_direct.*` and verifies visible center/ROI radiance.

**Required negative evidence:**
- Bake does not use old `payloads` as a second executor output system.
- Cache writer does not infer dimensions/media/format from manifests or fixtures.
- Bake activation rejects missing live `scene.environmentBake` / `scene.materialIblBake` resources.
- Helmet ray trace smoke fails if the direct-light ray trace output is black or mostly empty.

**Implementation constraints:**
- This smoke protects IBL from the OfflineRT hard cut.
- Use the same generic readback/executor path as OfflineRT.
- Keep bake file/manifest/KTX2/SH9 behavior in IBL post-processing, outside `FrameGraphExecutor`.

- [x] Run environment and material IBL bake through the new graph/readback path.
- [x] Verify produced payload metadata and cache files.
- [x] Activate baked resources into `SceneResourceTable`.
- [x] Leave inspectable bake artifacts and summary under `build/test-output/ibl_bake_smoke/neutral/`.
- [x] Render Helmet direct-light OfflineRT output and assert visible radiance under `build/test-output/offline_rt_cli_smoke/helmet_raytrace_direct.*`.
- [x] Run:

```bash
cmake --build build --target CompileShaders test_vulkan_ibl_bake test_frame_graph_executor test_shader_compiler lxe_editor
ctest --test-dir build --output-on-failure -R "(test_vulkan_ibl_bake|test_frame_graph_executor|test_shader_compiler)"
```

If a Vulkan device/display is required:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device -R "ibl"
```

Out of scope for 074-h:
- OfflineRT IBL-lit ray tracing. The current ray trace shader path is direct-light standard-pbr only; IBL-lit rendering must not be reported as a ray trace result until the ray trace shader/material hit path explicitly implements it.

---

## Final Verification

Run after all tasks:

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_render_work_compiler test_frame_graph_executor test_vulkan_ibl_bake test_shader_compiler test_lxe_editor_source_boundary lxe_offline_render lxe_editor
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler|test_frame_graph_executor|test_vulkan_ibl_bake|test_shader_compiler|test_lxe_editor_source_boundary)"
rg -n "OfflineRenderJob|offlineShader|createOfflineRenderFrameGraph|OfflineRenderGraphExecutor|software_compute_offline_integrator|techniques/OfflineRT|RenderComputeInput::readbackResource|payloads:" src assets
```

Expected final audit:
- no production hits for old OfflineRT path tokens;
- no positive `payloads:` render-path assets;
- no pass-name/shader-path heuristic for `OfflinePrimaryRay`;
- IBL bake and OfflineRT both produce outputs through `FrameGraphExecutionPayload`;
- realtime paths still use the same `RenderWorkCompiler` and `FrameGraphExecutor` record-only mode.
