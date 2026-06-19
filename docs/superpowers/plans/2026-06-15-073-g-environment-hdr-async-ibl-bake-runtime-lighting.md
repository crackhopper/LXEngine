# 073-g Environment HDR Async IBL Bake And Runtime Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Also use `render-agent-guardrails` before changing rendering architecture code.

**Goal:** Implement `REQ-073-g`: scene-node-driven async environment IBL bake, graph-executed bake work, adjacent baked assets, main-thread activation, dirty-gated upload, and Forward inline IBL runtime lighting.

**Architecture:** Scene nodes decide whether an environment or object participates in IBL bake. Referenced assets describe bake methods; bake work runs through RenderPathGraph / RenderWorkCompiler / FrameGraphExecutor; worker jobs only produce files and events. Main-thread activation loads adjacent baked assets, registers resources, uploads dirty GPU payloads, marks readiness tags, and a per-frame pass-feature policy fills volatile `feature.forwardPass` uniform fields.

**Tech Stack:** C++20, YAML-CPP, Vulkan, RenderPathGraph, FrameGraph, RenderWorkCompiler, SceneResourceTable, shader reflection, GLSL, KTX2/HDR assets, CMake/Ninja integration tests.

---

## Rendering Guardrail Prefix

Use current repo facts only. Read the relevant active requirement, subsystem note,
and current code before changing scope. This task is not a rename-only task.

First identify a negative test, audit, diagnostic, or code fact that proves the
current legacy path, silent fallback, ignored field, placeholder resource, or
mixed renderer graph path can leak through. Then implement the smallest change
that closes that path.

Hard constraints:
- Every parser allowlist field must be consumed into the target model. Unknown, legacy, or not-yet-modeled fields must fail-fast with diagnostics.
- No placeholder payloads may satisfy resource dependencies. A dependency must be truly parsed/registered, or the load/upload path must fail with diagnostics.
- Do not introduce a second public graph/contract system beside RenderPathGraph and RenderPassNode.
- Runtime-only FrameGraph branches are unfinished work unless the branch is explicitly dynamic and has a named owner requirement.
- Shader, RenderFeature, material, graph, and resource dependencies must resolve to live typed payloads before graph export/upload.
- Remove superseded legacy implementation and tests in the same slice.
- Keep verification warning-free for touched targets.

## File Map

New core files:
- `src/core/scene/scene_environment_node.hpp`: environment node and bake marker value types used by scene documents/runtime loaders.
- `src/core/scene/scene_environment_node.cpp`: validation helpers for environment node and `bake.ibl.enabled`.
- `src/core/frame_graph/frame_graph_executor.hpp`: backend-neutral compiled graph execution interface.
- `src/core/scene/ibl_bake_job.hpp`: bake job id, item id, event/status records, thread-safe event queue.
- `src/core/scene/ibl_bake_job.cpp`: event sequencing, status transitions, cancel flag, running-job guard.
- `src/core/scene/ibl_bake_keys.hpp`: normalized environment/material bake keys and dedup result types.
- `src/core/scene/ibl_bake_keys.cpp`: scene-node scan and dedup helpers.
- `src/core/scene/ibl_bake_manifest.hpp`: environment/material/SH9 manifest value types and validation helpers.
- `src/core/scene/ibl_bake_manifest.cpp`: derived mip count, all-or-nothing validation, output path helpers.
- `src/core/scene/environment_ibl_activation.hpp`: main-thread activation and readiness-tag boundary.
- `src/core/scene/environment_ibl_activation.cpp`: cache-hit activation, live resource checks, all-or-nothing readiness marking.
- `src/core/frame_graph/pass_feature_runtime_policy.hpp`: per-frame volatile pass-feature policy interface.
- `src/core/frame_graph/pass_feature_runtime_policy.cpp`: Forward IBL policy that fills volatile pass-feature uniforms.

New infra/backend files:
- `src/infra/resource_parsers/ibl_bake_manifest_parser.hpp`: strict YAML parser/writer API for manifests and SH9 payload.
- `src/infra/resource_parsers/ibl_bake_manifest_parser.cpp`: unknown-field rejection and atomic writer helpers.
- `src/backend/vulkan/vulkan_frame_graph_executor.hpp`: Vulkan implementation of `FrameGraphExecutor`.
- `src/backend/vulkan/vulkan_frame_graph_executor.cpp`: executes compiled graph work through existing Vulkan pass helpers.
- `src/backend/vulkan/vulkan_ibl_bake_payload_exporter.hpp`: writes bake graph outputs to adjacent payload files.
- `src/backend/vulkan/vulkan_ibl_bake_payload_exporter.cpp`: environment light and BRDF payload export from executed bake resources.

New assets:
- `assets/render_paths/bake_environment_ibl.render-path.yaml`
- `assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml`

Modified assets/code:
- `assets/effects/forward_pass.render-feature.yaml`
- `assets/effects/environment_lighting.render-feature.yaml`
- `assets/render_paths/forward_main.render-path.yaml`
- `assets/render_paths/deferred_main.render-path.yaml`
- `assets/shaders/glsl/common/ibl_lighting.glsl`
- `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- `assets/shaders/glsl/render_paths/Deferred/deferred_lighting.frag`
- `src/infra/scene_io/scene_document.hpp`
- `src/infra/scene_io/scene_document.cpp`
- `src/infra/resource_parsers/render_feature_resource_parser.cpp`
- `src/infra/resource_parsers/render_path_graph_resource_parser.cpp`
- `src/core/scene/scene_resource_table.hpp`
- `src/core/scene/scene_resource_table.cpp`
- `src/core/scene/scene_resource_table_upload_view.hpp`
- `src/core/frame_graph/render_work_compiler.cpp`
- `src/core/frame_graph/render_work_build_context.hpp`
- `src/core/frame_graph/render_work_build_context.cpp`
- `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- `src/editor/runtime/scene_runtime.hpp`
- `src/editor/runtime/scene_runtime.cpp`
- `src/editor/commands/lxe_editor_commands.hpp`
- `src/editor/commands/lxe_editor_commands.cpp`
- `src/test/CMakeLists.txt`

Deleted legacy files:
- `src/backend/vulkan/details/ibl_bake_renderer.hpp`
- `src/backend/vulkan/details/ibl_bake_renderer.cpp`

New/modified tests:
- `src/test/integration/test_scene_resource_upload_view_v2.cpp`
- `src/test/integration/test_render_resource_parsers.cpp`
- `src/test/integration/test_render_work_compiler.cpp`
- `src/test/integration/test_shader_compiler.cpp`
- `src/test/integration/test_lxe_editor_source_boundary.cpp`
- `src/test/integration/test_scene_bake_cache.cpp`
- `src/test/integration/test_frame_graph_executor.cpp`
- `src/test/integration/test_environment_ibl_activation.cpp`
- `src/test/integration/test_vulkan_ibl_bake.cpp`

---

### Task 1: Add Baseline Negative Audits For Legacy IBL Paths

**Files:**
- Modify: `src/test/integration/test_lxe_editor_source_boundary.cpp`
- Modify: `src/test/integration/test_shader_compiler.cpp`

**Required negative test or audit:**
- Prove current positive paths still mention `IblBakeRenderer::bakeStaticEnvironment`, `HAS_IBL`, `EnvironmentUBO`, `PrefilteredEnvMap`, `BrdfLut`, `feature.surfaceLighting`, or `feature.iblLighting`.
- Prove Forward/Deferred do not define IBL formulas outside `assets/shaders/glsl/common/ibl_lighting.glsl` after this plan is complete.

**Implementation constraints:**
- Only named negative audits may allow legacy tokens.
- Do not allow ordinary positive tests in `src/test` to depend on old IBL tokens.

- [ ] **Step 1: Add source-boundary audit helpers**

Add a helper in `test_lxe_editor_source_boundary.cpp`:

```cpp
struct LegacyTokenAudit final {
  std::string token;
  std::vector<std::string> allowedSubstrings;
};

bool lineAllowed(const std::string &line,
                 const std::vector<std::string> &allowed) {
  return std::any_of(allowed.begin(), allowed.end(),
                     [&](const std::string &needle) {
                       return line.find(needle) != std::string::npos;
                     });
}
```

- [ ] **Step 2: Add legacy-token audit cases**

Add audits for:

```cpp
const std::vector<LegacyTokenAudit> audits = {
    {"bakeStaticEnvironment", {"test_lxe_editor_source_boundary.cpp",
                               "notes/requirements", "docs/superpowers"}},
    {"IblBakeRenderer", {"test_lxe_editor_source_boundary.cpp",
                         "notes/requirements", "docs/superpowers"}},
    {"HAS_IBL", {"test_lxe_editor_source_boundary.cpp",
                 "notes/requirements", "docs/superpowers"}},
    {"EnvironmentUBO", {"test_lxe_editor_source_boundary.cpp",
                        "notes/requirements", "docs/superpowers"}},
    {"feature.surfaceLighting", {"test_lxe_editor_source_boundary.cpp",
                                 "docs/superpowers"}},
    {"feature.iblLighting", {"test_lxe_editor_source_boundary.cpp",
                             "docs/superpowers"}},
};
```

Expected before implementation: at least `bakeStaticEnvironment`, `HAS_IBL`, and `EnvironmentUBO` fail on production paths.

- [ ] **Step 3: Add shader formula audit**

