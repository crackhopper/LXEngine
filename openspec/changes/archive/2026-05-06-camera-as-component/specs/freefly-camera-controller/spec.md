## MODIFIED Requirements

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
