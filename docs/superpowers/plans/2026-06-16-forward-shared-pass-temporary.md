# Forward Shared Pass Temporary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Collapse default realtime background and post-processing behavior into the logical Forward/Deferred rendering stages instead of separate FrameGraph passes, while keeping shared shader code for Forward and GBuffer/Deferred paths.

**Architecture:** A render-path schema pass is a logical pass. The compiler may split it into multiple draw inputs by material/filter, but those inputs share the same FramePass target, attachments, and depth context. Background geometry is a scene renderable accepted by the Forward pass; tone mapping/gamma is a common shader function called by Forward and DeferredLighting shaders through a feature-backed UBO.

**Tech Stack:** C++20, Vulkan FrameGraph, LXEngine RenderPathGraph YAML, GLSL common shader includes, CTest/Ninja.

---

### Task 1: Lock The Graph Shape

**Files:**
- Modify: `src/test/integration/test_render_resource_parsers.cpp`
- Modify: `src/test/integration/test_shader_compiler.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`

- [x] **Step 1: Write failing graph tests**

Update default graph tests so:
- `forward_main.render-path.yaml` has only `Shadow`, `Forward`, `DebugOverlay`.
- `forward_bloom.render-path.yaml` is no longer a separate default graph shape for Bloom passes; either remove it from the default live shader payload audit or make it match the same pass list.
- `deferred_main.render-path.yaml` has only `Shadow`, `Deferred`, `DeferredLighting`, `DebugOverlay`.
- No default graph contains `EnvironmentBox`, `SkyboxBackground`, `PostProcess`, `BloomThreshold`, `BloomBlurH`, or `BloomBlurV`.
- `Forward` includes `feature.toneMapping` and `feature.environmentLighting` in sources, accepts `environment-box` material, and writes `swapchain.color` plus `depth.main`.
- `DeferredLighting` includes `feature.toneMapping` in sources and writes `swapchain.color`.

- [x] **Step 2: Run RED parser test**

Run:

```bash
cmake --build build --target test_render_resource_parsers && ./build/src/test/test_render_resource_parsers
```

Expected now: FAIL because the current graph still declares split background/post/bloom passes.

- [x] **Step 3: Write shader contract tests**

Update shader compiler tests so:
- `Forward/pbr.frag` reflects `ToneMappingUBO` or equivalent scene-level tone mapping feature binding.
- `Deferred/deferred_lighting.frag` reflects the same binding.
- Post/bloom shaders are not required as default-path contracts.
- `Environment/environment_box.frag` remains a scene-renderable shader and includes shared environment helper code.

- [x] **Step 4: Run RED shader test**

Run:

```bash
cmake --build build --target test_shader_compiler && ./build/src/test/test_shader_compiler
```

Expected now: FAIL until Forward and DeferredLighting call common tone mapping functions and expose the feature UBO.

### Task 2: Move Shared Shader Functions To Common Includes

**Files:**
- Modify: `assets/shaders/glsl/common/tone_mapping.glsl`
- Modify: `assets/shaders/glsl/common/environment_lighting.glsl`
- Modify: `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- Modify: `assets/shaders/glsl/render_paths/Deferred/deferred_lighting.frag`
- Modify: `assets/shaders/glsl/render_paths/Environment/environment_box.frag`

- [x] **Step 1: Add common post parameters and function**

Add a common `ToneMappingParams` struct and `lxApplyToneMapping()` helper to `common/tone_mapping.glsl`. It must support:
- enable flag
- exposure
- tone mapping mode
- gamma

- [x] **Step 2: Use common function in Forward**

In `Forward/pbr.frag`, include `common/tone_mapping.glsl`, bind the feature UBO, and apply `lxApplyToneMapping()` immediately before writing `outColor`.

- [x] **Step 3: Use common function in DeferredLighting**

In `Deferred/deferred_lighting.frag`, include `common/tone_mapping.glsl`, bind the same feature UBO, and apply `lxApplyToneMapping()` immediately before writing `outColor`.

- [x] **Step 4: Keep environment logic shared**

Ensure `Environment/environment_box.frag` keeps using `common/environment_lighting.glsl`; any yaw/sample direction helper that must be reused later should be moved into that common file instead of duplicated in pass shaders.

- [x] **Step 5: Compile shaders**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler && ./build/src/test/test_shader_compiler
```

Expected: shader compiler contracts pass.

### Task 3: Add Tone Mapping As A Scene Feature Resource

**Files:**
- Modify: `src/core/scene/ibl_environment.hpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/scene/scene.cpp`
- Modify: `src/core/asset/shader_binding_ownership.hpp`
- Modify: `assets/effects/tone_mapping.render-feature.yaml`

- [x] **Step 1: Add `ToneMappingData` GPU resource**

Create a small `IGpuResource` UBO with binding name `ToneMappingUBO`. It should carry `enabled`, `exposure`, `toneMappingMode`, and `gamma` in a 16-byte aligned layout.

- [x] **Step 2: Register tone mapping feature**

