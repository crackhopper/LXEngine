# REQ-072 Legacy Removal And Bindless Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove legacy default material/render fallback paths from migrated validation rendering and prove the bindless architecture is usable on a minimal Vulkan realtime scene.

**Architecture:** Add a strict validation contract around bindless rendering, make renderer fallback decisions observable, migrate default validation work to global scene/material/object resources, and prune legacy helper chains only after caller migration proves them redundant or unreachable. This plan starts with tests that fail on the current fallback behavior, then implements the smallest bindless validation path that can render non-black and reject legacy execution.

**Tech Stack:** C++20, CMake/Ninja, Vulkan, GLSL, LXEngine `SceneResourceTable`, `RenderWorkQueue`, `IGpuResourceTable`, xvfb/CTest integration tests.

---

## File Structure

- Create: `src/core/frame_graph/render_validation_contract.hpp`
  - Owns small, backend-independent validation diagnostics and compatibility helpers for migrated bindless passes.
- Modify: `src/core/frame_graph/render_queue.hpp`
  - Adds strict indirect/bindless coverage query APIs used by tests and renderer.
- Modify: `src/core/frame_graph/render_queue.cpp`
  - Implements coverage diagnostics without binding to Vulkan.
- Modify: `src/backend/vulkan/vulkan_gpu_resource_table.hpp`
  - Stores enough real table state for bindless slot deduplication, buffer/image/sampler bookkeeping, pipeline cache hit/miss, and progress.
- Modify: `src/backend/vulkan/vulkan_gpu_resource_table.cpp`
  - Replaces fake fixed slot and fake pipeline behavior with deterministic stateful behavior. This phase can remain a shell around real Vulkan objects only where no device is available, but tests must stop accepting no-op state.
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
  - Makes validation-mode migrated pass fallback fail explicitly instead of silently executing per-item legacy path.
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
  - Isolates legacy per-item descriptor/push-constant binding so migrated validation passes cannot reach it.
- Modify: `src/test/integration/test_bindless_indirect_contract.cpp`
  - Strengthens `VulkanGpuResourceTable` and queue tests.
- Modify: `src/test/integration/test_071_bridge_audit.cpp`
  - Converts synthetic bridge test into strict fallback rejection coverage.
- Create: `src/test/integration/test_bindless_validation_contract.cpp`
  - Tests validation diagnostics and real renderer-decision helpers without requiring a Vulkan device.
- Modify: `src/test/CMakeLists.txt`
  - Registers the new test target.
- Modify: `notes/requirements/072-071-closure-audit-and-validation-fixes.md`
  - Marks 072-D-first as the first active implementation phase and records validation results.
- Modify: `notes/requirements/071-d-gpu-resource-table-pipeline-cache-and-upload-tasks.md`
  - Records actual default-path status after this phase.
- Modify: `notes/requirements/071-f-rendering-equivalence-helmet-bmw-validation.md`
  - Records bridge audit outcome after this phase.

## Task 0: Baseline And Guardrails

**Files:**
- Modify: none

- [x] **Step 1: Inspect relevant dirty worktree paths**

Run:

```bash
git status --short src/core/frame_graph src/backend/vulkan src/test notes/requirements docs/superpowers/plans
```

Expected: Only known unrelated dirty docs/assets plus this plan if not committed. Do not revert unrelated changes.

- [x] **Step 2: Capture current targeted test baseline**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract test_071_bridge_audit
build/src/test/test_bindless_indirect_contract
build/src/test/test_071_bridge_audit
```

Expected: Current tests pass, but they are too weak because they accept fake bindless slot state and do not reject fallback.

- [x] **Step 3: Commit this plan**

Run:

```bash
git add docs/superpowers/plans/2026-06-11-072-legacy-removal-bindless-validation.md
git commit -m "Plan REQ-072 legacy removal bindless validation"
```

Expected: Plan-only commit.

## Task 1: Add Bindless Validation Contract Tests

**Files:**
- Create: `src/test/integration/test_bindless_validation_contract.cpp`
- Create: `src/core/frame_graph/render_validation_contract.hpp`
- Modify: `src/test/CMakeLists.txt`

- [x] **Step 1: Add failing contract tests**

Create `src/test/integration/test_bindless_validation_contract.cpp`:

```cpp
#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <iostream>
#include <vector>

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

