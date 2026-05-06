## ADDED Requirements

### Requirement: SceneNode exposes local and world bounds through MeshComponent
`SceneNode` SHALL expose `getLocalBounds()` and `getWorldBounds()` behavior derived from its attached `MeshComponent`.

`getLocalBounds()` SHALL return the attached mesh’s local-space `BoundingBox` when a `MeshComponent` is present, and SHALL return an invalid default-constructed `BoundingBox` when the node carries no mesh. `getWorldBounds()` SHALL transform the local bounds by the node’s current world transform.

#### Scenario: mesh-bearing node returns mesh bounds
- **WHEN** a `SceneNode` has a `MeshComponent` whose mesh carries valid local bounds
- **THEN** `getLocalBounds()` SHALL return those bounds

#### Scenario: non-mesh node returns invalid bounds
- **WHEN** a `SceneNode` has no `MeshComponent`
- **THEN** `getLocalBounds()` SHALL return an invalid `BoundingBox`
