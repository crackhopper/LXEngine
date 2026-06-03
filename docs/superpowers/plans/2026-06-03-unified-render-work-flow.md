# Unified Render Work Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename and refactor the current realtime render submission model so realtime and offline can share the same render work and upload planning vocabulary.

**Architecture:** `RenderingItem` becomes `RenderWorkItem`, meaning one pipeline-compatible GPU work submission. `RenderQueue` becomes `RenderWorkQueue`, meaning one pass's ordered work list. Raster-specific data moves into `RasterDrawWorkPayload`, and realtime upload becomes an explicit `RenderUploadPlan` path while preserving current rendering behavior.

**Tech Stack:** C++20, Vulkan, existing frame graph/render queue code, Ninja/CTest.

---

## File Structure

- Modify `src/core/scene/scene.hpp`: rename `RenderingItem` to `RenderWorkItem`; add `RenderDomain`, `RenderWorkKind`, and `RasterDrawWorkPayload`; keep raster convenience accessors during migration only if needed by focused call sites.
- Modify `src/core/frame_graph/pass.hpp`: add `Pass_OfflineRayTrace`.
- Modify `src/core/frame_graph/render_queue.hpp/.cpp`: rename `RenderQueue` to `RenderWorkQueue`; build `RenderWorkItem` with `RasterDrawWorkPayload`.
- Modify `src/core/frame_graph/frame_graph.hpp/.cpp`: update pass queue type and APIs to `RenderWorkQueue`.
- Modify `src/core/pipeline/pipeline_build_desc.hpp/.cpp`: consume `RenderWorkItem` and read raster payload for vertex layout/topology.
- Create `src/core/frame_graph/render_upload_plan.hpp`: define `RenderUploadPlan`, `RenderUploadPlanContext`, and `buildRenderUploadPlan(...)` for explicit resource upload planning.
- Create `src/core/frame_graph/render_upload_plan.cpp`: collect unique resources from work items.
- Create `src/core/offline/offline_render_work_graph.hpp`: declare the default offline render graph builder.
- Create `src/core/offline/offline_render_work_graph.cpp`: build a single-pass offline graph with an `OfflineRayTrace` compute work item.
- Modify `src/core/CMakeLists.txt`: compile `render_upload_plan.cpp`.
- Modify Vulkan backend files that consume render items:
  - `src/backend/vulkan/details/commands/command_buffer.hpp/.cpp`
  - `src/backend/vulkan/details/resource_manager.hpp/.cpp`
  - `src/backend/vulkan/vulkan_realtime_renderer.cpp`
  - `src/backend/vulkan/details/ibl_bake_renderer.cpp`
- Modify tests:
  - `src/test/integration/test_frame_graph.cpp`
  - `src/test/integration/test_vulkan_resource_manager.cpp`
  - `src/test/integration/test_vulkan_command_buffer.cpp`
  - `src/test/integration/test_vulkan_offscreen_submit_memory_probe.cpp`
  - Add or extend a focused test for `RenderUploadPlan`.

## Task 1: Rename Render Types and Preserve Raster Behavior

**Files:**
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/frame_graph/render_queue.hpp`
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: all compile-error call sites that reference `RenderingItem` or `RenderQueue`

- [ ] **Step 1: Replace core type names**

In `src/core/scene/scene.hpp`, replace `struct RenderingItem` with:

```cpp
enum class RenderDomain {
  Realtime,
  Offline,
};

enum class RenderWorkKind {
  RasterDraw,
  RasterBatch,
  ComputeDispatch,
  RayTracingDispatch,
};

struct RasterDrawWorkPayload final {
  PerDrawDataSharedPtr drawData;
  IGpuResourceSharedPtr vertexBuffer;
  IGpuResourceSharedPtr indexBuffer;
  u32 indexCount = 0;
  u32 firstIndex = 0;
  i32 vertexOffset = 0;
  u32 instanceCount = 1;
};

struct ComputeDispatchWorkPayload final {
  u32 groupCountX = 1;
  u32 groupCountY = 1;
  u32 groupCountZ = 1;
};

