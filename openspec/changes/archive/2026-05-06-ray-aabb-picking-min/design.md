## Context

`REQ-041` needs viewport click-selection before any TRS gizmo or inspector flow can become useful, but the current engine has no picking pipeline at all:

- `BoundingBox` already exists and is the project’s established AABB-like value type
- `Mesh` instances do not yet preserve local-space bounds
- `SceneNode` has no public bounds queries
- there is no `Ray` type or ray-box intersection helper
- there is no scene-level nearest-hit selection API

The surrounding constraints are deliberately narrow:

- `REQ-038-a` is the minimum subset only; acceleration structures stay deferred to Phase 2
- `REQ-037-a` means mesh ownership already flows through `MeshComponent`
- `REQ-037-b` means editor-facing pick-ray generation belongs on `CameraComponent`
- repo style forbids raw pointers for object references, so pick results should preserve ownership clarity

## Goals / Non-Goals

**Goals:**

- Reuse the existing `BoundingBox` type instead of introducing parallel AABB vocabulary.
- Make local-space bounds part of loaded mesh data so runtime picking does not rescan geometry.
- Expose node-local and world-space bounds through `SceneNode` in a component-aware way.
- Add a minimal ray-box intersection primitive with deterministic `t` semantics.
- Add a brute-force `Scene::pick(...)` API that returns the nearest visible `SceneNode`.
- Add a `CameraComponent::pickRay(...)` helper so editor code can convert screen pixels into world-space rays without duplicating camera math.

**Non-Goals:**

- No BVH, octree, loose-octree, or other acceleration structure.
- No triangle-level picking, hit point, or hit normal output.
- No frustum-culling work; this remains a later rendering concern.
- No rewrite of `BoundingBox::transformed(...)`; the current 8-corner path remains the contract.
- No editor/UI wiring in this change beyond the camera-side helper needed for later consumers.

## Decisions

### 1. Reuse `BoundingBox` and avoid a new `AABB` type

The change will treat the existing `BoundingBox` value type as the engine’s AABB contract for picking.

Why:

- `BoundingBox` already has the exact semantics this change needs: empty-box default state, merge operations, transformed bounds, and validity checks.
- Introducing `AABB` would force parallel terminology into math, mesh, and scene APIs for no functional gain.
- Existing code already includes `bounds.hpp` from mesh-related headers, so reuse is frictionless.

Alternatives considered:

- Add a new `AABB` type next to `BoundingBox`.
  Rejected: duplicates concepts and creates conversion churn across APIs.
- Add a picking-only wrapper around `BoundingBox`.
  Rejected: still creates redundant type layers without new behavior.

### 2. Store local-space bounds on `Mesh`, computed during ingestion

Local-space mesh bounds will become part of `Mesh` itself, and OBJ/GLTF loaders will compute them while they already iterate position data.

Why:

- Bounds are mesh data, not editor-only metadata.
- Computing them during ingestion keeps runtime picking O(nodes) rather than O(nodes * vertices).
- A single source of truth avoids loader-specific side channels or late recomputation.

Alternatives considered:

- Recompute bounds every time `Scene::pick` runs.
  Rejected: unnecessary repeated CPU work and awkward for procedurally created meshes.
- Cache bounds on `SceneNode` instead of `Mesh`.
  Rejected: bounds are inherent to mesh geometry and should survive mesh sharing across nodes.

### 3. Make `SceneNode` bounds derived through `MeshComponent`

`SceneNode::getLocalBounds()` will pull through `MeshComponent`; `getWorldBounds()` will transform the local box by the current world transform and will not cache the result.

Why:

- `REQ-037-a` already established `MeshComponent` as the structural source of mesh payloads.
- Nodes without meshes should naturally produce invalid bounds and therefore disappear from picking candidates.
- World bounds are a pure function of local bounds plus world transform, so on-demand recomputation keeps the API simple and avoids another invalidation path.

Alternatives considered:

- Reintroduce direct mesh fields or mesh-specific accessors on `SceneNode`.
  Rejected: breaks the component-model consolidation that just landed.
- Cache world bounds on `SceneNode`.
  Rejected: adds another dirtiness contract for little value at Phase 1.5 scale.

### 4. Define `intersectRayBox(...)` around first-hit `t` semantics

The ray-box helper will use slab semantics and return the earliest entering `t` as `std::optional<float>`, with `t = 0` when the ray origin starts inside the box.

Why:

- `std::optional<float>` is the smallest contract sufficient for nearest-hit comparison.
- Returning `t` instead of world hit position avoids premature API commitments before triangle-level picking exists.
- “Origin inside box => `t = 0`” is easy to reason about and avoids special casing in scene traversal.

Alternatives considered:

- Return a hit struct with point and normal.
  Rejected: those values are outside the v1 scope and would be misleadingly coarse for box-only hits.
- Require normalized input directions.
  Rejected: better to permit arbitrary directions and document that callers normalize when they need world-distance comparability.

### 5. Keep `Scene::pick(...)` brute-force and ownership-safe

`Scene::pick(...)` will iterate renderable `SceneNode`s linearly, filter by visibility mask and valid bounds, and return a `SceneNodeSharedPtr` plus distance.

Why:

- Phase 1.5 editor scenes are small enough that O(n) traversal is acceptable.
- Returning `SceneNodeSharedPtr` matches the existing scene ownership model and avoids dangling references.
- Layer-mask filtering composes with existing editor-vs-game visibility conventions.

Alternatives considered:

- Return raw pointers for convenience.
  Rejected: violates repo style and weakens lifetime guarantees.
- Add an acceleration structure now.
  Rejected: Phase 2 owns that complexity, and this change only needs a functional picking baseline.

### 6. Put screen-to-world ray generation on `CameraComponent`

`CameraComponent::pickRay(screenPixel, viewportSize)` will convert screen pixels into world-space rays for both perspective and orthographic projections, with normalized output direction.

Why:

- `CameraComponent` already owns the authoritative view/projection behavior after `REQ-037-b`.
- Editor code should not duplicate projection-specific math or reach into low-level matrix conventions.
- Normalizing direction inside the helper gives `Scene::pick(...)` stable world-distance semantics by default.

Alternatives considered:

- Put the helper on `Scene` or a free utility namespace.
  Rejected: those layers do not own camera projection semantics.
- Defer orthographic correctness to a later pass.
  Rejected: the helper contract is small enough to get right now, and future editor cameras may need it.

## Risks / Trade-offs

- `Brute-force traversal may become slow on large scenes` → Mitigation: keep the public `Scene::pick(...)` contract narrow so Phase 2 can swap in BVH-backed internals without API churn.
- `Bounds correctness depends on every mesh creation path` → Mitigation: push bounds into `Mesh::create(...)` so loaders/tests/procedural call sites all converge on the same constructor path.
- `Ray-box edge cases can become numerically inconsistent` → Mitigation: define origin-inside, zero-direction-contract, and axis-parallel behavior explicitly in the picking spec and cover them in integration tests.
- `Screen-to-ray math can drift between projection modes` → Mitigation: normatively specify both perspective and orthographic semantics in the camera delta spec and test downstream consumers against those invariants.
