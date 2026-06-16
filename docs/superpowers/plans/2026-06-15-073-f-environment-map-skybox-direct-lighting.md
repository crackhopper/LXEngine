# 073-f Environment Map Skybox Direct Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> Required rendering guardrail: use current repo facts only and apply `render-agent-guardrails`. This plan executes `REQ-073-f`; do not implement `REQ-073-g` surface lighting or `REQ-073-i` full PostProcess hard cut in this slice.

**Goal:** Move visible EnvMap skybox/background rendering onto RenderPathGraph + RenderFeature + SceneResourceTable, with constant-color and texture environments using one feature-owned `SkyboxMap` resource path.

**Architecture:** `feature.environmentLighting` owns both the EnvMap URI and shader-visible parameters. Forward and Deferred graph assets add a graph-authored fullscreen/background pass that writes `hdr.color` and uses `depth.main` as a read-only depth attachment. Backend code consumes graph/feature/resource facts and must not inject a manual skybox material or scene-side environment fallback.

**Tech Stack:** C++20, YAML render assets, GLSL shaders under `assets/shaders/glsl`, RenderPathGraph, RenderFeature, SceneResourceTable, FrameGraph, Vulkan realtime renderer, CMake/Ninja tests.

---

## Implementation Units

- `assets/effects/environment_lighting.render-feature.yaml`: new feature-owned EnvMap URI and background parameters.
- `assets/render_paths/forward_main.render-path.yaml` and `assets/render_paths/deferred_main.render-path.yaml`: graph-authored SkyboxBackground pass.
- `assets/shaders/glsl/common/environment_lighting.glsl`: shared EnvMap feature ABI helpers.
- `assets/shaders/glsl/render_paths/Skybox/skybox_background.vert` and `assets/shaders/glsl/render_paths/Skybox/skybox_background.frag`: graph shader URI for visible background.
- `src/core/asset/render_effect.hpp`, `src/infra/resource_parsers/render_feature_resource_parser.cpp`, `src/test/integration/test_render_resource_parsers.cpp`: RenderFeature parameter schema and parser coverage.
- `src/core/asset/render_effect.hpp`, `src/infra/resource_parsers/render_pass_node_parser.cpp`, `src/core/frame_graph/frame_graph*.{hpp,cpp}`, `src/test/integration/test_render_path_graph_pass_contract.cpp`, `src/test/integration/test_render_resource_parsers.cpp`: attachment usage contract and FrameGraph validation.
- `src/core/frame_graph/render_work_compiler.*`, `src/core/frame_graph/scene_descriptor_resource_resolver.*`, `src/test/integration/test_render_work_compiler.cpp`: feature-to-shader binding validation and descriptor input contract.
- `src/core/scene/scene_resource_table.*`, `src/infra/resource_parsers/render_resource_scene_parser_adapters.cpp`, `src/editor/runtime/scene_runtime.cpp`, `src/test/integration/test_scene_resource_upload_view_v2.cpp`: live `SkyboxMap` resource registration from feature resource parameter.
- `src/backend/vulkan/vulkan_post_process_builder.*`, `src/backend/vulkan/vulkan_realtime_renderer.cpp`, `src/test/integration/test_vulkan_post_process_builder.cpp`, `src/test/integration/test_lxe_editor_render_debug_dump.cpp`: removal of manual positive skybox helper path and Vulkan smoke/debug coverage.

---

## Task 1: Baseline Audit And Negative Test Map

**Purpose:** Freeze the known legacy leaks before changing behavior.

**Files:**
- Read: `notes/requirements/073-f-environment-map-skybox-direct-lighting.md`
- Read: `docs/superpowers/specs/2026-06-15-073-f-environment-map-skybox-direct-lighting-design.md`
- Read: `src/backend/vulkan/vulkan_post_process_builder.cpp`
- Read: `src/editor/runtime/scene_runtime.cpp`
- Read: `assets/shaders/glsl/skybox.frag`
- Modify: `docs/superpowers/plans/2026-06-15-073-f-environment-map-skybox-direct-lighting.md` only if the audit finds a required task missing.

**Required negative evidence:**
- `createSkyboxBackgroundMaterial` is currently a positive rendering path.
- `scene.environment` / `ambientColor` / `ambientIntensity` cannot remain a positive EnvMap source for this REQ.
- root shader URI `skybox` / `assets/shaders/glsl/skybox.*` must not remain the graph-authored pass URI.

- [ ] **Step 1: Run the baseline audit**

```bash
rg -n "createSkyboxBackgroundMaterial|scene\\.environment|ambientColor|ambientIntensity|shader: skybox|assets/shaders/glsl/skybox|SkyboxMap" src assets notes docs
```

Expected: hits exist in legacy code/docs/tests. Record which hits are legacy implementation, negative tests, docs, or future-owner docs.

