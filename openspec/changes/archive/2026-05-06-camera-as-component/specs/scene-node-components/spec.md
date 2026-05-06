## ADDED Requirements

### Requirement: SceneNode component composition supports camera-bearing non-renderable nodes
The node-local component contract SHALL support `CameraComponent` as a first-class non-renderable component type. A `SceneNode` containing `CameraComponent` MAY omit mesh, material, and skeleton components while still remaining valid for scene registration, hierarchy attachment, and component lookup.

`CameraComponent` SHALL coexist with the existing one-component-per-concrete-type rule and SHALL NOT require special storage outside the normal `SceneNode` component container.

#### Scenario: camera-only node is valid
- **WHEN** a caller creates a `SceneNode` with `CameraComponent` and no renderable structural components
- **THEN** component lookup and scene registration SHALL still succeed without requiring mesh or material payloads

#### Scenario: camera component follows normal node-local lookup rules
- **WHEN** a caller adds `CameraComponent` to a `SceneNode`
- **THEN** `getComponent<CameraComponent>()` SHALL return that same logical component instance through the normal component container contract
