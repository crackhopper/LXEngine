## Why

OrbitCamera 适合围绕单个模型调试，但在 Sponza 等大场景里需要 FPS 风格的自由移动来观察阴影边缘、光照衰减和可见性。`ICameraController` 抽象已就绪（REQ-015），现在可以直接复用它来实现第二种控制模式。

## What Changes

- 实现 `FreeFlyCameraController`，复用 `ICameraController` 抽象
- 支持 WASD 水平移动、Space/LShift 垂直升降、右键按住鼠标 look、LCtrl 加速
- 位移严格按 `deltaTime` 缩放，对角线移动归一化
- 新增集成测试覆盖移动、look、加速、pitch clamp 等行为
- **不修改** Camera 类签名，不修改 ICameraController 接口

## Capabilities

### New Capabilities
- `freefly-camera-controller`: FreeFlyCameraController 实现——FPS 风格键盘移动 + 鼠标 look，复用 ICameraController 抽象

### Modified Capabilities
（无——ICameraController 接口不变，Camera 不变）

## Impact

- **新增文件**: `src/core/scene/freefly_camera_controller.hpp`, `.cpp`, `src/test/integration/test_freefly_camera_controller.cpp`
- **修改文件**: `src/test/CMakeLists.txt`（注册新测试）
- **依赖**: `ICameraController`（REQ-015）, `IInputState`（REQ-012）, `Clock`（REQ-014）
- **下游**: REQ-019 lxe_editor Orbit/FreeFly 切换、REQ-018 调试面板