- [ ] **Step 2: Identify the first failing test for each leak**

Use this mapping before editing production code:

| Leak | First failing test/audit to add |
|---|---|
| manual skybox material helper still renders | `test_lxe_editor_render_debug_dump` or `test_vulkan_post_process_builder` rejects positive helper use |
| scene-side environment fields satisfy EnvMap | `test_render_resource_parsers` rejects or proves `scene.environment` cannot satisfy `feature.environmentLighting` |
| missing `environmentMap.uri` creates placeholder | `test_render_resource_parsers` / `test_scene_resource_upload_view_v2` rejects missing live `SkyboxMap` |
| root skybox shader URI remains positive | `test_shader_compiler` reflects `render_paths/Skybox/skybox_background`, and rg audit rejects `shader: skybox` |

- [ ] **Step 3: Commit checkpoint**

No commit is required if this task only records audit output. If a missing task is added to this plan, commit the plan-only change:

```bash
git add docs/superpowers/plans/2026-06-15-073-f-environment-map-skybox-direct-lighting.md
git commit -m "docs: expand 073-f skybox implementation audit"
```

---

## Task 2: Extend RenderFeature Parameter Schema For EnvMap Resources And UBO Members

**Purpose:** Make `effects/environment_lighting.render-feature.yaml` expressive enough to own `environmentMap.uri` plus shader UBO member metadata.

**Files:**
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/infra/resource_parsers/render_feature_resource_parser.cpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`
- Check: `assets/effects/tone_mapping.render-feature.yaml`

**Required negative test:**
- Unknown RenderFeature parameter fields still fail-fast.
- Missing `kind` still fails.
- `environmentMap` with `uri` but no live resource validation is accepted only as parsed feature data here; live payload validation is Task 9.

- [ ] **Step 1: Add parser tests before implementation**

Add focused tests to `src/test/integration/test_render_resource_parsers.cpp`:

```cpp
// Required test names:
// - testRenderFeatureParsesBindingMemberRequiredSchema
// - testRenderFeatureParsesTextureCubeUriParameter
// - testRenderFeatureRejectsUnknownParameterField
```

The positive fixture must include:

```yaml
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  color:
    kind: vec3
    value: [0.08, 0.08, 0.10]
    binding: EnvironmentLightingUBO
    member: color
    required: true
```

Assertions:
- `environmentMap.kind == "textureCube"`
- `environmentMap.uri == ResourceUri("builtin:env/white_cube")`
- `environmentMap.valueType == "linear-radiance"`
- `environmentMap.binding == "SkyboxMap"`
- `environmentMap.required == true`
- `color.binding == "EnvironmentLightingUBO"`
- `color.member == "color"`

- [ ] **Step 2: Run tests and confirm failure**

```bash
cmake --build build --target test_render_resource_parsers
ctest --test-dir build --output-on-failure -R test_render_resource_parsers
```

Expected before implementation: parser rejects `binding`, `member`, or `required` as unsupported parameter fields, or the new struct fields are absent.

- [ ] **Step 3: Extend the core model**

In `src/core/asset/render_effect.hpp`, extend `RenderFeatureParameter`:

```cpp
struct RenderFeatureParameter final {
  std::string kind;
  std::string value;
  ResourceUri uri;
  std::string valueType;
  std::string binding;
  std::string member;
  bool required = false;
};
```

- [ ] **Step 4: Extend parser allowlist and storage**

In `render_feature_resource_parser.cpp`, accept and store only these parameter fields:

```text
kind, value, uri, valueType, binding, member, required
```

Rules:
- `required` must be a boolean scalar.
- `uri` is allowed only when `kind` is a resource-like kind such as `textureCube`; invalid combinations produce diagnostics.
- Unknown fields still produce `unsupported render feature parameter field`.

- [ ] **Step 5: Run verification**

```bash
cmake --build build --target test_render_resource_parsers
ctest --test-dir build --output-on-failure -R test_render_resource_parsers
```

Expected: parser tests pass without new warnings.

- [ ] **Step 6: Commit checkpoint**

```bash
git add src/core/asset/render_effect.hpp src/infra/resource_parsers/render_feature_resource_parser.cpp src/test/integration/test_render_resource_parsers.cpp
git commit -m "feat: add render feature binding schema"
```

---

## Task 3: Add EnvironmentLighting Feature Asset

**Purpose:** Introduce the authoring surface for visible background EnvMap resources and parameters.

**Files:**
- Create: `assets/effects/environment_lighting.render-feature.yaml`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

**Required negative test:**
- Missing `environmentMap.uri` is rejected by the environment feature validation test added in this task.
- `skyboxEnabled` is rejected as an unknown field; `visibleInBackground` is the only background visibility control in 073-f.

- [ ] **Step 1: Add asset parser coverage**

Add a test to `src/test/integration/test_render_resource_parsers.cpp` named:

```cpp
testEnvironmentLightingRenderFeatureAssetParses()
```

Assertions:
- The asset loads through `SceneResourceParserRegistry`.
- `feature == "environmentLighting"`.
- Parameters include exactly `environmentMap`, `color`, `intensity`, `rotation`, `visibleInBackground`.
- No parameter named `skyboxEnabled`, `ambientColor`, or `ambientIntensity` exists.

- [ ] **Step 2: Create the feature asset**

Create `assets/effects/environment_lighting.render-feature.yaml`:

```yaml
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  color:
    kind: vec3
    value: [0.08, 0.08, 0.10]
    binding: EnvironmentLightingUBO
    member: color
    required: true
  intensity:
    kind: float
    value: 1.0
    binding: EnvironmentLightingUBO
    member: intensity
    required: true
  rotation:
    kind: float
    value: 0.0
    binding: EnvironmentLightingUBO
    member: rotation
    required: true
  visibleInBackground:
    kind: bool
    value: true
    binding: EnvironmentLightingUBO
    member: visibleInBackground
    required: true
