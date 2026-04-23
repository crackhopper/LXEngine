# REQ-012: Window 输入抽象接口（`IInputState`）

## 背景

当前窗口系统还没有向 `core/` 层暴露任何统一的输入状态接口。

2026-04-16 按当前代码核查：

- [src/core/platform/window.hpp](../../src/core/platform/window.hpp) 的 `Window` 接口只有窗口尺寸、graphics handle、`onClose()`、`shouldClose()` 等能力，没有键盘 / 鼠标访问入口
- [src/infra/window/sdl_window.cpp](../../src/infra/window/sdl_window.cpp) 的 SDL 事件循环只消费 `SDL_EVENT_QUIT`，其余输入事件全部被丢弃
- [src/core/gpu/engine_loop.cpp](../../src/core/gpu/engine_loop.cpp) 也还没有“输入帧推进”的统一时序
- `REQ-015` / `REQ-016` 的相机控制器、`REQ-017` 的 ImGui 输入协调都需要一个稳定的 `core` 层输入入口

[Phase 2 REQ-203](../../notes/roadmaps/main-roadmap/phase-2-foundation-layer.md) 规划了更完整的输入系统（含边沿检测、状态推进、扩展设备）。本 REQ 只做它的最小前置版本：先把 `core/` 层的只读输入快照接口立住，让后续 SDL3 实现和相机控制器有共同依赖。

本需求只定义接口、枚举和 dummy/testing helper；不实现 SDL/GLFW 的真实事件读取，不重构主循环。

## 目标

1. 在 `src/core/input/` 下新增最小输入抽象：`KeyCode`、`MouseButton`、`IInputState`
2. `Window` 接口新增 `getInputState()`，为后续输入实现提供统一入口
3. 提供一个“全零”的 `DummyInputState`，让当前代码和 headless 测试在没有真实输入实现时也能编译通过
4. 保持接口签名与 Phase 2 更完整输入系统兼容，不在 Phase 1 提前引入事件流、action map、手柄等范围

## 需求

### R1: `KeyCode` 枚举

新建 `src/core/input/key_code.hpp`：

```cpp
namespace LX_core {

enum class KeyCode : uint16_t {
  Unknown = 0,

  // 字母
  A, B, C, D, E, F, G, H, I, J, K, L, M,
  N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

  // 数字
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

  // 控制键
  Escape, Space, LShift, RShift, LCtrl, RCtrl, LAlt, RAlt, Enter, Tab,

  // 方向
  Left, Right, Up, Down,

  // 功能键（Phase 1 只先到 F4）
  F1, F2, F3, F4,

  Count
};

} // namespace LX_core
```

不在本 REQ 范围：

- F5-F12
- 数字小键盘
- 国际键盘扩展键

### R2: `MouseButton` 枚举

新建 `src/core/input/mouse_button.hpp`：

```cpp
namespace LX_core {

enum class MouseButton : uint8_t {
  Left   = 0,
  Right  = 1,
  Middle = 2,
  Count  = 3,
};

} // namespace LX_core
```

### R3: `IInputState` 接口

新建 `src/core/input/input_state.hpp`：

```cpp
#include "core/input/key_code.hpp"
#include "core/input/mouse_button.hpp"
#include "core/math/vec.hpp"
#include <memory>

namespace LX_core {

class IInputState {
public:
  virtual ~IInputState() = default;

  // ---- 键盘 ----
  virtual bool isKeyDown(KeyCode code) const = 0;

  // ---- 鼠标按键 ----
  virtual bool isMouseButtonDown(MouseButton button) const = 0;

  // ---- 鼠标位置（窗口客户区像素坐标，左上为 0,0）----
  virtual Vec2f getMousePosition() const = 0;

  // ---- 自上一输入帧累计的鼠标位移 ----
  virtual Vec2f getMouseDelta() const = 0;

  // ---- 自上一输入帧累计的滚轮位移 ----
  virtual float getMouseWheelDelta() const = 0;

  // ---- 帧推进 ----
  /// 清理由具体实现定义为 per-frame 的累计量。
  /// 本 REQ 只定义接口，不规定调用时机；实际时序由 REQ-013 决定。
  virtual void nextFrame() = 0;
};

using InputStatePtr = std::shared_ptr<IInputState>;

} // namespace LX_core
```

本接口故意省略：

- `isKeyPressed` / `isKeyReleased`
- `Modifier` 复合键查询
- 事件订阅 / observer pattern
- 文本输入
- 手柄输入