Teach `SceneResourceTable::registerRenderFeatureResources()` to detect `feature == "toneMapping"`, parse its parameters, and keep a live `ToneMappingData` resource.

- [x] **Step 3: Expose the resource to scene-level descriptors**

Append `ToneMappingData` from `Scene::getSceneLevelResources()` in both target-camera and explicit-camera overloads.

- [x] **Step 4: Mark binding system-owned**

Add `ToneMappingUBO` to system-owned binding validation so material instances do not need to own it.

- [x] **Step 5: Parse feature values strictly enough**

Update `tone_mapping.render-feature.yaml` to include explicit `enabled`, `mode`, `exposure`, and `gamma` values. Keep parser behavior consistent with current render-feature parameter envelopes.

### Task 4: Collapse Default Render Path Assets

**Files:**
- Modify: `assets/render_paths/forward_main.render-path.yaml`
- Modify: `assets/render_paths/forward_bloom.render-path.yaml`
- Modify: `assets/render_paths/deferred_main.render-path.yaml`
- Modify: `assets/render_paths/deferred_bloom.render-path.yaml`

- [x] **Step 1: Forward graph**

Remove `EnvironmentBox`, `SkyboxBackground`, and `PostProcess` passes. Make `Forward`:
- input material types include `environment-box`
- sources include `feature.toneMapping` and `feature.environmentLighting`
- attachments target `swapchain.color` and `depth.main`
- targets include `swapchain.color` and `depth.main`

- [x] **Step 2: Forward bloom graph**

Remove `BloomThreshold`, `BloomBlurH`, `BloomBlurV`, and `PostProcess` from the default graph. Keep the same pass shape as Forward until a future debug/feature path reintroduces bloom intentionally.

- [x] **Step 3: Deferred graphs**

Remove `EnvironmentBox`, `SkyboxBackground`, `PostProcess`, and Bloom passes. Make `DeferredLighting` write `swapchain.color` and include `feature.toneMapping`.

- [x] **Step 4: Run parser GREEN**

Run:

```bash
cmake --build build --target test_render_resource_parsers && ./build/src/test/test_render_resource_parsers
```

Expected: parser tests pass with the collapsed pass counts.

### Task 5: Runtime Uses Forward Pass For Background Geometry

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/core/frame_graph/pass.hpp`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Delete or leave unused only if tests prove no production reference: `assets/shaders/glsl/render_paths/Skybox/skybox_background.frag`

- [x] **Step 1: Environment box material uses `Pass_Forward`**

Change runtime finite box material creation so its pass definition is keyed by `Pass_Forward`, not `Pass_EnvironmentBox`.

- [x] **Step 2: Remove default pass validation references**

Remove `Pass_EnvironmentBox` and `Pass_SkyboxBackground` from the default expected pass sets and target assignment logic.

- [x] **Step 3: Keep draw inputs in one logical pass**

If ordering is needed, sort or build scene-renderable inputs so `environment.box` draw inputs are submitted before ordinary opaque draw inputs within the same `Forward` pass. Do not create a new FramePass.

- [x] **Step 4: Remove finiteBox rejection from skybox fullscreen path**

Once no default path uses `SkyboxBackground`, remove the special finiteBox fullscreen rejection or leave it only as a negative legacy audit if a test names it that way.

### Task 6: Verify End To End

**Files:**
- No new source files unless needed by test failures.

- [x] **Step 1: Build core targets**

Run:

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_shader_compiler test_render_work_compiler test_render_path_graph_pass_contract lxe_editor
```

- [x] **Step 2: Run tests**

Run:

```bash
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_shader_compiler|test_render_work_compiler|test_render_path_graph_pass_contract|test_scene_resource_upload_view_v2|test_helmet_standard_pbr_realtime_smoke)"
```

- [x] **Step 3: Realtime render smoke**

Run:

```bash
python3 src/tools/lxe_realtime_render/lxe_realtime_render.py --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml --profile preview --xvfb --require-nonblack --require-pipeline-metadata --project-name codex_forward_shared_pass --editor build/src/editor/lxe_editor
```

Expected: no fallback, accepted draw inputs include both helmet and finite box under the `Forward` pass.

- [ ] **Step 4: Commit, push, and deploy editor**

Commit only this task's files, push, then use lxe_manager MCP:
- stop editor if running
- repo pull
- build `lxe_editor`
- start editor
- load `assets/scenes/generated/helmet_standard_pbr.scene.yaml`
- report build info and camera state.

---

## Temporary Scope Notes

- This plan hard-cuts default realtime `PostProcess`/Bloom fullscreen passes from Forward/Deferred default paths. Debug color-transfer paths can keep their own debug fullscreen passes because those are explicitly debug paths.
- Deferred remains multi-pass because GBuffer plus DeferredLighting is intrinsic to that path. Shared functions must live in common shader includes so Forward and DeferredLighting use the same tone mapping behavior.
- Future bloom should return as an explicit debug/feature path with clear intermediate exports, not as a hidden default pass chain.
