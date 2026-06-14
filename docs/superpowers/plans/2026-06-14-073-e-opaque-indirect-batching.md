# REQ-073-e Opaque Indirect Batching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the realtime material-source geometry submission path with node-scoped prepared draw batching and Vulkan indirect submission, with no old per-item geometry fallback.

**Architecture:** `RenderWorkQueue` remains the RenderPathNode/pass work owner, but its realtime geometry data changes from old `RenderWorkItem` DTOs to `RenderPathNodeContext` plus `RenderPathNodeData`. Preparation resolves handle/ref-level draw inputs through `SceneResourceTableUploadView`, `RenderBatchCompiler` returns `RenderBatchAnalysis`, and the Vulkan backend submits only the accepted analysis batches.

**Tech Stack:** C++20, CMake/Ninja, LXEngine core frame graph and scene resource table, Vulkan realtime renderer, Python Helmet smoke harness.

---

## Current Code Facts

- `src/core/frame_graph/render_queue.hpp` stores `std::vector<RenderWorkItem> m_items` and exposes `getItems()`.
- `RenderWorkQueue::compileIndirectBatches()` currently returns `std::vector<RenderIndirectBatch>`.
- `RenderIndirectBatch` still carries `PipelineKey`, pass, target, descriptor resources, and vertex/index buffer identity.
- `src/core/frame_graph/render_queue.cpp` filters with `item.kind == RenderWorkKind::RasterDraw`, silently skips missing buffers and zero counts, and splits with `sameDescriptorResources()`.
- `src/backend/vulkan/vulkan_realtime_renderer.cpp` consumes the batch vector but builds a synthetic old `RenderWorkItem` from `items[batch.sourceItemIndices[0]]`, sets `RasterBatch`, then calls `cmd.executeWorkItem(batchItem)`.
- `src/test/integration/test_bindless_indirect_contract.cpp` contains the old positive assertion that descriptor identity changes split indirect batches.
- `src/test/integration/test_helmet_standard_pbr_realtime_smoke.py` checks Helmet non-black output and metadata, but not batch-analysis consumption.
- `REQ-074-d` package serialization/restore does not add a separate 073-e code path. 073-e consumes the resulting `SceneResourceTableUploadView` only.

## File Structure

- Modify: `src/core/frame_graph/render_queue.hpp`
  - Define node-scoped batching types.
  - Change `compileIndirectBatches()` to return `RenderBatchAnalysis`.
  - Remove `RenderIndirectBatch` and realtime geometry `getItems()` dependency from the positive path.
- Modify: `src/core/frame_graph/render_queue.cpp`
  - Build `RenderPathNodeContext` and `RenderPathNodeData`.
  - Resolve `RenderDrawInput` through `SceneResourceTableUploadView`.
  - Emit `PreparedRenderDrawCandidate` or diagnostics.
  - Merge by object data signature plus material type signature.
- Modify: `src/core/frame_graph/render_validation_contract.hpp`
- Modify: `src/core/frame_graph/render_validation_contract.cpp`
  - Replace coverage validation over old source item indices with validation over `RenderBatchAnalysis`.
- Modify: `src/core/pipeline/pipeline_build_desc.hpp`
- Modify: `src/core/pipeline/pipeline_build_desc.cpp`
  - Add geometry pipeline build desc creation from `RenderBatch` plus `RenderPathNodeContext`.
  - Keep non-geometry compute helpers separate from realtime geometry.
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/scene/object.hpp`
- Modify: `src/core/scene/object.cpp`
  - Stop treating `RenderWorkKind` / `RasterDraw` as realtime geometry contract.
  - Ensure `ValidatedRenderablePassData` provides only source facts that scene traversal can know.
- Modify: `src/core/frame_graph/render_upload_plan.cpp`
  - Upload realtime geometry resources from `RenderBatchAnalysis` / scene resource table data, not old raster payloads.
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
  - Submit accepted batches from `RenderBatchAnalysis`.
  - Expose submission stats proving compiler batch consumption.
- Modify: `src/backend/vulkan/details/commands/command_buffer.hpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
  - Add explicit indirect geometry batch execution API.
  - Keep old `executeWorkItem` only for named non-geometry compute/debug allowlisted routes, or delete if no live caller remains.
- Modify: `src/test/integration/test_bindless_indirect_contract.cpp`
  - Convert old descriptor split positive test into a failing old-path audit and new same-signature batching test.
- Modify: `src/test/integration/test_bindless_validation_contract.cpp`
  - Validate `RenderBatchAnalysis` diagnostics and stats.
