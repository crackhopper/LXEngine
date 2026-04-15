# REQ-013: SDL3 Window 的真实输入事件实现

## 背景

REQ-012 定义了 `IInputState` 接口与 `Window::getInputState()` 入口，但 SDL3 / GLFW 的 `Window` 实现在 REQ-012 中只是返回了 `DummyInputState` 占位。

`src/infra/window/sdl_window.cpp:39-46` 当前的 `Impl::shouldClose` 是这样：

```cpp
bool shouldClose() const {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT)
      return true;
  }
  return false;
}
```

**所有非 QUIT 的事件被静默丢弃**，包括：

- `SDL_EVENT_KEY_DOWN` / `SDL_EVENT_KEY_UP`
- `SDL_EVENT_MOUSE_MOTION`
- `SDL_EVENT_MOUSE_BUTTON_DOWN` / `SDL_EVENT_MOUSE_BUTTON_UP`
- `SDL_EVENT_MOUSE_WHEEL`
- `SDL_EVENT_WINDOW_RESIZED` 等

本需求把 SDL3 事件循环改造成"统一收集到一个 `Sdl3InputState`，shouldClose 复用同一次 poll"，让 REQ-012 的接口背后跑的是真正的输入数据。

GLFW 实现的状态：当前 `window_impl_glfw.cpp` 也存在但项目主用 SDL3。本 REQ 只**完成 SDL3 实现**，GLFW 实现保持 dummy 但留好 hook 点（注释说明等 Phase 2 REQ-203 时同步），不阻塞本 REQ。

## 目标

1. SDL3 后端真正解析 `SDL_Event` → 写入一个内部 `Sdl3InputState`
2. `Window::getInputState()` 在 SDL 实现下返回真实数据
3. `shouldClose` / `getInputState` 共用同一次 poll，避免事件被吃两次
4. SDL scancode → `KeyCode` 的映射表覆盖 REQ-012 R1 列出的所有键
5. `nextFrame()` 在每一帧的合适时机被调用，正确清理 mouse delta / wheel delta

## 需求

### R1: `Sdl3InputState` 实现类

新建 `src/infra/window/sdl3_input_state.hpp` + `.cpp`：

```cpp
namespace LX_infra {

class Sdl3InputState : public LX_core::IInputState {
public:
  Sdl3InputState();

  // ---- IInputState ----
  bool   isKeyDown(LX_core::KeyCode code) const override;
  bool   isMouseButtonDown(LX_core::MouseButton button) const override;
  LX_core::Vec2f getMousePosition() const override;
  LX_core::Vec2f getMouseDelta() const override;
  float  getMouseWheelDelta() const override;
  void   nextFrame() override;

  // ---- 由 SDL 事件循环调用 ----
  /// 处理一个 SDL 事件，根据类型更新内部状态。
  /// 返回 true 表示这是一个 SDL_EVENT_QUIT，调用方据此决定是否关闭窗口。
  bool handleSdlEvent(const SDL_Event &event);

private:
  std::array<bool, static_cast<size_t>(LX_core::KeyCode::Count)>     m_keyDown{};
  std::array<bool, static_cast<size_t>(LX_core::MouseButton::Count)> m_mouseButtonDown{};
  LX_core::Vec2f m_mousePos{0, 0};
  LX_core::Vec2f m_mouseDeltaAccum{0, 0};   // 累计自上次 nextFrame
  float          m_wheelDeltaAccum = 0.0f;
};

}
```

事件分发要点：

- `SDL_EVENT_KEY_DOWN` / `SDL_EVENT_KEY_UP`：用 `event.key.scancode` 经 R2 的映射表 → `KeyCode`，写 `m_keyDown[idx]`
- `SDL_EVENT_MOUSE_MOTION`：写 `m_mousePos = (event.motion.x, event.motion.y)`，累加 `m_mouseDeltaAccum += (event.motion.xrel, event.motion.yrel)`
- `SDL_EVENT_MOUSE_BUTTON_DOWN` / `_UP`：button 1 → Left, 2 → Middle, 3 → Right；写 `m_mouseButtonDown`
- `SDL_EVENT_MOUSE_WHEEL`：累加 `m_wheelDeltaAccum += event.wheel.y`

