## Why

Camera 当前是纯数据对象，调试 PBR/IBL/shadow 时缺少最基本的"围着模型转 + 滚轮缩放 + 拖拽平移"交互。把 orbit 逻辑放进独立 controller 而不是塞进 Camera，可以保持 Camera 职责单一，并为后续 FreeFly、控制器切换等扩展铺路。

## What Changes

- 引入 `ICameraController` 抽象基类，定义 `update(Camera&, IInputState&, dt)` 统一签名
- 实现 `OrbitCameraController`：左键旋转、右键平移、滚轮缩放
- 新增 `MockInputState` 可写测试辅助类，让控制器测试不依赖 SDL
- 新增集成测试覆盖 orbit 的核心行为（旋转、平移、缩放、clamp）
- **不修改** Camera 类签名

## Capabilities

### New Capabilities
- `camera-controller`: ICameraController 抽象基类 + OrbitCameraController 具体实现，定义相机控制器的统一接口与 orbit 交互行为
- `mock-input-state`: 可写入的 MockInputState 测试辅助类，补齐 input-abstraction 的测试缺口

### Modified Capabilities
（无——Camera、IInputState 的接口签名均不变）

## Impact

- **新增文件**: `src/core/scene/camera_controller.hpp`, `orbit_camera_controller.hpp/.cpp`, `src/core/input/mock_input_state.hpp`, `src/test/integration/test_orbit_camera_controller.cpp`
- **修改文件**: `src/core/CMakeLists.txt`（收集新 .cpp）, `src/test/CMakeLists.txt`（注册新测试）
- **依赖**: `IInputState`（REQ-012）, `Camera`（已有）
- **下游**: REQ-016 FreeFly 复用 ICameraController、REQ-019 lxe_editor 默认控制器
