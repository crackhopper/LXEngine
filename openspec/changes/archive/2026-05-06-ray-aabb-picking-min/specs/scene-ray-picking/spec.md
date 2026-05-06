## ADDED Requirements

### Requirement: Picking reuses BoundingBox as the engine AABB contract
The engine SHALL use the existing `BoundingBox` type as the axis-aligned box contract for minimal picking. This change SHALL NOT introduce a parallel `AABB` type or a separate `src/core/math/aabb.hpp`.

Any picking-facing API or implementation in this capability that refers to an axis-aligned box SHALL mean `BoundingBox`.

#### Scenario: no separate AABB type is introduced
- **WHEN** the picking capability is implemented
- **THEN** picking code SHALL use `BoundingBox` directly rather than defining a second AABB value type

### Requirement: The math layer exposes Ray and ray-box intersection
The engine SHALL provide a `Ray` value type in `src/core/math/ray.hpp` with `origin` and `direction` fields, plus a free function `intersectRayBox(const Ray&, const BoundingBox&) -> std::optional<float>`.

The function SHALL return the first entering `t` value along `ray.origin + t * ray.direction`, SHALL return `0` when the ray origin starts inside the box, and SHALL return `std::nullopt` when no forward hit exists. The input direction SHALL be allowed to be non-unit length; callers that compare hit distances across rays SHALL normalize before calling.

#### Scenario: ray starting inside a box returns zero
- **WHEN** `intersectRayBox(...)` is called with a ray whose origin lies inside the target `BoundingBox`
- **THEN** the function SHALL return `0`

#### Scenario: miss returns nullopt
- **WHEN** `intersectRayBox(...)` is called with a ray that does not intersect the box in the forward direction
- **THEN** the function SHALL return `std::nullopt`

### Requirement: Scene exposes nearest-hit brute-force picking
The engine SHALL provide a `Scene::pick(...)` API that evaluates picking candidates by brute-force traversal across scene nodes, using ray-box intersection against world-space bounds and returning the nearest hit.

The result SHALL carry a `SceneNodeSharedPtr` and the hit distance `t`. Picking SHALL skip nodes whose bounds are invalid and SHALL skip nodes whose visibility mask does not intersect the caller-provided `VisibilityLayerMask`.

#### Scenario: nearest valid node is selected
- **WHEN** multiple scene nodes intersect the same ray
- **THEN** `Scene::pick(...)` SHALL return the hit with the smallest non-negative distance

#### Scenario: layer mask excludes a hit
- **WHEN** a node intersects the ray but its visibility mask does not overlap the provided picking layer mask
- **THEN** that node SHALL be excluded from picking results
