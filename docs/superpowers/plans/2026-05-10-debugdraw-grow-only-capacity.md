# DebugDraw Grow-Only Capacity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `DebugDraw` survive frame-to-frame geometry growth by using grow-only bucket capacity, without changing generic Vulkan resource-resize behavior.

**Architecture:** Keep the policy local to `DebugDraw`. Each debug bucket tracks reserved vertex/index capacity, rebuilds its CPU resource objects only when current frame demand exceeds reserved capacity, and never auto-shrinks during a session. Backend Vulkan sync remains unchanged and sees grown buckets as fresh resource identities.

**Tech Stack:** C++20, ImGui editor overlay, Vulkan backend, CMake, existing integration-test executables

---

## File Map

- Modify: `src/core/debug_draw/debug_draw.cpp`
  - Add per-bucket reserved-capacity state
  - Add grow-only capacity helper
  - Rebuild bucket resources on capacity growth
- Modify: `src/core/debug_draw/debug_draw.hpp`
  - Add test-only inspection hooks only if needed for capacity assertions
- Modify: `src/test/integration/test_debug_draw.cpp`
  - Add red/green tests for grow-only bucket behavior
- Modify or Create: `src/test/integration/test_vulkan_resource_manager.cpp` or `src/test/integration/test_debug_draw_vulkan_growth.cpp`
  - Add Vulkan regression coverage for small-then-large debug geometry sync
- Modify: `src/test/CMakeLists.txt`
  - Only if a new dedicated Vulkan regression test executable is added

### Task 1: Lock DebugDraw Grow-Only Behavior With Tests

**Files:**
- Modify: `src/test/integration/test_debug_draw.cpp`
- Test: `src/test/integration/test_debug_draw.cpp`

- [ ] **Step 1: Write the failing tests**

Add tests that express the required bucket-capacity behavior. If `debug_draw.hpp` already exposes enough testing hooks, use them; otherwise, add the smallest possible test-only accessors in Task 2.

```cpp
void testBucketCapacityGrowsForLargerLaterFrame() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  DebugDraw::endFrame();
  const usize initialCapacity =
      DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay);

  DebugDraw::beginFrame();
  for (int i = 0; i < 128; ++i) {
    DebugDraw::drawLine({0.0f, 0.0f, 0.0f}, {float(i), 1.0f, 0.0f});
  }
  DebugDraw::endFrame();

  const usize grownCapacity =
      DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay);
  EXPECT(grownCapacity > initialCapacity,
         "later larger frame should grow reserved capacity");
  EXPECT(grownCapacity >= 256,
         "capacity should grow to cover the larger frame");
}

void testBucketCapacityDoesNotShrinkAfterSmallerFrame() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  for (int i = 0; i < 128; ++i) {
    DebugDraw::drawLine({0.0f, 0.0f, 0.0f}, {float(i), 1.0f, 0.0f});
  }
  DebugDraw::endFrame();
  const usize largeCapacity =
      DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay);

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  DebugDraw::endFrame();

  EXPECT(DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay) ==
             largeCapacity,
         "smaller later frame should not shrink reserved capacity");
}

void testEmptyFrameClearsGeometryWithoutShrinkingCapacity() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  for (int i = 0; i < 64; ++i) {
    DebugDraw::drawLine({0.0f, 0.0f, 0.0f}, {float(i), 1.0f, 0.0f});
  }
  DebugDraw::endFrame();
  const usize retainedCapacity =
      DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay);

  DebugDraw::beginFrame();
  DebugDraw::endFrame();

  EXPECT(DebugDraw::testing::flushedVertexCount(Layer_EditorOverlay) == 0,
         "empty frame should clear visible geometry");
  EXPECT(DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay) ==
             retainedCapacity,
         "empty frame should retain grown capacity");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_debug_draw -j4 && ./build/src/test/test_debug_draw`

Expected: FAIL because `reservedVertexCapacity(...)` does not exist yet or because capacity currently tracks exact frame size instead of grow-only semantics.

- [ ] **Step 3: Add the minimal test hooks needed for capacity assertions**

If the test cannot observe reserved capacity, add small testing accessors and no production-facing API.

```cpp
namespace LX_core::DebugDraw::testing {

usize reservedVertexCapacity(VisibilityLayerMask mask);
usize reservedIndexCapacity(VisibilityLayerMask mask);

} // namespace LX_core::DebugDraw::testing
```

Implementation target in `debug_draw.cpp`:

```cpp
usize reservedVertexCapacity(const VisibilityLayerMask mask) {
  auto &s = state();
  auto it = s.buckets.find(mask);
  return it == s.buckets.end() ? 0 : it->second.reservedVertexCount;
}

usize reservedIndexCapacity(const VisibilityLayerMask mask) {
  auto &s = state();
  auto it = s.buckets.find(mask);
  return it == s.buckets.end() ? 0 : it->second.reservedIndexCount;
}
```

- [ ] **Step 4: Re-run the DebugDraw test and keep it red for the right reason**

Run: `cmake --build build --target test_debug_draw -j4 && ./build/src/test/test_debug_draw`

Expected: FAIL in the new grow-only tests because current bucket updates do not retain capacity.