```

- [ ] **Step 3: Add missing-uri negative fixture**

Add a parser test named:

```cpp
testEnvironmentLightingFeatureRejectsMissingEnvironmentMapUri()
```

The fixture must define `environmentMap` without `uri`. Expected diagnostic contains:

```text
parameters.environmentMap.uri
```

- [ ] **Step 4: Run verification**

```bash
cmake --build build --target test_render_resource_parsers
ctest --test-dir build --output-on-failure -R test_render_resource_parsers
```

- [ ] **Step 5: Commit checkpoint**

```bash
git add assets/effects/environment_lighting.render-feature.yaml src/test/integration/test_render_resource_parsers.cpp
git commit -m "feat: add environment lighting feature asset"
```

---

## Task 4: Add RenderPathGraph Attachment Usage Contract

**Purpose:** Let graph YAML express read-only depth attachment usage without C++ pass-name special cases.

**Files:**
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/infra/resource_parsers/render_pass_node_parser.cpp`
- Modify: `src/test/integration/test_render_path_graph_pass_contract.cpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

**Required negative test:**
- A pass declaring `attachmentUsage: depth-attachment-read-only` and also listing the same depth resource in `targets` is rejected.
- Unknown `attachmentUsage` value is rejected.

- [ ] **Step 1: Add parser tests first**

Add tests:

```cpp
// testRenderPathGraphParsesAttachmentUsage
// testRenderPathGraphRejectsUnknownAttachmentUsage
// testRenderPathGraphRejectsReadOnlyDepthAttachmentInTargets
```

Positive YAML fixture:

```yaml
rendering:
  mode: dynamic
  attachments:
    - target: hdr.color
      format: RGBA16Float
      samples: 1
      layers: 1
      attachmentUsage: color-attachment-write
    - target: depth.main
      format: D32Float
      samples: 1
      layers: 1
      depth: true
      attachmentUsage: depth-attachment-read-only
sources: [feature.environmentLighting, depth.main]
targets: [hdr.color]
```

- [ ] **Step 2: Extend the model**

Add an enum and field in `src/core/asset/render_effect.hpp`:

```cpp
enum class RenderPathAttachmentUsage {
  ColorAttachmentWrite,
  DepthAttachmentReadOnly,
  DepthAttachmentWrite,
  DepthAttachmentReadWrite,
};

struct RenderPathAttachmentContract final {
  std::string target;
  ImageFormat format = ImageFormat::BGRA8;
  u32 samples = 1;
  u32 layers = 1;
  bool depth = false;
  RenderPathAttachmentUsage attachmentUsage =
      RenderPathAttachmentUsage::ColorAttachmentWrite;
};
```

Defaulting rule:
- color attachment without explicit usage defaults to `ColorAttachmentWrite`;
- depth attachment without explicit usage defaults to `DepthAttachmentWrite` to preserve current graph assets;
- explicit usage is required for read-only depth.

- [ ] **Step 3: Parse and validate attachmentUsage**

In `render_pass_node_parser.cpp`:
- add `attachmentUsage` to the attachment field allowlist;
- parse the four exact string values;
- reject `color-attachment-write` when `depth: true`;
- reject `depth-*` usage when `depth` is false;
- reject read-only depth usage when the same target appears in `targets`.

- [ ] **Step 4: Include usage in pass signatures**

In `src/core/frame_graph/frame_graph.cpp`, update `framePassAttachmentSignature()` so pipeline identity changes when attachment usage changes:

```text
;usage=<numeric-or-string-attachment-usage>
```

- [ ] **Step 5: Run verification**

```bash
cmake --build build --target test_render_path_graph_pass_contract test_render_resource_parsers
ctest --test-dir build --output-on-failure -R "(test_render_path_graph_pass_contract|test_render_resource_parsers)"
```

- [ ] **Step 6: Commit checkpoint**

```bash
git add src/core/asset/render_effect.hpp src/infra/resource_parsers/render_pass_node_parser.cpp src/core/frame_graph/frame_graph.cpp src/test/integration/test_render_path_graph_pass_contract.cpp src/test/integration/test_render_resource_parsers.cpp
git commit -m "feat: add render path attachment usage"
```

---

## Task 5: Teach FrameGraph And Pipeline Build About Read-Only Depth Attachments

**Purpose:** Preserve attachment read/write intent through FrameGraph build and backend pipeline desc without treating read-only depth as a writable target.

**Files:**
- Modify: `src/core/frame_graph/frame_graph_build_plan.cpp`
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`
- Modify: `src/core/pipeline/pipeline_build_desc.hpp`
- Modify: `src/core/pipeline/pipeline_build_desc.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`

