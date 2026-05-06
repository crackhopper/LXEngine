## MODIFIED Requirements

### Requirement: ICameraController abstract base class
`src/core/scene/camera_controller.hpp` SHALL define an abstract class `ICameraController` in namespace `LX_core` with:
- A virtual destructor
- A pure virtual method `void update(CameraComponent& camera, const IInputState& input, float dt)`
- A type alias `CameraControllerSharedPtr = std::shared_ptr<ICameraController>`

The `update()` method SHALL NOT directly upload matrices or issue render-side synchronization. It SHALL limit itself to camera-pose and controller-state updates through `CameraComponent`.

#### Scenario: ICameraController is abstract
- **WHEN** attempting to instantiate `ICameraController` directly
- **THEN** compilation SHALL fail because `update` is pure virtual

#### Scenario: update does not perform render-side synchronization
- **WHEN** any concrete controller's `update()` completes
- **THEN** scene-level camera resource upload state SHALL remain under the caller’s control rather than being forced from inside the controller

### Requirement: Camera position computed from orbit parameters
After processing input, the controller SHALL compute:
```
eye.x = target.x + distance * cos(pitchRad) * sin(yawRad)
eye.y = target.y + distance * sin(pitchRad)
eye.z = target.z + distance * cos(pitchRad) * cos(yawRad)
```
And SHALL write the result through `CameraComponent` so that the owning `SceneNode` pose matches the orbit state and the camera continues to look at `m_target` with up `{0, 1, 0}`.

#### Scenario: Default position is in front of target
- **WHEN** yaw=0, pitch=0, distance=5, target={0,0,0}
- **THEN** the resulting camera eye position SHALL be approximately `{0, 0, 5}`

#### Scenario: Yaw 90 degrees places camera on +X axis
- **WHEN** yaw=90, pitch=0, distance=5, target={0,0,0}
- **THEN** the resulting camera eye-position x component SHALL be approximately `5.0`

### Requirement: Integration test for OrbitCameraController
`src/test/integration/test_orbit_camera_controller.cpp` SHALL verify:
- Default position is in front of target
- Left drag rotates camera (yaw/pitch change)
- Pitch is clamped
- Wheel clamps distance
- Right drag pans target

All tests SHALL use `MockInputState`, SHALL NOT depend on SDL, and SHALL exercise the controller through a `CameraComponent` attached to a `SceneNode`.

#### Scenario: All orbit controller tests pass
- **WHEN** running `test_orbit_camera_controller`
- **THEN** all assertions SHALL pass