struct TestResource final : public IGpuResource {
  TestResource(ResourceType type, StringID bindingName, u32 byteSize)
      : type(type), bindingName(bindingName), bytes(byteSize, 0) {}

  ResourceType getType() const override { return type; }
  const void *getRawData() const override { return bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(bytes.size()); }
  StringID getBindingName() const override { return bindingName; }

  ResourceType type = ResourceType::None;
  StringID bindingName;
  std::vector<u8> bytes;
};

RenderWorkItem makeMigratedDraw(const IGpuResource &vertex,
                                const IGpuResource &index,
                                std::optional<PerDrawDataSharedPtr> drawData) {
  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::RasterDraw;
  item.pass = StringID("Forward");
  item.debugId = StringID("bindless.validation.draw");
  item.objectSignature = StringID("bindless.validation.mesh");
  item.materialSignature = StringID("bindless.validation.material");
  RenderTargetDesc target;
  target.role = RenderTargetRole::Swapchain;
  target.colorFormat = ImageFormat::BGRA8;
  target.depthFormat = ImageFormat::D32Float;
  item.target = target;
  item.pipelineKey =
      PipelineKey::build(item.objectSignature, item.materialSignature,
                         target.getPipelineSignature());
  item.raster.vertexBuffer = GpuResourceRef{vertex};
  item.raster.indexBuffer = GpuResourceRef{index};
  item.raster.indexCount = 3;
  item.raster.instanceCount = 1;
  if (drawData.has_value()) {
    item.raster.drawData = *drawData;
  }
  return item;
}

void testStrictContractAcceptsFullyCoveredBatch() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkQueue queue;
  queue.addItem(makeMigratedDraw(vertex, index, std::nullopt));
  queue.addItem(makeMigratedDraw(vertex, index, std::nullopt));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(result.ok, "strict bindless contract should accept full batch coverage");
  EXPECT(result.coveredItemCount == 2,
         "strict bindless contract should report covered items");
  EXPECT(result.diagnostics.empty(),
         "strict bindless contract should not emit diagnostics on success");
}

void testStrictContractRejectsDrawDataFallback() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  auto drawData = std::make_shared<PerDrawData>(sizeof(Mat4f));
  RenderWorkQueue queue;
  queue.addItem(makeMigratedDraw(vertex, index, drawData));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!result.ok, "strict bindless contract should reject drawData fallback");
  EXPECT(!result.diagnostics.empty(),
         "strict bindless contract should explain the rejection");
  EXPECT(result.diagnostics.front().reason.find("drawData") != std::string::npos,
         "diagnostic should name drawData as the fallback cause");
}

void testStrictContractRejectsPartialCoverage() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource missingIndex(ResourceType::None, StringID{}, 0);
  RenderWorkQueue queue;
  queue.addItem(makeMigratedDraw(vertex, missingIndex, std::nullopt));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!result.ok, "strict bindless contract should reject partial coverage");
  EXPECT(!result.diagnostics.empty(),
         "strict bindless contract should report uncovered draw item");
}

} // namespace

int main() {
  testStrictContractAcceptsFullyCoveredBatch();
  testStrictContractRejectsDrawDataFallback();
  testStrictContractRejectsPartialCoverage();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless validation contract checks failed\n";
    return 1;
  }
  return 0;
}
```

- [x] **Step 2: Add declaration header for compile failure shape**

Create `src/core/frame_graph/render_validation_contract.hpp` with only declarations:

```cpp
#pragma once

#include "core/frame_graph/render_queue.hpp"

#include <string>
#include <vector>

namespace LX_core {

struct BindlessValidationDiagnostic final {
  usize itemIndex = 0;
  StringID pass;
  StringID debugId;
  std::string reason;
};

struct BindlessValidationResult final {
  bool ok = false;
  usize coveredItemCount = 0;
  std::vector<BindlessValidationDiagnostic> diagnostics;
};

[[nodiscard]] BindlessValidationResult
validateBindlessMigratedQueue(const RenderWorkQueue &queue, StringID pass);

} // namespace LX_core
```

- [x] **Step 3: Register the test target**

Modify `src/test/CMakeLists.txt` by adding `test_bindless_validation_contract` to the integration test target list near the existing 071 tests:

```cmake
  test_bindless_indirect_contract
  test_bindless_validation_contract
  test_071_bridge_audit