- Modify: `src/test/integration/test_071_bridge_audit.cpp`
  - Narrow legacy allowlists so old geometry DTO terms are only negative audits.
- Modify: `src/test/integration/test_scene_resource_table.cpp`
  - Update queue/batch checks to use upload-view-driven prepared candidates.
- Modify: `src/test/integration/test_helmet_standard_pbr_realtime_smoke.py`
  - Assert batch/submission stats in realtime render metadata.
- Modify: `src/test/CMakeLists.txt`
  - Add a focused backend batch submission test if no existing test can observe backend stats.

## Target Data Model

Use this concrete shape unless implementation proves a smaller equivalent is cleaner:

```cpp
enum class RenderBatchDiagnosticReason {
  ObjectDataSignatureMismatch,
  MaterialTypeSignatureMismatch,
  SourceMaterialRefUnresolved,
  ObjectDrawRecordUnresolved,
  InvalidSourceMaterialRef,
  InvalidDrawRecord,
  MissingMeshRange,
  InvalidMeshRange,
  ZeroIndexCount,
  ZeroInstanceCount,
  GlobalGeometryTableMissing,
  BackendIndirectUnsupported,
  LegacyInputRejected,
};

struct RenderDrawInput final {
  usize inputIndex = 0;
  ObjectHandle object;
  MaterialHandle material;
  StringID pass;
  StringID debugId;
  Vec3f sortCenter{};
  ShaderProgramSet shaderProgram;
  IShaderSharedPtr shaderInfo;
  RenderState renderState;
  StringID materialTypeSignature;
};

struct RenderPathNodeContext final {
  StringID pass;
  StringID renderPathNodeSignature;
  std::optional<RenderPathNodeRenderingMode> renderingMode;
  std::optional<RenderPathGeometryContract> geometryContract;
  std::vector<RenderPathAttachmentContract> attachments;
  RenderTargetDesc target;
  StringID objectDataSignature = StringID("BindlessObjectData.v1");
  bool backendIndirectSupported = true;
};

struct PreparedRenderDrawCandidate final {
  usize inputIndex = 0;
  u32 drawRecordIndex = u32_max;
  u32 objectIndex = u32_max;
  u32 materialIndex = u32_max;
  u32 materialRefIndex = u32_max;
  u32 sourceStorageIndex = u32_max;
  u32 sourceLocalMaterialIndex = u32_max;
  u32 meshIndex = u32_max;
  u32 indexCount = 0;
  u32 firstIndex = 0;
  i32 vertexOffset = 0;
  u32 instanceCount = 1;
  StringID objectDataSignature;
  StringID materialTypeSignature;
  StringID finalShaderReflectionIdentity;
  StringID debugId;
  Vec3f sortCenter{};
};

struct RenderBatch final {
  usize batchIndex = 0;
  StringID objectDataSignature;
  StringID materialTypeSignature;
  PipelineKey derivedPipelineKey;
  u32 commandOffset = 0;
  u32 commandCount = 0;
  std::vector<IndexedIndirectDrawCommand> commands;
  std::vector<usize> candidateIndices;
};

struct RenderBatchStats final {
  usize inputDrawCount = 0;
  usize preparedCandidateCount = 0;
  usize batchCount = 0;
  usize drawCount = 0;
  usize indirectCapableDrawCount = 0;
  usize unsupportedDrawCount = 0;
  usize legacyRejectedDrawCount = 0;
  usize fallbackObservedCount = 0;
};

struct RenderBatchDiagnostic final {
  RenderBatchDiagnosticReason reason;
  usize inputIndex = 0;
  std::optional<usize> candidateIndex;
  StringID pass;
  StringID debugId;
  StringID objectDataSignature;
  StringID materialTypeSignature;
  std::optional<PipelineKey> derivedPipelineKey;
  u32 drawRecordIndex = u32_max;
  u32 materialRefIndex = u32_max;
  u32 meshIndex = u32_max;
};

struct RenderPathNodeData final {
  std::vector<RenderDrawInput> drawInputs;
  std::vector<PreparedRenderDrawCandidate> preparedCandidates;
  std::vector<RenderBatchDiagnostic> preparationDiagnostics;
};

struct RenderBatchAnalysis final {
  RenderPathNodeContext context;
  std::vector<PreparedRenderDrawCandidate> candidates;
  std::vector<RenderBatch> batches;
  std::vector<RenderBatchDiagnostic> diagnostics;
  RenderBatchStats stats;

  [[nodiscard]] bool ok() const {
    return diagnostics.empty() && stats.fallbackObservedCount == 0;
  }
};
```

