# 071b Source Target FrameGraph DAG Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the realtime FrameGraph from declared `sources` / `targets` across camera pre effects, material technique passes, and camera post effects, then execute it with resource-window-aware batched split barriers instead of fixed pass dependencies.

**Architecture:** Keep the existing material/effect pass contract parser and `GraphResourceRegistry`, but move FrameGraph ordering from “caller insertion order” to a registry-backed DAG compiler. Introduce a small plan object that carries sorted passes, producer/consumer diagnostics, resource usage windows, and barrier batches; Vulkan consumes that plan to transition/blit shared resources in grouped batches rather than per-write one-off barriers.

**Tech Stack:** C++20, CMake/Ninja, Vulkan backend, existing `ShaderCompiler` / `ShaderReflector`, existing `FrameGraph`, `RenderWorkQueue`, `RenderEffectResourceParser`, `GraphResourceRegistry`.

---

## Current Facts

- `FrameGraph::compile()` currently walks `m_passes` in insertion order and only checks that a read was written earlier.
- `GraphResourceRegistry` already knows standard imported resources and write modes, but `FrameGraph::compile()` does not use it.
- `validateTechniqueResources()` checks sources/targets and duplicate writers for one `MaterialTechnique`, but does not sort a graph or connect camera effects.
- `RenderEffectResourceParser` parses `schema: lxe.render-effect.v1`, `phase: pre|post`, and technique passes, but effects are not part of the real renderer graph.
- `VulkanRealtimeRenderer::initScene()` still hardcodes Shadow -> Deferred/Forward -> Bloom -> PostProcess -> DebugOverlay pass construction.
- Vulkan transitions are per attachment in `prepareOffscreenPass()` and `transitionPassWritesToShaderRead()`. There is no compile-time resource usage window or batched barrier plan.

## File Map

- `src/core/frame_graph/graph_resource_registry.*`: keep standard resource names, add aliases used by current renderer and versioned logical target helpers.
- `src/core/frame_graph/frame_graph.*`: add registry-backed DAG compile, phase ordering, resource usage windows, and barrier batch metadata.
- `src/core/frame_graph/technique_validator.*`: keep technique resource validation but route duplicate writer/missing producer semantics through the same registry rules as FrameGraph.
- `src/core/asset/render_effect.hpp`: keep `RenderEffect`, add selected-technique helper if needed.
- `src/infra/resource_parsers/render_effect_resource_parser.cpp`: keep parser shape; add validation hook tests only if parser diagnostics lack registry context.
- `src/infra/scene_io/scene_document.*`: parse and serialize `camera.preEffects` / `camera.postEffects` URIs.
- `src/demos/lxe_editor/scene_runtime.*` or the current scene asset loader path: resolve camera effect URIs into `RenderEffect` objects for renderer consumption.
- `src/backend/vulkan/vulkan_realtime_renderer.cpp`: replace fixed pass graph construction with `FrameGraphBuildPlan`; consume compiled barrier batches in command recording.
- `src/test/integration/test_frame_graph.cpp`: DAG sorting, missing producer, duplicate writer, writeMode, barrier batches.
- `src/test/integration/test_render_effect_resource_parser.cpp`: parser remains strict on fields and phases.
- `src/test/integration/test_vulkan_frame_graph.cpp`: renderer pass order should be derived from declarations, not hardcoded sequence.
- `assets/effects/*.render-effect.yaml`: add first real camera pre/post effects that replace current hardcoded shadow/bloom/post chain incrementally.

---

### Task 1: Registry-Backed FrameGraph DAG Compile