这些都留给后续更完整的输入系统。

### R4: `Window` 接口扩展

修改 `src/core/platform/window.hpp`，新增：

```cpp
virtual InputStatePtr getInputState() const = 0;
```

行为约束：

- 同一个 `Window` 实例多次调用 `getInputState()`，返回的应当是同一个共享输入对象
- 本 REQ 不新增 `pollEvents()`、不新增 `Window::nextFrame()`、不修改 `shouldClose()` / `onClose()` 契约
- SDL / GLFW 在本 REQ 中只需返回 dummy 占位输入状态，真实事件更新留给 `REQ-013`

### R5: `DummyInputState`

新建 `src/core/input/dummy_input_state.hpp`，提供一个 `IInputState` 的全零实现：

```cpp
namespace LX_core {

class DummyInputState : public IInputState {
public:
  bool isKeyDown(KeyCode) const override { return false; }
  bool isMouseButtonDown(MouseButton) const override { return false; }
  Vec2f getMousePosition() const override { return {0, 0}; }
  Vec2f getMouseDelta() const override { return {0, 0}; }
  float getMouseWheelDelta() const override { return 0.0f; }
  void nextFrame() override {}
};

} // namespace LX_core
```

用途：

- 当前 SDL / GLFW window 实现的临时占位返回值
- 不依赖真实输入的集成测试
- 后续相机控制器测试的最简空输入

### R6: `infra/window` 的占位适配

虽然本 REQ 不实现真实输入，但为保持 `Window` 新纯虚接口可编译，当前 window 实现必须同步提供占位实现：

- `src/infra/window/window.hpp` 增加 `getInputState() const override`
- `src/infra/window/sdl_window.cpp` 与 `src/infra/window/glfw_window.cpp` 持有一个 `DummyInputState`
- `getInputState()` 返回该共享对象

约束：

- 不在本 REQ 中读取任何 SDL / GLFW 键鼠事件
- 不在本 REQ 中修改 SDL `shouldClose()` 的行为
- 不在本 REQ 中引入新的窗口事件分发入口

## 测试

新增 `src/test/integration/test_input_state.cpp`，至少覆盖：

- `DummyInputState` 的所有 getter 返回零值
- `DummyInputState::nextFrame()` 可调用且不改变零值语义
- `KeyCode::Count` / `MouseButton::Count` 只作为边界值存在，不应被当作真实输入码使用

不在本 REQ 中测试：

- SDL 真实事件处理
- GLFW 真实事件处理
- 主循环中的输入时序

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/core/input/key_code.hpp` | 新增 |
| `src/core/input/mouse_button.hpp` | 新增 |
| `src/core/input/input_state.hpp` | 新增 |
| `src/core/input/dummy_input_state.hpp` | 新增 |
| `src/core/platform/window.hpp` | 新增 `getInputState()` 纯虚方法 |
| `src/core/CMakeLists.txt` | 把新增头文件纳入导出/构建可见范围 |
| `src/infra/window/window.hpp` | 同步声明 `getInputState() override` |
| `src/infra/window/sdl_window.cpp` | 返回 `DummyInputState` 占位实现 |
| `src/infra/window/glfw_window.cpp` | 返回 `DummyInputState` 占位实现 |
| `src/test/integration/test_input_state.cpp` | 新增 |
| `src/test/CMakeLists.txt` | 注册新测试 |

## 边界与约束

- 不实现 SDL/GLFW 的真实输入读取
- 不新增 `Window::nextFrame()` 或 `pollEvents()` 一类新接口
- 不做边沿检测
- 不做 action mapping
- 不做文本输入
- 不做手柄支持
- 不改 `shouldClose()` / `onClose()` 的现有契约

## 依赖

- 无

## 下游

- `REQ-013`：SDL3 的真实输入实现填充到本接口背后
- `REQ-015` / `REQ-016`：相机控制器读取 `IInputState`
- `REQ-017`：ImGui 输入协调复用同一抽象层
- Phase 2 更完整输入系统：在本接口上扩充边沿检测、设备类型与状态推进语义

## 实施状态

2026-04-16 实施完成。

- `src/core/input/` 已创建：`key_code.hpp`、`mouse_button.hpp`、`input_state.hpp`、`dummy_input_state.hpp`
- `Window` 已新增 `getInputState()` 纯虚方法
- SDL / GLFW window 实现已返回 `DummyInputState` 占位
- `test_input_state` 集成测试已通过（7 个测试函数）
