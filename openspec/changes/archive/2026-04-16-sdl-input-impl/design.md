## Context

SDL 事件循环当前在 `Impl::shouldClose()` 中执行 `SDL_PollEvent()`，只处理 `SDL_EVENT_QUIT`。`Window::getInputState()` 返回一个 static `DummyInputState`。`IInputState` 接口已由 REQ-012 定义（`isKeyDown`、`isMouseButtonDown`、`getMousePosition`、`getMouseDelta`、`getMouseWheelDelta`、`nextFrame`）。

## Goals / Non-Goals

**Goals:**
- `Sdl3InputState` 实现 `IInputState`，由 SDL 事件驱动
- `shouldClose()` 在同一轮 poll 中同时更新输入和检测 quit
- scancode → KeyCode 映射覆盖 REQ-012 全部枚举
- 集成测试通过手工构造 SDL_Event 验证行为

**Non-Goals:**
- 不实现 GLFW 真实输入
- 不做事件队列/observer
- 不做鼠标锁定/相对模式
- 不做文本输入
- 不新增 `Window::nextFrame()` 或 `pollEvents()`

## Decisions

### D1: handleSdlEvent() 作为事件消费入口

**选择**：`Sdl3InputState` 暴露 `handleSdlEvent(const SDL_Event&)`，由 `Impl::shouldClose()` 在 poll 循环中调用。返回 bool 表示是否收到 quit。

**替代方案**：让 `Sdl3InputState` 自己调 `SDL_PollEvent()` —— 会导致事件消费与 quit 检测分离。

**理由**：复用现有 poll 循环，事件不会被消费两次。

### D2: scancode 映射用 switch-case 静态函数

**选择**：`sdl3_input_state.cpp` 内部 `static KeyCode mapSdlScancode(SDL_Scancode)` 函数，switch-case 覆盖所有 REQ-012 枚举值，default 返回 `Unknown`。

**理由**：简单直接，编译器可优化为跳表。无需运行时初始化。

### D3: delta 和 wheel 在 nextFrame() 清零

**选择**：`nextFrame()` 只清 `m_mouseDeltaAccum` 和 `m_wheelDeltaAccum`，不清键盘/鼠标按键 down 状态。

**理由**：down 状态是持续的（直到 key_up），delta 是帧内累计量。

### D4: Impl 持有 shared_ptr<Sdl3InputState>

**选择**：`Impl` 构造时创建 `make_shared<Sdl3InputState>()`。`getInputState()` 返回该共享对象。

**理由**：与 REQ-012 的 `InputStateSharedPtr = shared_ptr<IInputState>` 一致。同一 Window 多次调用返回同一对象。

## Risks / Trade-offs

- **[SDL 事��处理在 shouldClose() 中]** → 这意味着不调 `shouldClose()` 就不会更新输入。当前主循环每帧都调，所以可接受。后续 EngineLoop 重构可调整。
- **[测试无法注入真实 SDL 事���]** → 通过手工构造 `SDL_Event` 结构体直接调 `handleSdlEvent()` 绕过 SDL 事件队列。CI 环境无 display 也可运行。
