# REQ-015: OrbitCameraController（轨道相机控制器）

## 背景

当前 `Camera` 仍然只是一个纯数据对象，没有任何内建输入交互逻辑。

2026-04-16 按当前代码核查：

- [src/core/scene/camera.hpp](../../src/core/scene/camera.hpp) 的 `Camera` 只持有 `position`、`target`、`up`、透视参数和 `updateMatrices()`
- 仓库里还没有 `ICameraController` 或 `OrbitCameraController`
- `REQ-012` 的输入抽象已存在，`DummyInputState` 也已经落地
- 但当前还没有一个可写入状态的 testing helper，因此控制器测试还缺 `MockInputState`

调试 PBR / IBL / shadow 时，最直接的交互就是“围着模型转 + 滚轮缩放 + 拖拽平移”，也就是典型的 orbit camera / model viewer 交互。把这套逻辑放进独立 controller，而不是塞进 `Camera` 本体，有几个现实好处：

- `Camera` 继续保持“运行时状态 + 矩阵更新”的职责，不混入输入依赖
- 后续 `REQ-016` 的 FreeFly 可以复用同一个 controller 抽象
- `REQ-019` 可以在 Orbit / FreeFly 之间切换，而不污染 `Camera` 的数据语义

本需求只实现 OrbitCameraController 及其抽象基类，不修改 `Camera` 类签名。

## 目标

1. 在 `core/scene/` 引入统一的相机控制器抽象 `ICameraController`
2. 提供第一个具体实现 `OrbitCameraController`
3. 支持左键旋转、滚轮缩放、右键平移三种基础 orbit 交互
4. 不要求 `Camera` 知道 controller 的存在
5. 为控制器补可写测试输入 `MockInputState`，让测试不依赖 SDL

## 需求

### R1: `ICameraController` 抽象基类

新建 `src/core/scene/camera_controller.hpp`：

```cpp
#pragma once
#include "core/input/input_state.hpp"
#include "core/scene/camera.hpp"

namespace LX_core {

class ICameraController {
public:
  virtual ~ICameraController() = default;

  /// 每帧调用一次。controller 根据输入修改 camera 的 position / target / up，
  /// 但不调用 camera.updateMatrices()；矩阵更新时机由调用方统一决定。
  virtual void update(Camera& camera, const IInputState& input, float dt) = 0;
};

using CameraControllerPtr = std::shared_ptr<ICameraController>;

} // namespace LX_core
```

约束：

- `update()` 接 `Camera&`，controller 不持有相机生命周期
- `update()` 接 `const IInputState&`，controller 不持有输入对象
- `dt` 参数保留在统一签名里，虽然 orbit 不强依赖，但后续 FreeFly 需要

### R2: `OrbitCameraController` 实现

新建 `src/core/scene/orbit_camera_controller.hpp` 与 `.cpp`：

```cpp
namespace LX_core {

class OrbitCameraController : public ICameraController {
public:
  OrbitCameraController(Vec3f target = {0, 0, 0},
                        float distance = 5.0f,
                        float yawDeg = 0.0f,
                        float pitchDeg = 20.0f);

  void update(Camera& camera, const IInputState& input, float dt) override;

  Vec3f getTarget() const { return m_target; }
  void setTarget(Vec3f t) { m_target = t; }

  float getDistance() const { return m_distance; }
  void setDistance(float d);

  float getYawDeg() const { return m_yawDeg; }
  float getPitchDeg() const { return m_pitchDeg; }

  float rotateSpeedDegPerPixel = 0.4f;
  float panSpeedPerPixel = 0.005f;
  float zoomSpeedPerWheel = 0.15f;
  float minDistance = 0.5f;
  float maxDistance = 200.0f;
  float minPitchDeg = -89.0f;
  float maxPitchDeg = 89.0f;

private:
  Vec3f m_target;
  float m_distance;
  float m_yawDeg;
  float m_pitchDeg;
};

} // namespace LX_core
```

行为要求：

- 左键按住 + 鼠标 delta：
  - 更新 yaw / pitch
  - `pitch` clamp 到 `[minPitchDeg, maxPitchDeg]`
- 右键按住 + 鼠标 delta：
  - 基于当前朝向计算 right / up
  - 平移 `m_target`
- 滚轮：
  - 按比例缩放 `m_distance`
  - clamp 到 `[minDistance, maxDistance]`
- 最终由 `(target, distance, yaw, pitch)` 反算 camera：
  - `camera.position`
  - `camera.target`
  - `camera.up = (0,1,0)`