`nextFrame()` 实现：

```cpp
void Sdl3InputState::nextFrame() {
  m_mouseDeltaAccum = {0, 0};
  m_wheelDeltaAccum = 0.0f;
  // 不清 m_keyDown / m_mouseButtonDown —— 这些是持续状态
}
```

### R2: SDL scancode → KeyCode 映射表

在 `sdl3_input_state.cpp` 内部：

```cpp
static LX_core::KeyCode toKeyCode(SDL_Scancode sc) {
  switch (sc) {
    case SDL_SCANCODE_A: return LX_core::KeyCode::A;
    // ... 26 个字母 ...
    case SDL_SCANCODE_0: return LX_core::KeyCode::Num0;
    // ... 10 个数字 ...
    case SDL_SCANCODE_ESCAPE: return LX_core::KeyCode::Escape;
    case SDL_SCANCODE_SPACE:  return LX_core::KeyCode::Space;
    case SDL_SCANCODE_LSHIFT: return LX_core::KeyCode::LShift;
    case SDL_SCANCODE_RSHIFT: return LX_core::KeyCode::RShift;
    case SDL_SCANCODE_LCTRL:  return LX_core::KeyCode::LCtrl;
    case SDL_SCANCODE_RCTRL:  return LX_core::KeyCode::RCtrl;
    case SDL_SCANCODE_LALT:   return LX_core::KeyCode::LAlt;
    case SDL_SCANCODE_RALT:   return LX_core::KeyCode::RAlt;
    case SDL_SCANCODE_RETURN: return LX_core::KeyCode::Enter;
    case SDL_SCANCODE_TAB:    return LX_core::KeyCode::Tab;
    case SDL_SCANCODE_LEFT:   return LX_core::KeyCode::Left;
    case SDL_SCANCODE_RIGHT:  return LX_core::KeyCode::Right;
    case SDL_SCANCODE_UP:     return LX_core::KeyCode::Up;
    case SDL_SCANCODE_DOWN:   return LX_core::KeyCode::Down;
    case SDL_SCANCODE_F1:     return LX_core::KeyCode::F1;
    case SDL_SCANCODE_F2:     return LX_core::KeyCode::F2;
    case SDL_SCANCODE_F3:     return LX_core::KeyCode::F3;
    case SDL_SCANCODE_F4:     return LX_core::KeyCode::F4;
    default:                  return LX_core::KeyCode::Unknown;
  }
}
```

不在 REQ-012 R1 列表里的 scancode 一律返回 `KeyCode::Unknown`，写 input state 时 short-circuit return 不写任何位。

### R3: SDL `Window::Impl` 改造

修改 `src/infra/window/sdl_window.cpp`：

- `Impl` 新增成员 `std::shared_ptr<Sdl3InputState> inputState`，在构造时 `make_shared`
- 把 `shouldClose` 从 `const` 移除 `const`（要写 inputState），改为：

```cpp
bool shouldClose() {
  bool quit = false;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (inputState->handleSdlEvent(event)) {
      quit = true;
    }
  }
  return quit;
}
```

- `Impl::nextFrame()` 新方法：调用 `inputState->nextFrame()`
- 暴露 `Impl::getInputState()` 返回 `inputState`

修改 `Window` 公开类：

- `Window::getInputState() const override` → `return pImpl->getInputState();`
- **新增** `Window::nextFrame()` public 方法（也加到 `core/platform/window.hpp` 接口里），由 game loop 在每帧末调用

**`nextFrame` 的归宿讨论**：可以放在 `pollEvents` 末尾或 game loop 末尾。本 REQ 选择**新增独立 public 方法**，由调用方主动调，原因：让 REQ-019 的 demo loop 显式 own 这个时机，避免 future 的 fixed-step accumulator 把 input frame edge 弄乱。

