# 073-e2 Render Work Compiler Single Path Hard Cut Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace all post-FrameGraph render work with the clean `FramePass` / `RenderWorkCompiler` / `RenderInput` / `RenderInputDesc` model from the spec.

**Architecture:** Build the target data model first, then migrate parser, `FramePass`, compiler, pipeline preparation, upload planning, and Vulkan execution onto that model. Old work owners, union payloads, batch result types, and batch-named metadata are deleted rather than wrapped or renamed.

**Tech Stack:** C++20, CMake/Ninja, yaml-cpp, LXEngine core FrameGraph, Vulkan backend, existing integration tests in `src/test/integration`.

---

## Hard-Cut Execution Policy

Old implementation files and classes may be read as migration references while
writing replacement code. They are not implementation foundations.

Rules for every task:

- Reference old code only to copy correct behavior into the target model.
- Do not include old headers from new target-model files.
- Do not move old classes into a namespace, rename them, type-alias them, or keep
  them as private helper structs.
- Do not leave old files in the build with empty shells or compatibility comments.
- When replacement code for an old file is in place, delete the old file in the
  same task or the next listed deletion task.
- A task that removes old code is incomplete until CMake/source lists stop
  compiling that file and `rg` proves the old type names are gone from `src` and
  `assets`.
- Requirement and concept docs may mention old names only as historical facts or
  audit tokens.

Old code can be used as a checklist for behavior transfer:

- scene traversal and visibility filtering move into `RenderWorkCompiler`;
- draw/indirect command payload moves into `RenderDrawInput`;
- compute dispatch payload moves into `RenderComputeInput`;
- diagnostics move into `RenderInputDiagnostic` on rejected descs;
- pipeline facts move into `RenderInputDesc.pipelineBuildDesc`;
- resource binding/upload facts move into `RenderInputDesc.bindingPlan` and
  `RenderInputDesc.resourceDependencies`.

## Target Data Model

Every implementation task below must use only this target model plus the listed reused engine facts. If code needs another public render-work class, stop and revise the design before implementing.

### Render Path Input Contract

Defined in `src/core/asset/render_effect.hpp`:

```cpp
enum class RenderPassInputKind {
  SceneRenderables,
  FullscreenTriangle,
  ComputeDispatch,
};

struct RenderPassObjectInputFilter final {
  std::vector<std::string> renderClasses;
};

struct RenderPassMaterialInputFilter final {
  std::vector<std::string> types;
  bool required = true;
};

struct RenderPassInputContract final {
  RenderPassInputKind kind = RenderPassInputKind::SceneRenderables;
  RenderPassObjectInputFilter object;
  RenderPassMaterialInputFilter material;
  std::optional<RenderPathGeometryContract> geometry;
};
```

`RenderPassNode` and `FramePass` store `RenderPassInputContract input`.

### Render Input Payloads

Defined in `src/core/frame_graph/render_input.hpp`:

```cpp
enum class RenderInputKind {
  Draw,
  Compute,
};

enum class RenderInputStatus {
  Accepted,
  Rejected,
};

enum class RenderDrawInputSource {
  SceneRenderable,
  FullscreenTriangle,
};

enum class RenderInputDiagnosticCode {
  UnsupportedInputContract,
  ObjectClassRejected,
  MaterialRequired,
  MaterialTypeRejected,
  MissingMesh,
  GeometryContractMismatch,
  MissingShaderReflection,
  MissingPipelineFacts,
  MissingBinding,
  MissingResource,
  ZeroDrawCount,
  BackendUnsupported,
};

struct RenderInputDiagnostic final {
  RenderInputDiagnosticCode code = RenderInputDiagnosticCode::UnsupportedInputContract;
  StringID pass;
  StringID debugId;
  std::string message;
};

struct RenderDrawCommand final {
  u32 indexCount = 0;
  u32 instanceCount = 1;
  u32 firstIndex = 0;
  i32 vertexOffset = 0;
  u32 firstInstance = 0;
};

struct RenderInput {
  virtual ~RenderInput() = default;
  [[nodiscard]] virtual RenderInputKind kind() const = 0;

  StringID pass;
  StringID debugId;
  usize inputIndex = 0;
};

struct RenderDrawInput final : RenderInput {
  [[nodiscard]] RenderInputKind kind() const override {
    return RenderInputKind::Draw;
  }

  RenderDrawInputSource source = RenderDrawInputSource::SceneRenderable;
  ObjectHandle object;
  MeshHandle mesh;
  MaterialHandle material;
  u32 primitiveIndex = u32_max;
  Vec3f sortCenter{};
  StringID objectDataSignature;
  StringID materialTypeSignature;
  std::vector<RenderDrawCommand> drawCommands;
};

struct RenderComputeInput final : RenderInput {
  [[nodiscard]] RenderInputKind kind() const override {
    return RenderInputKind::Compute;
  }

  u32 groupCountX = 1;
  u32 groupCountY = 1;
  u32 groupCountZ = 1;
  std::optional<StringID> readbackResource;
};
```

### Render Input Descriptors

`RenderInputDesc` is not a payload container. It points at a typed input by index and carries only validation, pipeline, binding, dependency, diagnostic, and stats facts.