约束：

- controller 更新 `Camera` 的空间参数，但不调用 `camera.updateMatrices()`
- orbit 只处理输入到相机姿态的映射，不做 smoothing、惯性或阻尼

### R3: `MockInputState` 测试辅助类

由于 `REQ-012` 的 `DummyInputState` 是全零只读实现，无法驱动交互测试，本 REQ 需要补一个可写入的测试 helper。

新建 `src/core/input/mock_input_state.hpp`：

```cpp
namespace LX_core {

class MockInputState : public IInputState {
public:
  void setKeyDown(KeyCode k, bool down);
  void setMouseButtonDown(MouseButton b, bool down);
  void setMousePosition(Vec2f p);
  void setMouseDelta(Vec2f d);
  void setMouseWheelDelta(float w);

  bool isKeyDown(KeyCode k) const override;
  bool isMouseButtonDown(MouseButton b) const override;
  Vec2f getMousePosition() const override;
  Vec2f getMouseDelta() const override;
  float getMouseWheelDelta() const override;
  void nextFrame() override;

private:
  std::array<bool, static_cast<size_t>(KeyCode::Count)> m_keys{};
  std::array<bool, static_cast<size_t>(MouseButton::Count)> m_buttons{};
  Vec2f m_pos{0.0f, 0.0f};
  Vec2f m_delta{0.0f, 0.0f};
  float m_wheel = 0.0f;
};

} // namespace LX_core
```

约束：

- 这是测试与 demo 辅助类，不依赖 SDL
- `nextFrame()` 至少清零 mouse delta 与 wheel delta
- 不要求它模拟真实平台事件，仅要求提供稳定可控的输入快照

### R4: 集成测试

新增 `src/test/integration/test_orbit_camera_controller.cpp`，至少覆盖：

- `default_position_is_in_front_of_target`
  - 默认 `yaw=0,pitch=0,distance=5`
  - 更新后相机位于 target 前方固定距离
- `left_drag_rotates_camera`
  - 左键按住 + mouse delta 改变 yaw/pitch
- `pitch_is_clamped`
  - 大幅垂直拖拽不会越过 pitch 限制
- `wheel_clamps_distance`
  - 滚轮缩放后 distance 保持在合法区间
- `right_drag_pans_target`
  - 右键拖拽改变 target

测试约束：

- 使用 `MockInputState`
- 不依赖 SDL window 或真实输入设备
- 不要求在测试里调用 `camera.updateMatrices()`，除非某个断言明确依赖矩阵结果

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/core/scene/camera_controller.hpp` | 新增 |
| `src/core/scene/orbit_camera_controller.hpp` | 新增 |
| `src/core/scene/orbit_camera_controller.cpp` | 新增 |
| `src/core/input/mock_input_state.hpp` | 新增 |
| `src/core/CMakeLists.txt` | 如有需要，确保新 `.cpp` 被 `CORE_SOURCES` 收集 |
| `src/test/integration/test_orbit_camera_controller.cpp` | 新增 |
| `src/test/CMakeLists.txt` | 注册新测试 |

## 边界与约束

- 不修改 `Camera` 类签名
- 不做 Trackball / ArcBall
- 不做触屏 / 手势
- 不做平滑插值、惯性或阻尼
- 不做 UI 抢占输入协调；那是 `REQ-017` / `REQ-019` 的范围
- 控制器不负责调用 `camera.updateMatrices()`

## 依赖

- `REQ-012`：`IInputState`、`MouseButton`
- `REQ-013`：推荐，但不是测试前置；真实 SDL 输入只影响运行时接线

## 下游

- `REQ-016`：复用 `ICameraController` 抽象
- `REQ-018`：调试面板可编辑 orbit 参数
- `REQ-019`：demo_scene_viewer 默认相机控制器
- Phase 2 更完整相机系统：在统一 controller 抽象上扩更多模式

## 实施状态

2026-04-17 已验证完成。

- `src/core/scene/camera_controller.hpp` 已提供 `ICameraController`
- `src/core/scene/orbit_camera_controller.hpp/.cpp` 已实现 Orbit 控制器
- `src/core/input/mock_input_state.hpp` 已实现可写测试输入
- `src/test/integration/test_orbit_camera_controller.cpp` 已覆盖默认位姿、旋转、pitch clamp、缩放 clamp、平移与 `nextFrame()` 行为，并在本次核查中通过