### R4: `core/platform/window.hpp` 接口同步

新增一个虚方法：

```cpp
class Window {
public:
  // ...

  /// 推进一帧的输入状态（清 mouse delta / wheel delta）。
  /// 由 game loop 每帧末尾调用。
  virtual void nextFrame() = 0;
};
```

REQ-012 把 `getInputState` 加成 stub 后，本 REQ 把 `nextFrame` 也补上。GLFW 实现走 stub no-op + TODO。

### R5: 集成测试

新建 `src/test/integration/test_sdl_input.cpp`：

由于 SDL 输入测试需要真实窗口 + 模拟事件，分两个层次：

**层 1（无窗口，纯类）**：直接构造 `Sdl3InputState`，喂 `SDL_Event` 结构体，断言 getter 返回正确：

```cpp
TEST(Sdl3InputState, key_down_after_event) {
  Sdl3InputState s;
  SDL_Event ev{};
  ev.type = SDL_EVENT_KEY_DOWN;
  ev.key.scancode = SDL_SCANCODE_W;
  s.handleSdlEvent(ev);
  EXPECT_TRUE(s.isKeyDown(LX_core::KeyCode::W));
}

TEST(Sdl3InputState, mouse_delta_resets_on_next_frame) {
  Sdl3InputState s;
  SDL_Event ev{};
  ev.type = SDL_EVENT_MOUSE_MOTION;
  ev.motion.xrel = 10;
  ev.motion.yrel = 5;
  s.handleSdlEvent(ev);
  EXPECT_FLOAT_EQ(s.getMouseDelta().x, 10.0f);
  s.nextFrame();
  EXPECT_FLOAT_EQ(s.getMouseDelta().x, 0.0f);
}
```

**层 2（真实窗口，可选）**：构造 `Window`，断言 `window->getInputState()` 不是 `DummyInputState`、断言 `nextFrame()` 可以调用而不崩。**不模拟真实键盘事件**（CI 环境跑不了），只验证生命周期与类型。

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/infra/window/sdl3_input_state.hpp` | 新增 |
| `src/infra/window/sdl3_input_state.cpp` | 新增 |
| `src/infra/window/sdl_window.cpp:11-46` | `Impl` 持有 `Sdl3InputState`，`shouldClose` 复用 poll；新增 `nextFrame` |
| `src/infra/window/window.hpp` | 声明 `getInputState() override` 走真实实现，新增 `nextFrame() override` |
| `src/infra/window/glfw_window.cpp` | stub `nextFrame` no-op + `getInputState` 返回 dummy + TODO 注释 |
| `src/core/platform/window.hpp` | 新增 `virtual void nextFrame() = 0` |
| `src/test/integration/test_sdl_input.cpp` | 新增 |
| `src/test/integration/CMakeLists.txt` | 注册新测试 |
| `src/test/test_render_triangle.cpp` | 在主循环末尾追加 `window->nextFrame()`（保持兼容） |

## 测试

见 R5。

## 边界与约束

- **不实现 GLFW 后端的真实输入** —— 留 stub
- **不做事件队列 dispatch / observer pattern** —— 只有 polling state
- **不做鼠标锁定 / 隐藏 / SetRelativeMouseMode** —— REQ-016 FreeFly 自己拉，不在本 REQ
- **不做 SDL_StartTextInput / 文本输入支持** —— Phase 2 REQ-205 之后
- 修改 `shouldClose` 去掉 const 是有意为之的破坏性改动，所有 caller（`test_render_triangle.cpp:106`、各集成测试）需要确认 build 通过

## 依赖

- **REQ-012**（必需）：接口与 `Sdl3InputState` 继承的 `IInputState`、KeyCode/MouseButton 枚举

## 下游

- **REQ-015 / REQ-016**：相机控制器读 `getInputState()`
- **REQ-017**：ImGui 输入 forwarding
- **REQ-019**：demo_scene_viewer 的相机交互
- **Phase 2 REQ-203**：在本 REQ 实现上加边沿检测、modifier 查询

## 实施状态

未开始。
