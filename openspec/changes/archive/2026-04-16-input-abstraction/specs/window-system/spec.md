## MODIFIED Requirements

### Requirement: Window provides input state access
The `Window` interface (`src/core/platform/window.hpp`) SHALL declare a pure virtual method:

```cpp
virtual InputStateSharedPtr getInputState() const = 0;
```

The same `Window` instance SHALL return the same shared input state object on every call. `LX_infra::Window` SHALL override this method.

#### Scenario: getInputState returns non-null
- **WHEN** calling `getInputState()` on any `Window` implementation
- **THEN** the returned `InputStateSharedPtr` SHALL NOT be null

#### Scenario: getInputState returns same object
- **WHEN** calling `getInputState()` twice on the same `Window` instance
- **THEN** both calls SHALL return the same pointer

#### Scenario: SDL window returns DummyInputState
- **WHEN** calling `getInputState()` on the SDL window implementation (before REQ-013)
- **THEN** the returned object SHALL be a `DummyInputState` with all-zero semantics

#### Scenario: GLFW window returns DummyInputState
- **WHEN** calling `getInputState()` on the GLFW window implementation (before REQ-013)
- **THEN** the returned object SHALL be a `DummyInputState` with all-zero semantics