**Required negative test:**
- Same-resource source+target is rejected unless the attachment usage is explicit read-write.
- `SkyboxBackground` with `depth.main` as source and read-only attachment compiles without a duplicate depth write.

- [ ] **Step 1: Add FrameGraph validation tests**

Add tests named:

```cpp
// testFrameGraphAllowsReadOnlyDepthAttachmentSource
// testFrameGraphRejectsReadOnlyAttachmentAsWriteTarget
// testFrameGraphAllowsExplicitDepthReadWriteSameResource
```

Expected behavior:
- read-only depth attachment appears in `FramePass.attachments`;
- it does not appear in `FramePass.writes`;
- it may appear in `FramePass.reads` with empty binding name because fixed-function depth is not sampled;
- read-write depth appears in reads and writes only when usage is `depth-attachment-read-write`.

- [ ] **Step 2: Convert render path attachments into FramePass facts**

In `frame_graph_build_plan.cpp`, keep the current `sources` -> `reads` and `targets` -> `writes` conversion, then add validation that cross-checks attachments:

```text
depth-attachment-read-only:
  target must be present in sources
  target must not be present in targets
depth-attachment-write:
  target must be present in targets
depth-attachment-read-write:
  target must be present in sources and targets
```

- [ ] **Step 3: Preserve usage into pipeline-facing data**

Ensure `PipelineBuildDesc` receives the full `RenderPathAttachmentContract` list including usage. Do not derive read-only depth from pass name or shader URI.

- [ ] **Step 4: Update backend dynamic rendering setup**

In `vulkan_realtime_renderer.cpp`, when constructing render pass/framebuffer/dynamic rendering state:
- read-only depth uses load operation and depth write disabled;
- writable depth keeps current write layout/transition behavior;
- depth layout/access transitions use attachment usage, not pass name.

- [ ] **Step 5: Run verification**

```bash
cmake --build build --target test_render_resource_parsers test_render_work_compiler lxe_editor
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler)"
```

- [ ] **Step 6: Commit checkpoint**

```bash
git add src/core/frame_graph src/core/pipeline src/backend/vulkan/vulkan_realtime_renderer.cpp src/test/integration/test_render_resource_parsers.cpp src/test/integration/test_render_work_compiler.cpp
git commit -m "feat: preserve read-only depth attachment usage"
```

---

## Task 6: Move Skybox Shader To RenderPath URI And Add Common Environment ABI

**Purpose:** Replace the root `skybox` shader URI with graph-owned shader files and shared environment binding declarations.

**Files:**
- Create: `assets/shaders/glsl/common/environment_lighting.glsl`
- Create: `assets/shaders/glsl/render_paths/Skybox/skybox_background.vert`
- Create: `assets/shaders/glsl/render_paths/Skybox/skybox_background.frag`
- Modify: `src/test/integration/test_shader_compiler.cpp`
- Check: `assets/shaders/glsl/skybox.vert`
- Check: `assets/shaders/glsl/skybox.frag`

**Required negative test/audit:**
- Positive graph assets no longer use `shader: skybox`.
- Shader reflection for `render_paths/Skybox/skybox_background` exposes `SkyboxMap` and `EnvironmentLightingUBO`.

- [ ] **Step 1: Add shader reflection test first**

In `test_shader_compiler.cpp`, add a test that compiles:

```text
assets/shaders/glsl/render_paths/Skybox/skybox_background.vert
assets/shaders/glsl/render_paths/Skybox/skybox_background.frag
```

Expected reflected bindings:

| Binding | Type | Expected use |
|---|---|---|
| `CameraUBO` | uniform buffer | camera matrices |
| `SkyboxMap` | texture cube | feature-owned EnvMap |
| `EnvironmentLightingUBO` | uniform buffer | color/intensity/rotation/visibleInBackground |

