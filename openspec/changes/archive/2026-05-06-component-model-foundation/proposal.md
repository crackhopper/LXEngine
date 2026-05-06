## Why

`SceneNode` still hardcodes mesh, material, and optional skeleton as dedicated structural fields, which makes every new attachable scene payload a bespoke extension of `SceneNode` instead of a reusable node-level contract. Phase 1.5 needs a uniform component surface before camera-on-node and editor inspector work can move forward without multiplying one-off getters, setters, and validation paths.

## What Changes

- Introduce a lightweight `IComponent` base plus typed component IDs for node-local component lookup.
- Add first-class `MeshComponent`, `MaterialComponent`, and `SkeletonComponent` wrappers that preserve current mesh/material/skeleton ownership and APIs.
- Refactor `SceneNode` to own a component list with `addComponent<T>()`, `getComponent<T>()`, `removeComponent<T>()`, and `listComponents()`.
- **BREAKING** Remove `SceneNode` structural mesh/material/skeleton fields and delete `getMesh`, `setMesh`, `getMaterialInstance`, `setMaterialInstance`, `getSkeleton`, and `setSkeleton`.
- Update renderable validation and rendering-item assembly to read structural data through components instead of dedicated fields.
- Migrate scene/demo/test construction paths from constructor-time mesh/material/skeleton injection to component attachment.

## Capabilities

### New Capabilities
- `scene-node-components`: Defines the node-local component container contract, typed lookup/removal semantics, and the first mesh/material/skeleton component types.

### Modified Capabilities
- `scene-node-validation`: `SceneNode` construction, revalidation triggers, and renderable-path data access move from dedicated structural fields to component-backed structural data.
- `skeleton-resource`: The codebase may restore scene-component infrastructure, but `Skeleton` itself remains a core resource and must not inherit `IComponent`.

## Impact

Affected areas include `src/core/scene/object.*`, new `src/core/scene/component*` and `src/core/scene/components/*` files, scene/demo/test node construction sites, and any call sites that currently read or mutate node mesh/material/skeleton state directly. This is a source-breaking API migration for scene-node callers, but it keeps `Mesh`, `MaterialInstance`, and `Skeleton` resource contracts intact.