struct RenderWorkItem final {
  RenderDomain domain = RenderDomain::Realtime;
  RenderWorkKind kind = RenderWorkKind::RasterDraw;
  ShaderPtr shaderInfo;
  MaterialInstanceSharedPtr material;
  RasterDrawWorkPayload raster;
  ComputeDispatchWorkPayload compute;
  std::vector<IGpuResourceSharedPtr> descriptorResources;
  StringID pass;
  RenderTargetDesc target;
  StringID debugId;
  StringID objectSignature;
  StringID materialSignature;
  PipelineKey pipelineKey;
};
```

Rename `RenderQueue` to `RenderWorkQueue` in `render_queue.hpp/.cpp` and update method signatures to use `RenderWorkItem`.

In `src/core/frame_graph/pass.hpp`, add:

```cpp
inline const StringID Pass_OfflineRayTrace = StringID("OfflineRayTrace");
```

- [ ] **Step 2: Run focused compile to expose call sites**

Run:

```bash
ninja -C build test_frame_graph
```

Expected: compile failures at remaining `RenderingItem` / `RenderQueue` references.

- [ ] **Step 3: Update compile-error call sites**

Replace:

```cpp
RenderingItem
RenderQueue
```

with:

```cpp
RenderWorkItem
RenderWorkQueue
```

Where code previously read:

```cpp
item.vertexBuffer
item.indexBuffer
item.drawData
```

change it to:

```cpp
item.raster.vertexBuffer
item.raster.indexBuffer
item.raster.drawData
```

Where code creates a raster item, set:

```cpp
item.domain = RenderDomain::Realtime;
item.kind = RenderWorkKind::RasterDraw;
```

- [ ] **Step 4: Verify focused frame graph tests**

Run:

```bash
ninja -C build test_frame_graph && ./build/src/test/test_frame_graph
```

Expected: build succeeds and test passes.

- [ ] **Step 5: Commit**

```bash
git add src/core src/backend src/test
git commit -m "refactor: rename render queue work items"
```

## Task 2: Move Raster-Only Backend Access Behind Payload Helpers

**Files:**
- Modify: `src/core/pipeline/pipeline_build_desc.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
- Modify: `src/backend/vulkan/details/resource_manager.cpp`
- Modify: affected tests

- [ ] **Step 1: Add raster payload validation helper**

In `pipeline_build_desc.cpp`, make `PipelineBuildDesc::fromRenderingItem` become `PipelineBuildDesc::fromRenderWorkItem` and read:

```cpp
const auto &raster = item.raster;
assert(item.kind == RenderWorkKind::RasterDraw &&
       "PipelineBuildDesc::fromRenderWorkItem: raster draw item required");
assert(raster.vertexBuffer &&
       "PipelineBuildDesc::fromRenderWorkItem: vertex buffer required");
assert(raster.indexBuffer &&
       "PipelineBuildDesc::fromRenderWorkItem: index buffer required");
```

- [ ] **Step 2: Update command buffer execution names**

Rename:

```cpp
bindResources(..., const RenderWorkItem &item)
drawItem(const RenderWorkItem &item)
```

to:

```cpp
bindResources(..., const RenderWorkItem &item)
executeRasterDrawItem(const RenderWorkItem &item)
```

`executeRasterDrawItem` must check `item.kind == RenderWorkKind::RasterDraw`,
then use `item.raster.indexCount` when non-zero, otherwise fallback to
`item.raster.indexBuffer->getByteSize() / sizeof(u32)`.

- [ ] **Step 3: Update renderer call sites**

Replace:

```cpp
cmd->drawItem(item);
cmd.drawItem(item);
```

with:

```cpp
cmd->executeRasterDrawItem(item);
cmd.executeRasterDrawItem(item);
```

- [ ] **Step 4: Verify Vulkan command buffer focused test**

Run:

```bash
ninja -C build test_vulkan_command_buffer && xvfb-run -a ./build/src/test/test_vulkan_command_buffer
```

Expected: test passes.

- [ ] **Step 5: Commit**

```bash
git add src/core src/backend src/test
git commit -m "refactor: isolate raster render work payload"
```

## Task 3: Add Explicit RenderUploadPlan

**Files:**
- Create: `src/core/frame_graph/render_upload_plan.hpp`
- Create: `src/core/frame_graph/render_upload_plan.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `src/test/integration/test_frame_graph.cpp`

- [ ] **Step 1: Add failing upload-plan test**

Add a focused test in `test_frame_graph.cpp`:

```cpp
void testRenderUploadPlanCollectsRasterResources() {
  RenderWorkItem item;
  item.kind = RenderWorkKind::RasterDraw;
  auto vertex = makeTestVertexBuffer();
  auto index = IndexBuffer::create({0u, 1u, 2u});
  auto draw = std::make_shared<PerDrawData>();
  auto materialUbo = std::make_shared<ParameterBuffer>(StringID("MaterialUBO"));
  item.raster.vertexBuffer = vertex;
  item.raster.indexBuffer = index;
  item.raster.drawData = draw;
  item.descriptorResources.push_back(materialUbo);

  RenderWorkQueue queue;
  queue.addItem(item);
  const RenderUploadPlan plan = buildRenderUploadPlan(queue);

  EXPECT(plan.resources.size() == 4,
         "upload plan should include vertex, index, draw, and descriptor resources");
}
```

Use existing test helpers/types in the file instead of introducing raw pointers.

- [ ] **Step 2: Run focused test to verify failure**

Run:

```bash
ninja -C build test_frame_graph
```

Expected: compile failure for missing `RenderUploadPlan`.

- [ ] **Step 3: Implement upload plan**

Create `render_upload_plan.hpp`:

```cpp
#pragma once

