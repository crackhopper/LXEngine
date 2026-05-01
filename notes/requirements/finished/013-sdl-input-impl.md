# REQ-013: SDL3 Window 的真实输入状态实现

## 背景

`REQ-012` 已经把 `IInputState` 和 `Window::getInputState()` 定义成 `core/` 层输入抽象，但当前窗口后端还没有真实输入实现。

2026-04-16 按当前代码核查：

- [src/infra/window/sdl_window.cpp](../../src/infra/window/sdl_window.cpp) 的 SDL 事件循环仍只处理 `SDL_EVENT_QUIT`
- 所有非 quit 事件当前都会被静默丢弃，包括键盘、鼠标移动、鼠标按键、滚轮和窗口事件
- `Window` 目前没有单独的 `pollEvents()` 或 `nextFrame()` 接口，主循环只依赖 `shouldClose()`
- `REQ-015` / `REQ-016` 的相机控制器、`REQ-017` 的 ImGui 输入协调，都需要一个能提供真实键鼠状态的 `getInputState()`

因此，本需求的职责是：把 SDL3 的事件循环接到 `REQ-012` 的输入抽象后面，使 `Window::getInputState()` 不再返回 dummy，而是返回一个真正随 SDL 事件更新的状态对象。

本需求只完成 SDL3 路径。GLFW 继续保留 dummy 占位实现，不阻塞 Phase 1 主路径。

## 目标

1. 新增 `Sdl3InputState`，实现 `REQ-012` 定义的 `IInputState`
2. SDL3 后端将 `SDL_Event` 映射到 `KeyCode` / `MouseButton` / 鼠标位置 / 鼠标 delta / 滚轮 delta
3. `Window::shouldClose()` 与输入更新复用同一轮 `SDL_PollEvent()`，避免事件被消费两次
4. 明确 `IInputState::nextFrame()` 的调用时序，但不新增 `Window::nextFrame()` 接口
5. 为 `Sdl3InputState` 增加可独立运行的集成测试

## 需求

### R1: `Sdl3InputState` 实现类

新建 `src/infra/window/sdl3_input_state.hpp` 与 `.cpp`：

```cpp
namespace LX_infra {

class Sdl3InputState : public LX_core::IInputState {
public:
  bool isKeyDown(LX_core::KeyCode code) const override;
  bool isMouseButtonDown(LX_core::MouseButton button) const override;
  LX_core::Vec2f getMousePosition() const override;
  LX_core::Vec2f getMouseDelta() const override;
  float getMouseWheelDelta() const override;
  void nextFrame() override;

  /// 消费一个 SDL 事件并更新内部状态。
  /// 返回 true 表示收到 quit 请求。
  bool handleSdlEvent(const SDL_Event& event);

private:
  std::array<bool, static_cast<size_t>(LX_core::KeyCode::Count)> m_keyDown{};
  std::array<bool, static_cast<size_t>(LX_core::MouseButton::Count)> m_mouseButtonDown{};
  LX_core::Vec2f m_mousePos{0.0f, 0.0f};
  LX_core::Vec2f m_mouseDeltaAccum{0.0f, 0.0f};
  float m_wheelDeltaAccum = 0.0f;
};

} // namespace LX_infra
```

行为要求：

- `isKeyDown()` / `isMouseButtonDown()` 返回持续状态
- `getMousePosition()` 返回窗口客户区像素坐标
- `getMouseDelta()` / `getMouseWheelDelta()` 返回自上次 `nextFrame()` 以来累计的增量
- `nextFrame()` 只清累计量，不清键盘和鼠标按键 down 状态

### R2: SDL 事件映射

`Sdl3InputState::handleSdlEvent()` 至少覆盖：

- `SDL_EVENT_KEY_DOWN` / `SDL_EVENT_KEY_UP`
- `SDL_EVENT_MOUSE_MOTION`
- `SDL_EVENT_MOUSE_BUTTON_DOWN` / `SDL_EVENT_MOUSE_BUTTON_UP`
- `SDL_EVENT_MOUSE_WHEEL`
- `SDL_EVENT_QUIT`

映射要求：

- `event.key.scancode` 通过内部映射函数转为 `LX_core::KeyCode`
- 不在 `REQ-012` 枚举列表内的 scancode 统一映射为 `KeyCode::Unknown` 并忽略
- SDL 鼠标按钮值按以下规则转换：
  - `SDL_BUTTON_LEFT` → `MouseButton::Left`
  - `SDL_BUTTON_RIGHT` → `MouseButton::Right`
  - `SDL_BUTTON_MIDDLE` → `MouseButton::Middle`
- 鼠标移动使用 `event.motion.x/y` 更新位置，使用 `event.motion.xrel/yrel` 累加 delta
- 滚轮使用 `event.wheel.y` 累加垂直滚动量

### R3: SDL scancode → `KeyCode` 映射表

在 `sdl3_input_state.cpp` 内部提供映射函数，至少覆盖 `REQ-012` 中定义的键：

- `A-Z`
- `Num0-Num9`
- `Escape`
- `Space`
- `LShift` / `RShift`
- `LCtrl` / `RCtrl`
- `LAlt` / `RAlt`
- `Enter`
- `Tab`
- 方向键
- `F1-F4`