In `test_shader_compiler.cpp`, add an audit that fails when `evaluateIbl` or `PrefilteredEnvMap` math appears in Forward/Deferred shader files instead of a common include:

```cpp
EXPECT(shaderSource("render_paths/Forward/pbr.frag").find("evaluateIblStandardPbr(") != std::string::npos,
       "Forward should call common IBL helper");
EXPECT(shaderSource("render_paths/Forward/pbr.frag").find("textureLod(PrefilteredEnvMap") == std::string::npos,
       "Forward must not inline prefiltered env formula");
EXPECT(shaderSource("render_paths/Deferred/deferred_lighting.frag").find("common/ibl_lighting.glsl") != std::string::npos,
       "Deferred should include common IBL helper");
```

- [ ] **Step 4: Run the audits and capture expected failures**

```bash
cmake --build build --target test_lxe_editor_source_boundary test_shader_compiler
ctest --test-dir build --output-on-failure -R "(test_lxe_editor_source_boundary|test_shader_compiler)"
```

Expected before later tasks: FAIL on current legacy positive hits.

- [ ] **Step 5: Commit**

```bash
git add src/test/integration/test_lxe_editor_source_boundary.cpp src/test/integration/test_shader_compiler.cpp
git commit -m "add ibl legacy boundary audits"
```

---

### Task 2: Extend Scene YAML With Environment Nodes And Node IBL Bake Markers

**Files:**
- Create: `src/core/scene/scene_environment_node.hpp`
- Create: `src/core/scene/scene_environment_node.cpp`
- Modify: `src/infra/scene_io/scene_document.hpp`
- Modify: `src/infra/scene_io/scene_document.cpp`
- Test: `src/test/integration/test_render_resource_parsers.cpp`

**Required negative test or audit:**
- `scene.environment` must not satisfy the environment node requirement.
- `material.bake.enabled` must be rejected or ignored with diagnostic; object participation is top-level `bake.ibl.enabled`.

**Implementation constraints:**
- Parser allowlist fields must be consumed into `SceneNodeDocument`.
- Unknown environment-node fields fail-fast.
- Environment node is the unified entry for skybox/background and IBL bake source.

- [ ] **Step 1: Add core value types**

Create `scene_environment_node.hpp`:

```cpp
#pragma once

#include "core/resource/resource_metadata.hpp"
#include "core/platform/types.hpp"
#include <optional>
#include <string>

namespace LX_core {

struct SceneIblBakeMarker final {
  bool enabled = false;
};

struct SceneEnvironmentNode final {
  ResourceUri featureUri;
  SceneIblBakeMarker bake;
};

struct SceneNodeBakeMarkers final {
  std::optional<SceneIblBakeMarker> ibl;
};

} // namespace LX_core
```

- [ ] **Step 2: Add document fields**

In `SceneNodeDocument`, add:

```cpp
std::optional<LX_core::SceneEnvironmentNode> environment;
LX_core::SceneNodeBakeMarkers bake;
```

- [ ] **Step 3: Parse environment node**

In `loadNodeDocument(...)`, parse:

```yaml
environment:
  feature:
    uri: assets/effects/environment_lighting.render-feature.yaml
  bake:
    enabled: true
```

Reject missing `environment.feature.uri`, missing `environment.bake.enabled`, and unknown `environment` children with diagnostics naming the YAML path.

- [ ] **Step 4: Parse object bake marker**

Parse top-level:

```yaml
bake:
  ibl:
    enabled: true
```

Reject `material.bake` with a diagnostic:

```text
nodes[].material.bake is not supported; use nodes[].bake.ibl.enabled
```

- [ ] **Step 5: Save YAML**

In `saveNodeDocument(...)`, emit the full `environment.feature.uri` and `environment.bake.enabled` block whenever `node.environment` is present, including `enabled: false`. Emit top-level `bake.ibl.enabled` when `node.bake.ibl` is present; absent object bake marker and explicit `enabled: false` both mean the object does not request IBL bake, but the explicit false round-trips when authored.

- [ ] **Step 6: Add parser tests**

Add cases to `test_render_resource_parsers.cpp`:

```cpp
EXPECT(parsed.rootNode().children[0].environment.has_value(),
       "environment node should parse");
EXPECT(parsed.rootNode().children[0].environment->featureUri ==
       ResourceUri("assets/effects/environment_lighting.render-feature.yaml"),
       "environment node should retain feature uri");
EXPECT(parsed.rootNode().children[1].bake.ibl.has_value() &&
       parsed.rootNode().children[1].bake.ibl->enabled,
       "object node should retain top-level bake.ibl.enabled");
EXPECT_THROWS(parseSceneYaml(yamlWithMaterialBake),
              "nodes[].material.bake is not supported");
EXPECT(!legacySceneEnvironmentSatisfiesEnvironmentNode(document),
       "legacy scene.environment must not satisfy environment node");
```

- [ ] **Step 7: Run tests**

```bash
cmake --build build --target test_render_resource_parsers
ctest --test-dir build --output-on-failure -R test_render_resource_parsers
```

Expected: PASS, with no warnings.

- [ ] **Step 8: Commit**

```bash
git add src/core/scene/scene_environment_node.* src/infra/scene_io/scene_document.* src/test/integration/test_render_resource_parsers.cpp
git commit -m "add environment nodes and ibl bake markers"
```

---

### Task 3: Register Environment Node Feature Payloads Into SceneResourceTable

**Files:**
- Modify: `src/editor/runtime/scene_runtime.cpp`
- Modify: `src/infra/offline/offline_scene_loader.cpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_scene_resource_upload_view_v2.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test or audit:**
- Graph source `feature.environmentLighting` must fail when scene has no environment node.
- Metadata-only RenderFeature must not satisfy `feature.environmentLighting`.

**Implementation constraints:**
- The environment node is the only positive scene entry for environment source.
- No implicit white cubemap when no environment node exists.

- [ ] **Step 1: Add environment-node state to SceneResourceTable**

Add storage/accessors:

```cpp
struct SceneEnvironmentRuntimeState final {
  RenderFeatureHandle feature;
  bool nodePresent = false;
  bool bakeRequested = false;
  u64 generation = 0;
};

void setEnvironmentRuntimeState(SceneEnvironmentRuntimeState state);
std::optional<SceneEnvironmentRuntimeState> environmentRuntimeState() const;
bool hasEnvironmentNode() const;
```

- [ ] **Step 2: Register live RenderFeature for environment nodes**

In scene runtime loading, when a `SceneNodeDocument` has `environment`, load the referenced render-feature asset through the existing resource parser registry and register it via `SceneResourceTable::registerRenderFeature`.

Set `SceneEnvironmentRuntimeState` with:

```cpp
state.nodePresent = true;
state.feature = featureHandle;
state.bakeRequested = node.environment->bake.enabled;
```

- [ ] **Step 3: Validate feature source dependency**

In `RenderWorkCompiler::validateEnvironmentLightingFeatureBindings`, add a check before looking up feature by name:

```cpp
if (!resources.hasEnvironmentNode()) {
  reject(desc, RenderInputDiagnosticCode::MissingResource,
         "feature.environmentLighting requires a scene environment node");
  return;
}
```

- [ ] **Step 4: Add tests**

Add a `test_render_work_compiler` case:

```cpp
EXPECT_REJECTED(descWithoutEnvironmentNode,
                "feature.environmentLighting requires a scene environment node");
EXPECT_ACCEPTED(descWithEnvironmentNodeAndLiveFeature);
```

Add a `test_scene_resource_upload_view_v2` case:

```cpp
SceneResourceTable table;
EXPECT(!table.hasEnvironmentNode(), "new table has no environment node");
table.setEnvironmentRuntimeState({.feature = featureHandle,
                                  .nodePresent = true,
                                  .bakeRequested = true});
EXPECT(table.hasEnvironmentNode(), "environment node state should be visible");
```

- [ ] **Step 5: Run tests**

```bash
cmake --build build --target test_scene_resource_upload_view_v2 test_render_work_compiler
ctest --test-dir build --output-on-failure -R "(test_scene_resource_upload_view_v2|test_render_work_compiler)"
```

- [ ] **Step 6: Commit**

```bash
git add src/editor/runtime/scene_runtime.cpp src/infra/offline/offline_scene_loader.cpp src/core/scene/scene_resource_table.* src/test/integration/test_scene_resource_upload_view_v2.cpp src/test/integration/test_render_work_compiler.cpp
git commit -m "register environment nodes as scene resources"
```

---

### Task 4: Add Volatile Pass-Feature Schema And Forward IBL Runtime Fields

**Files:**
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/infra/resource_parsers/render_feature_resource_parser.cpp`
- Modify: `src/core/frame_graph/render_path_feature_validation.cpp`
- Modify: `src/core/frame_graph/render_feature_shader_validation.cpp`
- Modify: `assets/effects/forward_pass.render-feature.yaml`
- Test: `src/test/integration/test_render_resource_parsers.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`
- Test: `src/test/integration/test_shader_compiler.cpp`

**Required negative test or audit:**
- `volatile: true` parameter with `value` is rejected.
- `volatile: true` parameter with `constantId` or specialization metadata is rejected.
- Volatile fields do not appear in pipeline specialization constants.

**Implementation constraints:**
- Volatile fields are uniform/pass-control data.
- Volatile values are runtime-filled and do not enter `PipelineKey`.
- Do not create `feature.surfaceLighting` or `feature.iblLighting`.