#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <vector>

namespace LX_core {

struct RenderUploadPlan final {
  RenderDomain domain = RenderDomain::Realtime;
  std::vector<IGpuResourceSharedPtr> resources;
};

[[nodiscard]] RenderUploadPlan
buildRenderUploadPlan(const RenderWorkQueue &queue);

} // namespace LX_core
```

Create `render_upload_plan.cpp` that iterates queue items and appends non-null
unique resources in this order for raster work: vertex, index, draw, descriptor
resources. Use `ResourceCacheIdentity` to deduplicate resources.

- [ ] **Step 4: Add file to CMake**

Add `frame_graph/render_upload_plan.cpp` to the core library source list.

- [ ] **Step 5: Verify focused test**

Run:

```bash
ninja -C build test_frame_graph && ./build/src/test/test_frame_graph
```

Expected: test passes.

- [ ] **Step 6: Commit**

```bash
git add src/core src/test
git commit -m "feat: add render upload plan"
```

## Task 4: Route Realtime Uploads Through RenderUploadPlan

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/backend/vulkan/details/ibl_bake_renderer.cpp`
- Modify: Vulkan-focused tests if needed

- [ ] **Step 1: Replace inline item resource sync loops**

For each realtime draw path that currently does:

```cpp
resourceManager().syncResource(commandBufferManager(), item.raster.vertexBuffer);
resourceManager().syncResource(commandBufferManager(), item.raster.indexBuffer);
for (auto &cpuRes : item.descriptorResources) {
  resourceManager().syncResource(commandBufferManager(), cpuRes);
}
```

replace with:

```cpp
RenderWorkQueue queue;
queue.addItem(item);
const RenderUploadPlan uploadPlan = buildRenderUploadPlan(queue);
for (const auto &resource : uploadPlan.resources) {
  resourceManager().syncResource(commandBufferManager(), resource);
}
```

For existing pass queues, prefer building one upload plan for the whole queue
instead of one plan per item.

- [ ] **Step 2: Keep execution unchanged**

Execution still binds resources and calls `executeRasterDrawItem(item)` for each
work item.

- [ ] **Step 3: Verify realtime Vulkan tests**

Run:

```bash
ninja -C build test_vulkan_resource_manager test_vulkan_command_buffer test_vulkan_frame_graph
xvfb-run -a ./build/src/test/test_vulkan_resource_manager
xvfb-run -a ./build/src/test/test_vulkan_command_buffer
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
```

Expected: all focused tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/backend src/test
git commit -m "refactor: make realtime uploads explicit"
```

## Task 5: Route Offline Software Compute Through RenderWorkQueue

**Files:**
- Create: `src/core/offline/offline_render_work_graph.hpp`
- Create: `src/core/offline/offline_render_work_graph.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `src/backend/vulkan/offline/software_compute_offline_integrator.cpp`
- Modify: `src/test/integration/test_offline_gpu_scene.cpp`

- [ ] **Step 1: Add failing offline graph test**

Add this test to `src/test/integration/test_offline_gpu_scene.cpp` and call it
from `main()`:

```cpp
[[nodiscard]] offline::OfflineRenderJob makeRenderableJobWithCamera() {
  offline::OfflineRenderJob job = makeRenderableJobWithoutCamera();
  const auto camera = job.scene.registerCamera(makeValidationCameraResource());
  (void)camera;
  return job;
}

void testOfflineRenderWorkGraphBuildsRayTracePass() {
  offline::OfflineRenderJob job = makeRenderableJobWithCamera();
  job.output.width = 17;
  job.output.height = 9;

  FrameGraph graph = offline::buildOfflineRenderWorkGraph(job);
  EXPECT(graph.getPasses().size() == 1,
         "offline MVP graph should contain one default pass");
  const FramePass &pass = graph.getPasses().front();
  EXPECT(pass.name == Pass_OfflineRayTrace,
         "offline MVP pass should be OfflineRayTrace");
  EXPECT(pass.queue.getItems().size() == 1,
         "offline ray trace pass should contain one compute work item");
  const RenderWorkItem &item = pass.queue.getItems().front();
  EXPECT(item.domain == RenderDomain::Offline,
         "offline work item should carry offline domain");
  EXPECT(item.kind == RenderWorkKind::ComputeDispatch,
         "offline ray trace pass should be a compute dispatch");
  EXPECT(item.compute.groupCountX == 3 && item.compute.groupCountY == 2 &&
             item.compute.groupCountZ == 1,
         "offline compute dispatch groups should match 8x8 shader groups");
}
```