```cpp
struct RenderInputBindingPlan final {
  std::vector<DescriptorResourceRef> descriptors;
};

struct RenderInputStats final {
  usize inputCount = 0;
  usize acceptedInputCount = 0;
  usize rejectedInputCount = 0;
  usize submittedDrawCount = 0;
  usize submittedDispatchCount = 0;
  usize fallbackObservedCount = 0;
};

struct RenderInputDesc final {
  RenderInputStatus status = RenderInputStatus::Rejected;
  usize inputIndex = 0;
  StringID pass;
  StringID debugId;
  PipelineKey pipelineKey;
  PipelineBuildDesc pipelineBuildDesc;
  StringID shaderUri;
  StringID shaderVariantKey;
  StringID reflectionIdentity;
  RenderInputBindingPlan bindingPlan;
  std::vector<GpuResourceRef> resourceDependencies;
  std::vector<RenderInputDiagnostic> diagnostics;
  RenderInputStats stats;

  [[nodiscard]] bool accepted() const {
    return status == RenderInputStatus::Accepted;
  }
};
```

### Allowed Reused Engine Facts

These existing types may be reused directly because they are domain facts, not old render-work payloads:

- `ObjectHandle`, `MeshHandle`, `MaterialHandle`
- `RenderPathGeometryContract`, `RenderPathAttachmentContract`
- `RenderTargetDesc`, `RenderState`, `PrimitiveTopology`, `VertexLayout`
- `PipelineKey`, `PipelineBuildDesc`
- `DescriptorResourceRef`, `GpuResourceRef`
- `Scene`, `SceneResourceTableUploadView`

### Forbidden Model Drift

Do not create public analysis/result/container classes for render work. `RenderWorkCompiler` should write into:

```cpp
std::vector<std::unique_ptr<RenderInput>>
std::vector<RenderInputDesc>
```

If a helper struct is necessary, keep it file-local in `render_work_compiler.cpp`.

### Explicit Hard-Cut Type List

Delete these names from production code by the end of this plan. Do not keep
them as public API, adapters, aliases, wrappers, or compiler-local helper types:

```text
RenderWorkQueue
RenderWorkItem
RenderWorkKind
DirectRasterWorkPayload
ComputeDispatchWorkPayload
DirectRasterPassPurpose
RenderBatch
RenderBatchAnalysis
RenderBatchDiagnosticReason
RenderBatchDiagnostic
RenderBatchStats
RenderBatchPipelineFacts
RenderBatchGeometryResources
RenderIndirectBatch
PreparedRenderDrawCandidate
RenderPathNodeContext
RenderPathNodeData
RenderInputAnalysis
ComputeAnalysis
OpaqueBatch
OpaqueGeometry
OpaqueIndirect
OfflineCompiler
OfflineWork
VulkanRealtimeRenderBatchStats
VulkanRenderBatchSubmissionStats
```

Migration rule:

- draw payload and indirect/draw command data goes to `RenderDrawInput`;
- compute dispatch payload goes to `RenderComputeInput`;
- diagnostics go to `RenderInputDiagnostic` inside rejected descs;
- coverage/submission counters go to `RenderInputStats`;
- pipeline facts go to `RenderInputDesc.pipelineBuildDesc`;
- binding facts go to `RenderInputDesc.bindingPlan`.

If an implementation step appears to need `PreparedRenderDrawCandidate` or
`RenderPathNodeContext`, it should instead add explicit fields to
`RenderDrawInput`, `FramePass`, or `RenderInputDesc` from the Target Data Model.

## Task 1: Lock Parser Schema With Failing Tests

**Files:**
- Modify: `src/test/integration/test_render_path_graph_pass_contract.cpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

- [ ] **Step 1: Add tests that old top-level input fields fail**

Add this test in `test_render_path_graph_pass_contract.cpp`:

```cpp
void testRasterPassRejectsOldTopLevelFiltersAndGeometry() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://old-input", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: Forward
    shader: render_paths/Forward/pbr
    stage: raster
    dispatch: draw
    filters:
      renderClass: [surface.opaque]
      bsdf: [matte]
    geometry:
      vertex: position-only
      topology: triangle-list
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera]
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
)");

  EXPECT(!parsed.renderPathGraph.has_value(),
         "old top-level filters and geometry must fail");
  EXPECT(hasDiagnosticContaining(parsed, "passes.Forward.filters"),
         "diagnostic should reject filters");
  EXPECT(hasDiagnosticContaining(parsed, "passes.Forward.geometry"),
         "diagnostic should reject geometry");
}
```

Call it from `main()`.

- [ ] **Step 2: Add tests for new scene-renderables and fullscreen input**

Update the existing complete-pass parser test to use:

```yaml
    input:
      kind: scene-renderables
      material:
        type: [matte, uber]
        required: true
      geometry:
        vertex: position-only
        topology: triangle-list
```

Assert:

```cpp
EXPECT(pass.input.kind == RenderPassInputKind::SceneRenderables,
       "input kind should be scene-renderables");
EXPECT(pass.input.material.types.size() == 2 &&
           pass.input.material.types.front() == "matte",
       "material type filter should be retained");
