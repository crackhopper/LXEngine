## ADDED Requirements

### Requirement: KeyCode enumeration
`src/core/input/key_code.hpp` SHALL define an `enum class KeyCode : uint16_t` in namespace `LX_core` with the following values: `Unknown`, letters `A`-`Z`, digits `Num0`-`Num9`, control keys (`Escape`, `Space`, `LShift`, `RShift`, `LCtrl`, `RCtrl`, `LAlt`, `RAlt`, `Enter`, `Tab`), direction keys (`Left`, `Right`, `Up`, `Down`), function keys (`F1`-`F4`), and a `Count` sentinel.

#### Scenario: KeyCode values are distinct
- **WHEN** comparing any two different KeyCode enumerators
- **THEN** they SHALL have different underlying values

#### Scenario: KeyCode Count is the last value
- **WHEN** evaluating `static_cast<uint16_t>(KeyCode::Count)`
- **THEN** it SHALL equal the total number of defined key codes (excluding Count itself)

### Requirement: MouseButton enumeration
`src/core/input/mouse_button.hpp` SHALL define an `enum class MouseButton : uint8_t` in namespace `LX_core` with values: `Left = 0`, `Right = 1`, `Middle = 2`, `Count = 3`.

#### Scenario: MouseButton Count equals 3
- **WHEN** evaluating `static_cast<uint8_t>(MouseButton::Count)`
- **THEN** the result SHALL be `3`

### Requirement: IInputState interface
`src/core/input/input_state.hpp` SHALL define an abstract class `IInputState` in namespace `LX_core` with the following pure virtual methods:

- `bool isKeyDown(KeyCode code) const`
- `bool isMouseButtonDown(MouseButton button) const`
- `Vec2f getMousePosition() const`
- `Vec2f getMouseDelta() const`
- `float getMouseWheelDelta() const`
- `void nextFrame()`

The class SHALL have a virtual destructor. A type alias `InputStateSharedPtr = std::shared_ptr<IInputState>` SHALL be provided.

#### Scenario: IInputState is abstract
- **WHEN** attempting to instantiate `IInputState` directly
- **THEN** compilation SHALL fail because all methods are pure virtual

### Requirement: DummyInputState implementation
`src/core/input/dummy_input_state.hpp` SHALL define a class `DummyInputState` in namespace `LX_core` that implements `IInputState` with all-zero return values.

#### Scenario: DummyInputState key query returns false
- **WHEN** calling `isKeyDown(KeyCode::W)` on a `DummyInputState`
- **THEN** the result SHALL be `false`

#### Scenario: DummyInputState mouse button returns false
- **WHEN** calling `isMouseButtonDown(MouseButton::Left)` on a `DummyInputState`
- **THEN** the result SHALL be `false`

#### Scenario: DummyInputState mouse position returns zero
- **WHEN** calling `getMousePosition()` on a `DummyInputState`
- **THEN** the result SHALL be `{0, 0}`

#### Scenario: DummyInputState mouse delta returns zero
- **WHEN** calling `getMouseDelta()` on a `DummyInputState`
- **THEN** the result SHALL be `{0, 0}`

#### Scenario: DummyInputState wheel delta returns zero
- **WHEN** calling `getMouseWheelDelta()` on a `DummyInputState`
- **THEN** the result SHALL be `0.0f`

#### Scenario: DummyInputState nextFrame is callable
- **WHEN** calling `nextFrame()` on a `DummyInputState`
- **THEN** the call SHALL succeed and all getters SHALL still return zero values

### Requirement: Integration test for DummyInputState
`src/test/integration/test_input_state.cpp` SHALL verify all `DummyInputState` zero-value semantics and `nextFrame()` idempotency.

#### Scenario: All DummyInputState tests pass
- **WHEN** running `test_input_state`
- **THEN** all assertions SHALL pass

### Requirement: nextFrame timing convention
Callers SHALL invoke `window->getInputState()->nextFrame()` after consuming input state for the current frame (typically at frame end). This clears per-frame accumulators (mouse delta, wheel delta) while preserving held-key states.

#### Scenario: nextFrame called at frame end
- **WHEN** a frame loop calls `nextFrame()` after reading mouse delta
- **THEN** the next frame's `getMouseDelta()` SHALL start from `{0, 0}`

### Requirement: IInputState reports UI capture flags

`IInputState`（`src/core/input/input_state.hpp`）SHALL 追加两个默认虚方法：

```cpp
virtual bool isUiCapturingMouse() const { return false; }
virtual bool isUiCapturingKeyboard() const { return false; }
```

默认实现 SHALL 返回 `false`，以保证既有调用点与既有实现无需修改即可编译。后续若上层 UI（如 ImGui）声明希望独占鼠标/键盘，具体实现（例如 `Sdl3InputState`）MAY 覆写这两个方法返回 `ImGui::GetIO().WantCaptureMouse` 与 `WantCaptureKeyboard`；本 REQ 不强制 `Sdl3InputState` 在当前版本就接通 ImGui，只规定接口契约。

相机控制器（REQ-015 / REQ-016）与 demo viewer（REQ-019）SHALL 通过这两个方法查询 UI capture 状态；本 REQ 不要求在此处定义具体的消费策略。

#### Scenario: 默认实现返回 false

- **WHEN** 调用任何直接继承 `IInputState` 但未覆写 capture 方法的实现（例如当前 `DummyInputState`）
- **THEN** `isUiCapturingMouse()` 与 `isUiCapturingKeyboard()` SHALL 返回 `false`

#### Scenario: 既有 Dummy/Mock 实现继续通过编译

- **WHEN** 重新编译依赖 `core/input/input_state.hpp` 的所有既有代码（包括 `DummyInputState`、`Sdl3InputState`、`MockInputState`、各相机控制器测试）
- **THEN** 编译 SHALL 成功，且无任何实现被强制覆写这两个方法
