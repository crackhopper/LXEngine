## Requirements

### Requirement: CameraComponent is the node-local camera authority
The engine SHALL provide a `CameraComponent` concrete `IComponent` type for `SceneNode`. `CameraComponent` SHALL be the authoritative high-level camera object for scene cameras and SHALL own:

- a GPU-facing `CameraData` resource suitable for scene-level descriptor collection
- projection configuration needed to build perspective or orthographic projection matrices
- render-target binding state used by `matchesTarget(...)`
- a camera culling mask
- an active/inactive flag

The engine SHALL NOT require a separate standalone `Camera` object once `CameraComponent` exists on a node.

#### Scenario: camera node exposes one authoritative camera component
- **WHEN** a caller creates a `SceneNode` and adds `CameraComponent`
- **THEN** that component is the only high-level camera object needed for scene registration, controller updates, and scene-level camera resource collection

### Requirement: CameraComponent derives view state from its owning SceneNode
`CameraComponent` SHALL derive its view matrix from the owning `SceneNode` transform hierarchy rather than from duplicated camera position/target/up fields stored outside the node. Translation and rotation SHALL come from the owner’s derived world transform. Node scale SHALL be ignored when constructing the camera view matrix.

The camera’s local forward direction SHALL be `-Z`, and the local up direction SHALL be `+Y`.

#### Scenario: parented camera follows owner hierarchy
- **WHEN** a camera node is parented under another `SceneNode` and the parent transform changes
- **THEN** the camera view matrix SHALL reflect the owner hierarchy’s composed translation and rotation

#### Scenario: camera scale does not affect view matrix
- **WHEN** a camera node’s scale changes while its translation and rotation remain unchanged
- **THEN** the derived camera view matrix SHALL remain unchanged

### Requirement: CameraComponent supports pose writeback through component methods
`CameraComponent` SHALL expose public methods sufficient for controller writeback and demo/editor manipulation, including position-based and look-at-based pose updates. These methods SHALL update the owning `SceneNode` transform rather than maintaining a second independent camera pose store.

#### Scenario: setPosition writes owner translation
- **WHEN** a caller updates camera position through `CameraComponent`
- **THEN** the owning `SceneNode` translation SHALL change to the corresponding camera pose

#### Scenario: lookAt writes owner orientation
- **WHEN** a caller updates camera pose through a look-at style API
- **THEN** the owning `SceneNode` transform SHALL be updated so the camera faces the requested target with the defined camera-up convention

### Requirement: Scene registers cameras through camera-bearing SceneNode instances
`Scene` SHALL register scene cameras through `SceneNode` instances that carry `CameraComponent`. Registered camera nodes SHALL remain path-addressable through the normal `SceneNode` hierarchy and SHALL be allowed to participate in parent-child transform relationships.

If a camera node is registered into a scene without an explicit parent, the scene SHALL attach it under the scene root according to the scene’s camera-registration contract.

#### Scenario: registered camera node remains discoverable by path
- **WHEN** a scene registers a node containing `CameraComponent`
- **THEN** `Scene::findByPath(...)` SHALL be able to find that node through the normal scene path rules

#### Scenario: camera registration does not require a renderable payload
- **WHEN** a node carries `CameraComponent` but no mesh/material components
- **THEN** the scene SHALL still allow it to be registered as a camera node

### Requirement: Inactive camera components do not contribute scene-level camera behavior
`CameraComponent` SHALL expose an active/inactive state. Scene-level camera resource collection and camera-derived culling-mask aggregation SHALL ignore inactive camera components while leaving the node and component attached to the scene.

#### Scenario: inactive camera is skipped for scene resources
- **WHEN** a registered camera component is inactive
- **THEN** its camera UBO SHALL NOT be included in scene-level resource output

#### Scenario: inactive camera is skipped for culling-mask aggregation
- **WHEN** a registered camera component is inactive
- **THEN** its culling mask SHALL NOT contribute to the combined camera culling mask for queue construction

### Requirement: CameraComponent can generate a world-space picking ray
`CameraComponent` SHALL expose a helper that converts viewport pixel
coordinates into a world-space picking ray using the camera’s current derived
pose and projection contract.

For perspective cameras, the ray origin SHALL be the camera’s world position.
For orthographic cameras, the ray origin SHALL correspond to the addressed
pixel on the near plane. The returned direction SHALL be normalized.

#### Scenario: perspective pick ray originates at camera eye
- **WHEN** `pickRay(screenPixel, viewportSize)` is called on a perspective
  `CameraComponent`
- **THEN** the returned ray origin SHALL equal the camera’s world-space eye
  position

#### Scenario: orthographic pick ray stays projection-correct
- **WHEN** `pickRay(screenPixel, viewportSize)` is called on an orthographic
  `CameraComponent`
- **THEN** the returned ray SHALL originate from the corresponding near-plane
  world position and use a normalized world-space direction