```

- [x] **Step 4: Run test to verify failure**

Run:

```bash
cmake --build build --target test_bindless_validation_contract
```

Expected: FAIL at link time because `validateBindlessMigratedQueue` is declared but not implemented.

## Task 2: Implement Queue Coverage Diagnostics

**Files:**
- Modify: `src/core/frame_graph/render_validation_contract.hpp`
- Create: `src/core/frame_graph/render_validation_contract.cpp`
- Modify: `src/core/frame_graph/render_queue.hpp`
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/test/CMakeLists.txt`

- [x] **Step 1: Add implementation file to build**

Update the CMake source list that contains `src/core/frame_graph/render_queue.cpp` to include:

```cmake
src/core/frame_graph/render_validation_contract.cpp
```

Use `rg -n "render_queue.cpp|frame_graph.cpp" CMakeLists.txt src -g 'CMakeLists.txt'` to find the exact list.

- [x] **Step 2: Implement diagnostics**

Create `src/core/frame_graph/render_validation_contract.cpp`:

```cpp
#include "core/frame_graph/render_validation_contract.hpp"

#include <string>
#include <unordered_set>

namespace LX_core {
namespace {

[[nodiscard]] std::string reasonForUncoveredItem(const RenderWorkItem &item) {
  if (item.kind != RenderWorkKind::RasterDraw) {
    return "item is not a raster draw";
  }
  if (!item.raster.vertexBuffer.isValid()) {
    return "raster draw has no vertex buffer";
  }
  if (!item.raster.indexBuffer.isValid()) {
    return "raster draw has no index buffer";
  }
  if (item.raster.drawData) {
    return "raster draw still uses per-draw drawData push constants";
  }
  if (item.raster.indexCount == 0) {
    return "raster draw has zero indexCount";
  }
  if (item.raster.instanceCount == 0) {
    return "raster draw has zero instanceCount";
  }
  return "raster draw was not covered by an indirect bindless batch";
}

} // namespace

BindlessValidationResult
validateBindlessMigratedQueue(const RenderWorkQueue &queue, StringID pass) {
  BindlessValidationResult result;
  const auto &items = queue.getItems();
  const auto batches = queue.compileIndirectBatches();
  std::unordered_set<usize> covered;
  for (const auto &batch : batches) {
    for (const usize index : batch.sourceItemIndices) {
      covered.insert(index);
    }
  }
  result.coveredItemCount = covered.size();

  for (usize i = 0; i < items.size(); ++i) {
    const RenderWorkItem &item = items[i];
    if (covered.find(i) != covered.end()) {
      continue;
    }
    BindlessValidationDiagnostic diagnostic;
    diagnostic.itemIndex = i;
    diagnostic.pass = pass;
    diagnostic.debugId = item.debugId;
    diagnostic.reason = reasonForUncoveredItem(item);
    result.diagnostics.push_back(std::move(diagnostic));
  }

  result.ok = result.diagnostics.empty();
  return result;
}

} // namespace LX_core
```

- [x] **Step 3: Run contract tests**

Run:

```bash
cmake --build build --target test_bindless_validation_contract
build/src/test/test_bindless_validation_contract
```

Expected: PASS.

- [x] **Step 4: Commit**

Run:

```bash
git add src/core/frame_graph src/test/CMakeLists.txt src/test/integration/test_bindless_validation_contract.cpp
git commit -m "Add bindless validation contract diagnostics"
```

Expected: commit includes the new diagnostics and test.

## Task 3: Make VulkanGpuResourceTable Stateful Enough To Prove Bindless

**Files:**
- Modify: `src/backend/vulkan/vulkan_gpu_resource_table.hpp`
- Modify: `src/backend/vulkan/vulkan_gpu_resource_table.cpp`
- Modify: `src/test/integration/test_bindless_indirect_contract.cpp`

- [x] **Step 1: Strengthen failing GPU table tests**

In `src/test/integration/test_bindless_indirect_contract.cpp`, add tests:

```cpp
void testDifferentTextureIdentityProducesDifferentBindlessSlots() {
  VulkanGpuResourceTable table;
  const GpuDescriptorTableHandle descriptors = table.createDescriptorTable();
  const GpuBindlessSlot first =
      table.updateBindlessSlot(descriptors, GpuImageHandle{42},
                               GpuSamplerHandle{7});
  const GpuBindlessSlot second =
      table.updateBindlessSlot(descriptors, GpuImageHandle{43},
                               GpuSamplerHandle{7});
  EXPECT(first.index != second.index,
         "different image identity should allocate a different bindless slot");
}

void testPipelineCacheFindAndCreateAreObservable() {
  VulkanGpuResourceTable table;
  const GpuPipelineDesc desc{.key = "forward.bindless.pipeline"};
  EXPECT(!table.findPipeline(desc).has_value(),
         "pipeline find should miss before getOrCreate");
  const GpuPipelineHandle created = table.getOrCreatePipeline(desc);
  const auto found = table.findPipeline(desc);
  EXPECT(found.has_value(), "pipeline find should hit after getOrCreate");
  EXPECT(found->id == created.id, "pipeline cache should return same handle");
}

void testProgressTracksResourceWork() {
  VulkanGpuResourceTable table;
  const u8 bytes[4] = {1, 2, 3, 4};
  (void)table.createBuffer(GpuBufferDesc{.byteSize = 4},
                           std::span<const u8>(bytes));
  (void)table.createImage(GpuImageDesc{.width = 1, .height = 1, .mipLevels = 1},
                          std::span<const u8>(bytes));
  const GpuProgress progress = table.queryProgress();
  EXPECT(progress.totalTasks >= 2,
         "resource table progress should count created resources");
  EXPECT(progress.completedTasks == progress.totalTasks,
         "synchronous shell uploads should report completed tasks");
}
```

Call them from `main()`.

- [x] **Step 2: Run test to verify failure**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract
build/src/test/test_bindless_indirect_contract
```

Expected: FAIL because current `updateBindlessSlot` always returns slot `0`, `findPipeline` always misses, and progress is empty.

- [x] **Step 3: Add state fields**

Modify `src/backend/vulkan/vulkan_gpu_resource_table.hpp`:

```cpp
#include <map>
#include <unordered_map>

struct VulkanBindlessKey final {
  u64 table = 0;
  u64 image = 0;
  u64 sampler = 0;

  friend bool operator==(const VulkanBindlessKey &lhs,
                         const VulkanBindlessKey &rhs) = default;
};

struct VulkanBindlessKeyHash final {
  usize operator()(const VulkanBindlessKey &key) const noexcept {
    return static_cast<usize>((key.table * 1315423911ull) ^
                              (key.image * 2654435761ull) ^
                              (key.sampler * 97531ull));
  }
};
```

Add private members:

```cpp
  std::unordered_map<VulkanBindlessKey, GpuBindlessSlot,
                     VulkanBindlessKeyHash>
      m_bindlessSlots;
  std::unordered_map<std::string, GpuPipelineHandle> m_pipelines;
  u32 m_completedTasks = 0;
  u32 m_totalTasks = 0;
```

- [x] **Step 4: Implement stateful behavior**

Modify `src/backend/vulkan/vulkan_gpu_resource_table.cpp`:

```cpp
GpuBufferHandle VulkanGpuResourceTable::createBuffer(
    const GpuBufferDesc &desc, std::span<const u8> initialData) {
  (void)desc;
  (void)initialData;
  ++m_totalTasks;
  ++m_completedTasks;
  return GpuBufferHandle{m_nextId++};
}

GpuImageHandle VulkanGpuResourceTable::createImage(
    const GpuImageDesc &desc, std::span<const u8> initialData) {
  (void)desc;
  (void)initialData;
  ++m_totalTasks;
  ++m_completedTasks;
  return GpuImageHandle{m_nextId++};
}

GpuSamplerHandle
VulkanGpuResourceTable::createSampler(const GpuSamplerDesc &desc) {
  (void)desc;
  ++m_totalTasks;
  ++m_completedTasks;
  return GpuSamplerHandle{m_nextId++};
}