EXPECT(pass.input.material.required,
       "material required flag should be retained");
EXPECT(pass.input.geometry.has_value(),
       "scene-renderables input should retain geometry");
```

Add a fullscreen pass test:

```cpp
void testFullscreenTriangleInputParses() {
  LX_infra::RenderPathGraphResourceParser parser;
  const auto parsed = parser.parse("memory://fullscreen", R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: PostProcess
    shader: post_process
    stage: raster
    dispatch: fullscreen
    input:
      kind: fullscreen-triangle
    sources: [hdr.color]
    targets: [swapchain.color]
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false
      depthOp: Always
)");

  EXPECT(parsed.renderPathGraph.has_value(),
         "fullscreen-triangle input should parse");
  EXPECT(parsed.renderPathGraph->passes.front().input.kind ==
             RenderPassInputKind::FullscreenTriangle,
         "fullscreen input kind should be retained");
}
```

- [ ] **Step 3: Run tests to prove they fail before implementation**

Run:

```bash
cmake --build build --target test_render_path_graph_pass_contract
./build/src/test/test_render_path_graph_pass_contract
```

Expected: compile failure for missing `RenderPassInputKind` or `RenderPassNode::input`.

- [ ] **Step 4: Commit test characterization**

```bash
git add src/test/integration/test_render_path_graph_pass_contract.cpp src/test/integration/test_render_resource_parsers.cpp
git commit -m "test: characterize render path input schema hard cut"
```

## Task 2: Implement RenderPathGraph Input Contract

**Files:**
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/core/asset/render_effect.cpp`
- Modify: `src/infra/resource_parsers/render_pass_node_parser.cpp`
- Modify: `src/infra/resource_parsers/render_path_graph_resource_parser.cpp`
- Modify: `assets/render_paths/deferred_bloom.render-path.yaml`
- Modify: `assets/render_paths/deferred_main.render-path.yaml`
- Modify: `assets/render_paths/forward_bloom.render-path.yaml`
- Modify: `assets/render_paths/forward_main.render-path.yaml`
- Modify: parser tests from Task 1

- [ ] **Step 1: Add target input types**

In `render_effect.hpp`, add the Render Path Input Contract types from the Target Data Model. Change `RenderPassNode` to store:

```cpp
RenderPassInputContract input;
```

Remove public top-level pass filter and geometry fields from `RenderPassNode`.

- [ ] **Step 2: Include input in render path node signature**

In `render_effect.cpp`, append deterministic signature fields:

```cpp
fields.push_back(StringID(input.kind == RenderPassInputKind::SceneRenderables
                              ? "input=scene-renderables"
                              : input.kind == RenderPassInputKind::FullscreenTriangle
                                    ? "input=fullscreen-triangle"
                                    : "input=compute-dispatch"));
for (const std::string &renderClass : input.object.renderClasses) {
  fields.push_back(StringID("object.renderClass=" + renderClass));
}
fields.push_back(StringID(input.material.required ? "material.required=true"
                                                  : "material.required=false"));
for (const std::string &type : input.material.types) {
  fields.push_back(StringID("material.type=" + type));
}
if (input.geometry.has_value()) {
  fields.push_back(geometrySignature(*input.geometry));
}
```

- [ ] **Step 3: Parse input strictly**

In `render_pass_node_parser.cpp`, accept only `input` as the pass input field. Old top-level `filters` and `geometry` must produce unsupported-field diagnostics.

Parsing rules:

- `input.kind` is required.
- `scene-renderables` requires `input.geometry`.
- `fullscreen-triangle` requires `stage: raster` and `dispatch: fullscreen`.
- `fullscreen-triangle` rejects `object`, `material`, and `geometry`.
- `input.material.type` replaces old `filters.bsdf`.
- `input.material.required` defaults to `true`.

- [ ] **Step 4: Remove old filter parser**

Delete old filter parsing from `render_path_graph_resource_parser.cpp`. Do not map old fields to new fields.

- [ ] **Step 5: Migrate render path assets**

For PBR draw passes, use:

```yaml
input:
  kind: scene-renderables
  material:
    type: [matte, uber, metal, substrate, standard-pbr]
    required: true
  geometry:
    vertex: position-only
    topology: triangle-list
```

For debug mesh passes, use:

```yaml
input:
  kind: scene-renderables
  object:
    renderClass: [debug.mesh]
  material:
    required: false
  geometry:
    vertex: position-only
    topology: line-list
```

For fullscreen passes, use:

```yaml
input:
  kind: fullscreen-triangle
```

- [ ] **Step 6: Verify schema**

Run:

```bash
cmake --build build --target test_render_path_graph_pass_contract test_render_resource_parsers
./build/src/test/test_render_path_graph_pass_contract
./build/src/test/test_render_resource_parsers
```

Expected: both pass.

- [ ] **Step 7: Commit schema implementation**

```bash
git add src/core/asset/render_effect.hpp src/core/asset/render_effect.cpp src/infra/resource_parsers/render_pass_node_parser.cpp src/infra/resource_parsers/render_path_graph_resource_parser.cpp assets/render_paths src/test/integration/test_render_path_graph_pass_contract.cpp src/test/integration/test_render_resource_parsers.cpp
git commit -m "feat: define render path input contract"
```

