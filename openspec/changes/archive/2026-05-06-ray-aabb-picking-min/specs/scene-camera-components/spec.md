## ADDED Requirements

### Requirement: CameraComponent can generate a world-space picking ray
`CameraComponent` SHALL expose a helper that converts viewport pixel coordinates into a world-space picking ray using the camera’s current derived pose and projection contract.

For perspective cameras, the ray origin SHALL be the camera’s world position. For orthographic cameras, the ray origin SHALL correspond to the addressed pixel on the near plane. The returned direction SHALL be normalized.

#### Scenario: perspective pick ray originates at camera eye
- **WHEN** `pickRay(screenPixel, viewportSize)` is called on a perspective `CameraComponent`
- **THEN** the returned ray origin SHALL equal the camera’s world-space eye position

#### Scenario: orthographic pick ray stays projection-correct
- **WHEN** `pickRay(screenPixel, viewportSize)` is called on an orthographic `CameraComponent`
- **THEN** the returned ray SHALL originate from the corresponding near-plane world position and use a normalized world-space direction
