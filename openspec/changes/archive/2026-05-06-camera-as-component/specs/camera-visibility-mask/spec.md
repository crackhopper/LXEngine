## MODIFIED Requirements

### Requirement: Cameras expose a culling mask
Scene cameras SHALL expose a culling-mask value that defines which renderable layers are visible to that camera. After cameras move into `CameraComponent`, that culling mask SHALL remain part of the camera contract and SHALL be read from registered camera-bearing `SceneNode` instances during render-queue construction.

#### Scenario: camera limits visible layers
- **WHEN** a camera culling mask excludes a renderable's layer bits
- **THEN** that renderable is omitted from queue output for that camera

## ADDED Requirements

### Requirement: Inactive cameras do not affect visibility filtering
An inactive `CameraComponent` SHALL NOT contribute to scene-level culling-mask aggregation or to scene-level camera resource collection.

#### Scenario: inactive camera mask is ignored
- **WHEN** queue construction evaluates combined camera culling masks
- **THEN** inactive camera components SHALL be excluded from the aggregation result