## Task 3: Replace FramePass Queue Ownership With Input Contract

**Files:**
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`
- Modify: `src/core/frame_graph/frame_graph_build_plan.cpp`
- Modify: `src/test/integration/test_frame_graph.cpp`

- [ ] **Step 1: Add a FrameGraph boundary test**

Add:

```cpp
void testFramePassCarriesInputContractAndCompileStaysGraphOnly() {
  RenderPassNode node;
  node.id = "PostProcess";
  node.shaderUri = ResourceUri("post_process");
  node.stage = RenderPassStage::Raster;
  node.dispatch = RenderPassDispatch::Fullscreen;
  node.input.kind = RenderPassInputKind::FullscreenTriangle;
  node.sources = {"hdr.color"};
  node.targets = {"swapchain.color"};
  node.renderState.cullMode = CullMode::None;
  node.renderState.depthTestEnable = false;
  node.renderState.depthWriteEnable = false;
  node.renderState.depthOp = CompareOp::Always;

  RenderPathGraph graphAsset;
  graphAsset.name = "InputGraph";
  graphAsset.passes.push_back(node);

  FrameGraph graph =
      buildFrameGraphFromRenderPathGraph(graphAsset,
                                         GraphResourceRegistry::makeDefault());
  EXPECT(graph.getPasses().front().input.kind ==
             RenderPassInputKind::FullscreenTriangle,
         "FramePass should retain input contract");
  EXPECT(graph.compile().isValid(),
         "FrameGraph compile should remain graph-only");
}
```

- [ ] **Step 2: Remove queue from FramePass**

In `frame_graph.hpp`, remove `FramePass::queue`. Add:

```cpp
RenderPassInputContract input;
```

`FrameGraph::compile()` must not include or inspect input payloads. It may include input contract in signatures through `FramePass::renderPathNodeSignature`.

- [ ] **Step 3: Move builder assignment**

In `frame_graph_build_plan.cpp`, assign:

```cpp
pass.input = node.input;
```

- [ ] **Step 4: Remove queue-derived build and preload APIs**

Remove any `FrameGraph` method that builds queues or collects pipeline descs from pass-local work. Realtime and offline renderers will call `RenderWorkCompiler` after `FrameGraph::compile()`.

- [ ] **Step 5: Verify FrameGraph tests**

Run:

```bash
cmake --build build --target test_frame_graph
./build/src/test/test_frame_graph
```

Expected: pass after all queue references in FrameGraph tests are moved to compiler tests.

- [ ] **Step 6: Commit FramePass ownership change**

```bash
git add src/core/frame_graph/frame_graph.hpp src/core/frame_graph/frame_graph.cpp src/core/frame_graph/frame_graph_build_plan.cpp src/test/integration/test_frame_graph.cpp
git commit -m "feat: make FramePass own input contract"
```

## Task 4: Add Clean RenderInput Target Model

**Files:**
- Create: `src/core/frame_graph/render_input.hpp`
- Create: `src/core/frame_graph/render_input.cpp`
- Modify: `src/test/CMakeLists.txt`
- Create: `src/test/integration/test_render_work_compiler.cpp`

- [ ] **Step 1: Add target model test**

Create `test_render_work_compiler.cpp` with:

```cpp
#include "core/frame_graph/render_input.hpp"

#include <iostream>

using namespace LX_core;

namespace {
int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testDescReferencesInputWithoutOwningPayload() {
  RenderDrawInput input;
  input.inputIndex = 0;
  input.source = RenderDrawInputSource::FullscreenTriangle;
  input.drawCommands.push_back(RenderDrawCommand{.indexCount = 3});

  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.inputIndex = input.inputIndex;

  EXPECT(desc.accepted(), "desc should expose accepted state");
  EXPECT(desc.inputIndex == 0, "desc should reference input by index");
  EXPECT(input.drawCommands.size() == 1,
         "draw command payload should remain on input");
}
} // namespace

int main() {
  testDescReferencesInputWithoutOwningPayload();
  if (g_failures != 0) {
    return 1;
  }
  return 0;
}
```

Add `test_render_work_compiler` to `src/test/CMakeLists.txt`.

- [ ] **Step 2: Implement `render_input.hpp` exactly from Target Data Model**

Create the target model types from the Target Data Model section. Include only clean model dependencies:

```cpp
#include "core/asset/render_effect.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/scene/object.hpp"
```

- [ ] **Step 3: Verify target model**

Run:

```bash
cmake --build build --target test_render_work_compiler
./build/src/test/test_render_work_compiler
```

Expected: pass.

- [ ] **Step 4: Commit target model**

```bash
git add src/core/frame_graph/render_input.hpp src/core/frame_graph/render_input.cpp src/test/CMakeLists.txt src/test/integration/test_render_work_compiler.cpp
git commit -m "feat: add clean render input target model"
```

## Task 5: Implement RenderWorkCompiler On Target Model

**Files:**
- Create: `src/core/frame_graph/render_work_compiler.hpp`
- Create: `src/core/frame_graph/render_work_compiler.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`

- [ ] **Step 1: Define compiler API without result wrapper**

Use caller-owned standard containers:

```cpp
class RenderWorkCompiler final {
public:
  void buildInputs(const FramePass &pass, const RenderWorkBuildContext &context,
                   std::vector<std::unique_ptr<RenderInput>> &outInputs) const;

