# DebugDraw Grow-Only Capacity Design

## Context

`DebugDraw` is a per-frame, high-churn geometry producer used by editor-facing
overlay features such as selection wire boxes, camera frusta, and directional
light arrows. Its CPU-side line lists can grow and shrink sharply from one
frame to the next depending on editor interaction.

Today, `DebugDraw::endFrame()` rewrites the CPU-side `VertexBuffer` and
`IndexBuffer` contents every frame, but the Vulkan resource sync path assumes
buffer sizes are stable for an existing resource identity. When a `DebugDraw`
bucket grows past the size of the GPU buffer that was first created for that
identity, the CPU-side draw count advances while the bound Vulkan index buffer
remains too small. This produces `vkCmdDrawIndexed` validation errors after
editor interactions that add overlay geometry.

The current request is intentionally scoped to `DebugDraw` only. It must not
generalize Vulkan dynamic-buffer resizing for every resource type in this
change.

## Goal

Make `DebugDraw` robust under frame-to-frame geometry growth by giving each
debug bucket a grow-only capacity policy:

- buckets expand when current frame geometry exceeds reserved capacity
- buckets never auto-shrink during a session
- existing `kMaxLinesPerFrame` remains the hard safety ceiling
- normal mesh / material / generic Vulkan resource semantics remain unchanged

## Non-Goals

- no automatic shrink policy
- no generic `VulkanResourceManager` buffer-resize framework
- no `VulkanBuffer::resize()` API
- no changes to non-`DebugDraw` resource identity semantics
- no persistent-debug-draw feature work from REQ-041-i

## Options Considered

### Option A: Fixed max preallocation

Allocate every `DebugDraw` bucket at `kMaxLinesPerFrame` capacity up front and
reuse it forever.

Pros:

- simplest runtime behavior
- no expansion logic after first creation

Cons:

- permanently reserves worst-case memory even for tiny editor scenes
- wastes memory for sessions that never approach the cap

### Option B: Grow-only capacity per bucket

Each `DebugDraw` bucket tracks reserved vertex/index capacity. If a frame needs
more, rebuild that bucket at a larger capacity. Never shrink automatically.

Pros:

- matches `DebugDraw`'s high-frequency, bursty workload
- avoids repeated realloc/shrink thrash
- keeps implementation local to `DebugDraw`
- avoids expanding scope into generic Vulkan resource policy

Cons:

- requires explicit bucket rebuild logic
- peak session memory is retained until reset / scene reattach

### Option C: Generic backend-managed dynamic resizing

Teach `VulkanResourceManager` to detect size growth and recreate GPU buffers for
all variable-sized resources.

Pros:

- reusable for future dynamic resources

Cons:

- broader scope than needed
- changes backend-wide resource semantics
- mixes a `DebugDraw`-specific workload problem into generic infrastructure

## Decision

Adopt **Option B: grow-only capacity per `DebugDraw` bucket**.

This is the narrowest solution that matches the actual workload. `DebugDraw`
already has a strong frame-level ceiling and a clearly bounded lifecycle.
Keeping the policy local avoids accidental changes to other GPU resource flows.

## Design

### 1. Bucket-owned reserved capacity

Each `DebugDraw` bucket will track:

- `reservedVertexCount`
- `reservedIndexCount`

These values represent the currently provisioned CPU/GPU geometry capacity for
that bucket, not the geometry size of the current frame.

For line-list debug geometry, `requiredIndexCount` always equals
`requiredVertexCount`, but both counters remain explicit so the structure stays
clear and future-proof.

### 2. Growth rule

At `DebugDraw::endFrame()` time, each queued bucket computes:

- `requiredVertexCount = vertices.size()`
- `requiredIndexCount = requiredVertexCount`

If both required counts are within reserved capacity, the bucket only updates
current-frame contents.

If either required count exceeds reserved capacity, the bucket is rebuilt to a
larger capacity before uploading current-frame contents.

Growth policy:

- start from a small minimum capacity such as `256`
- double capacity until it covers the current requirement
- never exceed the hard ceiling implied by `kMaxLinesPerFrame`

This yields stable amortized behavior without overcommitting worst-case memory
up front.

### 3. No automatic shrink