GpuBindlessSlot VulkanGpuResourceTable::updateBindlessSlot(
    GpuDescriptorTableHandle table, GpuImageHandle image,
    GpuSamplerHandle sampler) {
  const VulkanBindlessKey key{table.id, image.id, sampler.id};
  if (const auto it = m_bindlessSlots.find(key); it != m_bindlessSlots.end()) {
    return it->second;
  }
  const GpuBindlessSlot slot{
      static_cast<u32>(m_bindlessSlots.size())};
  m_bindlessSlots.emplace(key, slot);
  ++m_totalTasks;
  ++m_completedTasks;
  return slot;
}

std::optional<GpuPipelineHandle>
VulkanGpuResourceTable::findPipeline(const GpuPipelineDesc &desc) const {
  if (const auto it = m_pipelines.find(desc.key); it != m_pipelines.end()) {
    return it->second;
  }
  return std::nullopt;
}

GpuPipelineHandle
VulkanGpuResourceTable::getOrCreatePipeline(const GpuPipelineDesc &desc) {
  if (const auto existing = findPipeline(desc); existing.has_value()) {
    return *existing;
  }
  const GpuPipelineHandle handle{m_nextId++};
  m_pipelines.emplace(desc.key, handle);
  ++m_totalTasks;
  ++m_completedTasks;
  return handle;
}

GpuProgress VulkanGpuResourceTable::queryProgress() const {
  GpuProgress progress;
  progress.completedTasks = m_completedTasks;
  progress.totalTasks = m_totalTasks;
  progress.currentTask =
      (m_completedTasks == m_totalTasks) ? "ready" : "uploading";
  return progress;
}
```

Keep `updateBuffer`, `updateIndirectDrawBuffer`, import/export behavior unless tests require state.

- [x] **Step 5: Run strengthened tests**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract
build/src/test/test_bindless_indirect_contract
```

Expected: PASS.

- [x] **Step 6: Commit**

Run:

```bash
git add src/backend/vulkan/vulkan_gpu_resource_table.* src/test/integration/test_bindless_indirect_contract.cpp
git commit -m "Make Vulkan GPU resource table bindless state observable"
```

Expected: commit with stateful bindless/pipeline/progress behavior.

## Task 4: Enforce No Fallback For Migrated Validation Queues

**Files:**
- Modify: `src/test/integration/test_071_bridge_audit.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/core/frame_graph/render_validation_contract.hpp`
- Modify: `src/core/frame_graph/render_validation_contract.cpp`

- [ ] **Step 1: Strengthen bridge audit to reject drawData fallback**

Replace `testDefaultRasterQueueHasIndirectBridge()` in `src/test/integration/test_071_bridge_audit.cpp` with two tests:

```cpp
void testMigratedQueueRejectsDrawDataFallback() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::UniformBuffer,
                         StringID("MaterialParams"), 64);

  RenderWorkQueue queue;
  RenderWorkItem item =
      makeDefaultPathDraw(vertex, index, camera, material, 0);
  item.raster.drawData = std::make_shared<PerDrawData>(sizeof(Mat4f));
  queue.addItem(std::move(item));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "migrated validation queue must reject per-draw drawData fallback");
  EXPECT(!result.diagnostics.empty(),
         "migrated validation queue must explain rejected fallback");
}

void testMigratedQueueAcceptsFullyCoveredIndirectBatch() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::UniformBuffer,
                         StringID("MaterialParams"), 64);

  RenderWorkQueue queue;
  queue.addItem(makeDefaultPathDraw(vertex, index, camera, material, 0));
  queue.addItem(makeDefaultPathDraw(vertex, index, camera, material, 3));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(result.ok, "fully covered migrated queue should pass bindless audit");
  EXPECT(result.coveredItemCount == queue.getItems().size(),
         "audit should cover every migrated draw item");
}
```

Include `core/frame_graph/render_validation_contract.hpp` and call both tests from `main()`.

- [ ] **Step 2: Run bridge audit**

Run:

```bash
cmake --build build --target test_071_bridge_audit
build/src/test/test_071_bridge_audit
```

Expected: PASS after Task 2 implementation.

- [ ] **Step 3: Add renderer helper for strict queue validation**

In `src/backend/vulkan/vulkan_realtime_renderer.cpp`, include:

```cpp
#include "core/frame_graph/render_validation_contract.hpp"
```

Inside `drawPassQueue`, before per-item fallback, add:

```cpp
    const bool migratedValidationPass =
        m_activeValidationProfile.has_value() &&
        queueShouldUseBindlessValidation(passIndex);
```

If no equivalent validation profile state exists, add a private helper that returns `false` for now and a comment pointing to `SceneValidationProfile` integration. Then before the fallback loop:

```cpp
    if (migratedValidationPass) {
      const auto validation =
          LX_core::validateBindlessMigratedQueue(queue, m_frameGraph.getPasses()[passIndex].pass);
      if (!validation.ok) {
        throw std::runtime_error("bindless validation rejected migrated pass: " +
                                 validation.diagnostics.front().reason);
      }
    }
```

If member names differ, use the pass identifier available in the compiled pass object. The important behavior is that validation-mode migrated passes throw before the legacy per-item loop.

- [ ] **Step 4: Build renderer target**

Run:

```bash
cmake --build build --target lxe_editor
```

Expected: PASS.

- [ ] **Step 5: Commit**

Run:

```bash
git add src/backend/vulkan/vulkan_realtime_renderer.cpp src/test/integration/test_071_bridge_audit.cpp
git commit -m "Reject legacy draw fallback for migrated validation passes"
```

Expected: commit isolates fallback enforcement. If strict profile state is not wired yet, record the helper as pending in the task result before moving to Task 5.

## Task 5: Dead-Code-Prune Legacy Material Roots

**Files:**
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/demos/lxe_editor/scene_builder.cpp`
- Modify: `src/core/editor/commands/builtin_commands.cpp`
- Modify: `src/core/editor/inspector_panel.cpp`
- Modify: `src/infra/scene_asset/scene_material_loader.cpp`
- Modify: `src/infra/scene_asset/gltf_scene_asset_loader.cpp`
- Modify or remove tests that only exercise deleted legacy behavior.

- [ ] **Step 1: Inventory inbound callers for legacy roots**

Run:

```bash
rg -n "MaterialUBO\\.baseColor|nodeMaterial\\.baseColor|baseColorFactor|metallicFactor|roughnessFactor|apply_material_override|setNodeMaterialBaseColor|clearNodeMaterialBaseColor" src/demos src/core src/infra src/test
```

Expected: Produce the legacy root list. Record roots in the task result before editing.

- [ ] **Step 2: Classify roots**

For each root, classify:

```text
root: <symbol or command>
classification: redundant dead path | unreachable dead path | justified debug path
canonical replacement: Material v2 envelope override or bindless material index path
delete now: yes/no
reason if kept: live debug/editor path, string command ambiguity, or test-only fixture
```

Expected: No deletion until every root has classification.

- [ ] **Step 3: Migrate redundant validation callers**

For any validation/smoke/test caller still using old PBR truth, migrate it to Material v2 or bindless material records. Minimal target shape:

```yaml
version: 2
bsdf:
  type: matte
  parameters:
    Kd:
      kind: rgb
      value: [0.8, 0.2, 0.1]
    sigma:
      kind: float
      value: 0.0
defaultTechnique: Forward
techniques:
  Forward:
    passes:
      - name: Forward
        shader: asset://shaders/glsl/techniques/Forward/pbr_forward
        stage: raster
        dispatch: draw
        sources: []
        targets: [swapchainColor]
        renderState:
          cull: back
          depthTest: true
          depthWrite: true