  [[nodiscard]] std::vector<RenderInputDesc>
  prepare(const FramePass &pass, const RenderWorkBuildContext &context,
          const std::vector<std::unique_ptr<RenderInput>> &inputs) const;
};
```

If `RenderWorkBuildContext` still contains old-work assumptions, strip it down in this task to domain plus realtime scene/offline job access. Do not introduce a second public result class.

- [ ] **Step 2: Add fullscreen compiler test**

Add:

```cpp
void testFullscreenTriangleBuildsOneInputAndDesc() {
  FramePass pass;
  pass.name = StringID("PostProcess");
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  pass.shaderUri = ResourceUri("post_process");

  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  compiler.buildInputs(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);
  const auto descs =
      compiler.prepare(pass, RenderWorkBuildContext::realtimeEmpty(), inputs);

  EXPECT(inputs.size() == 1, "fullscreen input should create one input");
  const auto *draw = dynamic_cast<const RenderDrawInput *>(inputs.front().get());
  EXPECT(draw != nullptr, "fullscreen input should be a draw input");
  EXPECT(draw->source == RenderDrawInputSource::FullscreenTriangle,
         "draw input should identify fullscreen triangle source");
  EXPECT(descs.size() == 1 && descs.front().accepted(),
         "fullscreen input should prepare one accepted desc");
}
```

- [ ] **Step 3: Implement fullscreen input builder**

`buildInputs()` creates one `RenderDrawInput` with:

```cpp
source = RenderDrawInputSource::FullscreenTriangle;
drawCommands = {RenderDrawCommand{.indexCount = 3, .instanceCount = 1}};
```

`prepare()` creates one accepted `RenderInputDesc` with `inputIndex`, `pass`, `debugId`, `PipelineKey`, `PipelineBuildDesc`, shader URI, binding plan, and stats. It does not copy draw commands into the desc.

- [ ] **Step 4: Implement scene-renderables builder**

For `input.kind: scene-renderables`, `buildInputs()` traverses `Scene::getRenderables()` and applies:

- visibility rules already used by the current renderer;
- `object.renderClass` if present;
- `material.required`;
- `material.type` if present;
- geometry contract.

Rejected inputs are represented by rejected descs from `prepare()`, not by silent skipping when the renderable was selected by the input contract and then failed validation.

- [ ] **Step 5: Verify compiler**

Run:

```bash
cmake --build build --target test_render_work_compiler
./build/src/test/test_render_work_compiler
```

Expected: pass.

- [ ] **Step 6: Commit compiler**

```bash
git add src/core/frame_graph/render_work_compiler.hpp src/core/frame_graph/render_work_compiler.cpp src/test/integration/test_render_work_compiler.cpp
git commit -m "feat: compile frame passes into render inputs"
```

## Task 6: Prepare PipelineBuildDesc From RenderInputDesc

**Files:**
- Modify: `src/core/pipeline/pipeline_build_desc.hpp`
- Modify: `src/core/pipeline/pipeline_build_desc.cpp`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Modify: `src/test/integration/test_pipeline_build_info.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`

- [ ] **Step 1: Replace old derivation with explicit constructors**

Expose only direct construction helpers:

```cpp
static PipelineBuildDesc graphics(PipelineKey key,
                                  StringID shaderVariantKey,
                                  RenderTargetDesc target,
                                  std::vector<ShaderStageCode> stages,
                                  std::vector<ShaderResourceBinding> bindings,
                                  VertexLayout vertexLayout,
                                  RenderState renderState,
                                  PrimitiveTopology topology,
                                  std::optional<RenderPathNodeRenderingMode>
                                      renderingMode,
                                  std::vector<RenderPathAttachmentContract>
                                      attachments);

static PipelineBuildDesc compute(PipelineKey key,
                                 StringID shaderVariantKey,
                                 std::vector<ShaderStageCode> stages,
                                 std::vector<ShaderResourceBinding> bindings);
```

- [ ] **Step 2: Make compiler prepare write pipeline facts**

`RenderWorkCompiler::prepare()` must fill:

```cpp
desc.pipelineKey
desc.pipelineBuildDesc
desc.shaderUri
desc.shaderVariantKey
desc.reflectionIdentity
desc.bindingPlan
desc.resourceDependencies
```

from pass contract, shader/reflection, material type, geometry contract, and domain resources. It must not ask the backend to infer these values.

- [ ] **Step 3: Rewrite pipeline tests around target model**

In `test_pipeline_build_info.cpp`, test `PipelineBuildDesc::graphics()` and `PipelineBuildDesc::compute()` directly. In `test_render_work_compiler.cpp`, assert accepted descs carry a non-empty `pipelineBuildDesc.key`.

- [ ] **Step 4: Verify pipeline preparation**

Run:

```bash
cmake --build build --target test_pipeline_build_info test_render_work_compiler
./build/src/test/test_pipeline_build_info
./build/src/test/test_render_work_compiler
```

Expected: pass.

- [ ] **Step 5: Commit pipeline preparation**

```bash
git add src/core/pipeline/pipeline_build_desc.hpp src/core/pipeline/pipeline_build_desc.cpp src/core/frame_graph/render_work_compiler.cpp src/test/integration/test_pipeline_build_info.cpp src/test/integration/test_render_work_compiler.cpp
git commit -m "feat: prepare pipeline descs from render input descs"
```

## Task 7: Route Upload And Vulkan Execution Through Target Model

**Files:**
- Modify: `src/core/frame_graph/render_upload_plan.hpp`
- Modify: `src/core/frame_graph/render_upload_plan.cpp`
- Modify: `src/backend/vulkan/details/resource_manager.hpp`
- Modify: `src/backend/vulkan/details/resource_manager.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.hpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: Vulkan tests

