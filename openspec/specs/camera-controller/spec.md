## ADDED Requirements

### Requirement: ICameraController abstract base class
`src/core/scene/camera_controller.hpp` SHALL define an abstract class `ICameraController` in namespace `LX_core` with:
- A virtual destructor
- A pure virtual method `void update(Camera& camera, const IInputState& input, float dt)`
- A type alias `CameraControllerSharedPtr = std::shared_ptr<ICameraController>`

The `update()` method SHALL NOT call `camera.updateMatrices()`.

#### Scenario: ICameraController is abstract
- **WHEN** attempting to instantiate `ICameraController` directly
- **THEN** compilation SHALL fail because `update` is pure virtual

#### Scenario: update does not call updateMatrices
- **WHEN** any concrete controller's `update()` completes
- **THEN** the camera's view/projection matrices SHALL remain unchanged from their pre-call state

### Requirement: OrbitCameraController class
`src/core/scene/orbit_camera_controller.hpp` and `.cpp` SHALL define a class `OrbitCameraController` in namespace `LX_core` that inherits `ICameraController`.

The constructor SHALL accept `(Vec3f target, float distance, float yawDeg, float pitchDeg)` with defaults `({0,0,0}, 5.0f, 0.0f, 20.0f)`.

The class SHALL expose:
- `Vec3f getTarget() const` / `void setTarget(Vec3f t)`
- `float getDistance() const` / `void setDistance(float d)`
- `float getYawDeg() const` / `float getPitchDeg() const`
- Public tuning fields: `rotateSpeedDegPerPixel`, `panSpeedPerPixel`, `zoomSpeedPerWheel`, `minDistance`, `maxDistance`, `minPitchDeg`, `maxPitchDeg`

#### Scenario: Default construction
- **WHEN** constructing `OrbitCameraController` with no arguments
- **THEN** target SHALL be `{0,0,0}`, distance SHALL be `5.0f`, yawDeg SHALL be `0.0f`, pitchDeg SHALL be `20.0f`

### Requirement: Orbit left-drag rotates camera
When `update()` is called and the left mouse button is down, the controller SHALL apply mouse delta to yaw and pitch:
- `yawDeg += mouseDelta.x * rotateSpeedDegPerPixel`
- `pitchDeg -= mouseDelta.y * rotateSpeedDegPerPixel`

#### Scenario: Left drag changes yaw and pitch
- **WHEN** left mouse button is down and mouse delta is `(10, 0)`
- **THEN** yawDeg SHALL increase by `10 * rotateSpeedDegPerPixel`

#### Scenario: Left drag vertical changes pitch
- **WHEN** left mouse button is down and mouse delta is `(0, -20)`
- **THEN** pitchDeg SHALL increase by `20 * rotateSpeedDegPerPixel`

### Requirement: Pitch clamping
After applying rotation, `pitchDeg` SHALL be clamped to `[minPitchDeg, maxPitchDeg]`. Default limits are `[-89.0f, 89.0f]`.

#### Scenario: Pitch does not exceed maximum
- **WHEN** pitchDeg would exceed `maxPitchDeg` after rotation
- **THEN** pitchDeg SHALL equal `maxPitchDeg`

#### Scenario: Pitch does not go below minimum
- **WHEN** pitchDeg would go below `minPitchDeg` after rotation
- **THEN** pitchDeg SHALL equal `minPitchDeg`

### Requirement: Orbit scroll zooms distance
When `update()` is called with non-zero wheel delta, the controller SHALL scale distance:
- `distance *= (1.0f - wheelDelta * zoomSpeedPerWheel)`
- Result SHALL be clamped to `[minDistance, maxDistance]`

#### Scenario: Scroll forward decreases distance
- **WHEN** wheel delta is positive (e.g., `1.0f`)
- **THEN** distance SHALL decrease

#### Scenario: Distance clamped to minimum
- **WHEN** repeated scroll-forward would push distance below `minDistance`
- **THEN** distance SHALL equal `minDistance`

#### Scenario: Distance clamped to maximum
- **WHEN** repeated scroll-backward would push distance above `maxDistance`
- **THEN** distance SHALL equal `maxDistance`

### Requirement: Orbit right-drag pans target
When `update()` is called and the right mouse button is down, the controller SHALL compute camera-local right and up vectors from current yaw/pitch, then translate `m_target`:
- `target -= right * mouseDelta.x * panSpeedPerPixel`
- `target += up * mouseDelta.y * panSpeedPerPixel`

#### Scenario: Right drag moves target
- **WHEN** right mouse button is down and mouse delta is `(10, 0)`
- **THEN** target SHALL shift along the camera-local right axis

### Requirement: Camera position computed from orbit parameters
After processing input, the controller SHALL compute:
```
eye.x = target.x + distance * cos(pitchRad) * sin(yawRad)
eye.y = target.y + distance * sin(pitchRad)
eye.z = target.z + distance * cos(pitchRad) * cos(yawRad)
```
And set `camera.position = eye`, `camera.target = m_target`, `camera.up = {0, 1, 0}`.

#### Scenario: Default position is in front of target
- **WHEN** yaw=0, pitch=0, distance=5, target={0,0,0}
- **THEN** camera.position SHALL be approximately `{0, 0, 5}`

#### Scenario: Yaw 90 degrees places camera on +X axis
- **WHEN** yaw=90, pitch=0, distance=5, target={0,0,0}
- **THEN** camera.position.x SHALL be approximately `5.0`

### Requirement: Integration test for OrbitCameraController
`src/test/integration/test_orbit_camera_controller.cpp` SHALL verify:
- Default position is in front of target
- Left drag rotates camera (yaw/pitch change)
- Pitch is clamped
- Wheel clamps distance
- Right drag pans target

All tests SHALL use `MockInputState` and SHALL NOT depend on SDL.

#### Scenario: All orbit controller tests pass
- **WHEN** running `test_orbit_camera_controller`
- **THEN** all assertions SHALL pass
