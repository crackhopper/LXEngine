# Camera Visibility Mask

## Requirements

### Requirement: Renderables expose a visibility layer mask
Renderable scene items SHALL expose a layer-mask value used to decide whether a camera can see them during render-queue construction.

#### Scenario: renderable belongs to a specific layer set
- **WHEN** a renderable is configured with a layer mask
- **THEN** queue building can compare that mask against the active camera culling mask

### Requirement: Cameras expose a culling mask
Cameras SHALL expose a culling-mask value that defines which renderable layers are visible to that camera.

#### Scenario: camera limits visible layers
- **WHEN** a camera culling mask excludes a renderable's layer bits
- **THEN** that renderable is omitted from queue output for that camera

### Requirement: Scene-level camera resources remain independent
Camera-derived scene-level resources SHALL NOT be suppressed solely because a specific renderable is filtered out by visibility masks.

#### Scenario: visibility filtering does not remove camera resources
- **WHEN** queue construction filters some renderables by mask
- **THEN** camera scene-level resources are still collected according to the active camera contract