Also include `core/offline/offline_render_work_graph.hpp` at the top of the
test file.

- [ ] **Step 2: Run focused test to verify failure**

Run:

```bash
ninja -C build test_offline_gpu_scene
```

Expected: compile failure for missing `buildOfflineRenderWorkGraph`.

- [ ] **Step 3: Implement offline graph builder**

Create `src/core/offline/offline_render_work_graph.hpp`:

```cpp
#pragma once

#include "core/frame_graph/frame_graph.hpp"
#include "core/offline/offline_render_job.hpp"

namespace LX_core::offline {

[[nodiscard]] FrameGraph
buildOfflineRenderWorkGraph(const OfflineRenderJob &job);

} // namespace LX_core::offline
```

Create `src/core/offline/offline_render_work_graph.cpp`:

```cpp
#include "core/offline/offline_render_work_graph.hpp"

#include "core/frame_graph/pass.hpp"

namespace LX_core::offline {

FrameGraph buildOfflineRenderWorkGraph(const OfflineRenderJob &job) {
  FrameGraph graph;
  FramePass pass;
  pass.name = Pass_OfflineRayTrace;
  pass.target = RenderTargetDesc::offscreenColor(ImageFormat::RGBA8);

  RenderWorkItem item;
  item.domain = RenderDomain::Offline;
  item.kind = RenderWorkKind::ComputeDispatch;
  item.pass = Pass_OfflineRayTrace;
  item.target = pass.target;
  item.compute.groupCountX = (job.output.width + 7u) / 8u;
  item.compute.groupCountY = (job.output.height + 7u) / 8u;
  item.compute.groupCountZ = 1u;
  pass.queue.addItem(std::move(item));

  graph.addPass(std::move(pass));
  return graph;
}

} // namespace LX_core::offline
```

Add `offline/offline_render_work_graph.cpp` to the core library source list.

- [ ] **Step 4: Use offline graph in software integrator**

In `SoftwareComputeOfflineIntegrator::render`, build the offline graph before
uploading scene packets:

```cpp
FrameGraph graph = LX_core::offline::buildOfflineRenderWorkGraph(job);
const CompiledFrameGraph compiled = graph.compile();
if (!compiled.isValid()) {
  throw std::runtime_error("offline render graph invalid: " +
                           compiled.errorText());
}
if (graph.getPasses().empty() || graph.getPasses().front().queue.getItems().empty()) {
  throw std::runtime_error("offline render graph produced no work");
}
const RenderWorkItem &workItem = graph.getPasses().front().queue.getItems().front();
if (workItem.kind != RenderWorkKind::ComputeDispatch) {
  throw std::runtime_error("offline ray trace pass did not produce compute work");
}
const SceneResourceTableUploadView sceneUploadView = job.scene.buildUploadView();
const SceneSoftwareBvh acceleration = SceneSoftwareBvh::build(sceneUploadView);
const SceneGpuFrameParams frameParams =
    makeShaderParams(job, sceneUploadView, acceleration);
```

Use `workItem.compute.groupCountX/Y/Z` for `vkCmdDispatch`. Keep output pixels
and descriptor layout unchanged.

- [ ] **Step 5: Verify offline focused tests**

Run:

```bash
ninja -C build test_offline_gpu_scene test_offline_render_cli
./build/src/test/test_offline_gpu_scene
./build/src/test/test_offline_render_cli
```

Expected: both tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/core src/backend/vulkan/offline src/test
git commit -m "feat: route offline compute through render work graph"
```

## Task 6: Final Verification

**Files:**
- All touched files.

- [ ] **Step 1: Search for retired names in source**

Run:

```bash
rg -n "RenderingItem|RenderQueue|drawItem\\(" src
```

Expected: no matches except documentation that intentionally explains the old
name, if any.

- [ ] **Step 2: Build primary targets**

Run:

```bash
ninja -C build CompileShaders
ninja -C build lxe_editor lxe_offline_render BuildTest
```

Expected: all targets build.

- [ ] **Step 3: Run headless tests**

Run:

```bash
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
```

Expected: all tests pass.

- [ ] **Step 4: Run video-device tests**

Run:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

Expected: all tests pass, including `test_realtime_offline_compare_flat`.

- [ ] **Step 5: Inspect final git state**

Run:

```bash
git status --short
git log --oneline -n 8
```

Expected: worktree is clean after final commits, and recent commits correspond
to this plan.