**Files:**
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`
- Modify: `src/test/integration/test_frame_graph.cpp`

- [x] **Step 1: Add failing DAG sort test**

Add this test in `test_frame_graph.cpp`:

```cpp
void testFrameGraphCompileSortsPassesBySourceTargetDag() {
  using namespace LX_core;
  FrameGraph graph;
  graph.addPass(FramePass{
      StringID("ToneMapping"),
      RenderTargetDesc::swapchain(),
      {},
      {FrameGraphRead::sampled(StringID("ldr.color"), StringID("SceneColor"))},
      {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
          StringID("swapchain.color"))}}});
  graph.addPass(FramePass{
      StringID("Forward"),
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
      {},
      {FrameGraphRead::sampled(StringID("camera.ubo")),
       FrameGraphRead::sampled(StringID("geometry.vertex")),
       FrameGraphRead::sampled(StringID("material.bsdf"))},
      {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
          StringID("hdr.color"))}}});
  graph.addPass(FramePass{
      StringID("Exposure"),
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA8Unorm),
      {},
      {FrameGraphRead::sampled(StringID("hdr.color"), StringID("HdrColor"))},
      {FrameGraphWrite{FrameGraphResourceRef::colorAttachment(
          StringID("ldr.color"))}}});

  const auto compiled = graph.compile(GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(), compiled.errorText());
  const auto &passes = compiled.getPasses();
  EXPECT(passes.size() == 3, "compiled graph should keep all passes");
  EXPECT(passes[0].name == StringID("Forward"),
         "producer should run before hdr.color consumer");
  EXPECT(passes[1].name == StringID("Exposure"),
         "middle pass should run after hdr.color producer");
  EXPECT(passes[2].name == StringID("ToneMapping"),
         "swapchain writer should run last");
}
```

Run:

```bash
cmake --build build --target test_frame_graph
ctest --test-dir build --output-on-failure -R '^test_frame_graph$'
```

Expected: FAIL because current `compile()` preserves insertion order.

- [x] **Step 2: Extend public compile API**

In `frame_graph.hpp`, keep the old `compile() const` wrapper for compatibility and add:

```cpp
enum class FrameGraphPhase { PreEffect, Material, PostEffect, Debug };

struct FrameGraphWrite {
  FrameGraphResourceRef resource;
  std::optional<std::string> writeMode;
};

struct FramePass {
  StringID name;
  RenderTargetDesc target;
  RenderWorkQueue queue;
  std::vector<FrameGraphRead> reads;
  std::vector<FrameGraphWrite> writes;
  FrameGraphPhase phase = FrameGraphPhase::Material;
  u32 stableOrder = 0;
};

[[nodiscard]] CompiledFrameGraph
compile(const GraphResourceRegistry &registry) const;
[[nodiscard]] CompiledFrameGraph compile() const;
```

The old wrapper must call:

```cpp
return compile(GraphResourceRegistry::makeDefault());
```

- [x] **Step 3: Implement DAG compile**

In `frame_graph.cpp`, replace insertion-order validation with:

1. Build `producerByTarget` from all writes.
2. Reject unknown source/target names via `registry.contains()`.
3. Treat `registry.isImported(source)` as already available.
4. Reject source with no producer and not imported.
5. Reject duplicate writers unless both writes use the same allowed `writeMode`.
6. Add dependency edge `producer -> consumer` for each non-imported source.
7. Add phase edges: all `PreEffect` before all `Material`, all `Material` before all `PostEffect`, all non-debug before `Debug`.
8. Topologically sort with stable tie-break `(phase, stableOrder)`.
9. Emit cycle diagnostics listing pass names.

Use Kahn sorting. Store sorted `CompiledFrameGraphPass` in `out.m_passes`.

- [x] **Step 4: Verify DAG test passes**

Run:

```bash
cmake --build build --target test_frame_graph
ctest --test-dir build --output-on-failure -R '^test_frame_graph$'
```

Expected: PASS for new DAG sort test and existing compile tests.

### Task 2: Missing Producer, Duplicate Writer, WriteMode Diagnostics

**Files:**
- Modify: `src/test/integration/test_frame_graph.cpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`
- Modify: `src/core/frame_graph/graph_resource_registry.cpp`

- [x] **Step 1: Add source/target registry tests**

Add tests:

```cpp
void testFrameGraphCompileRejectsRegistryUnknownSource() {
  LX_core::FrameGraph graph;
  graph.addPass(LX_core::FramePass{
      LX_core::StringID("BadPass"),
      LX_core::RenderTargetDesc::offscreenColor(LX_core::ImageFormat::RGBA16Float),
      {},
      {LX_core::FrameGraphRead::sampled(LX_core::StringID("freeform.input"))},
      {LX_core::FrameGraphWrite{LX_core::FrameGraphResourceRef::colorAttachment(
          LX_core::StringID("hdr.color"))}}});

  const auto compiled =
      graph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(), "unknown source should fail");
  EXPECT(compiled.errorText().find("unknown source freeform.input") !=
             std::string::npos,
         "diagnostic should name unknown source");
}