- [ ] **Step 2: Create common environment include**

Create `assets/shaders/glsl/common/environment_lighting.glsl` with the shared ABI names:

```glsl
#ifndef LXE_COMMON_ENVIRONMENT_LIGHTING_GLSL
#define LXE_COMMON_ENVIRONMENT_LIGHTING_GLSL

struct EnvironmentLightingParams {
    vec3 color;
    float intensity;
    float rotation;
    int visibleInBackground;
};

vec3 lxeApplyEnvironmentRadiance(vec3 sampleRadiance,
                                 EnvironmentLightingParams params) {
    return sampleRadiance * params.color * max(params.intensity, 0.0);
}

#endif
```

Keep binding declarations in the pass shader so reflection still sees concrete descriptor names.

- [ ] **Step 3: Create graph-owned skybox shaders**

Create the render path shader files under `assets/shaders/glsl/render_paths/Skybox/`.

Rules:
- use fullscreen triangle input;
- sample `SkyboxMap`;
- apply `EnvironmentLightingUBO.color/intensity`;
- obey `visibleInBackground`;
- do not compute surface BRDF lighting;
- do not write depth.

- [ ] **Step 4: Keep or remove root skybox files deliberately**

If `assets/shaders/glsl/skybox.*` still serve bake/debug tests, keep them as legacy-only files and update tests so positive graph coverage uses the new URI. If no positive path needs them, remove them and update CMake/shader compilation inputs.

- [ ] **Step 5: Run verification**

```bash
cmake --build build --target CompileShaders test_shader_compiler
ctest --test-dir build --output-on-failure -R test_shader_compiler
rg -n "shader: skybox|compileProgram\\(.*skybox\\.vert|compileProgram\\(.*skybox\\.frag" assets src/test src
```

Expected: no positive render-path asset uses `shader: skybox`; any root skybox shader compile coverage is named legacy or removed.

- [ ] **Step 6: Commit checkpoint**

```bash
git add assets/shaders/glsl/common/environment_lighting.glsl assets/shaders/glsl/render_paths/Skybox src/test/integration/test_shader_compiler.cpp
git commit -m "feat: add graph skybox shader ABI"
```

---

## Task 7: Add SkyboxBackground Pass To Forward And Deferred Graph Assets

**Purpose:** Make visible skybox/background rendering graph-authored in both realtime render paths.

**Files:**
- Modify: `assets/render_paths/forward_main.render-path.yaml`
- Modify: `assets/render_paths/deferred_main.render-path.yaml`
- Modify: `assets/render_paths/forward_bloom.render-path.yaml`
- Modify: `assets/render_paths/deferred_bloom.render-path.yaml`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`
- Modify: `src/test/integration/test_render_path_graph_pass_contract.cpp`

**Required negative test:**
- A graph missing `feature.environmentLighting` while declaring `SkyboxBackground` is rejected.
- A skybox pass that lists `depth.main` in `targets` with read-only usage is rejected.

- [ ] **Step 1: Add graph parser tests first**

Add tests:

```cpp
// testForwardGraphIncludesSkyboxBackgroundPass
// testDeferredGraphIncludesSkyboxBackgroundPass
// testSkyboxBackgroundRequiresEnvironmentLightingFeature
// testSkyboxBackgroundRejectsDepthTargetWrite
```

Expected pass contract:

```yaml
- id: SkyboxBackground
  stage: raster
  dispatch: fullscreen
  shader: render_paths/Skybox/skybox_background
  input:
    kind: fullscreen-triangle
  rendering:
    mode: dynamic
    attachments:
      - target: hdr.color
        format: RGBA16Float
        samples: 1
        layers: 1
        attachmentUsage: color-attachment-write
      - target: depth.main
        format: D32Float
        samples: 1
        layers: 1
        depth: true
        attachmentUsage: depth-attachment-read-only
  sources: [feature.environmentLighting, depth.main]
  targets: [hdr.color]
  renderState:
    cullMode: None
    depthTest: true
    depthWrite: false
    depthOp: LessEqual
    blendEnable: false
```

- [ ] **Step 2: Add feature dependency to each graph**

Add:

```yaml
features:
  toneMapping:
    uri: effects/tone_mapping.render-feature.yaml
  environmentLighting:
    uri: effects/environment_lighting.render-feature.yaml
