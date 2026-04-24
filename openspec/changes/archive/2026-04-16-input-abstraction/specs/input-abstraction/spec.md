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
