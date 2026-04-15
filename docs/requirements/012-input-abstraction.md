# REQ-012: Window 输入抽象接口（IInputState）

## 背景

当前窗口系统 **完全没有暴露任何输入事件**：

- `src/core/platform/window.hpp:8-37` 的 `Window` 接口只有 `getWidth/Height` / `updateSize` / `shouldClose` / `onClose`，没有任何鼠标 / 键盘相关方法
- `src/infra/window/sdl_window.cpp:39-46` 的 `Impl::shouldClose` 直接吃掉 `SDL_PollEvent` 的所有事件并丢弃 —— 即使 SDL 已经分发了鼠标 / 键盘事件，上层也无从获取
- `src/test/test_render_triangle.cpp:104-114` 的循环里相机参数全部硬编码，没有任何交互

为了支持 REQ-015 / REQ-016 的相机控制器和 REQ-017 的 ImGui 输入 forwarding，必须先把"键盘按键状态、鼠标位置、鼠标按键状态、滚轮"这四类基础信号从 SDL 抽象到 `core/` 层。

[Phase 2 REQ-203](../../notes/roadmaps/phase-2-foundation-layer.md) 规划了完整的 `IInputState` 接口（含 `isKeyPressed`/`isKeyReleased` 这种边沿检测 + `nextFrame()` 状态推进）。本 REQ 是 Phase 2 REQ-203 的**最小可用前置版本**：只暴露 Phase 1 调试链路真正需要的字段，避免一次性引入 Phase 2 的全套抽象。Phase 2 落地时再扩展，接口签名兼容。

本需求**只**定义 `core/` 层接口，不写任何 SDL 实现 —— 实现见 REQ-013。

## 目标

1. `core/input/` 新增 `key_code.hpp` / `mouse_button.hpp` / `input_state.hpp` 三个 header
2. `IInputState` 接口涵盖键盘 down / 鼠标 down / 鼠标位置 / 鼠标 delta / 滚轮 delta 五类查询
3. `Window` 接口新增 `getInputState()` 暴露每帧最新的 input snapshot
4. 接口签名与 Phase 2 REQ-203 兼容（即 Phase 2 是本 REQ 的超集，不做向后不兼容修改）

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
  // 功能键（先到 F4，足够 Phase 1 调试）
  F1, F2, F3, F4,

  Count
};

}
```

不在本 REQ 范围：F5-F12 / 小键盘 / 国际键盘扩展键。Phase 2 REQ-203 扩。

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

}
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
  /// 当前是否按住。
  virtual bool isKeyDown(KeyCode code) const = 0;

  // ---- 鼠标按键 ----
  virtual bool isMouseButtonDown(MouseButton button) const = 0;

  // ---- 鼠标位置（窗口客户区像素坐标，左上为 0,0）----
  virtual Vec2f getMousePosition() const = 0;

  /// 自上一帧到本帧的鼠标移动量（像素）。
  /// REQ-013 的实现会在每帧 nextFrame() 时累计 delta。
  virtual Vec2f getMouseDelta() const = 0;

  // ---- 滚轮 ----
  /// 自上一帧累计的滚轮 delta（垂直方向，+1 = 上滚一格）。
  virtual float getMouseWheelDelta() const = 0;

  // ---- 帧推进 ----
  /// 由 Window::pollEvents 在每帧末尾调用，清零 per-frame delta（mouse / wheel）。
  /// 不清键盘 / 鼠标按键的 down 状态。
  virtual void nextFrame() = 0;
};

using InputStatePtr = std::shared_ptr<IInputState>;

}
```

**接口的故意省略**（与 Phase 2 REQ-203 的差异）：

- 没有 `isKeyPressed` / `isKeyReleased`（边沿检测）—— Phase 2 加。Phase 1 的相机控制器只用 down 状态足够
- 没有 `Modifier` 复合键查询 —— 用 `isKeyDown(LShift)` 自己组合
- 没有事件订阅式回调（observer pattern）—— polling 状态足够 Phase 1
- 没有手柄 —— Phase 2 REQ-204

### R4: `Window` 接口扩展

修改 `src/core/platform/window.hpp:8`：

```cpp
class Window {
public:
  // ...现有方法保持不变...

  /// 返回该 Window 持有的 input state 快照。
  /// 同一个 Window 实例返回的是同一个 InputState，不要重复 cache。
  /// REQ-013 的 SDL 实现在 shouldClose() / pollEvents() 内部更新这个快照。
  virtual InputStatePtr getInputState() const = 0;
};
```

注意：本 REQ 只增 `getInputState()` 一个虚方法，不动 `shouldClose` / `onClose` 的现有签名。

### R5: dummy implementation 用于 headless 测试

新建 `src/core/input/dummy_input_state.hpp`，提供一个 `IInputState` 的"全零"实现：

```cpp
class DummyInputState : public IInputState {
public:
  bool isKeyDown(KeyCode) const override { return false; }
  bool isMouseButtonDown(MouseButton) const override { return false; }
  Vec2f getMousePosition() const override { return {0, 0}; }
  Vec2f getMouseDelta() const override { return {0, 0}; }
  float getMouseWheelDelta() const override { return 0.0f; }
  void nextFrame() override {}
};
```

用途：

- 让 `RenderQueue` / FrameGraph 这类不需要输入的集成测试能注入一个空 input state
- 让 REQ-015 / REQ-016 的 controller 单元测试可以构造确定性 input

## 测试

- 新建 `src/test/integration/test_input_state.cpp`：
  - 测试 `DummyInputState` 所有 getter 返回零值
  - 测试 `KeyCode::Count` / `MouseButton::Count` 枚举值不被使用作真实 key
  - 不测试 SDL 实现（那是 REQ-013 的事）

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/core/input/key_code.hpp` | 新增 |
| `src/core/input/mouse_button.hpp` | 新增 |
| `src/core/input/input_state.hpp` | 新增 |
| `src/core/input/dummy_input_state.hpp` | 新增 |
| `src/core/platform/window.hpp` | 新增 `getInputState()` 纯虚方法 |
| `src/core/CMakeLists.txt` | 把新增 header 加入 install / public include |
| `src/infra/window/window.hpp` | 同步声明 `getInputState() override` |
| `src/infra/window/sdl_window.cpp` / `window_impl_glfw.cpp` | **临时** stub 返回 `DummyInputState`（保持编译通过；真实实现见 REQ-013） |
| `src/test/integration/test_input_state.cpp` | 新增 |
| `src/test/integration/CMakeLists.txt` | 注册新测试 |

## 边界与约束

- **不实现 SDL/GLFW 真正的事件读取** —— 那是 REQ-013
- **不做边沿检测**（isKeyPressed / isKeyReleased）—— Phase 2 REQ-203 加
- **不做 action mapping** —— Phase 2 REQ-205
- 不动 `shouldClose` / `onClose` 现有契约
- `getInputState()` 必须保证多次调用返回同一对象（`shared_ptr` 同 ptr），让相机控制器可以提前 cache

## 依赖

- 无（纯接口添加）

## 下游

- **REQ-013**：SDL3 实现填进去
- **REQ-015 / REQ-016**：相机控制器持有 `InputStatePtr`
- **REQ-017**：ImGui 输入 forwarding 走同一个 input state
- **Phase 2 REQ-203**：在本 REQ 接口上扩 isKeyPressed / isKeyReleased / Modifier / nextFrame 语义增强

## 实施状态

未开始。