## Task 1: Lock Failing Batch Contract Tests

**Files:**
- Modify: `src/test/integration/test_bindless_indirect_contract.cpp`
- Modify: `src/test/integration/test_bindless_validation_contract.cpp`

- [ ] **Step 1: Replace the descriptor split positive test**

In `src/test/integration/test_bindless_indirect_contract.cpp`, replace `testDescriptorChangeSplitsIndirectBatches()` with a test named `testDescriptorIdentityDoesNotSplitSameSignatureBatch()`.

```cpp
void testDescriptorIdentityDoesNotSplitSameSignatureBatch() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 2,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});

  const RenderBatchAnalysis analysis =
      fixture.queue.compileIndirectBatches();

  EXPECT(analysis.ok(), "same signature batch analysis should be accepted");
  EXPECT(analysis.batches.size() == 1,
         "descriptor identity must not split same material type signature");
  EXPECT(analysis.stats.fallbackObservedCount == 0,
         "new batch compiler must not report old fallback usage");
}
```

Add the local helper in the same anonymous namespace. It should create a
`SceneResourceTable`, register one mesh with `indexCount` indices, register one
source material, register `drawCount` objects that reference that mesh/material
pair, call `queue.setNodeContext(...)`, add one `RenderDrawInput` per object,
call `queue.prepareDrawInputs(table.buildUploadView())`, and return the table
plus queue so upload-view spans remain valid while the analysis runs.

Also update `main()` to call `testDescriptorIdentityDoesNotSplitSameSignatureBatch()`.

- [ ] **Step 2: Add no-silent-skip diagnostics tests**

Add these tests to `src/test/integration/test_bindless_validation_contract.cpp`:

```cpp
void testZeroIndexCountProducesDiagnostic() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 1,
                            .indexCount = 0,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});

  const RenderBatchAnalysis analysis =
      fixture.queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "zero index count should reject the draw");
  EXPECT(analysis.diagnostics.size() == 1,
         "zero index count should produce exactly one diagnostic");
  EXPECT(analysis.diagnostics.front().reason ==
             RenderBatchDiagnosticReason::ZeroIndexCount,
         "zero index count diagnostic reason should be exact");
}

void testMissingDrawRecordProducesDiagnostic() {
  BatchQueueFixture fixture = makeBatchQueueFixture(
      BatchQueueFixtureDesc{.drawCount = 1,
                            .indexCount = 3,
                            .materialTypeSignature =
                                StringID("standard-pbr-opaque")});
  fixture.queue.clearItems();
  fixture.queue.addDrawInput(RenderDrawInput{
      .inputIndex = 0,
      .object = ObjectHandle{},
      .material = fixture.material,
      .pass = StringID("Forward"),
      .debugId = StringID("helmet.missingObject"),
      .materialTypeSignature = StringID("standard-pbr-opaque")});
  fixture.queue.prepareDrawInputs(fixture.table.buildUploadView());

  const RenderBatchAnalysis analysis =
      fixture.queue.compileIndirectBatches();

  EXPECT(!analysis.ok(), "missing object/draw record should reject the draw");
  EXPECT(analysis.diagnostics.front().reason ==
             RenderBatchDiagnosticReason::ObjectDrawRecordUnresolved,
         "missing object record reason should be exact");
}
```