void testFrameGraphCompileRejectsDuplicateWriterWithoutWriteMode() {
  LX_core::FrameGraph graph;
  for (const char *passName : {"Opaque", "Transparent"}) {
    graph.addPass(LX_core::FramePass{
        LX_core::StringID(passName),
        LX_core::RenderTargetDesc::offscreenColor(LX_core::ImageFormat::RGBA16Float),
        {},
        {LX_core::FrameGraphRead::sampled(LX_core::StringID("camera.ubo"))},
        {LX_core::FrameGraphWrite{LX_core::FrameGraphResourceRef::colorAttachment(
            LX_core::StringID("hdr.color"))}}});
  }

  const auto compiled =
      graph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(!compiled.isValid(), "duplicate writer without writeMode should fail");
  EXPECT(compiled.errorText().find("duplicate writer hdr.color") !=
             std::string::npos,
         "diagnostic should name duplicate target");
}

void testFrameGraphCompileAllowsExplicitBlendWriter() {
  LX_core::FrameGraph graph;
  LX_core::FrameGraphWrite write{
      LX_core::FrameGraphResourceRef::colorAttachment(LX_core::StringID("hdr.color")),
      "blend"};
  graph.addPass(LX_core::FramePass{
      LX_core::StringID("Opaque"),
      LX_core::RenderTargetDesc::offscreenColor(LX_core::ImageFormat::RGBA16Float),
      {},
      {LX_core::FrameGraphRead::sampled(LX_core::StringID("camera.ubo"))},
      {write}});
  graph.addPass(LX_core::FramePass{
      LX_core::StringID("Transparent"),
      LX_core::RenderTargetDesc::offscreenColor(LX_core::ImageFormat::RGBA16Float),
      {},
      {LX_core::FrameGraphRead::sampled(LX_core::StringID("hdr.color"))},
      {write}});

  const auto compiled =
      graph.compile(LX_core::GraphResourceRegistry::makeDefault());
  EXPECT(compiled.isValid(), compiled.errorText());
}
```

Expected before implementation: at least duplicate writer writeMode behavior fails.

- [x] **Step 2: Align registry names with current 071-b names**

Update `GraphResourceRegistry::makeDefault()` so the standard names include both REQ names and current renderer aliases during transition:

```cpp
"depth.main",
"gbuffer.albedo",
"gbuffer.normal",
"gbuffer.material",
"gbuffer.emissive",
"hdr.color",
"ldr.color",
"swapchain.color",
"shadow.main",
"environment.radiance",
"bloom.threshold",
"bloom.blurH",
"bloom.blur",
```

Keep imported:

```cpp
"geometry.vertex",
"geometry.index",
"material.bsdf",
"camera.ubo",
"scene.lights",
"scene.bvh",
"scene.environment",
```

Do not add arbitrary legacy names like `scene.hdrColor`; instead migrate renderer/material/effect declarations to the registry names in later tasks.

### Task 3: Build FrameGraph Passes From Material/Effect Contracts

**Files:**
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Create: `src/core/frame_graph/frame_graph_build_plan.hpp`
- Create: `src/core/frame_graph/frame_graph_build_plan.cpp`
- Modify: `src/core/frame_graph/CMakeLists.txt` or parent CMake list that owns frame_graph sources
- Modify: `src/test/integration/test_frame_graph.cpp`

- [x] **Step 1: Introduce build input structures**

Add:

```cpp
struct FrameGraphEffectInput {
  RenderEffectPhase phase = RenderEffectPhase::Post;
  MaterialTechnique technique;
};

struct FrameGraphMaterialTechniqueInput {
  MaterialTechnique technique;
};

struct FrameGraphBuildPlanInput {
  std::vector<FrameGraphEffectInput> preEffects;
  std::vector<FrameGraphMaterialTechniqueInput> materialTechniques;
  std::vector<FrameGraphEffectInput> postEffects;
};

[[nodiscard]] FrameGraph
buildFrameGraphFromSourceTargetContracts(const FrameGraphBuildPlanInput &input,
                                         const GraphResourceRegistry &registry);