Buckets do not shrink during a running session.

Reasoning:

- `DebugDraw` geometry can fluctuate sharply per frame
- shrink heuristics would require arbitrary thresholds and idle windows
- repeated grow/shrink cycles would add churn to the exact path that should stay
  cheap and predictable

Capacity resets are allowed only at explicit lifecycle boundaries:

- `DebugDraw::reset()`
- `DebugDraw::attachScene(newScene)` when it reinitializes state

### 4. Rebuild by replacing resource identities

The existing Vulkan sync path does not support resizing an existing buffer for
the same resource identity. Therefore, `DebugDraw` should not try to mutate an
existing `VertexBuffer` or `IndexBuffer` past its original capacity.

When a bucket grows:

- create a new `VertexBuffer`
- create a new `IndexBuffer`
- create a new `Mesh` referencing them
- repoint the bucket's `MeshComponent` to the new mesh
- update reserved capacity values

Because the CPU resource objects are new, their backend cache identities are
new. The existing `VulkanResourceManager::syncResource()` path will naturally
materialize new GPU buffers instead of reusing the undersized old ones.

This keeps the resizing logic local to `DebugDraw` and avoids changing generic
backend behavior.

### 5. Per-frame empty geometry behavior

An empty frame still updates the bucket's effective geometry to zero vertices
and zero indices, but it does not shrink reserved capacity.

This preserves existing semantics from `beginFrame()/endFrame()`:

- previous overlay geometry disappears when nothing is queued
- capacity remains available for future frames

### 6. Safety limits

The current `kMaxLinesPerFrame` clipping behavior remains the top-level guard.

Derived limits:

- max line count = `kMaxLinesPerFrame`
- max vertex count = `kMaxLinesPerFrame * 2`
- max index count = `kMaxLinesPerFrame * 2`

No growth path may allocate beyond those effective maxima.

## Data Flow

1. Editor/game systems enqueue debug primitives into `queuedVertices`.
2. `DebugDraw::endFrame()` groups by visibility-mask bucket.
3. For each bucket:
   - compute required counts
   - ensure reserved capacity is sufficient
   - rebuild bucket resources if growth is needed
   - write current-frame vertex/index contents
4. Renderer continues using existing frame-graph and resource-sync flow.
5. Because grown buckets use fresh CPU resource identities, backend sync
   creates fresh Vulkan buffers sized for the new geometry.

## Files Expected To Change

- `src/core/debug_draw/debug_draw.cpp`
- possibly `src/core/debug_draw/debug_draw.hpp` if capacity test hooks need
  exposure
- `src/test/integration/test_debug_draw.cpp`
- one Vulkan-facing regression test covering growing debug geometry, likely in
  `src/test/integration/test_vulkan_resource_manager.cpp` or a new focused test

## Testing Strategy

### DebugDraw behavior tests

Extend `test_debug_draw` to cover:

- bucket grows when a later frame needs more geometry
- capacity remains larger after a subsequent smaller frame
- empty frame clears flushed geometry without shrinking reserved capacity

If needed, add test-only inspection helpers for reserved capacity.

### Vulkan regression test

Add a focused integration test that:

- creates a scene with `DebugDraw` attached
- flushes a small amount of debug geometry
- flushes a larger amount in a later frame
- syncs resources through the Vulkan path
- verifies no size mismatch / out-of-bounds condition occurs

The goal is to catch the exact class of bug where CPU-side draw counts outgrow
the previously created GPU buffers.

### Viewport-triggered scenario coverage

Preserve the current viewport-overlay tests and, if practical, add coverage for
a "selection increases debug geometry" path so the original reproduction remains
represented.

## Risks

- Rebuilding bucket resources changes CPU resource identities, so care is needed
  to ensure the mesh/component references are swapped atomically within the
  bucket update path.
- Retained peak capacity may grow large in extreme editor sessions, but this is
  bounded by the existing frame line cap and is preferable to shrink thrash.

## Rollout Notes

This change should be implemented first in `DebugDraw` only. If future work
reveals similar bursty dynamic-buffer workloads elsewhere, that can motivate a
separate design for generic backend-managed grow-only buffers. That is explicitly
out of scope for this change.