- [ ] **Step 3: Run tests and capture the failing baseline**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract test_bindless_validation_contract
```

Expected: FAIL. Acceptable first failure is compile failure for missing `RenderBatchAnalysis`, `setNodeContext`, or `addDrawInput`. If it still compiles and the old descriptor split test passes, the test edit was incomplete.

- [ ] **Step 4: Commit failing tests**

```bash
git add src/test/integration/test_bindless_indirect_contract.cpp src/test/integration/test_bindless_validation_contract.cpp
git commit -m "test: lock 073e batch analysis contract"
```

## Task 2: Introduce Queue-Owned Batch Analysis Types

**Files:**
- Modify: `src/core/frame_graph/render_queue.hpp`
- Modify: `src/core/frame_graph/render_queue.cpp`

- [ ] **Step 1: Add the target data model to `render_queue.hpp`**

Add the types from the Target Data Model section above near `RenderWorkQueue`. Remove `RenderIndirectBatch` from the header. Change the public API:

```cpp
void setNodeContext(RenderPathNodeContext context);
void addDrawInput(RenderDrawInput input);
void prepareDrawInputs(const SceneResourceTableUploadView &uploadView);
const RenderPathNodeData &nodeData() const;
const RenderBatchAnalysis &lastBatchAnalysis() const;
RenderBatchAnalysis compileIndirectBatches() const;
```

Keep `compileIndirectBatches()` as the queue-owned entry point so old call sites break on return-type and field changes instead of silently keeping the vector path.

- [ ] **Step 2: Replace old item storage for realtime geometry**

In `RenderWorkQueue`, replace `std::vector<RenderWorkItem> m_items` for realtime geometry with:

```cpp
std::optional<RenderPathNodeContext> m_context;
RenderPathNodeData m_nodeData;
mutable RenderBatchAnalysis m_lastBatchAnalysis;
```

If compute/offline still needs a work item container during this task, name it `m_nonGeometryDispatchItems` and keep it private. Do not expose it through `getItems()` for realtime geometry.

- [ ] **Step 3: Add minimal compiling implementations**

In `render_queue.cpp`, implement `setNodeContext`, `addDrawInput`,
`prepareDrawInputs`, `nodeData`, and a minimal `compileIndirectBatches()` that
returns diagnostics for every input until preparation is implemented:

```cpp
RenderBatchAnalysis RenderWorkQueue::compileIndirectBatches() const {
  RenderBatchAnalysis analysis;
  if (m_context.has_value()) {
    analysis.context = *m_context;
  }
  analysis.stats.inputDrawCount = m_nodeData.drawInputs.size();
  for (usize i = 0; i < m_nodeData.drawInputs.size(); ++i) {
    RenderBatchDiagnostic diagnostic;
    diagnostic.reason = RenderBatchDiagnosticReason::GlobalGeometryTableMissing;
    diagnostic.inputIndex = i;
    diagnostic.pass = analysis.context.pass;
    diagnostic.debugId = m_nodeData.drawInputs[i].debugId;
    diagnostic.objectDataSignature = analysis.context.objectDataSignature;
    diagnostic.materialTypeSignature =
        m_nodeData.drawInputs[i].materialTypeSignature;
    analysis.diagnostics.push_back(std::move(diagnostic));
  }
  analysis.stats.unsupportedDrawCount = analysis.diagnostics.size();
  m_lastBatchAnalysis = analysis;
  return analysis;
}
```

- [ ] **Step 4: Build focused tests**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract test_bindless_validation_contract
```

Expected: compile progresses past missing type errors. New tests fail behaviorally because every input is rejected with `GlobalGeometryTableMissing`.

- [ ] **Step 5: Commit batch analysis skeleton**

```bash
git add src/core/frame_graph/render_queue.hpp src/core/frame_graph/render_queue.cpp
git commit -m "feat: introduce render batch analysis model"
```

## Task 3: Build Draw Inputs From RenderPathNode Traversal

**Files:**
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/core/scene/object.hpp`
- Modify: `src/core/scene/object.cpp`
- Modify: `src/test/integration/test_frame_graph.cpp`
- Modify: `src/test/integration/test_scene_resource_table.cpp`

- [ ] **Step 1: Remove old `makeItemFromValidatedData` from realtime geometry**

Delete the anonymous helper that constructs `RenderWorkItem` from `ValidatedRenderablePassData`. Replace the realtime branch in `RenderWorkQueue::buildRealtime()` with construction of `RenderDrawInput`.

```cpp
RenderDrawInput input;
input.inputIndex = m_nodeData.drawInputs.size();
input.object = validatedData.objectHandle;
input.material = validatedData.materialHandle;
input.pass = pass;
input.debugId = renderable->getDebugId();
input.sortCenter = validatedData.sortCenter;
input.shaderProgram = validatedData.shaderProgram;
input.shaderInfo = validatedData.shaderInfo;
input.renderState = validatedData.renderState;
input.materialTypeSignature = validatedData.materialTypeVariant;
m_nodeData.drawInputs.push_back(std::move(input));
```

- [ ] **Step 2: Build `RenderPathNodeContext` from the existing build arguments**

At the start of `buildRealtime()`, set:

```cpp
RenderPathNodeContext context;
context.pass = pass;
context.renderPathNodeSignature = renderPathNodeSignature;
context.renderingMode = renderingMode;
context.geometryContract = geometryContract;
context.attachments = std::move(attachments);
context.target = target.toDesc();
context.objectDataSignature = StringID("BindlessObjectData.v1");
context.backendIndirectSupported = true;
setNodeContext(std::move(context));
```

- [ ] **Step 3: Preserve source facts only**

Ensure `ValidatedRenderablePassData` no longer needs `objectSignature`, `materialSignature`, or per-item `renderPathNodeSignature` for realtime geometry batching. If a non-geometry test still reads those fields, move that assertion to pipeline/build-desc tests or a negative audit.

- [ ] **Step 4: Run focused frame graph tests**

Run:

```bash
cmake --build build --target test_frame_graph test_scene_resource_table
```

Expected: either PASS for unaffected tests or compile failures only at old `RenderWorkItem` geometry assertions. Fix those assertions to inspect `RenderPathNodeData` and `RenderBatchAnalysis`.

- [ ] **Step 5: Commit draw input traversal**

```bash
git add src/core/frame_graph/render_queue.cpp src/core/scene/object.hpp src/core/scene/object.cpp src/test/integration/test_frame_graph.cpp src/test/integration/test_scene_resource_table.cpp
git commit -m "feat: build render draw inputs for node batching"
```

## Task 4: Implement Preparation From `SceneResourceTableUploadView`

**Files:**
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp` only if a required handle-to-index mapping is missing
- Modify: `src/test/integration/test_bindless_indirect_contract.cpp`
- Modify: `src/test/integration/test_scene_resource_table.cpp`