- [ ] **Step 1: Extend RenderFeatureParameter**

Add:

```cpp
bool volatileRuntime = false;
```

Keep serialization naming as `volatile` in YAML, but avoid C++ keyword conflicts by using `volatileRuntime`.

- [ ] **Step 2: Parse volatile fields strictly**

In the parser:

```cpp
parameter.volatileRuntime = yaml["volatile"] && yaml["volatile"].as<bool>();
if (parameter.volatileRuntime && yaml["value"]) {
  diagnostic("volatile parameter must not define value");
}
if (parameter.volatileRuntime && yaml["constantId"]) {
  diagnostic("volatile parameter must not define constantId");
}
```

Also reject any specialization-related keys on volatile parameters.

- [ ] **Step 3: Update Forward pass feature asset**

Add to `assets/effects/forward_pass.render-feature.yaml`:

```yaml
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

- [ ] **Step 4: Exclude volatile fields from specialization constants**

In pass feature specialization collection, skip `parameter.volatileRuntime`.

Add a test:

```cpp
EXPECT(!hasSpecializationConstant(desc, "enableIblLighting"),
       "volatile IBL field must not become specialization constant");
```

- [ ] **Step 5: Validate uniform binding reflection**

Extend render feature shader validation so volatile parameters must satisfy reflected UBO members when the shader declares `PassRuntimeUBO`.

- [ ] **Step 6: Add parser tests**

Cases:

```cpp
EXPECT_REJECTS("volatile with value", yamlWithVolatileValue);
EXPECT_REJECTS("volatile with constantId", yamlWithVolatileConstant);
EXPECT_ACCEPTS("volatile uniform field", yamlWithVolatileUniform);
```

- [ ] **Step 7: Run tests**

```bash
cmake --build build --target test_render_resource_parsers test_render_work_compiler test_shader_compiler
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler|test_shader_compiler)"
```

- [ ] **Step 8: Commit**

```bash
git add src/core/asset/render_effect.hpp src/infra/resource_parsers/render_feature_resource_parser.cpp src/core/frame_graph/render_path_feature_validation.cpp src/core/frame_graph/render_feature_shader_validation.cpp assets/effects/forward_pass.render-feature.yaml src/test/integration/test_render_resource_parsers.cpp src/test/integration/test_render_work_compiler.cpp src/test/integration/test_shader_compiler.cpp
git commit -m "add volatile pass feature fields"
```

---

### Task 5: Add Dirty-Gated Prepared Graph, Work, Descriptor, And Upload State

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.hpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`
- Test: `src/test/integration/test_vulkan_post_process_builder.cpp`

**Required negative test or audit:**
- Static scene/render-path repeated frames must not call `FrameGraph::compile`, `RenderWorkCompiler::buildInputs`, `RenderWorkCompiler::prepare`, descriptor plan rebuild, or unchanged upload prep every frame.

**Implementation constraints:**
- Dirty sources: scene node, resource generation, render feature, render path graph, swapchain target shape, bake activation.
- Per-frame policy can update small volatile UBO data without rebuilding structural work.

- [ ] **Step 1: Add generation counters**

In `SceneResourceTable`:

```cpp
u64 graphGeneration() const;
u64 resourceGeneration() const;
u64 featureGeneration() const;
u64 uploadGeneration() const;
void markFeatureRuntimeDirty();
void markBakedResourceDirty();
```

Increment the right generation in register/update/release methods and bake activation.

- [ ] **Step 2: Add renderer prepared-state key**

In `VulkanRealtimeRenderer`:

```cpp
struct PreparedRenderStateKey final {
  u64 graphGeneration = 0;
  u64 resourceGeneration = 0;
  u64 featureGeneration = 0;
  RenderTargetDesc target;
};

bool operator==(const PreparedRenderStateKey&, const PreparedRenderStateKey&);
```

- [ ] **Step 3: Gate frame graph/work rebuild**

Before rebuilding:

```cpp
if (m_preparedStateKey == nextKey && m_preparedGraphValid) {
  reusePreparedGraphAndWork();
} else {
  rebuildFrameGraphAndRenderInputs();
  m_preparedStateKey = nextKey;
}
```

- [ ] **Step 4: Gate descriptor/upload work**

Add upload generation checks:

```cpp
if (m_uploadedGeneration != resources.uploadGeneration()) {
  uploadDirtySceneResources(resources);
  m_uploadedGeneration = resources.uploadGeneration();
}
```

Do not skip per-frame volatile UBO upload if the policy marks the pass-control buffer dirty.

- [ ] **Step 5: Add instrumentation tests**

Introduce test-only counters in a small helper or renderer diagnostic struct:

```cpp
EXPECT_EQ(report.frameGraphCompileCount, 1u);
EXPECT_EQ(report.renderInputBuildCount, 1u);
EXPECT_EQ(report.descriptorPlanBuildCount, 1u);
EXPECT_EQ(report.unchangedUploadCount, 0u);
```

After mutating a scene node:

```cpp
EXPECT_GT(report.frameGraphCompileCountAfterMutation,
          report.frameGraphCompileCount);
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target test_render_work_compiler test_vulkan_post_process_builder
ctest --test-dir build --output-on-failure -R "(test_render_work_compiler|test_vulkan_post_process_builder)"
```

- [ ] **Step 7: Commit**

```bash
git add src/backend/vulkan/vulkan_realtime_renderer.* src/core/scene/scene_resource_table.* src/test/integration/test_render_work_compiler.cpp src/test/integration/test_vulkan_post_process_builder.cpp
git commit -m "cache prepared render work by generation"
```

---

### Task 6: Introduce FrameGraphExecutor Boundary

**Files:**
- Create: `src/core/frame_graph/frame_graph_executor.hpp`
- Create: `src/backend/vulkan/vulkan_frame_graph_executor.hpp`
- Create: `src/backend/vulkan/vulkan_frame_graph_executor.cpp`
- Test: `src/test/integration/test_frame_graph_executor.cpp`
- Modify: `src/test/CMakeLists.txt`

**Required negative test or audit:**
- Missing source, target, shader, format, or typed payload must be rejected before backend execution.
- Executor must not accept a bake-only hardcoded shader order.

**Implementation constraints:**
- Public boundary is "execute compiled graph work".
- No second public graph/contract system.

- [ ] **Step 1: Add core interface**

```cpp
struct FrameGraphExecutionRequest final {
  const FrameGraph *graph = nullptr;
  const CompiledFrameGraph *compiled = nullptr;
  std::span<const PreparedFramePassWork> preparedPasses;
};

struct FrameGraphExecutionResult final {
  bool ok = false;
  std::vector<std::string> diagnostics;
};

class FrameGraphExecutor {
public:
  virtual ~FrameGraphExecutor() = default;
  virtual FrameGraphExecutionResult execute(const FrameGraphExecutionRequest&) = 0;
};
```

- [ ] **Step 2: Add prepared pass work type**

Define `PreparedFramePassWork` close to `RenderInputDesc`:

```cpp
struct PreparedFramePassWork final {
  StringID passName;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  std::vector<RenderInputDesc> descs;
};
```

- [ ] **Step 3: Implement Vulkan executor facade**

`VulkanFrameGraphExecutor::execute(...)` validates the request before any Vulkan command recording:

```cpp
rejectIfNull(request.graph, "frame graph is required");
rejectIfNull(request.compiled, "compiled graph is required");
rejectIfPreparedWorkMissingForCompiledPass(request);
rejectIfAnyInputDescRejected(request);
rejectIfAnyPassLacksResolvedShaderTargetFormatOrPayload(request);
```

Move the existing per-pass Vulkan recording body out of `VulkanRealtimeRenderer` into an internal helper that records one resolved pass contract plus its prepared `RenderInput` data. Realtime rendering and `VulkanFrameGraphExecutor` both call that helper. The helper dispatches by the compiled pass contract from `RenderPathGraph`; it does not dispatch by bake-specific pass names.

- [ ] **Step 4: Add executor tests**

Create `test_frame_graph_executor.cpp`:

```cpp
EXPECT_REJECTS(executeMissingCompiledGraph(), "compiled graph is required");
EXPECT_REJECTS(executeMissingPreparedPass(), "prepared pass work missing");
EXPECT_REJECTS(executeRejectedInputDesc(), "input desc rejected");
```

- [ ] **Step 5: Register test target**

Add `test_frame_graph_executor` to `TEST_INTEGRATION_EXE_LIST`.

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target test_frame_graph_executor
ctest --test-dir build --output-on-failure -R test_frame_graph_executor
```

- [ ] **Step 7: Commit**

```bash
git add src/core/frame_graph/frame_graph_executor.hpp src/backend/vulkan/vulkan_frame_graph_executor.* src/test/integration/test_frame_graph_executor.cpp src/test/CMakeLists.txt
git commit -m "add compiled frame graph executor boundary"
```

---

### Task 7: Add Async IBL Bake Job Service And Commands

**Files:**
- Create: `src/core/scene/ibl_bake_job.hpp`
- Create: `src/core/scene/ibl_bake_job.cpp`
- Modify: `src/editor/commands/lxe_editor_commands.hpp`
- Modify: `src/editor/commands/lxe_editor_commands.cpp`
- Test: `src/test/integration/test_scene_bake_cache.cpp`
- Modify: `src/test/CMakeLists.txt`

**Required negative test or audit:**
- Worker thread must not mutate UI, `SceneResourceTable`, or GPU resources directly.
- `bake ibl start --force` must be rejected when another job is running.

**Implementation constraints:**
- One IBL bake job at a time.
- Event queue is thread-safe and sequence is monotonic.
- Commands: `bake ibl start`, `bake ibl start --force`, `bake job status <id>`, `bake job logs <id> [since]`, `bake job cancel <id>`.

- [ ] **Step 1: Add job/event types**

```cpp
using BakeJobId = u64;
using BakeItemId = u64;