```

- [ ] **Step 3: Place pass in graph order**

Place `SkyboxBackground`:
- after `Forward` in forward graphs;
- after `DeferredLighting` in deferred graphs;
- before `PostProcess`;
- before debug overlay passes.

Do not add a new pass phase in this task. Current graph build maps graph-authored passes to `FrameGraphPhase::Material`; declaration order and resource dependencies provide the current ordering.

- [ ] **Step 4: Run verification**

```bash
cmake --build build --target test_render_resource_parsers test_render_path_graph_pass_contract
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_path_graph_pass_contract)"
```

- [ ] **Step 5: Commit checkpoint**

```bash
git add assets/render_paths src/test/integration/test_render_resource_parsers.cpp src/test/integration/test_render_path_graph_pass_contract.cpp
git commit -m "feat: declare skybox background graph pass"
```

---

## Task 8: Validate Environment Feature Against Shader Reflection And RenderWorkCompiler Inputs

**Purpose:** Make `feature.environmentLighting` satisfy shader descriptors only when YAML parameter names, resource binding, UBO binding and shader reflection agree.

**Files:**
- Modify: `src/core/frame_graph/render_work_compiler.hpp`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Modify: `src/core/frame_graph/scene_descriptor_resource_resolver.hpp`
- Modify: `src/core/frame_graph/scene_descriptor_resource_resolver.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`
- Modify: `src/test/integration/test_shader_compiler.cpp`

**Required negative test:**
- Shader requires `EnvironmentLightingUBO.color`, but feature YAML lacks `color`: rejected.
- Shader requires `SkyboxMap`, but feature YAML lacks `environmentMap.uri`: rejected.
- Feature binding name typo such as `SkyboxTexture` does not satisfy `SkyboxMap`.

- [ ] **Step 1: Add RenderWorkCompiler tests first**

Add tests:

```cpp
// testRenderWorkCompilerAcceptsEnvironmentLightingFeatureBindings
// testRenderWorkCompilerRejectsMissingEnvironmentMapParameter
// testRenderWorkCompilerRejectsEnvironmentBindingNameMismatch
// testRenderWorkCompilerRejectsMissingEnvironmentUboMember
```

Expected diagnostics include concrete names:

```text
feature.environmentLighting
SkyboxMap
EnvironmentLightingUBO.color
```

- [ ] **Step 2: Resolve feature dependencies by graph source name**

When a pass source is `feature.environmentLighting`, map it to the graph feature dependency slot `environmentLighting` and require a live `RenderFeature` payload from `SceneResourceTable`.

- [ ] **Step 3: Validate texture resource parameter**

Rules:
- parameter `environmentMap.kind` must be `textureCube`;
- `environmentMap.binding` must equal reflected binding `SkyboxMap`;
- `environmentMap.uri` must be non-empty;
- this task validates the feature contract; Task 9 validates live GPU/resource payload.

- [ ] **Step 4: Validate UBO member parameters**

Rules:
- required reflected members in `EnvironmentLightingUBO` must exist in feature parameters;
- `binding` and `member` must match reflection names;
- kind mismatch such as `intensity.kind: vec3` rejects prepare.

- [ ] **Step 5: Run verification**

```bash
cmake --build build --target test_render_work_compiler test_shader_compiler
ctest --test-dir build --output-on-failure -R "(test_render_work_compiler|test_shader_compiler)"
```

- [ ] **Step 6: Commit checkpoint**

```bash
git add src/core/frame_graph/render_work_compiler.* src/core/frame_graph/scene_descriptor_resource_resolver.* src/test/integration/test_render_work_compiler.cpp src/test/integration/test_shader_compiler.cpp
git commit -m "feat: validate environment feature shader bindings"
```

---

## Task 9: Register Live SkyboxMap From Feature URI

**Purpose:** Ensure `environmentMap.uri` creates a real `SkyboxMap` payload, including `builtin:env/white_cube`, and no placeholder descriptor can satisfy the pass.

**Files:**
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/infra/resource_parsers/render_resource_scene_parser_adapters.cpp`
- Modify: `src/editor/runtime/scene_runtime.cpp`
- Modify: `src/test/integration/test_scene_resource_upload_view_v2.cpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

**Required negative test:**
- Missing `environmentMap.uri` does not create a default cubemap.
- Metadata-only or failed texture resource does not satisfy `SkyboxMap`.
- Legacy `scene.environment.uri` does not satisfy `feature.environmentLighting`.

- [ ] **Step 1: Add resource tests first**

Add tests:

```cpp
// testEnvironmentFeatureBuiltinWhiteCubeRegistersLiveSkyboxMap
// testEnvironmentFeatureMissingUriDoesNotRegisterSkyboxMap
// testSceneEnvironmentUriDoesNotSatisfyFeatureEnvironmentDependency
// testFailedEnvironmentTextureDoesNotSatisfySkyboxMap
```

- [ ] **Step 2: Add built-in white cubemap payload**

Implement `builtin:env/white_cube` as a live 1x1 cubemap resource:
- all six faces are white linear radiance;
- resource binding name is `SkyboxMap`;
- resource type is texture cube, not a special metadata placeholder.

- [ ] **Step 3: Route texture URIs through the same path**

For HDR/EXR and KTX2 cubemap URIs:
- register the payload under the same descriptor binding `SkyboxMap`;
- keep KTX2 cubemap handling limited to the current `VK_FORMAT_R16G16B16A16_SFLOAT` subset documented by the requirement;
- do not add BasisU or general material KTX2 support in this task.

- [ ] **Step 4: Remove scene-side satisfaction**

Update scene/runtime code so `scene.environment` may be parsed only as legacy/migration data and cannot satisfy `feature.environmentLighting.parameters.environmentMap.uri`.

- [ ] **Step 5: Run verification**

```bash
cmake --build build --target test_scene_resource_upload_view_v2 test_render_resource_parsers
ctest --test-dir build --output-on-failure -R "(test_scene_resource_upload_view_v2|test_render_resource_parsers)"
```

- [ ] **Step 6: Commit checkpoint**

```bash
git add src/core/scene src/infra/resource_parsers/render_resource_scene_parser_adapters.cpp src/editor/runtime/scene_runtime.cpp src/test/integration/test_scene_resource_upload_view_v2.cpp src/test/integration/test_render_resource_parsers.cpp
git commit -m "feat: register skybox map from environment feature"
```

---

## Task 10: Remove Manual Skybox Material Positive Path

**Purpose:** Ensure default realtime rendering cannot bypass RenderPathGraph with `createSkyboxBackgroundMaterial()`.

**Files:**
- Modify: `src/backend/vulkan/vulkan_post_process_builder.hpp`
- Modify: `src/backend/vulkan/vulkan_post_process_builder.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_vulkan_post_process_builder.cpp`
- Modify: `src/test/integration/test_lxe_editor_render_debug_dump.cpp`

**Required negative test/audit:**
- Default positive path has no call to `createSkyboxBackgroundMaterial`.
- If the helper remains, tests prove it is not reachable from default realtime graph execution.

- [ ] **Step 1: Add or update tests first**

Add assertions:
- compiled frame graph pass names include `SkyboxBackground` when environment background is enabled;
- no debug dump path reports a manually injected skybox material;
- positive post-process builder tests do not call `createSkyboxBackgroundMaterial`.

- [ ] **Step 2: Remove or demote the helper**

Preferred outcome:
- delete `createSkyboxBackgroundMaterial()` if no remaining negative test needs it.

Allowed fallback:
- keep it only behind a named negative audit and remove all positive production call sites.

- [ ] **Step 3: Update realtime initialization**

In `vulkan_realtime_renderer.cpp`, remove skybox material injection from initialization. The renderer should use the graph-authored `SkyboxBackground` pass and feature/resource descriptor facts.

- [ ] **Step 4: Run verification**

```bash
cmake --build build --target test_vulkan_post_process_builder test_lxe_editor_render_debug_dump lxe_editor
ctest --test-dir build --output-on-failure -R "(test_vulkan_post_process_builder|test_lxe_editor_render_debug_dump)"
rg -n "createSkyboxBackgroundMaterial" src assets docs notes
```

Expected rg result:
- no production positive call sites;
- remaining hits, if any, are named negative audits or historical docs.

- [ ] **Step 5: Commit checkpoint**

```bash
git add src/backend/vulkan/vulkan_post_process_builder.* src/backend/vulkan/vulkan_realtime_renderer.cpp src/test/integration/test_vulkan_post_process_builder.cpp src/test/integration/test_lxe_editor_render_debug_dump.cpp
git commit -m "refactor: remove manual skybox material path"
```

---

## Task 11: Vulkan Smoke For Visible Background Enable/Disable

**Purpose:** Prove the graph-authored skybox renders a non-black background and `visibleInBackground` disables background rendering without affecting future surface lighting ownership.

**Files:**
- Modify or create: `assets/scenes/generated/helmet_standard_pbr.scene.yaml`
- Modify or create: a focused smoke fixture under `assets/scenes/generated/`
- Modify: `src/test/integration/test_lxe_editor_render_debug_dump.cpp`
- Check: `src/demos/lxe_editor/` command/debug dump paths

**Required negative test:**
- `visibleInBackground: false` removes visible background contribution.
- Disabling visible background does not delete or rename the `environmentMap` feature resource; 073-g consumes the same feature for surface lighting.

- [ ] **Step 1: Add smoke fixtures**

Create two fixtures or two runtime profiles:

| Fixture | Environment feature state | Expected image/debug result |
|---|---|---|
| enabled | `environmentMap.uri: builtin:env/white_cube`, `color` non-black, `visibleInBackground: true` | non-black HDR background pixels |
| disabled | same `environmentMap.uri` and color, `visibleInBackground: false` | background contribution absent |

- [ ] **Step 2: Add debug dump assertions**

In `test_lxe_editor_render_debug_dump.cpp`, assert:
- compiled pass list contains `SkyboxBackground`;
- `hdr.color` exists after the pass;
- enabled fixture has non-zero color statistics in depth-empty pixels;
- disabled fixture removes visible background contribution.

- [ ] **Step 3: Run headless verification**

```bash
cmake --build build --target test_lxe_editor_render_debug_dump lxe_editor
ctest --test-dir build --output-on-failure -R test_lxe_editor_render_debug_dump
```

If the test requires a video device on the current machine:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -R test_lxe_editor_render_debug_dump
```

