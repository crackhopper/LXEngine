# REQ-016: FreeFlyCameraController（FPS 风格自由飞行相机）

## 背景

OrbitCamera 适合围绕单个模型调试，但在 Sponza 这类大场景里观察阴影边缘、光照衰减和可见性行为时，需要一套能在场景内部自由移动的相机控制方式。

2026-04-16 按当前代码与需求状态核查：

- [src/core/scene/camera.hpp](../../src/core/scene/camera.hpp) 的 `Camera` 仍然只是纯数据对象
- `REQ-012` 的输入抽象已经存在，`Window::getInputState()` 已是正式入口
- `REQ-014` 的 `Clock` 已能提供 `deltaTime()`，足以支撑基于时间的位移
- `REQ-015` 已经把 Orbit 的目标形态定义为“先引入 `ICameraController` 与 `MockInputState`”，但这些类型本身尚未落地
- 仓库里还没有 `FreeFlyCameraController`

本需求的目标是提供一个最小可用的 FPS 风格自由飞行控制器，供后续 demo 场景漫游和大场景调试使用。它不追求完整输入系统，也不引入 action map、手柄、鼠标锁定状态机等更高阶段特性。

## 目标

1. 在 `core/scene/` 中实现 `FreeFlyCameraController`
2. 复用 `REQ-015` 约定的 `ICameraController` 抽象
3. 支持 `W/A/S/D` 水平移动、`Space` / `LShift` 垂直移动
4. 鼠标右键按住时启用 mouse look
5. 位移严格按 `deltaTime()` 缩放
6. `LCtrl` 作为加速键

## 需求

### R1: `FreeFlyCameraController` 类

新建 `src/core/scene/freefly_camera_controller.hpp` 与 `.cpp`：

```cpp
#pragma once
#include "core/scene/camera_controller.hpp"

namespace LX_core {

class FreeFlyCameraController : public ICameraController {
public:
  explicit FreeFlyCameraController(Vec3f startPos = {0, 0, 5},
                                   float yawDeg = 180.0f,
                                   float pitchDeg = 0.0f);

  void update(Camera& camera, const IInputState& input, float dt) override;

  Vec3f getPosition() const { return m_position; }
  float getYawDeg() const { return m_yawDeg; }
  float getPitchDeg() const { return m_pitchDeg; }

  void setPosition(Vec3f p) { m_position = p; }
  void setYawDeg(float y) { m_yawDeg = y; }
  void setPitchDeg(float p) { m_pitchDeg = p; }

  float moveSpeedPerSecond = 4.0f;
  float boostMultiplier = 4.0f;
  float lookSpeedDegPerPixel = 0.15f;
  float minPitchDeg = -89.0f;
  float maxPitchDeg = 89.0f;

private:
  Vec3f m_position;
  float m_yawDeg;
  float m_pitchDeg;
};

} // namespace LX_core
```

说明：

- 控制器内部维护自己的位置和欧拉角状态
- `Camera` 只是最终写回目标，不反向拥有 controller 状态

### R2: 更新规则

`update()` 的行为要求：

1. 鼠标 look

- 仅当 `input.isMouseButtonDown(MouseButton::Right)` 为 true 时启用
- `m_yawDeg -= input.getMouseDelta().x * lookSpeedDegPerPixel`
- `m_pitchDeg -= input.getMouseDelta().y * lookSpeedDegPerPixel`
- `pitch` clamp 到 `[minPitchDeg, maxPitchDeg]`

2. 朝向基向量

- 由 `yaw/pitch` 计算 `forward`
- 由 `forward` 与世界上方向 `(0,1,0)` 计算 `right`
- 世界上方向固定为 `(0,1,0)`

3. 键盘位移

- `W` → `+forward`
- `S` → `-forward`
- `D` → `+right`
- `A` → `-right`
- `Space` → `+worldUp`
- `LShift` → `-worldUp`

4. 速度

- 基础速度：`moveSpeedPerSecond`
- 若 `input.isKeyDown(KeyCode::LCtrl)`，速度乘以 `boostMultiplier`
- 位移量为 `movementNormalized * speed * dt`

5. 写回 camera

- `camera.position = m_position`
- `camera.target = m_position + forward`
- `camera.up = (0, 1, 0)`

约束：

- 对角线移动必须归一化，避免速度变成单轴移动的 `sqrt(2)` 倍
- 控制器不调用 `camera.updateMatrices()`
- 若 `dt <= 0`，允许无位移更新，但鼠标 look 逻辑仍可按实现决定是否处理

### R3: 鼠标锁定策略

本 REQ 不做 SDL 相对鼠标模式或鼠标锁定：

- 不调用 `SDL_SetWindowRelativeMouseMode`
- 不隐藏系统鼠标
- 不设计“右键按下进入锁定、松开退出”的状态机

原因：

- 这会与 `REQ-017` 的 ImGui 输入协调发生交叉
- 当前输入系统还没有 UI capture 协调的正式闭环
- 对 Phase 1 调试来说，“右键按住时用相对 delta 观察”已经足够

留作 `REQ-017` / `REQ-019` 或后续输入增强需求再评估。

### R4: 测试

新增 `src/test/integration/test_freefly_camera_controller.cpp`，至少覆盖：

- `w_key_moves_forward`
  - `yaw=0,pitch=0` 时，`W` 应让相机沿正前方向移动
- `mouse_look_only_with_right_button`
  - 未按右键时鼠标 delta 不改变 yaw/pitch
  - 按住右键后鼠标 delta 才生效
- `diagonal_movement_does_not_exceed_speed`
  - `W + D` 同按时总位移长度仍等于单轴速度
- `boost_multiplies_speed`
  - `LCtrl` 按住时移动速度按倍数放大
- `pitch_is_clamped`
  - 大幅垂直 mouse look 仍不越界

测试约束：

- 使用 `REQ-015` 中定义的 `MockInputState`
- 不依赖 SDL window 或真实输入设备
- 测试注册点为 [src/test/CMakeLists.txt](../../src/test/CMakeLists.txt)

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/core/scene/freefly_camera_controller.hpp` | 新增 |
| `src/core/scene/freefly_camera_controller.cpp` | 新增 |
| `src/core/CMakeLists.txt` | 如有需要，确保新 `.cpp` 被 `CORE_SOURCES` 收集 |
| `src/test/integration/test_freefly_camera_controller.cpp` | 新增 |
| `src/test/CMakeLists.txt` | 注册新测试 |

## 边界与约束

- 不依赖 ActionMap
- 不做手柄支持
- 不做鼠标锁定 / 隐藏
- 不做加速度、阻尼、惯性
- 不做碰撞检测或场景约束
- 不做 UI capture 协调；那是 `REQ-017` / `REQ-019` 的范围

## 依赖

- `REQ-012`：`IInputState`、`KeyCode`、`MouseButton`
- `REQ-014`：`Clock` 提供 `deltaTime()`
- `REQ-015`：`ICameraController` 抽象与 `MockInputState`

## 下游

- `REQ-019`：demo_scene_viewer 在 Orbit / FreeFly 之间切换
- `REQ-018`：调试面板显示或编辑 free-fly 参数
- Phase 2 更完整相机系统：扩展为 action map、手柄和鼠标锁定版本

## 实施状态

2026-04-17 已验证完成。

- `src/core/scene/freefly_camera_controller.hpp/.cpp` 已实现 FreeFly 控制器
- 复用了 `REQ-015` 已落地的 `ICameraController` 与 `MockInputState`
- `src/test/integration/test_freefly_camera_controller.cpp` 已覆盖前进、mouse look、对角线归一化、加速与 pitch clamp，并在本次核查中通过