enum class IblBakeJobPhase {
  Queued, CacheCheck, Filter, WriteCache, ItemComplete, Activate,
  Complete, Failed, ActivationFailed, CancelPending
};

struct IblBakeJobEvent final {
  BakeJobId job = 0;
  BakeItemId item = 0;
  IblBakeJobPhase phase = IblBakeJobPhase::Queued;
  IblBakeJobSeverity severity = IblBakeJobSeverity::Info;
  float progress = 0.0f;
  std::string message;
  std::string fix;
  u64 sequence = 0;
};
```

- [ ] **Step 2: Implement event queue**

Use mutex + vector/deque:

```cpp
class IblBakeEventQueue final {
public:
  IblBakeJobEvent push(IblBakeJobEvent event);
  std::vector<IblBakeJobEvent> drainSince(u64 sequence) const;
private:
  mutable std::mutex m_mutex;
  u64 m_nextSequence = 1;
  std::vector<IblBakeJobEvent> m_events;
};
```

- [ ] **Step 3: Implement running-job guard**

`IblBakeJobService::start(force)`:

```cpp
if (m_runningJob.has_value()) {
  if (force) {
    return StartResult::rejected("bake job already running; cancel it first");
  }
  return StartResult::alreadyRunning(*m_runningJob);
}
```

- [ ] **Step 4: Add command handlers**

Register `bake` command with subcommands and return command text:

```text
bake ibl start -> started bake job 7
bake job status 7 -> phase=cache-check progress=0.25
bake job logs 7 12 -> [13] info ...
bake job cancel 7 -> cancel pending
```

- [ ] **Step 5: Add tests**

In `test_scene_bake_cache.cpp`:

```cpp
EXPECT(start.ok && start.job != 0, "start returns job id");
EXPECT(second.alreadyRunning, "duplicate start returns running job");
EXPECT(forceWhileRunning.rejected, "--force while running is rejected");
EXPECT(events[0].sequence + 1 == events[1].sequence, "sequence is monotonic");
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target test_scene_bake_cache
ctest --test-dir build --output-on-failure -R test_scene_bake_cache
```

- [ ] **Step 7: Commit**

```bash
git add src/core/scene/ibl_bake_job.* src/editor/commands/lxe_editor_commands.* src/test/integration/test_scene_bake_cache.cpp src/test/CMakeLists.txt
git commit -m "add async ibl bake job commands"
```

---

### Task 8: Collect And Deduplicate Scene Bake Items

**Files:**
- Create: `src/core/scene/ibl_bake_keys.hpp`
- Create: `src/core/scene/ibl_bake_keys.cpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_scene_bake_cache.cpp`

**Required negative test or audit:**
- Scene-referenced assets without node bake marker must not create bake work.
- Multiple objects with the same `standard-pbr` type must produce one material bake key.
- Different environment input URI/hash must produce different environment keys.

**Implementation constraints:**
- Scene scan produces bake items, not work commands.
- Environment dedup key: render-feature input URI/hash.
- Material dedup key: material type / BSDF bake model.

- [ ] **Step 1: Define key types**

```cpp
struct EnvironmentIblBakeKey final {
  ResourceUri featureUri;
  ResourceUri environmentMapUri;
  std::string sourceHash;
  bool operator==(const EnvironmentIblBakeKey&) const = default;
};

struct MaterialIblBakeKey final {
  std::string materialType;
  ResourceUri materialSourceUri;
  std::string brdfModel;
  bool operator==(const MaterialIblBakeKey&) const = default;
};
```

- [ ] **Step 2: Define bake item records**

```cpp
enum class IblBakeItemKind { EnvironmentLight, MaterialBrdf };

struct IblBakeItem final {
  BakeItemId id = 0;
  IblBakeItemKind kind;
  std::variant<EnvironmentIblBakeKey, MaterialIblBakeKey> key;
  ResourceUri bakeRenderPathUri;
};
```

- [ ] **Step 3: Implement scene scan**

Scan:
- environment nodes with `environment.bake.enabled == true`;
- object nodes with top-level `bake.ibl.enabled == true`;
- material assets referenced by those object nodes.

Skip unsupported material types with a warning event; do not fail the scene.

- [ ] **Step 4: Add tests**

```cpp
EXPECT_EQ(collect(sceneWithTwoStandardPbrObjects).materialItems.size(), 1u);
EXPECT_EQ(collect(sceneWithTwoDifferentEnvs).environmentItems.size(), 2u);
EXPECT_EQ(collect(sceneWithoutBakeMarkers).items.size(), 0u);
EXPECT_WARNING(collect(sceneWithUnsupportedMaterial), "unsupported material type");
```

- [ ] **Step 5: Run tests**

```bash
cmake --build build --target test_scene_bake_cache
ctest --test-dir build --output-on-failure -R test_scene_bake_cache
```

- [ ] **Step 6: Commit**

```bash
git add src/core/scene/ibl_bake_keys.* src/core/scene/scene_resource_table.* src/test/integration/test_scene_bake_cache.cpp
git commit -m "collect scene ibl bake items"
```

---

### Task 9: Add Strict IBL Manifest Types, Parser, And Atomic Commit

**Files:**
- Create: `src/core/scene/ibl_bake_manifest.hpp`
- Create: `src/core/scene/ibl_bake_manifest.cpp`
- Create: `src/infra/resource_parsers/ibl_bake_manifest_parser.hpp`
- Create: `src/infra/resource_parsers/ibl_bake_manifest_parser.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Test: `src/test/integration/test_scene_bake_cache.cpp`

**Required negative test or audit:**
- Unknown fields, missing payload files, wrong source hash, wrong mip count, wrong SH layout, and metadata-only manifests must be rejected.

**Implementation constraints:**
- Strict parser, no unknown fields.
- `mips = floor(log2(resolution)) + 1`.
- Writes use temp file + atomic rename.

- [ ] **Step 1: Add manifest value types**

Define:

```cpp
struct Sh9IrradiancePayload final {
  std::array<Vec3f, 9> coefficients;
};

struct EnvironmentIblBakeManifest final {
  ResourceUri sourceUri;
  std::string sourceHash;
  u32 specularResolution = 256;
  u32 specularMips = 9;
  std::string specularFormat = "RGBA16Float";
  std::string diffuseBasis = "sh9";
  std::filesystem::path diffuseFile;
  std::filesystem::path specularFile;
};

struct MaterialIblBakeManifest final {
  std::string materialType = "standard-pbr";
  ResourceUri materialSourceUri;
  std::string materialSourceHash;
  std::string brdfModel = "ggx-smith";
  std::string brdfFormat = "RG16Float";
  u32 brdfSize = 256;
  std::filesystem::path brdfFile;
};
```

- [ ] **Step 2: Add validation helpers**

Implement:

```cpp
u32 deriveMipCount(u32 resolution);
ValidationResult validate(const EnvironmentIblBakeManifest&);
ValidationResult validate(const MaterialIblBakeManifest&);
ValidationResult validate(const Sh9IrradiancePayload&);
```

- [ ] **Step 3: Add strict parser/writer**

Reject unknown fields at each YAML level. Diagnostics must name paths such as `bake.specular.mips`.

- [ ] **Step 4: Add atomic commit helper**

```cpp
AtomicCommitResult writeAtomically(const std::filesystem::path &finalPath,
                                   std::string_view contents);
```

Write to sibling temp path, close, rename to final path. If validation fails before rename, keep existing final file intact.

- [ ] **Step 5: Add tests**

Tests:

```cpp
EXPECT_EQ(deriveMipCount(256), 9u);
EXPECT_REJECTS(envManifestWithUnknownField, "unknown field");
EXPECT_REJECTS(envManifestWrongMipCount, "bake.specular.mips");
EXPECT_REJECTS(shWithEightCoefficients, "coefficients must contain 9");
EXPECT_REJECTS(materialManifestWrongBrdfSize, "brdf.size");
EXPECT_KEEPS_OLD_FILE(atomicCommitWithInvalidManifest);
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target test_scene_bake_cache
ctest --test-dir build --output-on-failure -R test_scene_bake_cache
```

- [ ] **Step 7: Commit**

```bash
git add src/core/scene/ibl_bake_manifest.* src/infra/resource_parsers/ibl_bake_manifest_parser.* src/infra/CMakeLists.txt src/test/integration/test_scene_bake_cache.cpp
git commit -m "add strict ibl bake manifests"
```

---

### Task 10: Author Bake Render Paths And Compile Bake Work