- [ ] **Step 1: Change upload API**

Use:

```cpp
RenderUploadPlan buildRenderUploadPlan(
    const std::vector<std::unique_ptr<RenderInput>> &inputs,
    const std::vector<RenderInputDesc> &descs);
```

Collect descriptor resources and explicit resource dependencies from accepted descs, plus any typed input resources that are not descriptor-bound.

- [ ] **Step 2: Change resource manager API**

Use:

```cpp
VulkanPipelineRef getOrCreatePipeline(const LX_core::RenderInputDesc &desc);
VulkanPipelineRef getOrCreatePipeline(const LX_core::PipelineBuildDesc &desc);
```

Both use `desc.pipelineBuildDesc` / explicit `PipelineBuildDesc`; neither inspects typed input payload.

- [ ] **Step 3: Change command buffer execution**

Use:

```cpp
void executeRenderInput(const LX_core::RenderInput &input,
                        const LX_core::RenderInputDesc &desc);
```

Rules:

- rejected desc: no command recorded;
- `RenderDrawInputSource::FullscreenTriangle`: `vkCmdDraw(3, 1, 0, 0)`;
- scene draw input: execute `RenderDrawInput::drawCommands`;
- compute input: `vkCmdDispatch(groupCountX, groupCountY, groupCountZ)`.

- [ ] **Step 4: Change realtime renderer pass loop**

For each compiled `FramePass`:

```cpp
std::vector<std::unique_ptr<RenderInput>> inputs;
compiler.buildInputs(pass, context, inputs);
std::vector<RenderInputDesc> descs = compiler.prepare(pass, context, inputs);
syncRenderUploadPlan(inputs, descs);
for (const RenderInputDesc &desc : descs) {
  if (!desc.accepted()) {
    recordDiagnostic(desc);
    continue;
  }
  const RenderInput &input = *inputs.at(desc.inputIndex);
  auto pipeline = resourceManager().getOrCreatePipeline(desc);
  cmd.bindPipeline(pipeline);
  cmd.bindResources(resourceManager(), pipeline, desc.bindingPlan);
  cmd.executeRenderInput(input, desc);
}
```

- [ ] **Step 5: Verify Vulkan targets**

Run:

```bash
cmake --build build --target test_vulkan_resource_manager test_vulkan_command_buffer
./build/src/test/test_vulkan_resource_manager
./build/src/test/test_vulkan_command_buffer
```

Expected: pass.

- [ ] **Step 6: Commit backend migration**

```bash
git add src/core/frame_graph/render_upload_plan.hpp src/core/frame_graph/render_upload_plan.cpp src/backend/vulkan/details/resource_manager.hpp src/backend/vulkan/details/resource_manager.cpp src/backend/vulkan/details/commands/command_buffer.hpp src/backend/vulkan/details/commands/command_buffer.cpp src/backend/vulkan/vulkan_realtime_renderer.cpp src/test/integration/test_vulkan_resource_manager.cpp src/test/integration/test_vulkan_command_buffer.cpp
git commit -m "feat: execute Vulkan render inputs from prepared descs"
```

## Task 8: Delete Old Work Path And Migrate Tests

**Files:**
- Delete: `src/core/frame_graph/render_queue.hpp`
- Delete: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/frame_graph/render_validation_contract.hpp`
- Modify: `src/core/frame_graph/render_validation_contract.cpp`
- Modify: build source lists that mention `render_queue.cpp`
- Modify: tests under `src/test/integration/`
- Modify: `src/test/integration/test_helmet_standard_pbr_realtime_smoke.py`

- [ ] **Step 1: Remove old public work model from production code**

Delete every name in the Explicit Hard-Cut Type List from production code.
Do not leave aliases, adapters, wrappers, compatibility overloads, or
compiler-local helpers with those names.

- [ ] **Step 2: Delete old queue files and remove them from the build**

Run:

```bash
git rm src/core/frame_graph/render_queue.hpp src/core/frame_graph/render_queue.cpp
rg -n "render_queue\\.cpp|render_queue\\.hpp|render_queue" CMakeLists.txt src CMakePresets.json
```

Remove any build/source-list entries that still mention the deleted files.
Expected after edits: the `rg` command prints no production build references.

- [ ] **Step 3: Delete old scene work DTOs**

In `src/core/scene/scene.hpp`, delete all old render-work ownership and payload
types from the Explicit Hard-Cut Type List. If another current type needs
`RenderDomain`, move only `RenderDomain` to the clean target-model header where
it is used; do not keep `RenderWorkKind` or old payload structs beside it.

Expected after edits:

```bash
rg -n "RenderWorkItem|RenderWorkKind|DirectRasterWorkPayload|ComputeDispatchWorkPayload|DirectRasterPassPurpose" src/core/scene src/core/frame_graph
```

prints no output.

- [ ] **Step 4: Replace validation wrappers with desc validation**

Use only:

```cpp
struct RenderInputValidationResult final {
  bool ok = false;
  std::vector<RenderInputDiagnostic> diagnostics;
};