```

Adjust exact asset paths to current material parser expectations.

- [ ] **Step 4: Delete proven dead roots and callee slices**

Use `apply_patch` only after Step 2 proves removability. Remove declarations, definitions, tests that only covered removed legacy behavior, and now-unused includes.

- [ ] **Step 5: Re-search second-order dead code**

Run:

```bash
rg -n "MaterialUBO\\.baseColor|nodeMaterial\\.baseColor|baseColorFactor|metallicFactor|roughnessFactor" src/demos src/core src/infra src/test
```

Expected: Remaining hits are either deleted, justified debug-only, or documented in the task result.

- [ ] **Step 6: Build editor and tests**

Run:

```bash
cmake --build build --target lxe_editor BuildTest
```

Expected: PASS, or record exact compile/test failure and fix before continuing.

- [ ] **Step 7: Commit**

Run:

```bash
git add src/demos src/core src/infra src/test
git commit -m "Prune legacy material default paths"
```

Expected: commit includes only proven legacy cleanup and caller migration.

## Task 6: Minimal Bindless Vulkan Smoke

**Files:**
- Create: `assets/materials/validation/bindless_matte_red.material`
- Create: `assets/materials/validation/bindless_matte_green.material`
- Create: `assets/scenes/bindless_validation_smoke.scene.yaml`
- Create or modify: `src/test/integration/test_bindless_validation_smoke.py`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add validation materials**

Create two Material v2 files using the exact parser-supported shape from existing Material v2 tests. Use `rg -n "bsdf:|defaultTechnique|techniques:" assets src/test notes/requirements/071-a-material-v2-pbrt-surface-contract.md` to copy the current accepted syntax.

- [ ] **Step 2: Add minimal scene**

Create `assets/scenes/bindless_validation_smoke.scene.yaml` with two visible objects, one camera, one direct light, no shadows, no IBL, no transparency, and references to the two new materials.

- [ ] **Step 3: Add smoke test**

Create `src/test/integration/test_bindless_validation_smoke.py` using the same harness shape as `src/test/integration/test_realtime_offline_compare_flat.py`, but target `bindless_validation_smoke.scene.yaml`.

The test should assert:

```python
assert render_result.returncode == 0
assert output_exr.exists()
assert "bindless" in metadata_text
assert "legacy" not in metadata_text.lower()
```

Use the repository's current output metadata helper if one exists; otherwise assert non-black output through the existing EXR metric helper.

- [ ] **Step 4: Register CTest**

Modify `src/test/CMakeLists.txt` to register the Python smoke under `requires_video_device`.

- [ ] **Step 5: Run smoke**

Run:

```bash
cmake --build build --target lxe_editor lxe_realtime_render
xvfb-run -a ctest --test-dir build --output-on-failure -R "bindless_validation_smoke" -L requires_video_device
```

Expected: PASS, output non-black, metadata proves bindless/global-array path.

- [ ] **Step 6: Commit**

Run:

```bash
git add assets/materials/validation assets/scenes/bindless_validation_smoke.scene.yaml src/test/integration/test_bindless_validation_smoke.py src/test/CMakeLists.txt
git commit -m "Add bindless validation smoke scene"
```

Expected: commit with assets and test only.

## Task 7: Documentation And Final Gates

**Files:**
- Modify: `notes/requirements/072-071-closure-audit-and-validation-fixes.md`
- Modify: `notes/requirements/071-d-gpu-resource-table-pipeline-cache-and-upload-tasks.md`
- Modify: `notes/requirements/071-f-rendering-equivalence-helmet-bmw-validation.md`
- Modify: `docs/superpowers/plans/2026-06-11-072-legacy-removal-bindless-validation.md`

- [ ] **Step 1: Update requirement status**

Record:

- deleted legacy roots and why they were removable;
- migrated callers;
- retained debug-only paths and why they remain;
- bindless smoke results;
- remaining non-072 failures, if any.

- [ ] **Step 2: Run final gates**

Run:

```bash
cmake --build build --target BuildTest
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

Expected: PASS. If failures remain, record exact command, failing test, and whether it blocks 072-D.

- [ ] **Step 3: Commit documentation**

Run:

```bash
git add notes/requirements/072-071-closure-audit-and-validation-fixes.md notes/requirements/071-d-gpu-resource-table-pipeline-cache-and-upload-tasks.md notes/requirements/071-f-rendering-equivalence-helmet-bmw-validation.md docs/superpowers/plans/2026-06-11-072-legacy-removal-bindless-validation.md
git commit -m "Document REQ-072 bindless validation closure"
```

Expected: final docs commit for this phase.

## Self-Review

- Spec coverage: Tasks cover strict bindless validation mode, real bindless smoke, GPUResourceTable state, renderer fallback enforcement, dead-code-prune cleanup, docs, and gates.
- Placeholder scan: No placeholder markers are used. Where exact current syntax depends on existing parser fixtures, the plan names the search command and the concrete target file to update before writing assets.
- Type consistency: `BindlessValidationResult`, `BindlessValidationDiagnostic`, and `validateBindlessMigratedQueue` are introduced once and reused consistently.
- Scope check: Full package restore and helmet/BMW matrix remain out of this plan, matching the approved 072-D-first design.