```

- [x] **Step 2: Add test for three-stage ordering**

Construct:

- pre effect pass `Shadow` writes `shadow.main`
- material pass `Forward` reads `shadow.main` and writes `hdr.color`
- post effect pass `ToneMapping` reads `hdr.color` and writes `swapchain.color`

Assert `compile(registry)` returns `Shadow, Forward, ToneMapping`.

- [x] **Step 3: Implement translation**

For each `MaterialPassContract`:

- `FramePass.name = StringID(pass.name)`
- `FramePass.phase = PreEffect / Material / PostEffect`
- `FramePass.reads = pass.sources` converted to `FrameGraphRead::sampled(StringID(source))`
- `FramePass.writes = pass.targets` converted to color/depth by registry metadata or simple name rule:
  - contains `"depth"` or starts with `"shadow."` => depth
  - otherwise color
- `FramePass.writes[*].writeMode = pass.writeMode`
- `stableOrder` increments by insertion.

Return a `FrameGraph` but do not build queues here.

### Task 4: Run Technique Resource Validation On Material/Effect Load

**Files:**
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Modify: `src/infra/resource_parsers/render_effect_resource_parser.cpp`
- Modify: `src/test/integration/test_generic_material_loader.cpp`
- Modify: `src/test/integration/test_render_effect_resource_parser.cpp`

- [ ] **Step 1: Add material load fail-fast test for missing producer**

Create a material with pass source `hdr.color` but no earlier pass target. Assert `loadGenericMaterial()` throws a `logic_error` containing `source 'hdr.color' has no producer`.

- [ ] **Step 2: Add effect parse/load validation test**

In `test_render_effect_resource_parser.cpp`, parse an effect:

```yaml
schema: lxe.render-effect.v1
name: bad_post
techniques:
  Forward:
    phase: post
    passes:
      Composite:
        shader: techniques/Forward/pbr
        stage: raster
        dispatch: fullscreen
        sources: [unknown.freeform]
        targets: [swapchain.color]
        renderState:
          cullMode: None
          depthTest: false
          depthWrite: false
```

Assert diagnostics contain `unknown source 'unknown.freeform'`.

- [ ] **Step 3: Hook validation**

After material/effect pass parsing creates `MaterialTechnique`, call:

```cpp
const auto report =
    LX_core::validateTechniqueResources(technique,
                                        LX_core::GraphResourceRegistry::makeDefault());
```

Fail if `!report.diagnostics.empty()`.

### Task 5: Parse Camera preEffects/postEffects In Scene YAML

**Files:**
- Modify: `src/infra/scene_io/scene_document.hpp`
- Modify: `src/infra/scene_io/scene_document.cpp`
- Modify: `src/test/integration/test_scene_document.cpp`

- [ ] **Step 1: Extend camera document state**

Add to camera state:

```cpp
std::vector<std::string> preEffects;
std::vector<std::string> postEffects;
```

- [ ] **Step 2: Add round-trip test**

Use YAML:

```yaml
camera:
  active: true
  preEffects:
    - uri: effects/shadow.render-effect.yaml
  postEffects:
    - uri: effects/tone_mapping.render-effect.yaml
```

Assert load stores both URIs and save writes them back.

- [ ] **Step 3: Implement parse/serialize**

Accept only sequence entries with `uri`. Invalid shapes fail scene document load with a diagnostic naming `camera.preEffects` or `camera.postEffects`.

### Task 6: Add Real Effect Assets For Existing Fixed Passes

**Files:**
- Create: `assets/effects/shadow.render-effect.yaml`
- Create: `assets/effects/deferred_lighting.render-effect.yaml`
- Create: `assets/effects/bloom.render-effect.yaml`
- Create: `assets/effects/tone_mapping.render-effect.yaml`
- Modify: `assets/shaders/glsl/...` only if effect shader URIs need technique paths

- [ ] **Step 1: Shadow pre effect**

Create:

```yaml
schema: lxe.render-effect.v1
name: shadow
techniques:
  Forward:
    phase: pre
    passes:
      Shadow:
        shader: techniques/Forward/shadow_depth_only
        stage: raster
        dispatch: draw
        sources: [geometry.vertex, camera.ubo, scene.lights]
        targets: [shadow.main]
        renderState:
          cullMode: None
          depthTest: true
          depthWrite: true
```

- [ ] **Step 2: Tone mapping post effect**

Create:

```yaml
schema: lxe.render-effect.v1
name: tone_mapping
techniques:
  Forward:
    phase: post
    passes:
      PostProcess:
        shader: post_process
        stage: raster
        dispatch: fullscreen
        sources: [hdr.color]
        targets: [swapchain.color]
        renderState:
          cullMode: None
          depthTest: false
          depthWrite: false
