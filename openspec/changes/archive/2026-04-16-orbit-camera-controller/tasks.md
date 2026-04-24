## 1. MockInputState

- [x] 1.1 Create `src/core/input/mock_input_state.hpp` — header-only, implements IInputState with writable setters; nextFrame clears delta and wheel

## 2. ICameraController 抽象

- [x] 2.1 Create `src/core/scene/camera_controller.hpp` — ICameraController 纯虚基类 with `update(Camera&, const IInputState&, float dt)` and CameraControllerSharedPtr alias

## 3. OrbitCameraController 实现

- [x] 3.1 Create `src/core/scene/orbit_camera_controller.hpp` — class declaration with orbit state (target, distance, yaw, pitch) and tuning fields
- [x] 3.2 Create `src/core/scene/orbit_camera_controller.cpp` — implement update(): left-drag rotation, right-drag pan, scroll zoom, pitch/distance clamping, spherical-to-cartesian position computation
- [x] 3.3 Update `src/core/CMakeLists.txt` — ensure `orbit_camera_controller.cpp` is collected by CORE_SOURCES (verify glob pattern covers it)

## 4. 集成测试

- [x] 4.1 Create `src/test/integration/test_orbit_camera_controller.cpp` — tests: default_position, left_drag_rotates, pitch_clamped, wheel_clamps_distance, right_drag_pans_target; all use MockInputState
- [x] 4.2 Update `src/test/CMakeLists.txt` — register `test_orbit_camera_controller` in TEST_INTEGRATION_EXE_LIST

## 5. 验证

- [x] 5.1 Build passes (`ninja LX_Core` and test target)
- [x] 5.2 All orbit controller tests pass