RenderInputValidationResult
validatePreparedRenderInputs(const std::vector<RenderInputDesc> &descs);
```

This validation reads desc diagnostics and accepted/rejected status. It does not recreate a second analysis object.

- [ ] **Step 5: Delete old batch and node-context model**

Remove every production definition and use of:

```text
RenderBatch
RenderBatchAnalysis
RenderBatchDiagnosticReason
RenderBatchDiagnostic
RenderBatchStats
RenderBatchPipelineFacts
RenderBatchGeometryResources
RenderIndirectBatch
PreparedRenderDrawCandidate
RenderPathNodeContext
RenderPathNodeData
```

Do not keep a file-local replacement using these names. Move retained behavior
into target-model fields:

- draw commands: `RenderDrawInput::drawCommands`;
- material/object signatures: `RenderDrawInput::objectDataSignature` and
  `RenderDrawInput::materialTypeSignature`;
- accepted/rejected coverage: `RenderInputDesc::status` and
  `RenderInputStats`;
- diagnostics: `RenderInputDesc::diagnostics`;
- pipeline facts: `RenderInputDesc::pipelineBuildDesc`;
- binding/resource facts: `RenderInputDesc::bindingPlan` and
  `RenderInputDesc::resourceDependencies`.

Expected after edits:

```bash
rg -n "RenderBatch\\b|RenderBatchAnalysis|RenderBatchDiagnosticReason|RenderBatchDiagnostic|RenderBatchStats|RenderBatchPipelineFacts|RenderBatchGeometryResources|RenderIndirectBatch|PreparedRenderDrawCandidate|RenderPathNodeContext|RenderPathNodeData" src
```

prints no output.

- [ ] **Step 6: Delete old backend entry points**

Remove old function declarations, definitions, tests, and call sites for:

```text
compileIndirectBatches
executeRenderBatch
fromRenderBatch
fromRenderWorkItem
getOrCreatePipeline(RenderWorkItem)
getOrCreatePipeline(RenderBatch, context)
```

Expected after edits:

```bash
rg -n "compileIndirectBatches|executeRenderBatch|fromRenderBatch|fromRenderWorkItem|getOrCreatePipeline\\(.*RenderWorkItem|getOrCreatePipeline\\(.*RenderBatch" src
```

prints no output.

- [ ] **Step 7: Migrate positive tests**

Rewrite tests to assert:

```cpp
inputs.size()
descs.size()
desc.accepted()
desc.pipelineBuildDesc
desc.bindingPlan
RenderInputStats accepted/rejected/submitted counts
```

Do not keep old type tokens in ordinary test source. Negative compatibility tests must prove rejection through the new API without declaring old types.

- [ ] **Step 8: Rename metadata**

Use input/desc names:

```text
renderInputStats
compilerInputCount
acceptedInputCount
rejectedInputCount
submittedDrawCount
submittedDispatchCount
fallbackObservedCount
```

Helmet smoke must assert non-black output and zero fallback through these names.

- [ ] **Step 9: Run focused test set**

Run:

```bash
cmake --build build --target test_render_work_compiler test_bindless_indirect_contract test_bindless_validation_contract test_frame_graph
./build/src/test/test_render_work_compiler
./build/src/test/test_bindless_indirect_contract
./build/src/test/test_bindless_validation_contract
./build/src/test/test_frame_graph
python3 src/test/integration/test_helmet_standard_pbr_realtime_smoke.py
```

Expected: pass; Helmet output is non-black and reports input/desc stats.

- [ ] **Step 10: Run hard-cut audit before commit**

Run:

```bash
rg -n "RenderWorkItem|RenderWorkKind|DirectRasterWorkPayload|ComputeDispatchWorkPayload|DirectRasterPassPurpose|RenderWorkQueue|RenderBatch\\b|RenderBatchAnalysis|RenderBatchDiagnosticReason|RenderBatchDiagnostic|RenderBatchStats|RenderBatchPipelineFacts|RenderBatchGeometryResources|RenderIndirectBatch|PreparedRenderDrawCandidate|RenderPathNodeContext|RenderPathNodeData|compileIndirectBatches|executeRenderBatch|fromRenderBatch|getOrCreatePipeline\\(.*RenderWorkItem|getOrCreatePipeline\\(.*RenderBatch|fromRenderWorkItem" src assets
```

Expected: no output. If there is output, this task is not complete.

- [ ] **Step 11: Commit deletion and test migration**

```bash
git add src src/test/integration
git commit -m "refactor: remove legacy render work path"
```

## Task 9: Documentation And Final Audit

**Files:**
- Modify: `notes/concepts-design/rendering-pipeline/render-work-compiler.md`
- Modify: `notes/concepts-design/rendering-pipeline/render-path-graph.md`
- Modify: `notes/concepts-design/rendering-pipeline/realtime-offline-shared-flow.md`
- Modify or delete: `notes/concepts-design/rendering-pipeline/render-queue.md`
- Modify: `notes/nav.yml`
- Modify: relevant active REQs under `notes/requirements/`

- [ ] **Step 1: Document the clean flow**

Docs must show:

```text
RenderPathGraph input
  -> FramePass input contract
  -> RenderWorkCompiler
  -> RenderInput[] payloads
  -> RenderInputDesc[] validation/pipeline/binding facts
  -> Vulkan pipeline/upload/execute