```

- [ ] **Step 3: Bloom post effect**

Create one effect with `BloomThreshold`, `BloomBlurH`, `BloomBlurV` passes:

```yaml
sources: [hdr.color]
targets: [bloom.threshold]
```

then:

```yaml
sources: [bloom.threshold]
targets: [bloom.blurH]
```

then:

```yaml
sources: [bloom.blurH]
targets: [bloom.blur]
```

Use complete `renderState` for each pass.

### Task 7: Connect Camera Effects Into Realtime FrameGraph

**Files:**
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/demos/lxe_editor/scene_runtime.hpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_vulkan_frame_graph.cpp`

- [ ] **Step 1: Add renderer test using camera effects**

Create a scene with active camera:

```yaml
camera:
  preEffects:
    - uri: assets/effects/shadow.render-effect.yaml
  postEffects:
    - uri: assets/effects/tone_mapping.render-effect.yaml
```

Assert `compiledFrameGraphPassNames()` returns `Shadow`, then material pass `Forward`, then `PostProcess`, without hardcoded renderer insertion.

- [ ] **Step 2: Resolve effects during scene load**

Load each URI through `RenderEffectResourceParser`, pick active technique by scene technique name, and store pre/post `RenderEffect` lists on the runtime scene/camera state.

- [ ] **Step 3: Build graph from contracts**

In renderer init:

1. Collect active camera pre effects.
2. Collect active material techniques from renderables. Deduplicate pass contracts by pass name/target/source signature.
3. Collect active camera post effects.
4. Call `buildFrameGraphFromSourceTargetContracts()`.
5. Call `compile(registry)`.
6. Build queues for sorted material passes.

Keep legacy fixed pass builder behind one temporary function named:

```cpp
buildLegacyFixedRealtimeFrameGraphForCompatibility()
```

and call it only if no camera effects and no material technique contracts are available. Add a warning log so remaining usage is visible.

### Task 8: Resource Usage Windows And Batched Split Barrier Plan

**Files:**
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`
- Modify: `src/test/integration/test_frame_graph.cpp`

- [ ] **Step 1: Add barrier plan data types**

Add:

```cpp
enum class FrameGraphResourceAccess {
  RenderTargetWrite,
  DepthStencilWrite,
  ShaderRead,
  TransferRead,
  Present,
};

struct FrameGraphResourceUsage {
  StringID resource;
  usize passIndex = 0;
  FrameGraphResourceAccess access = FrameGraphResourceAccess::ShaderRead;
};

struct FrameGraphBarrierBatch {
  usize beforePassIndex = 0;
  std::vector<StringID> resources;
  FrameGraphResourceAccess fromAccess = FrameGraphResourceAccess::RenderTargetWrite;
  FrameGraphResourceAccess toAccess = FrameGraphResourceAccess::ShaderRead;
};
```

Expose:

```cpp
[[nodiscard]] const std::vector<FrameGraphBarrierBatch> &
getBarrierBatches() const;
```

on `CompiledFrameGraph`.

- [ ] **Step 2: Add batched barrier test**

Build:

- `GBuffer` writes `gbuffer.albedo`, `gbuffer.normal`, `gbuffer.material`
- `DeferredLighting` reads all three

Assert compiled barrier batches contain one batch before `DeferredLighting` with all three resources.

```cpp
EXPECT(batches.size() == 1, "gbuffer resources should share one barrier batch");
EXPECT(batches[0].resources.size() == 3,
       "same producer/consumer window should batch gbuffer barriers");
```

- [ ] **Step 3: Implement resource usage windows**

During DAG compile:

1. For each write, record producer pass index and write access.
2. For each read, record consumer pass index and read access.
3. Group transitions by `(beforePassIndex, fromAccess, toAccess)`.
4. Put all resources in the same group into one `FrameGraphBarrierBatch`.
5. Preserve deterministic order by resource debug string.

This is the compile-time “resource window” analysis. It is intentionally logical; Vulkan mapping happens in Task 9.

### Task 9: Consume Batched Barrier Plan In Vulkan

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.hpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
- Modify: `src/test/integration/test_vulkan_frame_graph.cpp`

- [ ] **Step 1: Add command buffer helper for image barrier arrays**

Add:

```cpp
void VulkanCommandBuffer::pipelineBarriers(
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage,
    std::span<const VkImageMemoryBarrier> imageBarriers);
