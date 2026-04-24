## Context

当前 `Window` 接口（`src/core/platform/window.hpp`��只提供窗口尺寸、graphics handle、`onClose()` 和 `shouldClose()`。SDL 事件循环（`sdl_window.cpp`）只处理 `SDL_EVENT_QUIT`。`src/core/input/` 目录不存在。`core/math/vec.hpp` 已���供 `Vec2f`。

`LX_infra::Window` 使用 PImpl 隐藏 SDL/GLFW 细节，在 `src/infra/window/window.hpp` 声明，`sdl_window.cpp` 和 `glfw_window.cpp` 分别实现。

## Goals / Non-Goals

**Goals:**
- 在 `core/` 层定义最小输入抽象：枚举 + 只读快照接口
- `Window` 获得 `getInputState()` 入口，为后续 SDL3 实现和相机控制器提供统一依��
- 提供 `DummyInputState` 全零实现，使当前代码和 headless 测试编译通过

**Non-Goals:**
- 不实现 SDL/GLFW 真实事件读取（属 REQ-013）
- 不做边沿检测（`isKeyPressed`/`isKeyReleased`）
- 不做 action mapping、文本输入、手柄支持
- 不新增 `Window::nextFrame()` 或 `pollEvents()`
- 不修改 `shouldClose()`/`onClose()` 契约

## Decisions

### D1: 接口放在 `core/input/`，每个关注点一个头文件

**选择**：`key_code.hpp`、`mouse_button.hpp`、`input_state.hpp`、`dummy_input_state.hpp` 各自独立。

**替代方案**：合并为单个 `input.hpp` —— 违背项目"多个小文件"原则。

**理由**：高内聚低耦合，只需要枚举的消费者不必引入接口定义。

### D2: `IInputState` 为只读快照接口，不含事件流

**选择**：纯 getter 接口 + `nextFrame()` 帧推进方法。不含 observer、callback、事件队列。

**理由**：最小前置，与 Phase 2 完整输入系统兼容。`nextFrame()` 只定义接口签名，调用时序由 REQ-013 决定。

### D3: `DummyInputState` 为 header-only 内联实现

**选择**：全部方法在头文件内联实现，返回零值。

**理由**：实现极其简单，无需 .cpp 文件。headless 测试和占位使用场景不需要链接额外目标。

### D4: `Window::getInputState()` 返回 `shared_ptr<IInputState>`

**选择**：同一 Window 实例多次调用返回同一共享对象。

**替代方案**：返回引用 —— 生命周期管理更复杂。返回 `unique_ptr` —— 不适合共享语义���

**理由**：与项目现有 `WindowSharedPtr = shared_ptr<Window>` 风格一致。

### D5: KeyCode 范围限定在 Phase 1 最小集

**选择**：字母 A-Z、数字 0-9、常用控制键、方向键、F1-F4。

**理由**：覆盖相机控制器需求（WASD、Shift、Space、Escape、方向键）。F5-F12、小键盘、国际键留给后续扩展。

## Risks / Trade-offs

- **[`nextFrame()` 时序未定]** → 只定义接口，不规定调用时机。REQ-013 负责集成到主循环。
- **[枚举扩展需修改 `KeyCode`]** → Phase 2 可能需要扩展，但 `enum class` 的 underlying type 为 `uint16_t`，空间充足。
- **[`DummyInputState` 永远返回零值]** → 这正是设计意图：占位实现，不会误导消费者以为有真实输入。