```

- [ ] **Step 2: Document input schema**

Include:

```yaml
input:
  kind: scene-renderables
  material:
    type: [matte, uber, metal, substrate, standard-pbr]
    required: true
  geometry:
    vertex: position-only
    topology: triangle-list
```

and:

```yaml
input:
  kind: fullscreen-triangle
```

- [ ] **Step 3: Run final old-token audits**

Run:

```bash
rg -n "RenderWorkItem|RenderWorkKind|DirectRasterWorkPayload|ComputeDispatchWorkPayload|DirectRasterPassPurpose|RenderWorkQueue|RenderBatch\\b|RenderBatchAnalysis|RenderBatchDiagnosticReason|RenderBatchDiagnostic|RenderBatchStats|RenderBatchPipelineFacts|RenderBatchGeometryResources|RenderIndirectBatch|PreparedRenderDrawCandidate|RenderPathNodeContext|RenderPathNodeData|compileIndirectBatches|executeRenderBatch|fromRenderBatch|getOrCreatePipeline\\(.*RenderWorkItem|getOrCreatePipeline\\(.*RenderBatch|fromRenderWorkItem|Pass_OfflineRayTrace|OfflinePrimaryRayCompute" src assets
```

Expected: no output.

Run:

```bash
rg -n "RenderInputAnalysis|ComputeAnalysis|OpaqueBatch|OpaqueGeometry|OpaqueIndirect|Offline.*Compiler|Offline.*Work|compilerBatch|renderBatchStats|VulkanRealtimeRenderBatchStats|VulkanRenderBatchSubmissionStats" src assets
```

Expected: no output.

- [ ] **Step 4: Run final focused verification**

Run:

```bash
cmake --build build --target test_render_path_graph_pass_contract test_render_resource_parsers test_frame_graph test_pipeline_build_info test_render_work_compiler test_bindless_indirect_contract test_bindless_validation_contract test_vulkan_resource_manager test_vulkan_command_buffer
ctest --test-dir build --output-on-failure -R "render_path|frame_graph|pipeline|bindless|render_work|vulkan_resource_manager|vulkan_command_buffer"
```

Expected: all selected focused tests pass; final audit commands produce no output.

- [ ] **Step 5: Run final end-to-end smoke**

Build the editor and registered test targets:

```bash
cmake --build build --target lxe_editor BuildTest
```

Run the full video-device smoke set through CTest:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

Also run the Helmet render smoke directly so its JSON payload is visible in the
log for this hard-cut audit:

```bash
python3 src/test/integration/test_helmet_standard_pbr_realtime_smoke.py \
  --source-dir . \
  --editor build/src/demos/lxe_editor/lxe_editor
```

Expected:

- `test_helmet_standard_pbr_realtime_smoke` passes;
- Helmet output is non-black;
- the JSON stats use `renderInputStats` / input-desc names, not
  `renderBatchStats` or batch-named counters;
- `fallbackObservedCount` is zero;
- `test_lxe_editor_api_blackbox` and other `requires_video_device` tests pass
  under `xvfb-run`.

- [ ] **Step 6: Commit docs and audit closure**

```bash
git add notes/concepts-design/rendering-pipeline notes/nav.yml notes/requirements
git commit -m "docs: document render input compiler hard cut"
```

## Self-Review Notes

- Spec coverage: Task 1 and Task 2 cover schema; Task 3 covers `FramePass` and `FrameGraph::compile()`; Task 4 defines the target data model; Task 5 implements compiler input/desc creation; Task 6 moves pipeline facts into desc preparation; Task 7 moves upload/backend execution to the target model; Task 8 deletes the old path and migrates tests; Task 9 covers docs, final audits, and final end-to-end smoke.
- Model cleanliness: every new public class used by the plan is defined in Target Data Model. The plan does not define or reuse old batch/queue/item diagnostic/result classes as implementation building blocks.
- Type consistency: public names used across tasks are `RenderPassInputKind`, `RenderPassInputContract`, `RenderInputKind`, `RenderInputStatus`, `RenderDrawInputSource`, `RenderInputDiagnosticCode`, `RenderInputDiagnostic`, `RenderDrawCommand`, `RenderInput`, `RenderDrawInput`, `RenderComputeInput`, `RenderInputBindingPlan`, `RenderInputStats`, `RenderInputDesc`, and `RenderWorkCompiler`.