- [ ] **Step 4: Commit checkpoint**

```bash
git add assets/scenes/generated src/test/integration/test_lxe_editor_render_debug_dump.cpp
git commit -m "test: add skybox background smoke"
```

---

## Task 12: Final Cross-Path Audit And Documentation Handoff

**Purpose:** Close 073-f without silently implementing 073-g or leaving old positive paths.

**Files:**
- Modify: `notes/requirements/073-f-environment-map-skybox-direct-lighting.md`
- Modify: `docs/superpowers/specs/2026-06-15-073-f-environment-map-skybox-direct-lighting-design.md`
- Modify: `docs/superpowers/plans/2026-06-15-073-f-environment-map-skybox-direct-lighting.md`
- Check: `notes/requirements/073-g-environment-hdr-async-ibl-bake-and-runtime-lighting.md`

**Required audit:**
- old tokens are absent from default positive paths;
- remaining mentions are docs, migration notes, or negative tests;
- Forward/Deferred surface lighting remains assigned to `REQ-073-g`.

- [x] **Step 1: Run full verification**

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_render_path_graph_pass_contract test_render_work_compiler test_shader_compiler test_scene_resource_upload_view_v2 test_vulkan_post_process_builder test_lxe_editor_render_debug_dump lxe_editor
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_path_graph_pass_contract|test_render_work_compiler|test_shader_compiler|test_scene_resource_upload_view_v2|test_vulkan_post_process_builder|test_lxe_editor_render_debug_dump)"
scripts/notes/serve_site.sh --build
```

- [x] **Step 2: Run legacy and boundary audit**

```bash
rg -n "createSkyboxBackgroundMaterial|shader: skybox|scene\\.environment|ambientColor|ambientIntensity|skyboxEnabled|IblBakeRenderer|bakeStaticEnvironment" src assets docs notes
```

Classify every remaining hit as one of:
- negative test;
- historical/requirement documentation;
- `REQ-073-g` or `REQ-073-i` owner;
- code that must be fixed before finishing 073-f.

- [x] **Step 3: Update implementation status**

In `notes/requirements/073-f-environment-map-skybox-direct-lighting.md`, update `实施状态` with:
- implemented files;
- verification commands and results;
- remaining work explicitly owned by `REQ-073-g`, `REQ-073-h`, or `REQ-073-i`.

- [ ] **Step 4: Commit checkpoint**

```bash
git add notes/requirements/073-f-environment-map-skybox-direct-lighting.md docs/superpowers/specs/2026-06-15-073-f-environment-map-skybox-direct-lighting-design.md docs/superpowers/plans/2026-06-15-073-f-environment-map-skybox-direct-lighting.md
git commit -m "docs: record 073-f skybox completion evidence"
```

Not run in this implementation pass because the workspace already contains
unrelated requirement renumbering, asset, and notes changes. Leave commit
curation to a separate explicit commit step.

---

## Final Verification Command Set

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_render_path_graph_pass_contract test_render_work_compiler test_shader_compiler test_scene_resource_upload_view_v2 test_vulkan_post_process_builder test_lxe_editor_render_debug_dump lxe_editor
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_path_graph_pass_contract|test_render_work_compiler|test_shader_compiler|test_scene_resource_upload_view_v2|test_vulkan_post_process_builder|test_lxe_editor_render_debug_dump)"
scripts/notes/serve_site.sh --build
rg -n "createSkyboxBackgroundMaterial|shader: skybox|scene\\.environment|ambientColor|ambientIntensity|skyboxEnabled|IblBakeRenderer|bakeStaticEnvironment" src assets docs notes
```

Expected final state:
- `SkyboxBackground` is graph-authored in Forward and Deferred paths.
- `feature.environmentLighting.parameters.environmentMap.uri` registers live `SkyboxMap`.
- `builtin:env/white_cube` and texture EnvMaps share the same shader sampler path.
- `visibleInBackground` controls only visible background rendering.
- `scene.environment`, `ambientColor`, `ambientIntensity`, and `skyboxEnabled` do not satisfy positive rendering paths.
- No default path calls manual skybox material injection.
- Surface environment lighting remains deferred to `REQ-073-g`.