- [ ] **Step 1: Add resolver helpers in `render_queue.cpp`**

Add local helpers:

```cpp
std::optional<u32> findObjectIndex(const SceneResourceTableUploadView &view,
                                   ObjectHandle handle);
std::optional<u32> findMaterialIndex(const SceneResourceTableUploadView &view,
                                     MaterialHandle handle);
std::optional<PreparedRenderDrawCandidate>
prepareDrawCandidate(const RenderPathNodeContext &context,
                     const RenderDrawInput &input,
                     const SceneResourceTableUploadView &view,
                     std::vector<RenderBatchDiagnostic> &diagnostics);
```

`prepareDrawCandidate()` resolves:

- `objectIndex` from `view.objectIndexByHandle`.
- `drawRecordIndex` from the same typed object index when `view.draws` has that row.
- `meshIndex` and `materialRefIndex` from `view.draws[drawRecordIndex]`.
- `materialIndex` from `view.materialIndexByHandle`.
- `SceneGpuMeshRecord` from `view.meshes[meshIndex]`.
- source-local material storage from `view.materialRefs[materialRefIndex]`.

Implement `RenderWorkQueue::prepareDrawInputs()` as the only stage that writes
`m_nodeData.preparedCandidates` and `m_nodeData.preparationDiagnostics`:

```cpp
void RenderWorkQueue::prepareDrawInputs(
    const SceneResourceTableUploadView &uploadView) {
  m_nodeData.preparedCandidates.clear();
  m_nodeData.preparationDiagnostics.clear();
  for (const RenderDrawInput &input : m_nodeData.drawInputs) {
    std::vector<RenderBatchDiagnostic> diagnostics;
    auto candidate =
        prepareDrawCandidate(*m_context, input, uploadView, diagnostics);
    m_nodeData.preparationDiagnostics.insert(
        m_nodeData.preparationDiagnostics.end(),
        std::make_move_iterator(diagnostics.begin()),
        std::make_move_iterator(diagnostics.end()));
    if (candidate.has_value()) {
      m_nodeData.preparedCandidates.push_back(std::move(*candidate));
    }
  }
}
```

- [ ] **Step 2: Emit exact diagnostics during preparation**

Map failures to exact reasons:

```cpp
ObjectHandle{} or missing object row -> ObjectDrawRecordUnresolved
missing draw row -> InvalidDrawRecord
missing mesh row -> MissingMeshRange
mesh.indexCount == 0 -> ZeroIndexCount
candidate.instanceCount == 0 -> ZeroInstanceCount
missing material ref -> SourceMaterialRefUnresolved
invalid material ref row -> InvalidSourceMaterialRef
missing source storage row -> InvalidSourceMaterialRef
```

- [ ] **Step 3: Build indirect commands from global table ranges**

For each accepted candidate:

```cpp
candidate.indexCount = mesh.indexCount;
candidate.firstIndex = mesh.indexOffset;
candidate.vertexOffset = static_cast<i32>(mesh.vertexOffset);
candidate.instanceCount = 1;
```

Do not read `GpuResourceRef` vertex/index buffers for batching. If a required mesh range is not present in the upload view, reject with `GlobalGeometryTableMissing` or `MissingMeshRange`.