```

Implementation calls one `vkCmdPipelineBarrier` with `imageBarrierCount = imageBarriers.size()`.

- [ ] **Step 2: Replace per-write shader-read transition**

In realtime recording loop:

1. Before each pass, fetch compiled `FrameGraphBarrierBatch` where `beforePassIndex == passIndex`.
2. Convert each resource to `VkImageMemoryBarrier`.
3. Submit one `pipelineBarriers()` call per batch.
4. Remove or bypass `transitionPassWritesToShaderRead(compiledPass, cmd)` for graph resources covered by the batch plan.

Keep pass-begin write layout transitions in `prepareOffscreenPass()`, but batch them too when multiple attachments in the same pass require the same write layout:

```cpp
std::vector<VkImageMemoryBarrier> writeBarriers;
// append all attachments for the pass
cmd.pipelineBarriers(srcStage, dstStage, writeBarriers);
```

- [ ] **Step 3: Track attachment layout through batches**

When applying a batch, update `VulkanFrameGraphAttachment::currentLayout` for each resource to the destination layout.

- [ ] **Step 4: Add regression observable**

Expose debug stats:

```cpp
usize lastFrameGraphBarrierBatchCount() const;
usize lastFrameGraphBarrierResourceCount() const;
```

Test that a deferred GBuffer frame has fewer barrier calls than resources transitioned:

```cpp
EXPECT(renderer->lastFrameGraphBarrierBatchCount() <
       renderer->lastFrameGraphBarrierResourceCount(),
       "batched barriers should group shared resource windows");
```

### Task 10: Remove Or Isolate Fixed Pass Dependency Builder

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_vulkan_frame_graph.cpp`
- Search: `rg -n "scene.hdrColor|gbuffer.albedoAlpha|shadow.cascade|Pass_Bloom|Pass_PostProcess|buildLegacyFixedRealtimeFrameGraph"`

- [ ] **Step 1: Rename legacy builder**

Move the current hardcoded `m_frameGraph.addPass()` sequence into:

```cpp
FrameGraph buildLegacyFixedRealtimeFrameGraphForCompatibility(...);
```

Add:

```cpp
LX_LOG_WARN("Using legacy fixed realtime frame graph; camera effects/source-target contracts are missing");
```

- [ ] **Step 2: Prefer source-target builder**

Use the new contract builder whenever:

- active camera has effects, or
- active materials expose technique pass contracts.

Legacy path is only allowed for old tests/assets that have not opted into effects yet.

- [ ] **Step 3: Add audit test**

Add a test that loads a scene with camera effects and asserts the warning string is absent and pass order comes from graph compile.

### Task 11: Verification Gate

**Files:**
- No source modifications.

- [ ] **Step 1: Focused tests**

Run:

```bash
cmake --build build --target test_frame_graph test_render_effect_resource_parser test_generic_material_loader test_vulkan_frame_graph
ctest --test-dir build --output-on-failure -R 'test_(frame_graph|render_effect_resource_parser|generic_material_loader|vulkan_frame_graph)$'
```

Expected: PASS.

- [ ] **Step 2: Existing 071-b related gate**

Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_(offline_gpu_scene|assets_layout|shader_compiler|generic_material_loader|material_instance|material_variant_rules|scene_node_validation|vulkan_shader|vulkan_frame_graph)$'
```

Expected: PASS.

- [ ] **Step 3: Static cleanup checks**

Run:

```bash
rg -n "scene.hdrColor|gbuffer.albedoAlpha|gbuffer.normalRoughness|shadow.cascade|resource was not written by an earlier pass" src assets
git diff --check
```

Expected:

- old logical resource names are gone or confined to tests explicitly named legacy compatibility.
- old insertion-order error text is gone.
- no whitespace errors.

---

## Coverage Check Against REQ-071-b

This plan directly advances:

- R2: full pass field contract remains required for materials/effects.
- R3: technique resource validation runs for material/effect load.
- R5: camera pre/post RenderEffect files become real graph inputs.
- R6: FrameGraph builds from camera pre effects -> material passes -> camera post effects.
- R7/R7.1: registry-backed sources/targets, producer/consumer diagnostics, duplicate writer/writeMode rules.
- T1/T3/T4: fail-fast validation and DAG order tests.

This plan also adds the requested GPU sync work:

- resource usage windows are computed during FrameGraph compile.
- barriers are grouped by same producer/consumer window and access transition.
- Vulkan submits one batched barrier per group instead of one barrier per resource.

This plan intentionally postpones:

- full removal of all `Pass_*` constants across tests and old APIs.
- OfflineRT common ABI extraction.
- BSDF-grouped deferred lighting internals.
- transparent glass rendering validation.

Those should be separate follow-up slices after the source/target DAG is active.
