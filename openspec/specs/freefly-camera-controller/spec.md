## Requirements

### Requirement: FreeFlyCameraController class
`src/core/scene/freefly_camera_controller.hpp` and `.cpp` SHALL define a class `FreeFlyCameraController` in namespace `LX_core` that inherits `ICameraController`.

The constructor SHALL accept `(Vec3f startPos, float yawDeg, float pitchDeg)` with defaults `({0,0,5}, 180.0f, 0.0f)`.

The class SHALL expose:
- `Vec3f getPosition() const` / `void setPosition(Vec3f p)`
- `float getYawDeg() const` / `void setYawDeg(float y)`
- `float getPitchDeg() const` / `void setPitchDeg(float p)`
- Public tuning fields: `moveSpeedPerSecond`, `boostMultiplier`, `lookSpeedDegPerPixel`, `minPitchDeg`, `maxPitchDeg`

#### Scenario: Default construction
- **WHEN** constructing `FreeFlyCameraController` with no arguments
- **THEN** position SHALL be `{0,0,5}`, yawDeg SHALL be `180.0f`, pitchDeg SHALL be `0.0f`

### Requirement: Mouse look only with right button
The controller SHALL apply mouse delta to yaw and pitch ONLY when `input.isMouseButtonDown(MouseButton::Right)` is true:
- `yawDeg -= mouseDelta.x * lookSpeedDegPerPixel`
- `pitchDeg -= mouseDelta.y * lookSpeedDegPerPixel`
- pitchDeg SHALL be clamped to `[minPitchDeg, maxPitchDeg]`

When right button is NOT down, mouse delta SHALL NOT affect yaw or pitch.

#### Scenario: Right button enables mouse look
- **WHEN** right mouse button is down and mouse delta is `(10, 0)`
- **THEN** yawDeg SHALL change by `-10 * lookSpeedDegPerPixel`

#### Scenario: No look without right button
- **WHEN** right mouse button is NOT down and mouse delta is `(100, 100)`
- **THEN** yawDeg and pitchDeg SHALL remain unchanged

#### Scenario: Pitch is clamped
- **WHEN** right mouse button is down and large vertical mouse delta is applied
- **THEN** pitchDeg SHALL not exceed maxPitchDeg or go below minPitchDeg

### Requirement: Keyboard movement
The controller SHALL compute a movement vector from keyboard input:
- `W` → `+forward`
- `S` → `-forward`
- `D` → `+right`
- `A` → `-right`
- `Space` → `+worldUp (0,1,0)`
- `LShift` → `-worldUp`

`forward` and `right` SHALL be derived from current yaw/pitch. `forward` SHALL lie in the direction the camera faces. `right` SHALL be perpendicular to forward and world up.

#### Scenario: W key moves forward
- **WHEN** W is down, yaw=180, pitch=0, dt=1.0
- **THEN** position SHALL move in the -Z direction by approximately moveSpeedPerSecond

#### Scenario: Space key moves up
- **WHEN** Space is down, dt=1.0
- **THEN** position.y SHALL increase by approximately moveSpeedPerSecond

### Requirement: Diagonal movement normalization
When multiple movement keys are pressed simultaneously, the combined movement vector SHALL be normalized before applying speed, so total displacement per frame equals single-axis speed.

#### Scenario: W+D does not exceed single-axis speed
- **WHEN** W and D are both down, dt=1.0
- **THEN** total displacement length SHALL be approximately equal to moveSpeedPerSecond (not sqrt(2) times)

### Requirement: Boost with LCtrl
When `input.isKeyDown(KeyCode::LCtrl)` is true, the effective move speed SHALL be `moveSpeedPerSecond * boostMultiplier`.

#### Scenario: LCtrl multiplies speed
- **WHEN** W and LCtrl are both down, dt=1.0
- **THEN** displacement SHALL be approximately `moveSpeedPerSecond * boostMultiplier`

### Requirement: Camera writeback
After processing input, the controller SHALL write camera pose through `CameraComponent` so that:
- the owning camera node’s translation matches the controller’s internal position
- the owning camera node faces `position + forward`
- the camera-up convention remains `{0, 1, 0}`

The controller SHALL NOT directly upload matrices or perform render-side synchronization.

#### Scenario: Camera reflects controller state
- **WHEN** update completes
- **THEN** the camera node pose SHALL reflect the controller's internal position and forward direction

### Requirement: Integration test for FreeFlyCameraController
`src/test/integration/test_freefly_camera_controller.cpp` SHALL verify:
- W key moves forward
- Mouse look only with right button
- Diagonal movement normalization
- Boost multiplies speed
- Pitch is clamped

All tests SHALL use `MockInputState`, SHALL NOT depend on SDL, and SHALL exercise the controller through a `CameraComponent` attached to a `SceneNode`.

#### Scenario: All freefly controller tests pass
- **WHEN** running `test_freefly_camera_controller`
- **THEN** all assertions SHALL pass