- [ ] **Step 4: Run preparation tests**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract test_scene_resource_table
```

Expected: preparation diagnostics tests pass; same-signature batching may still fail until Task 5 merges candidates.

- [ ] **Step 5: Commit preparation**

```bash
git add src/core/frame_graph/render_queue.cpp src/core/scene/scene_resource_table_upload_view.hpp src/test/integration/test_bindless_indirect_contract.cpp src/test/integration/test_scene_resource_table.cpp
git commit -m "feat: prepare render draw candidates from scene tables"
```

## Task 5: Implement Batch Compiler Compatibility

**Files:**
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/core/frame_graph/render_validation_contract.*`
- Modify: `src/test/integration/test_bindless_indirect_contract.cpp`
- Modify: `src/test/integration/test_bindless_validation_contract.cpp`

- [ ] **Step 1: Merge candidates by the 073-e batch key**

Inside `compileIndirectBatches()`, after preparation:

```cpp
const bool compatible =
    current.objectDataSignature == candidate.objectDataSignature &&
    current.materialTypeSignature == candidate.materialTypeSignature;
```

Do not compare `PipelineKey`, `RenderPathNodeSignature`, target, attachments, vertex/index buffer resource identity, topology, descriptor resources, material URI/name, material parameter values, or texture resource identity.

- [ ] **Step 2: Derive pipeline lookup from context and batch identity**

For 073-e, compute the derived backend key as:

```cpp
batch.derivedPipelineKey =
    PipelineKey::build(batch.materialTypeSignature,
                       analysis.context.renderPathNodeSignature);
```

This is a backend lookup product, not a batch split key.

- [ ] **Step 3: Fill stats and coverage**

Populate:

```cpp
analysis.stats.inputDrawCount = m_nodeData.drawInputs.size();
analysis.stats.preparedCandidateCount = analysis.candidates.size();
analysis.stats.batchCount = analysis.batches.size();
analysis.stats.drawCount = sum(batch.commands.size());
analysis.stats.indirectCapableDrawCount = analysis.stats.drawCount;
analysis.stats.fallbackObservedCount = 0;
```

If any candidate is rejected, increment `unsupportedDrawCount` or `legacyRejectedDrawCount` according to the reason.

- [ ] **Step 4: Replace validation contract coverage**

`validateBindlessMigratedQueue()` should call `compileIndirectBatches()` and validate `RenderBatchAnalysis` directly:

```cpp
const RenderBatchAnalysis analysis = queue.compileIndirectBatches();
result.coveredItemCount = analysis.stats.drawCount;
result.ok = analysis.ok() &&
            analysis.stats.inputDrawCount == analysis.stats.drawCount;
```

Translate `RenderBatchDiagnostic` into existing validation diagnostics until the old validation type is deleted.

- [ ] **Step 5: Run compiler tests**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract test_bindless_validation_contract
./build/src/test/test_bindless_indirect_contract
./build/src/test/test_bindless_validation_contract
```

Expected: PASS. The descriptor identity test must assert one batch, not two.

- [ ] **Step 6: Commit compiler behavior**

```bash
git add src/core/frame_graph/render_queue.cpp src/core/frame_graph/render_validation_contract.hpp src/core/frame_graph/render_validation_contract.cpp src/test/integration/test_bindless_indirect_contract.cpp src/test/integration/test_bindless_validation_contract.cpp
git commit -m "feat: compile render batches by material and object ABI"
```

## Task 6: Hard Cut Old Realtime Geometry DTO Paths

**Files:**
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/pipeline/pipeline_build_desc.*`
- Modify: `src/core/frame_graph/render_upload_plan.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.*`
- Modify: `src/backend/vulkan/offline/offline_render_graph_executor.cpp`
- Modify: `src/backend/vulkan/details/ibl_bake_renderer.cpp`
- Modify: affected tests under `src/test/integration/`

- [ ] **Step 1: Remove `RasterDraw` and `RasterBatch` from production geometry routing**

Delete or isolate the old realtime raster payload fields:

```cpp
RenderWorkKind::RasterDraw
RenderWorkKind::RasterBatch
RasterDrawWorkPayload
RasterBatchWorkPayload
RenderWorkItem::raster
RenderWorkItem::rasterBatch
```

If `RenderWorkItem` remains for compute/offline, rename the type or fields so the remaining API cannot submit realtime material-source geometry. Allowed remaining paths must be named compute, debug, fullscreen, IBL bake, or negative audit.

- [ ] **Step 2: Remove old batch split helpers**

Delete from `render_queue.cpp`:

```cpp
sameResourceRef()
sameDescriptorResources()
canAppendToBatch()
resolveIndexCount(const RasterDrawWorkPayload &)
makeIndirectCommand(const RasterDrawWorkPayload &)
```

Use prepared candidate data for indirect commands.

- [ ] **Step 3: Update pipeline build desc creation**

Add a geometry batch helper:

```cpp
PipelineBuildDesc PipelineBuildDesc::fromRenderBatch(
    const RenderBatch &batch,
    const RenderPathNodeContext &context,
    const ShaderProgramSet &shaderProgram,
    const RenderState &renderState);
```

Keep compute/offline `fromRenderWorkItem()` only if its callers are non-geometry and named that way.

- [ ] **Step 4: Run hard-cut compile check**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract test_bindless_validation_contract test_frame_graph test_scene_resource_table
```

Expected: PASS with no compile references to old positive geometry DTO fields.

- [ ] **Step 5: Commit hard cut**

```bash
git add src/core/scene/scene.hpp src/core/pipeline/pipeline_build_desc.hpp src/core/pipeline/pipeline_build_desc.cpp src/core/frame_graph/render_upload_plan.cpp src/backend/vulkan/details/commands/command_buffer.hpp src/backend/vulkan/details/commands/command_buffer.cpp src/backend/vulkan/offline/offline_render_graph_executor.cpp src/backend/vulkan/details/ibl_bake_renderer.cpp src/test/integration
git commit -m "refactor: remove old realtime geometry work item routing"
```

## Task 7: Make Vulkan Backend Consume Batch Analysis Directly

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.hpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
- Modify: `src/test/integration/test_vulkan_command_buffer.cpp`
- Modify: `src/test/integration/test_vulkan_resource_manager.cpp`
- Modify: `src/test/CMakeLists.txt` if a new focused test is needed

- [ ] **Step 1: Add submission stats**

Add a backend-visible stats struct near the realtime renderer or command submission boundary:

```cpp
struct VulkanRenderBatchSubmissionStats final {
  usize compilerBatchCountConsumed = 0;
  usize submittedIndirectBatchCount = 0;
  usize submittedIndirectDrawCount = 0;
  u32 firstCommandOffset = 0;
  u32 lastCommandOffset = 0;
  usize fallbackObservedCount = 0;
};
```

- [ ] **Step 2: Submit analysis batches without rebuilding old work items**

Replace the current `BindlessBatch` branch in `submitBindlessQueue()`:

```cpp
const RenderBatchAnalysis analysis = queue.compileIndirectBatches();
if (!analysis.ok()) {
  throw std::runtime_error(formatFirstRenderBatchDiagnostic(analysis));
}
for (const RenderBatch &batch : analysis.batches) {
  auto pipeline = resourceManager().getOrCreatePipeline(batch, analysis.context);
  cmd.bindPipeline(pipeline);
  cmd.bindSceneBindlessResources(resourceManager(), pipeline, analysis.context);
  cmd.executeRenderBatch(batch);
}
```

The implementation must not read `queue.getItems()`, `RenderDrawInput`, or old raster payloads in this branch.

- [ ] **Step 3: Add command buffer indirect batch API**

Add:

```cpp
void VulkanCommandBuffer::executeRenderBatch(const RenderBatch &batch);
```

Implementation records indexed indirect commands from `batch.commands`. It does not inspect material descriptors or per-item raster fields.

- [ ] **Step 4: Add focused backend regression**

In `src/test/integration/test_vulkan_command_buffer.cpp`, add a test that constructs a two-command `RenderBatch`, calls `executeRenderBatch()`, and asserts command-buffer stats report exactly one indirect batch and two indirect draws.

- [ ] **Step 5: Run backend tests**

Run:

```bash
cmake --build build --target test_vulkan_command_buffer test_vulkan_resource_manager
./build/src/test/test_vulkan_command_buffer
./build/src/test/test_vulkan_resource_manager
```

Expected: PASS. The test must fail if backend rebuilds a `RenderWorkItem` or executes per-item raster commands.

- [ ] **Step 6: Commit backend consumption**

```bash
git add src/backend/vulkan/vulkan_realtime_renderer.cpp src/backend/vulkan/details/commands/command_buffer.hpp src/backend/vulkan/details/commands/command_buffer.cpp src/test/integration/test_vulkan_command_buffer.cpp src/test/integration/test_vulkan_resource_manager.cpp src/test/CMakeLists.txt
git commit -m "feat: submit compiler batches through Vulkan indirect draws"
```

## Task 8: Extend Helmet Smoke For Batch Observability

**Files:**
- Modify: `src/tools/lxe_realtime_render/lxe_realtime_render.py`
- Modify: `src/test/integration/test_helmet_standard_pbr_realtime_smoke.py`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`

- [ ] **Step 1: Export batch stats to realtime metadata**

Ensure realtime render metadata includes:

```json
{
  "renderBatchStats": {
    "compilerBatchCountConsumed": 1,
    "submittedIndirectBatchCount": 1,
    "submittedIndirectDrawCount": 1,
    "fallbackObservedCount": 0
  }
}
```

Use actual counts from `VulkanRenderBatchSubmissionStats`.

- [ ] **Step 2: Update Helmet smoke assertions**

In `test_helmet_standard_pbr_realtime_smoke.py`, after loading `payload`, add:

```python
batch_stats = payload.get("renderBatchStats", {})
self.assertGreater(int(batch_stats.get("compilerBatchCountConsumed", 0)), 0)
self.assertEqual(
    int(batch_stats.get("compilerBatchCountConsumed", 0)),
    int(batch_stats.get("submittedIndirectBatchCount", -1)),
)
self.assertGreater(int(batch_stats.get("submittedIndirectDrawCount", 0)), 0)
self.assertEqual(int(batch_stats.get("fallbackObservedCount", -1)), 0)
```

- [ ] **Step 3: Run Helmet smoke**

Run:

```bash
cmake --build build --target lxe_editor
xvfb-run -a ctest --output-on-failure -R test_helmet_standard_pbr_realtime_smoke
```

Expected: PASS. The test must fail if metadata is missing, the output is black, or fallback count is non-zero.

- [ ] **Step 4: Commit Helmet smoke observability**

```bash
git add src/tools/lxe_realtime_render/lxe_realtime_render.py src/test/integration/test_helmet_standard_pbr_realtime_smoke.py src/backend/vulkan/vulkan_realtime_renderer.cpp
git commit -m "test: prove helmet uses indirect render batches"
```

## Task 9: Hard-Cut rg Audit And Full Verification

**Files:**
- Modify: only files with audit failures

- [ ] **Step 1: Run deleted concept audits**

Run:

```bash
rg -n "OpaqueBatch|OpaqueGeometry|OpaqueIndirect" src/core src/backend src/test
rg -n "RenderWorkKind|RasterDraw|RasterBatch|\\.kind\\b" src/core src/backend src/test
rg -n "DescriptorResourceList|sameDescriptorResources|descriptor-resource-mismatch" src/core src/backend src/test
rg -n "vertex-layout-mismatch|topology-mismatch|target-mismatch|geometry-buffer-mismatch" src/core src/backend src/test
rg -n "executeWorkItem|direct.*draw|per-item" src/backend src/core src/test
```

Expected:

- No production hit for old realtime geometry routing.
- Any `RenderWorkKind` / `executeWorkItem` hit is non-geometry compute/debug/IBL/offline or named negative audit.
- No `sameDescriptorResources` hit.
- No positive test expects descriptor/resource/vertex/topology/target/geometry-buffer identity to split batches.

- [ ] **Step 2: Run core tests**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract test_bindless_validation_contract test_frame_graph test_scene_resource_table test_vulkan_command_buffer test_vulkan_resource_manager
./build/src/test/test_bindless_indirect_contract
./build/src/test/test_bindless_validation_contract
./build/src/test/test_frame_graph
./build/src/test/test_scene_resource_table
./build/src/test/test_vulkan_command_buffer
./build/src/test/test_vulkan_resource_manager
```

Expected: all commands exit 0.

- [ ] **Step 3: Run broader non-video suite**

Run:

```bash
ctest --output-on-failure -L auto -LE requires_video_device
```

Expected: all selected tests pass.

- [ ] **Step 4: Run video smoke**

Run:

```bash
xvfb-run -a ctest --output-on-failure -R test_helmet_standard_pbr_realtime_smoke
```

Expected: Helmet smoke passes with non-black output and `fallbackObservedCount == 0`.

- [ ] **Step 5: Commit audit cleanup**

```bash
git add src/core src/backend src/test
git commit -m "chore: audit 073e legacy geometry paths"
```

## Completion Checklist

- [ ] `RenderBatchAnalysis` is the only positive backend input for realtime material-source geometry.
- [ ] `RenderWorkKind` / `RasterDraw` / `RasterBatch` cannot submit realtime material-source geometry.
- [ ] Descriptor identity, vertex buffer identity, target, attachment, topology, and old mesh-derived object signature are not batch split keys.
- [ ] `PipelineKey` is derived for backend lookup and not independently compared as a batch key.
- [ ] Every input draw is prepared or diagnosed; every prepared candidate is batched or diagnosed.
- [ ] Backend stats prove compiler batch count consumed equals submitted indirect batch count.
- [ ] Helmet smoke proves non-black output and zero fallback.
- [ ] rg audits have no ordinary production or positive-test hits for deleted geometry concepts.