未覆盖键统一映射到 `KeyCode::Unknown`。

### R4: SDL `Window` 实现接入真实输入状态

修改 [src/infra/window/sdl_window.cpp](../../src/infra/window/sdl_window.cpp)：

- `Impl` 新增 `std::shared_ptr<Sdl3InputState> inputState`
- 构造时创建该对象
- `Impl::shouldClose()` 改为在同一轮 `SDL_PollEvent()` 中：
  - 调用 `inputState->handleSdlEvent(event)`
  - 收集 quit 状态
  - 返回本轮是否收到 quit
- `Window::getInputState() const override` 返回 `inputState`

约束：

- 不再让 SDL 路径返回 `DummyInputState`
- 不新增 `Window::nextFrame()` 或 `pollEvents()` 接口
- 不改变 `Window::shouldClose()` 的主职责；它仍是当前主循环的事件轮询入口

### R5: `nextFrame()` 时序

本 REQ 需要明确 `IInputState::nextFrame()` 的实际使用时机，但不通过新增 `Window` 接口来承载。

约定如下：

- 调用方在“完成一帧对输入状态的消费之后”调用 `window->getInputState()->nextFrame()`
- 典型位置是 demo 或主循环的一帧末尾
- 本 REQ 不要求统一修改所有循环框架；只要求在引入真实 SDL 输入的示例/测试路径中采用一致时序

理由：

- 这与 `REQ-012` 的边界一致，不额外扩大 `Window` 契约
- 允许后续 `EngineLoop` 或 demo 自己决定输入帧边界

### R6: GLFW 保持占位实现

本 REQ 不实现 GLFW 真实输入。

要求：

- [src/infra/window/glfw_window.cpp](../../src/infra/window/glfw_window.cpp) 继续返回 `DummyInputState`
- 可加 TODO 注释说明 Phase 2 再补 GLFW 输入路径
- 不因 SDL 主路径推进而要求 GLFW 同步完成功能对齐

## 测试

新增 `src/test/integration/test_sdl_input.cpp`，测试分两层：

1. 纯 `Sdl3InputState` 测试

- 手工构造 `SDL_Event`
- 调 `handleSdlEvent()`
- 验证：
  - `W` 键 down / up
  - 鼠标按键 down / up
  - 鼠标位置更新
  - 鼠标 delta 累加
  - 滚轮 delta 累加
  - `nextFrame()` 清零 delta 与 wheel，但不清 down 状态

2. SDL `Window` 生命周期冒烟测试

- 构造真实 SDL window
- `getInputState()` 返回值不为空
- 返回对象类型是 SDL 实现而非 dummy
- `shouldClose()` / `getInputState()` 可共同工作且不崩溃

不在本 REQ 中测试：

- 真实物理键盘输入
- CI 环境下的人工事件注入闭环
- GLFW 输入

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/infra/window/sdl3_input_state.hpp` | 新增 |
| `src/infra/window/sdl3_input_state.cpp` | 新增 |
| `src/infra/window/sdl_window.cpp` | `Impl` 持有 `Sdl3InputState`，`shouldClose()` 复用同一轮 poll 并更新输入状态 |
| `src/infra/window/window.hpp` | 确认 `getInputState() const override` 走真实 SDL 输入实现 |
| `src/infra/window/glfw_window.cpp` | 保持 dummy 实现，可补 TODO 注释 |
| `src/infra/CMakeLists.txt` | 将 `sdl3_input_state.cpp` 加入 `LX_Infra` sources |
| `src/test/integration/test_sdl_input.cpp` | 新增 |
| `src/test/CMakeLists.txt` | 注册新测试 |
| 使用真实 SDL 输入的 demo / 示例循环 | 在帧末调用 `window->getInputState()->nextFrame()` |

## 边界与约束

- 不实现 GLFW 后端的真实输入
- 不做事件队列 / observer pattern
- 不做鼠标锁定 / 相对鼠标模式
- 不做文本输入
- 不新增 `Window::nextFrame()` / `Window::pollEvents()` 接口
- 不修改 `REQ-012` 已经约定好的 `Window` 抽象边界

## 依赖

- `REQ-012`：`IInputState`、`KeyCode`、`MouseButton`、`Window::getInputState()`

## 下游

- `REQ-015` / `REQ-016`：相机控制器读取真实 SDL 输入
- `REQ-017`：ImGui 输入协调与事件 forwarding
- `REQ-019`：demo_scene_viewer 的相机交互
- Phase 2 更完整输入系统：在本实现上扩边沿检测、设备类型和更多平台支持

## 实施状态

2026-04-16 实施完成。

- `Sdl3InputState` 已实现（`sdl3_input_state.hpp/.cpp`），覆盖键盘、鼠标移动/按键/滚轮和 quit 事件
- SDL scancode → KeyCode 映射表覆盖 REQ-012 全部枚举值
- `sdl_window.cpp` 的 `shouldClose()` 已复用同一轮 poll 更新输入
- `Window::getInputState()` 已返回真实 `Sdl3InputState`（不再是 DummyInputState）
- GLFW 继续保持 DummyInputState 占位
- `test_sdl_input` 集成测试已通过（9 个测试函数）