- [ ] **Step 5: Commit the red test state**

```bash
git add src/core/debug_draw/debug_draw.hpp src/test/integration/test_debug_draw.cpp
git commit -m "test: lock debugdraw grow-only capacity behavior"
```

### Task 2: Implement Grow-Only Bucket Capacity In DebugDraw

**Files:**
- Modify: `src/core/debug_draw/debug_draw.cpp`
- Modify: `src/core/debug_draw/debug_draw.hpp` only if Task 1 needed test hooks
- Test: `src/test/integration/test_debug_draw.cpp`

- [ ] **Step 1: Add reserved-capacity fields to `BucketState`**

Update `BucketState` in `src/core/debug_draw/debug_draw.cpp`:

```cpp
struct BucketState {
  VisibilityLayerMask mask = Layer_EditorOverlay;
  SceneNodeSharedPtr node;
  std::shared_ptr<VertexBuffer<DebugLineVertex>> vertexBuffer;
  IndexBufferSharedPtr indexBuffer;
  MeshSharedPtr mesh;
  usize flushedVertexCount = 0;
  usize reservedVertexCount = 0;
  usize reservedIndexCount = 0;
};
```

- [ ] **Step 2: Add a helper that computes grow-only capacity**

Add a helper near `makeSequentialIndices(...)`:

```cpp
usize nextDebugCapacity(const usize currentCapacity, const usize requiredCapacity) {
  constexpr usize kMinCapacity = 256;
  usize capacity = std::max(currentCapacity, kMinCapacity);
  while (capacity < requiredCapacity) {
    capacity *= 2;
  }
  return capacity;
}
```

Add a ceiling assertion or clamp based on `kMaxLinesPerFrame * 2`.

- [ ] **Step 3: Split bucket rebuild from per-frame content update**

Replace the current single-path `updateBucket(...)` with:

```cpp
void rebuildBucketCapacity(BucketState &bucket,
                           const usize reservedVertexCount,
                           const usize reservedIndexCount) {
  bucket.vertexBuffer = VertexBuffer<DebugLineVertex>::create({});
  bucket.indexBuffer = IndexBuffer::create({}, PrimitiveTopology::LineList);
  bucket.mesh = Mesh::create(bucket.vertexBuffer, bucket.indexBuffer);
  bucket.mesh->bounds = BoundingBox{};

  auto meshComponent = bucket.node->getComponent<MeshComponent>();
  if (!meshComponent.has_value()) {
    throw std::runtime_error("DebugDraw bucket missing MeshComponent");
  }
  meshComponent->get().setMesh(bucket.mesh);
  bucket.reservedVertexCount = reservedVertexCount;
  bucket.reservedIndexCount = reservedIndexCount;
}
```

If `MeshComponent` does not currently expose a mesh setter, stop and add the
smallest compatible setter in the same task before proceeding.

- [ ] **Step 4: Implement grow-only `updateBucket(...)`**

Update the function so it grows only when needed and never shrinks:

```cpp
void updateBucket(BucketState &bucket,
                  const std::vector<DebugLineVertex> &vertices) {
  const usize requiredVertexCount = vertices.size();
  const usize requiredIndexCount = requiredVertexCount;

  if (requiredVertexCount > bucket.reservedVertexCount ||
      requiredIndexCount > bucket.reservedIndexCount) {
    const usize newReservedVertexCount =
        nextDebugCapacity(bucket.reservedVertexCount, requiredVertexCount);
    const usize newReservedIndexCount =
        nextDebugCapacity(bucket.reservedIndexCount, requiredIndexCount);
    rebuildBucketCapacity(bucket, newReservedVertexCount, newReservedIndexCount);
  }

  bucket.vertexBuffer->update(vertices);
  bucket.indexBuffer->update(makeSequentialIndices(requiredIndexCount));
  bucket.mesh->bounds = computeBounds(vertices);
  bucket.flushedVertexCount = requiredVertexCount;
}
```

- [ ] **Step 5: Verify DebugDraw tests pass**

Run: `cmake --build build --target test_debug_draw -j4 && ./build/src/test/test_debug_draw`

Expected: PASS including the new grow-only tests.

- [ ] **Step 6: Commit the grow-only DebugDraw implementation**

```bash
git add src/core/debug_draw/debug_draw.cpp src/core/debug_draw/debug_draw.hpp src/test/integration/test_debug_draw.cpp
git commit -m "feat: add grow-only debugdraw bucket capacity"
```

### Task 3: Add Vulkan Regression Coverage For Growing Debug Geometry

**Files:**
- Modify: `src/test/integration/test_vulkan_resource_manager.cpp` or create `src/test/integration/test_debug_draw_vulkan_growth.cpp`
- Modify: `src/test/CMakeLists.txt` only if a new test executable is created
- Test: chosen Vulkan integration test file

- [ ] **Step 1: Write the failing Vulkan regression test**

Prefer a focused new test executable if that is simpler than extending
`test_vulkan_resource_manager.cpp`.

Core scenario:

```cpp
DebugDraw::reset();
DebugDraw::attachScene(scene);

DebugDraw::beginFrame();
DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
DebugDraw::endFrame();
resourceManager->syncResource(*cmdBufferMgr, debugVertexBuffer);
resourceManager->syncResource(*cmdBufferMgr, debugIndexBuffer);

DebugDraw::beginFrame();
for (int i = 0; i < 128; ++i) {
  DebugDraw::drawLine({0.0f, 0.0f, 0.0f}, {float(i), 1.0f, 0.0f});
}
DebugDraw::endFrame();
resourceManager->syncResource(*cmdBufferMgr, debugVertexBufferAfterGrowth);
resourceManager->syncResource(*cmdBufferMgr, debugIndexBufferAfterGrowth);

EXPECT(debugIndexBufferAfterGrowth->getByteSize() >
           debugIndexBufferBeforeGrowth->getByteSize(),
       "grown debug geometry should use a larger CPU index buffer");
EXPECT(resourceManager->getBuffer(debugIndexBufferAfterGrowth->getBackendCacheIdentity())
           .has_value(),
       "grown debug geometry should materialize a matching GPU index buffer");
```

The test should also create a pipeline for `Pass_DebugOverlay` and record one
draw if practical, so the regression stays close to the original failure mode.

- [ ] **Step 2: Run the Vulkan regression test to verify it fails**

If you added a new executable:

Run: `cmake --build build --target test_debug_draw_vulkan_growth -j4 && xvfb-run -a ./build/src/test/test_debug_draw_vulkan_growth`

If you extended the existing resource-manager test:

Run: `cmake --build build --target test_vulkan_resource_manager -j4 && xvfb-run -a ./build/src/test/test_vulkan_resource_manager`

Expected: FAIL before Task 2 is present, or PASS only after the grow-only implementation lands.

- [ ] **Step 3: Wire the test executable into CMake if needed**

Add to `src/test/CMakeLists.txt` if using a new test:

```cmake
list(APPEND TEST_INTEGRATION_EXE_LIST
  test_debug_draw_vulkan_growth
)

list(APPEND TEST_VIDEO_DEVICE_EXE_LIST
  test_debug_draw_vulkan_growth
)
```

- [ ] **Step 4: Run the focused Vulkan regression test and verify it passes**

Run the same command from Step 2 after Task 2 implementation is present.

Expected: PASS with no Vulkan validation errors related to undersized index buffers.

- [ ] **Step 5: Run the nearby editor/debug regression suite**

Run:

```bash
cmake --build build --target test_debug_draw test_viewport_overlay test_lxe_editor_layout test_vulkan_resource_manager -j4
./build/src/test/test_debug_draw
./build/src/test/test_viewport_overlay
./build/src/test/test_lxe_editor_layout
xvfb-run -a ./build/src/test/test_vulkan_resource_manager
```

Expected:

- `OK: all debug_draw tests passed`
- `[PASS] viewport_overlay tests passed.`
- `[PASS] lxe_editor layout tests passed.`
- Vulkan resource-manager test exits successfully without new validation complaints

- [ ] **Step 6: Commit the regression coverage**

```bash
git add src/test/integration/test_vulkan_resource_manager.cpp src/test/integration/test_debug_draw.cpp src/test/CMakeLists.txt
git commit -m "test: cover debugdraw buffer growth in vulkan path"
```

### Task 4: Final Verification And Cleanup

**Files:**
- Modify: none expected unless verification exposes a defect
- Test: existing debug/editor/Vulkan tests

- [ ] **Step 1: Run the final targeted verification set**

Run:

```bash
cmake --build build --target test_debug_draw test_viewport_overlay test_lxe_editor_layout test_vulkan_resource_manager lxe_editor -j4
./build/src/test/test_debug_draw
./build/src/test/test_viewport_overlay
./build/src/test/test_lxe_editor_layout
xvfb-run -a ./build/src/test/test_vulkan_resource_manager
```

Expected: all commands succeed.

- [ ] **Step 2: Inspect git diff for scope discipline**

Run:

```bash
git diff --stat HEAD~3..HEAD
git status --short
```

Expected:

- only `DebugDraw`-scoped implementation and tests changed
- unrelated files remain untouched

- [ ] **Step 3: Create the final integration commit if tasks were squashed instead of committed individually**

If the work was already committed task-by-task, skip this step. If not:

```bash
git add src/core/debug_draw/debug_draw.cpp src/core/debug_draw/debug_draw.hpp src/test/integration/test_debug_draw.cpp src/test/integration/test_vulkan_resource_manager.cpp src/test/CMakeLists.txt
git commit -m "fix debugdraw buffer growth for vulkan overlays"
```

## Self-Review

- Spec coverage:
  - grow-only bucket capacity: Task 2
  - no auto-shrink: Task 1 + Task 2 assertions
  - DebugDraw-only scope: Task 2 architecture and Task 4 scope check
  - Vulkan regression coverage: Task 3
- Placeholder scan:
  - no `TODO/TBD`
  - commands and code snippets included for each task
- Type consistency:
  - capacity terms consistently use `reservedVertexCount` / `reservedIndexCount`
  - rebuild flow consistently replaces CPU resource identities instead of resizing generic Vulkan buffers