**Files:**
- Create: `assets/render_paths/bake_environment_ibl.render-path.yaml`
- Create: `assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml`
- Modify: `src/infra/resource_parsers/render_path_graph_resource_parser.cpp`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Test: `src/test/integration/test_render_resource_parsers.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test or audit:**
- Missing source/target/shader/format/payload in bake graph is rejected.
- Backend must not fill default bake shader order.

**Implementation constraints:**
- RenderPathGraph owns bake pass shape.
- Bake render-path YAML does not express file commit policy.

- [ ] **Step 1: Add environment bake graph**

`bake_environment_ibl.render-path.yaml` declares passes for:
- equirect/cubemap input conversion when needed;
- diffuse SH output;
- prefiltered specular cubemap output.

Each pass declares explicit `shader`, `sources`, `targets`, `rendering.attachments`, and payload resource names.

- [ ] **Step 2: Add material BRDF graph**

`bake_standard_pbr_brdf_lut.render-path.yaml` declares fullscreen/compute BRDF LUT work, RG16Float 256 output, and shader URI.

- [ ] **Step 3: Extend parser validation**

Bake graph must reject:

```text
missing shader
missing output target
missing output format
missing bake payload declaration
metadata-only resource dependency
```

- [ ] **Step 4: Extend RenderWorkCompiler**

Allow bake graph input kinds used by these assets, but keep them typed. Do not branch on pass names such as `"IBLBake"`.

- [ ] **Step 5: Add tests**

```cpp
EXPECT_ACCEPTS(parse("assets/render_paths/bake_environment_ibl.render-path.yaml"));
EXPECT_ACCEPTS(parse("assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml"));
EXPECT_REJECTS(bakeGraphMissingShader, "shader");
EXPECT_REJECTS(bakeGraphMissingPayload, "payload");
EXPECT_REJECTS(metadataOnlyDependency, "typed payload");
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target test_render_resource_parsers test_render_work_compiler
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler)"
```

- [ ] **Step 7: Commit**

```bash
git add assets/render_paths/bake_environment_ibl.render-path.yaml assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml src/infra/resource_parsers/render_path_graph_resource_parser.cpp src/core/frame_graph/render_work_compiler.cpp src/test/integration/test_render_resource_parsers.cpp src/test/integration/test_render_work_compiler.cpp
git commit -m "add graph authored ibl bake paths"
```

---

### Task 11: Execute Vulkan Bake Graphs And Export Adjacent Payloads

**Files:**
- Create: `src/backend/vulkan/vulkan_ibl_bake_payload_exporter.hpp`
- Create: `src/backend/vulkan/vulkan_ibl_bake_payload_exporter.cpp`
- Modify: `src/backend/vulkan/vulkan_frame_graph_executor.cpp`
- Test: `src/test/integration/test_vulkan_ibl_bake.cpp`
- Modify: `src/test/CMakeLists.txt`

**Required negative test or audit:**
- New bake execution must not include or call `src/backend/vulkan/details/ibl_bake_renderer.*`.
- Old positive tests for direct private bake renderer are deleted or rewritten to exercise the graph executor path.
- Bake smoke must reject generated manifests or payload files whose size is zero.
- Bake smoke must reject KTX2 payloads whose header reports zero width, height, or mip count.

**Implementation constraints:**
- Do not wrap, adapt, or rename `IblBakeRenderer`. Missing convolution, prefilter, BRDF, readback, or file-export capability must be implemented in `VulkanFrameGraphExecutor`, bake render-path shaders, or `VulkanIblBakePayloadExporter`.
- Output assets follow source asset adjacent paths.
- No C++ hardcoded public bake pass order.
- An item is not successful until manifest, SH payload, specular cubemap, and BRDF LUT files all pass non-empty payload smoke validation.

- [ ] **Step 1: Add payload exporter API**

```cpp
struct IblBakePayloadExportRequest final {
  IblBakeItem item;
  std::filesystem::path outputDirectory;
  std::span<const GpuResourceRef> graphOutputs;
};

struct IblBakePayloadExportResult final {
  bool ok = false;
  std::vector<std::filesystem::path> files;
  std::vector<std::string> diagnostics;
};
```

- [ ] **Step 2: Export environment light payloads**

Write:

```text
.lxe-ibl/manifest.yaml
.lxe-ibl/diffuse_sh9.yaml
.lxe-ibl/specular_prefilter.ktx2
```

Use atomic commit for manifest and payload files.
After atomic commit, call the same smoke validation helper used by `test_vulkan_ibl_bake.cpp`. Return `ok=false` if any file is missing, zero bytes, or semantically invalid.

- [ ] **Step 3: Export material BRDF payload**

Write near canonical material type/source asset:

```text
.lxe-ibl/manifest.yaml
.lxe-ibl/brdf_lut.ktx2
```

After atomic commit, call the same smoke validation helper used by `test_vulkan_ibl_bake.cpp`. Return `ok=false` if `manifest.yaml` or `brdf_lut.ktx2` is missing, zero bytes, or semantically invalid.

- [ ] **Step 4: Implement graph-owned Vulkan bake output path**

Implement environment-light and BRDF output handling behind the new graph execution/export boundary:

```cpp
FrameGraphExecutionResult graphResult = executor.execute(request);
EXPECT(graphResult.ok, "compiled bake graph must execute");

IblBakePayloadExportResult exportResult =
    exporter.exportPayloads({.item = item,
                             .outputDirectory = outputDirectory,
                             .graphOutputs = graphResult.outputs});
EXPECT(exportResult.ok, "compiled bake graph outputs must export");
```

If a required algorithm currently exists only in the old private renderer, implement that capability in the new bake shader, graph executor, or exporter code in this task. Committed code must not include `ibl_bake_renderer.hpp`, call `bakeStaticEnvironment`, or dispatch bake work by C++ pass order. Shader selection and pass order come from the bake render-path YAML compiled in Task 10.

- [ ] **Step 5: Add generated asset smoke helpers**

`test_vulkan_ibl_bake.cpp`:

```cpp
struct SmokeValidationResult final {
  bool ok = false;
  std::string diagnostic;
};

SmokeValidationResult smokeOk() { return {.ok = true}; }

SmokeValidationResult smokeFail(std::string diagnostic) {
  return {.ok = false, .diagnostic = std::move(diagnostic)};
}

SmokeValidationResult validateNonEmptyFile(const std::filesystem::path &path,
                                           std::string_view label) {
  if (!std::filesystem::exists(path)) {
    return smokeFail(std::string(label) + " must exist");
  }
  if (!std::filesystem::is_regular_file(path)) {
    return smokeFail(std::string(label) + " must be a regular file");
  }
  if (std::filesystem::file_size(path) == 0u) {
    return smokeFail(std::string(label) + " must not be empty");
  }
  return smokeOk();
}

struct Ktx2HeaderSmoke final {
  uint32_t vkFormat = 0;
  uint32_t pixelWidth = 0;
  uint32_t pixelHeight = 0;
  uint32_t faceCount = 0;
  uint32_t levelCount = 0;
};

struct Ktx2SmokeResult final {
  SmokeValidationResult validation;
  Ktx2HeaderSmoke header;
};

Ktx2SmokeResult validateKtx2HeaderSmoke(const std::filesystem::path &path,
                                        std::string_view label) {
  const auto nonEmpty = validateNonEmptyFile(path, label);
  if (!nonEmpty.ok) {
    return {.validation = nonEmpty};
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {.validation = smokeFail(std::string(label) + " must open")};
  }
  std::array<unsigned char, 12> identifier{};
  in.read(reinterpret_cast<char *>(identifier.data()), identifier.size());
  const std::array<unsigned char, 12> expected = {
      0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
  if (identifier != expected) {
    return {.validation = smokeFail(std::string(label) +
                                    " KTX2 identifier must match")};
  }

  Ktx2HeaderSmoke header{};
  uint32_t typeSize = 0;
  uint32_t pixelDepth = 0;
  uint32_t layerCount = 0;
  uint32_t supercompression = 0;
  in.read(reinterpret_cast<char *>(&header.vkFormat), sizeof(uint32_t));
  in.read(reinterpret_cast<char *>(&typeSize), sizeof(uint32_t));
  in.read(reinterpret_cast<char *>(&header.pixelWidth), sizeof(uint32_t));
  in.read(reinterpret_cast<char *>(&header.pixelHeight), sizeof(uint32_t));
  in.read(reinterpret_cast<char *>(&pixelDepth), sizeof(uint32_t));
  in.read(reinterpret_cast<char *>(&layerCount), sizeof(uint32_t));
  in.read(reinterpret_cast<char *>(&header.faceCount), sizeof(uint32_t));
  in.read(reinterpret_cast<char *>(&header.levelCount), sizeof(uint32_t));
  in.read(reinterpret_cast<char *>(&supercompression), sizeof(uint32_t));
  if (!in) {
    return {.validation = smokeFail(std::string(label) +
                                    " KTX2 header must be complete")};
  }
  if (header.pixelWidth == 0u) {
    return {.validation = smokeFail(std::string(label) +
                                    " KTX2 width must be nonzero")};
  }
  if (header.pixelHeight == 0u) {
    return {.validation = smokeFail(std::string(label) +
                                    " KTX2 height must be nonzero")};
  }
  if (header.levelCount == 0u) {
    return {.validation = smokeFail(std::string(label) +
                                    " KTX2 mip count must be nonzero")};
  }
  return {.validation = smokeOk(), .header = header};
}

SmokeValidationResult validateEnvironmentBakePayloads(
    const EnvironmentIblBakeManifest &manifest,
    const std::filesystem::path &outputDir) {
  const auto manifestPath = outputDir / "manifest.yaml";
  const auto diffusePath = outputDir / manifest.diffuseFile;
  const auto specularPath = outputDir / manifest.specularFile;

  if (auto result = validateNonEmptyFile(manifestPath, "environment manifest");
      !result.ok) {
    return result;
  }
  if (auto result = validateNonEmptyFile(diffusePath, "diffuse SH payload");
      !result.ok) {
    return result;
  }
  const auto sh = parseSh9Payload(diffusePath);
  if (!sh.ok) {
    return smokeFail(sh.diagnostic);
  }
  if (sh.value.coefficients.size() != 9u) {
    return smokeFail("SH payload must contain 9 coefficients");
  }
  const bool anyFiniteNonzero = std::any_of(
      sh.value.coefficients.begin(), sh.value.coefficients.end(),
      [](const Vec3f &c) {
        return std::isfinite(c.x) && std::isfinite(c.y) &&
               std::isfinite(c.z) &&
               (std::abs(c.x) + std::abs(c.y) + std::abs(c.z)) > 0.000001f;
      });
  if (!anyFiniteNonzero) {
    return smokeFail("SH payload must contain a finite nonzero coefficient");
  }

  const auto ktx = validateKtx2HeaderSmoke(specularPath,
                                          "specular prefilter payload");
  if (!ktx.validation.ok) {
    return ktx.validation;
  }
  if (ktx.header.faceCount != 6u) {
    return smokeFail("specular prefilter must be a cubemap");
  }
  if (ktx.header.levelCount != manifest.specularMips) {
    return smokeFail("specular mip count must match manifest");
  }
  if (ktx.header.pixelWidth != manifest.specularResolution) {
    return smokeFail("specular width must match manifest");
  }
  return smokeOk();
}

SmokeValidationResult validateMaterialBrdfPayloads(
    const MaterialIblBakeManifest &manifest,
    const std::filesystem::path &outputDir) {
  const auto manifestPath = outputDir / "manifest.yaml";
  const auto brdfPath = outputDir / manifest.brdfFile;

  if (auto result = validateNonEmptyFile(manifestPath, "material manifest");
      !result.ok) {
    return result;
  }
  const auto ktx = validateKtx2HeaderSmoke(brdfPath, "BRDF LUT payload");
  if (!ktx.validation.ok) {
    return ktx.validation;
  }
  if (ktx.header.faceCount != 1u) {
    return smokeFail("BRDF LUT must be a 2D texture");
  }
  if (ktx.header.levelCount != 1u) {
    return smokeFail("BRDF LUT must have one mip level");
  }
  if (ktx.header.pixelWidth != manifest.brdfSize) {
    return smokeFail("BRDF LUT width must match");
  }
  if (ktx.header.pixelHeight != manifest.brdfSize) {
    return smokeFail("BRDF LUT height must match");
  }
  return smokeOk();
}
```

- [ ] **Step 6: Add Vulkan generated asset smoke**

Use a real non-empty environment input such as `assets/env/studio_small_03_2k.hdr` or the canonical environment map referenced by the test render-feature. Assert the source file is non-empty before starting the bake so a broken fixture cannot produce a false failure.

`test_vulkan_ibl_bake.cpp`:

```cpp
EXPECT(validateNonEmptyFile(sourceEnvironmentPath, "source environment").ok,
       "source environment fixture must be non-empty");
EXPECT(runBakeGraph(environmentItem).ok, "environment bake graph executes");
const auto envSmoke =
    validateEnvironmentBakePayloads(environmentManifest, environmentOutputDir);
EXPECT(envSmoke.ok, envSmoke.diagnostic);
EXPECT(runBakeGraph(brdfItem).ok, "BRDF bake graph executes");
const auto brdfSmoke =
    validateMaterialBrdfPayloads(materialManifest, materialOutputDir);
EXPECT(brdfSmoke.ok, brdfSmoke.diagnostic);
```

- [ ] **Step 7: Add empty-output rejection smoke**

`test_vulkan_ibl_bake.cpp`:

```cpp
{
  std::ofstream emptyDiffuse(environmentOutputDir / "diffuse_sh9.yaml",
                             std::ios::binary | std::ios::trunc);
}
const auto emptyDiffuseSmoke =
    validateEnvironmentBakePayloads(environmentManifest, environmentOutputDir);
EXPECT(!emptyDiffuseSmoke.ok, "empty diffuse SH must fail smoke");
EXPECT(emptyDiffuseSmoke.diagnostic.find("diffuse SH payload must not be empty") !=
           std::string::npos,
       "empty diffuse SH diagnostic must name the empty payload");

{
  std::ofstream emptyBrdf(materialOutputDir / "brdf_lut.ktx2",
                         std::ios::binary | std::ios::trunc);
}
const auto emptyBrdfSmoke =
    validateMaterialBrdfPayloads(materialManifest, materialOutputDir);
EXPECT(!emptyBrdfSmoke.ok, "empty BRDF LUT must fail smoke");
EXPECT(emptyBrdfSmoke.diagnostic.find("BRDF LUT payload must not be empty") !=
           std::string::npos,
       "empty BRDF diagnostic must name the empty payload");
```

- [ ] **Step 8: Register and run test**

Add target to `src/test/CMakeLists.txt`.

```bash
cmake --build build --target test_vulkan_ibl_bake
xvfb-run -a ctest --test-dir build --output-on-failure -R test_vulkan_ibl_bake
```

- [ ] **Step 9: Commit**

```bash
git add src/backend/vulkan/vulkan_ibl_bake_payload_exporter.* src/backend/vulkan/vulkan_frame_graph_executor.cpp src/test/integration/test_vulkan_ibl_bake.cpp src/test/CMakeLists.txt
git commit -m "execute ibl bake graphs through frame graph executor"
```

---

### Task 12: Main-Thread Activation, Cache-Hit Resource Checks, And All-Or-Nothing Readiness

**Files:**
- Create: `src/core/scene/environment_ibl_activation.hpp`
- Create: `src/core/scene/environment_ibl_activation.cpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_environment_ibl_activation.cpp`
- Modify: `src/test/CMakeLists.txt`

**Required negative test or audit:**
- Uploading one item must not enable IBL.
- Cache hit must still check live resource existence and dirty upload/descriptor state.
- Any required failure preserves old active IBL state.

**Implementation constraints:**
- Job does not directly enable pass contribution.
- Activation only marks scene/resource readiness tags.
- All-or-nothing for `073-g`.

- [ ] **Step 1: Add readiness state**

```cpp
struct EnvironmentIblReadinessState final {
  bool environmentLightReady = false;
  bool standardPbrBrdfReady = false;
  u64 activeGeneration = 0;
};
```

Expose:

```cpp
EnvironmentIblReadinessState environmentIblReadiness() const;
void markEnvironmentIblReadiness(EnvironmentIblReadinessState state);
void clearPendingEnvironmentIblReadiness();
```

- [ ] **Step 2: Add activation component**

```cpp
class EnvironmentIblActivation final {
public:
  ActivationResult onItemComplete(const IblBakeItem&, SceneResourceTable&);
  ActivationResult tryActivateJob(BakeJobId, SceneResourceTable&);
};
```

- [ ] **Step 3: Implement cache-hit activation semantics**

If disk cache valid:

```cpp
if (!table.hasLiveBakedPayload(item) || table.bakedPayloadDirty(item) ||
    table.descriptorStateDirty(item)) {
  loadRegisterUpload(item);
} else {
  reuseLivePayload(item);
}
```

- [ ] **Step 4: Enforce all-or-nothing**

If any required item fails:

```cpp
return ActivationResult::failedPreservingOldState(...);
```

Do not change active readiness tags.

- [ ] **Step 5: Add tests**

```cpp
EXPECT(!policyEnabledAfterOneItemUpload, "single upload must not enable IBL");
EXPECT(cacheHitMissingLivePayload.loadsAndUploads, "cache hit checks resource layer");
EXPECT(failurePreservesOldGeneration, "old active IBL generation remains");
EXPECT(allItemsReadyMarksReadiness, "all required items activate readiness tags");
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target test_environment_ibl_activation
ctest --test-dir build --output-on-failure -R test_environment_ibl_activation
```

- [ ] **Step 7: Commit**

```bash
git add src/core/scene/environment_ibl_activation.* src/core/scene/scene_resource_table.* src/test/integration/test_environment_ibl_activation.cpp src/test/CMakeLists.txt
git commit -m "activate baked ibl resources on main thread"
```

---

### Task 13: Per-Frame Forward Pass Runtime Policy

**Files:**
- Create: `src/core/frame_graph/pass_feature_runtime_policy.hpp`
- Create: `src/core/frame_graph/pass_feature_runtime_policy.cpp`
- Modify: `src/core/frame_graph/render_work_build_context.hpp`
- Modify: `src/core/frame_graph/render_work_build_context.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Test: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test or audit:**
- No job or activation code directly writes pass enablement.
- Volatile policy writes do not rebuild graph/work/descriptor/upload state.

**Implementation constraints:**
- Forward fields live on `feature.forwardPass`.
- Runtime policy fills volatile uniform/pass-control buffer fields each frame.
- Missing required volatile value fails-fast or conservative false with diagnostic.

- [ ] **Step 1: Add runtime policy interface**

```cpp
struct PassFeatureRuntimeValues final {
  bool enableIblLighting = false;
  bool environmentIblReady = false;
  bool standardPbrIblReady = false;
};

class PassFeatureRuntimePolicy final {
public:
  PassFeatureRuntimeValues evaluateForwardPass(
      const SceneResourceTable &resources,
      const RenderPathGraph &graph) const;
};
```

- [ ] **Step 2: Implement Forward IBL logic**

```cpp
values.enableIblLighting =
    resources.hasEnvironmentNode() &&
    resources.environmentRuntimeState()->bakeRequested &&
    resources.environmentIblReadiness().environmentLightReady &&
    resources.environmentIblReadiness().standardPbrBrdfReady;
values.environmentIblReady = resources.environmentIblReadiness().environmentLightReady;
values.standardPbrIblReady = resources.environmentIblReadiness().standardPbrBrdfReady;
```

- [ ] **Step 3: Hook policy before draw**

Before pass execution, fill/update the pass runtime UBO. Mark only that small buffer dirty; do not invalidate prepared graph/work state.

- [ ] **Step 4: Add tests**

```cpp
EXPECT_FALSE(policy(sceneWithoutEnvironment).enableIblLighting);
EXPECT_FALSE(policy(envNodeWithoutBakeReady).enableIblLighting);
EXPECT_TRUE(policy(envAndBrdfReady).enableIblLighting);
EXPECT_EQ(rebuildCounters.afterPolicyFrame, rebuildCounters.beforePolicyFrame);
```

- [ ] **Step 5: Run tests**

```bash
cmake --build build --target test_render_work_compiler
ctest --test-dir build --output-on-failure -R test_render_work_compiler
```

- [ ] **Step 6: Commit**

```bash
git add src/core/frame_graph/pass_feature_runtime_policy.* src/core/frame_graph/render_work_build_context.* src/backend/vulkan/vulkan_realtime_renderer.cpp src/test/integration/test_render_work_compiler.cpp
git commit -m "fill volatile forward pass feature state"
```

---

### Task 14: Add Common IBL GLSL And Forward Runtime Consumption

**Files:**
- Create: `assets/shaders/glsl/common/ibl_lighting.glsl`
- Modify: `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- Modify: `assets/shaders/glsl/render_paths/Deferred/deferred_lighting.frag`
- Test: `src/test/integration/test_shader_compiler.cpp`

**Required negative test or audit:**
- Forward must not use `HAS_IBL`, `EnvironmentUBO`, `PrefilteredEnvMap`, `BrdfLut`, or hardcoded `iblIntensity` as IBL truth.
- Forward and Deferred must not implement separate IBL formulas.

**Implementation constraints:**
- Forward keeps IBL inside surface pass.
- No `ForwardIblLighting` additive/geometry pass.
- Deferred structural parity only; no Deferred image smoke required.

- [ ] **Step 1: Add common helper**

Create helper and move the existing Forward ambient IBL math into this file. Use the same BRDF terms already used by Forward, but make this common include the only place that samples SH diffuse, prefiltered specular, and BRDF LUT:

```glsl
#ifndef LX_COMMON_IBL_LIGHTING_GLSL
#define LX_COMMON_IBL_LIGHTING_GLSL

struct LxIblStandardPbrInput {
  vec3 N;
  vec3 V;
  vec3 baseColor;
  float metallic;
  float roughness;
  float ao;
};

vec3 evaluateIblStandardPbr(LxIblStandardPbrInput input) {
  float nDotV = max(dot(input.N, input.V), 0.0);
  vec3 f0 = mix(vec3(0.04), input.baseColor, input.metallic);
  vec3 F = fresnelSchlickRoughness(nDotV, f0, input.roughness);
  vec3 kS = F;
  vec3 kD = (vec3(1.0) - kS) * (1.0 - input.metallic);

  vec3 irradiance = lxSampleIblDiffuseSh9(input.N);
  vec3 diffuse = irradiance * input.baseColor;

  vec3 R = reflect(-input.V, input.N);
  float maxMip = float(lxIblSpecularMipCount - 1);
  vec3 prefiltered = textureLod(lxIblSpecularPrefiltered, R,
                                input.roughness * maxMip).rgb;
  vec2 brdf = texture(lxIblBrdfLut, vec2(nDotV, input.roughness)).rg;
  vec3 specular = prefiltered * (F * brdf.x + brdf.y);

  return (kD * diffuse + specular) * input.ao;
}

#endif
```

Define `lxSampleIblDiffuseSh9`, `lxIblSpecularPrefiltered`, `lxIblSpecularMipCount`, and `lxIblBrdfLut` through the same shader ABI path that reflection validates for Forward. `pbr.frag` must call `evaluateIblStandardPbr` and must not keep duplicate texture/SH sampling math.

- [ ] **Step 2: Update Forward shader**

Add:

```glsl
#include "common/ibl_lighting.glsl"

layout(set = 3, binding = X) uniform PassRuntimeUBO {
  int enableIblLighting;
  int environmentIblReady;
  int standardPbrIblReady;
} passRuntime;
```

Use:

```glsl
if (passRuntime.enableIblLighting != 0 &&
    passRuntime.environmentIblReady != 0 &&
    passRuntime.standardPbrIblReady != 0) {
  ambient = evaluateIblStandardPbr(iblInput);
}
```

- [ ] **Step 3: Update Deferred shader**

Include `common/ibl_lighting.glsl` and reserve matching ABI declarations behind current Deferred structural path. Do not require Deferred image smoke.

- [ ] **Step 4: Add shader tests**

```cpp
EXPECT_NO_TOKEN("render_paths/Forward/pbr.frag", "HAS_IBL");
EXPECT_NO_TOKEN("render_paths/Forward/pbr.frag", "EnvironmentUBO");
EXPECT_CONTAINS("render_paths/Forward/pbr.frag", "common/ibl_lighting.glsl");
EXPECT_CONTAINS("render_paths/Deferred/deferred_lighting.frag", "common/ibl_lighting.glsl");
```

- [ ] **Step 5: Run tests**

```bash
cmake --build build --target test_shader_compiler
ctest --test-dir build --output-on-failure -R test_shader_compiler
```

- [ ] **Step 6: Commit**

```bash
git add assets/shaders/glsl/common/ibl_lighting.glsl assets/shaders/glsl/render_paths/Forward/pbr.frag assets/shaders/glsl/render_paths/Deferred/deferred_lighting.frag src/test/integration/test_shader_compiler.cpp
git commit -m "consume baked ibl in forward shader"
```

---

### Task 15: Wire Bake Service To Job Execution, Activation, And Editor Logging

**Files:**
- Modify: `src/core/scene/ibl_bake_job.hpp`
- Modify: `src/core/scene/ibl_bake_job.cpp`
- Modify: `src/editor/runtime/scene_runtime.hpp`
- Modify: `src/editor/runtime/scene_runtime.cpp`
- Modify: `src/editor/commands/lxe_editor_commands.cpp`
- Test: `src/test/integration/test_scene_bake_cache.cpp`

**Required negative test or audit:**
- Worker never touches UI or `SceneResourceTable`.
- Main thread drains item-complete events and activation handles resource loading/upload.

**Implementation constraints:**
- Cache hit emits item-complete and still runs activation resource checks.
- `--force` rebakes and exports.

- [ ] **Step 1: Add job runner dependencies**

Inject:

```cpp
struct IblBakeJobDependencies final {
  FrameGraphExecutor &executor;
  IblBakePayloadExporter &exporter;
  IblBakeEventQueue &events;
};
```

- [ ] **Step 2: Worker algorithm**

```text
collect items
for each item:
  cache-check
  if valid cache and !force:
    emit item-complete
  else:
    compile bake graph
    execute graph
    export payloads atomically
    emit item-complete
emit complete after all item events emitted
```

- [ ] **Step 3: Main-thread drain**

In `SceneRuntime::update(...)`:

```cpp
for (const auto &event : bakeEvents.drainSince(lastSeq)) {
  logToEditor(event);
  if (event.phase == IblBakeJobPhase::ItemComplete) {
    activation.onItemComplete(resolveItem(event.item), resources);
  }
  activation.tryActivateJob(event.job, resources);
}
```

- [ ] **Step 4: Command output**

Command prompt prints:

```text
[bake 7 item 2] cache-hit
[bake 7 item 2] active-resource-ready
[bake 7] complete
```

- [ ] **Step 5: Tests**

```cpp
EXPECT(cacheHitEmitsItemComplete, "cache hit reuses item activation path");
EXPECT(workerDidNotTouchResourceTable, "worker only emits events");
EXPECT(commandLogsContainFixOnFailure, "failure logs include fix");
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target test_scene_bake_cache
ctest --test-dir build --output-on-failure -R test_scene_bake_cache
```

- [ ] **Step 7: Commit**

```bash
git add src/core/scene/ibl_bake_job.* src/editor/runtime/scene_runtime.* src/editor/commands/lxe_editor_commands.cpp src/test/integration/test_scene_bake_cache.cpp
git commit -m "wire ibl bake jobs to activation"
```

---

### Task 16: Remove Default Private Bake Path And Legacy Positive Fixtures

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Delete: `src/backend/vulkan/details/ibl_bake_renderer.hpp`
- Delete: `src/backend/vulkan/details/ibl_bake_renderer.cpp`
- Modify: `src/test/integration/test_lxe_editor_source_boundary.cpp`
- Modify: `src/test/integration/test_shader_compiler.cpp`

**Required negative test or audit:**
- `rg -n "bakeStaticEnvironment|IblBakeRenderer|HAS_IBL|EnvironmentUBO|iblIntensity|ForwardIblLighting" src assets` has no production positive hits.
- `src/backend/vulkan/details/ibl_bake_renderer.*` no longer exists.

**Implementation constraints:**
- This is a hard cut. Do not fix, wrap, adapt, rename, or preserve the legacy bake renderer.
- Missing bake capability must already exist in the new RenderPathGraph / FrameGraphExecutor / exporter path from Tasks 10 and 11 before this task is marked done.
- Do not keep dormant production branches.
- Legacy tokens in tests must be narrow source-boundary audits only; ordinary positive tests must exercise the new graph path.

- [ ] **Step 1: Remove renderer startup bake**

Delete default path in `VulkanRealtimeRenderer` that creates `IblBakeRenderer` and calls `bakeStaticEnvironment`.

- [ ] **Step 2: Delete private bake renderer files**

Delete:

```text
src/backend/vulkan/details/ibl_bake_renderer.hpp
src/backend/vulkan/details/ibl_bake_renderer.cpp
```

Use:

```bash
git rm src/backend/vulkan/details/ibl_bake_renderer.hpp src/backend/vulkan/details/ibl_bake_renderer.cpp
```

Remove includes, CMake/source references if any, and all call sites. Do not introduce replacement classes with the same private-renderer shape.

- [ ] **Step 3: Verify new architecture owns the missing ability**

Before deleting the old files, confirm the replacement capability is covered by the current architecture tests:

```cpp
EXPECT(runBakeGraph(environmentItem).ok,
       "environment bake must run through FrameGraphExecutor");
EXPECT(validateEnvironmentBakePayloads(environmentManifest,
                                       environmentOutputDir).ok,
       "environment bake must export non-empty assets");
EXPECT(runBakeGraph(brdfItem).ok,
       "BRDF bake must run through FrameGraphExecutor");
EXPECT(validateMaterialBrdfPayloads(materialManifest, materialOutputDir).ok,
       "BRDF bake must export non-empty assets");
```

If one of these fails because the old renderer was the only implementation, add the missing work to Task 10 or Task 11 implementation, not to this task.

- [ ] **Step 4: Update audits**

Tighten allowlist so production files cannot mention:

```text
IblBakeRenderer
bakeStaticEnvironment
HAS_IBL
EnvironmentUBO
feature.surfaceLighting
feature.iblLighting
ForwardIblLighting
```

- [ ] **Step 5: Run rg audit**

```bash
rg -n "IblBakeRenderer|bakeStaticEnvironment|renderPath: IBLBake|ibl_prefilter_env|ibl_brdf_lut|HAS_IBL|EnvironmentUBO|iblIntensity|ForwardIblLighting|feature\\.surfaceLighting|feature\\.iblLighting" src assets docs notes
```

Expected: no `src` or `assets` production hit for `IblBakeRenderer` or `bakeStaticEnvironment`; no old shader/runtime-token production hit. Allowed hits are docs, historical requirements, or the named source-boundary audit.

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target test_vulkan_ibl_bake test_lxe_editor_source_boundary test_shader_compiler
xvfb-run -a ctest --test-dir build --output-on-failure -R test_vulkan_ibl_bake
ctest --test-dir build --output-on-failure -R "(test_lxe_editor_source_boundary|test_shader_compiler)"
```

- [ ] **Step 7: Commit**

```bash
git add src/backend/vulkan/vulkan_realtime_renderer.cpp src/test/integration/test_lxe_editor_source_boundary.cpp src/test/integration/test_shader_compiler.cpp
git add -u src/backend/vulkan/details
git commit -m "hard cut private ibl bake renderer"
```

---

### Task 17: Add End-To-End Forward IBL Smoke

**Files:**
- Modify/Create: `assets/scenes/generated/helmet_standard_pbr.scene.yaml`
- Test: `src/test/integration/test_vulkan_ibl_bake.cpp`
- Test: `src/test/integration/test_lxe_editor_render_debug_dump.cpp`

**Required negative test or audit:**
- Scene without environment node does not enable environment lighting.
- Scene with environment node but `bake.enabled=false` can show background but does not enable surface IBL.
- Scene with successful bake activates IBL only after all required items are ready.

**Implementation constraints:**
- Forward is the image-producing acceptance path.
- Bloom remains screen-space pass.
- No scene reload or pipeline rebuild on bake activation.

- [ ] **Step 1: Add smoke scene variant**

Create or update a generated scene with:

```yaml
- nodeName: studio_env
  name: studio_env
  environment:
    feature:
      uri: assets/effects/environment_lighting.render-feature.yaml
    bake:
      enabled: true
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

- [ ] **Step 2: Run command flow in smoke**

Smoke sequence:

```text
bake ibl start
bake job status <id>
bake job logs <id>
render debug dump hdr.color artifacts/ibl-after.exr
```

- [ ] **Step 3: Assert behavior**

Assertions:

```cpp
EXPECT(noSceneReloadDuringBake);
EXPECT(noPipelineRebuildAfterActivation);
EXPECT(iblRuntimePolicyEnabledAfterAllItemsReady);
EXPECT(renderDumpChangedAfterActivation);
EXPECT(bloomPassStillScreenSpace);
```

- [ ] **Step 4: Run smoke**

```bash
cmake --build build --target test_vulkan_ibl_bake test_lxe_editor_render_debug_dump lxe_editor
xvfb-run -a ctest --test-dir build --output-on-failure -R "(test_vulkan_ibl_bake|test_lxe_editor_render_debug_dump)"
```

- [ ] **Step 5: Commit**

```bash
git add assets/scenes/generated/helmet_standard_pbr.scene.yaml src/test/integration/test_vulkan_ibl_bake.cpp src/test/integration/test_lxe_editor_render_debug_dump.cpp
git commit -m "add forward ibl bake smoke"
```

---

### Task 18: Update Docs, Run Full Verification, And Close Plan

**Files:**
- Modify: `notes/concepts/material/pass-rendering-flow.md`
- Modify: `notes/concepts-design/architecture.md`
- Modify: `notes/requirements/073-g-environment-hdr-async-ibl-bake-and-runtime-lighting.md`
- Modify: `docs/superpowers/specs/2026-06-15-073-g-environment-hdr-async-ibl-bake-runtime-lighting-design.md` if implementation discovers minor drift

**Required negative test or audit:**
- Docs must not describe `feature.surfaceLighting`, `feature.iblLighting`, default `IblBakeRenderer::bakeStaticEnvironment`, or legacy `scene.environment` as positive paths.

**Implementation constraints:**
- Current docs describe implemented behavior only.
- Future paths name their owner requirement.

- [ ] **Step 1: Update notes**

Document:
- environment node as skybox/background and bake entry;
- pass-feature volatile uniform fields;
- cache-hit activation resource checks;
- dirty-gated graph/work/descriptor/upload state.

- [ ] **Step 2: Run notes build**

```bash
scripts/notes/serve_site.sh --build
```

Expected: build completes. Existing unrelated MkDocs warnings may remain; new warnings from touched files must be fixed.

- [ ] **Step 3: Run build/test suite**

```bash
cmake --build build --target \
  test_render_resource_parsers \
  test_scene_resource_upload_view_v2 \
  test_render_work_compiler \
  test_scene_bake_cache \
  test_frame_graph_executor \
  test_environment_ibl_activation \
  test_shader_compiler \
  test_lxe_editor_source_boundary \
  lxe_editor
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_scene_resource_upload_view_v2|test_render_work_compiler|test_scene_bake_cache|test_frame_graph_executor|test_environment_ibl_activation|test_shader_compiler|test_lxe_editor_source_boundary)"
xvfb-run -a ctest --test-dir build --output-on-failure -R "(test_vulkan_ibl_bake|test_lxe_editor_render_debug_dump)"
```

- [ ] **Step 4: Run final rg audit**

```bash
rg -n "IblBakeRenderer|bakeStaticEnvironment|renderPath: IBLBake|HAS_IBL|EnvironmentUBO|iblIntensity|ForwardIblLighting|feature\\.surfaceLighting|feature\\.iblLighting|scene\\.environment" src assets docs notes
```

Expected:
- no production positive path uses removed tokens;
- docs mentions are historical, negative audit, or explicitly owned by finished/active requirements;
- `scene.environment` may exist only as legacy parser/rejection/history, not positive runtime input.

- [ ] **Step 5: Commit docs and final verification updates**

```bash
git add notes docs/superpowers/specs notes/requirements
git commit -m "document graph driven environment ibl bake"
```

---

## Self-Review

Spec coverage:
- Scene node extension: Tasks 2 and 3.
- Compiled graph executor foundation: Task 6.
- Async worker and event queue: Task 7.
- Bake item scan/dedup/cache: Task 8.
- Asset layout and manifests: Task 9.
- Bake render paths: Task 10.
- Graph execution and export: Task 11.
- Loading/upload/policy/all-or-nothing activation: Tasks 12 and 13.
- Forward runtime lighting and Deferred structural parity: Task 14.
- Commands/logging: Task 15.
- Legacy closure: Task 16.
- Smoke and docs: Tasks 17 and 18.

Known scope decisions:
- `073-g` implements Forward pass-feature volatile fields on `feature.forwardPass`.
- Deferred keeps common GLSL/ABI structural parity; a complete `feature.deferredPass` runtime path is not required in this slice.
- Partial per-material activation is not implemented; required item failure preserves old active IBL state.
- Cache hit skips bake/export only; activation still checks resource dirty state and uploads when needed.
