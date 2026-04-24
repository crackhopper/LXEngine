## Purpose

Define the current window-system contract for creating, managing, and exposing platform window handles to the renderer.

## Requirements

### Requirement: Window creation and lifecycle management
The window system SHALL provide a `Window` class with a constructor taking `(width, height, title)`, a destructor, and a PImpl pointer to hide platform-specific implementation details.

#### Scenario: Create SDL window
- **WHEN** `Window` is constructed with SDL backend enabled
- **THEN** an `Impl` struct SHALL be allocated containing an `SDL_Window*` and the window SHALL be centered on screen with Vulkan and resizable flags

#### Scenario: Window destruction cleans up resources
- **WHEN** `Window` is destroyed
- **THEN** the underlying SDL window SHALL be destroyed via `SDL_DestroyWindow` and `SDL_Quit` SHALL be called

### Requirement: Window dimension queries
The window system SHALL allow querying the current width and height of the window.

#### Scenario: Query window dimensions
- **WHEN** `getWidth()` or `getHeight()` is called on a `Window` instance
- **THEN** the current pixel dimensions of the window SHALL be returned

### Requirement: Vulkan surface creation
The window system SHALL expose the ability to create a Vulkan `VkSurfaceKHR` from the window for rendering.

#### Scenario: Create Vulkan surface
- **WHEN** `getVulkanSurface(VkInstance)` is called
- **THEN** a valid `VkSurfaceKHR` SHALL be created via `SDL_Vulkan_CreateSurface` and returned

#### Scenario: Vulkan surface creation failure
- **WHEN** `SDL_Vulkan_CreateSurface` fails
- **THEN** a `std::runtime_error` SHALL be thrown with an appropriate message

### Requirement: Window close detection

`shouldClose()` SHALL poll all SDL events in a single loop. For every event, it SHALL, in order:

1. Call `ImGui_ImplSDL3_ProcessEvent(&event)` so that ImGui can observe focus, input, and resize events. The SDL backend implementation SHALL guarantee this call is a no-op when ImGui has not been initialized yet (either by guarding with its own initialization flag or by relying on `ImGui_ImplSDL3_ProcessEvent`'s documented early-return behavior).
2. Forward the event to `Sdl3InputState::handleSdlEvent()` so the input state reflects the latest key/mouse data.
3. Check for `SDL_EVENT_QUIT` and remember whether quit was seen.

Events SHALL NOT be consumed twice and SHALL NOT be re-polled in a second loop. After the loop, `shouldClose()` SHALL return `true` if any `SDL_EVENT_QUIT` was seen in this invocation.

#### Scenario: shouldClose forwards events to ImGui, input state, and detects quit

- **WHEN** `shouldClose()` is called and SDL events include a key press and a quit event
- **THEN** `ImGui_ImplSDL3_ProcessEvent` SHALL have been invoked for each event, the key press state SHALL be updated in `Sdl3InputState`, and `shouldClose()` SHALL return `true`

#### Scenario: shouldClose updates input without quit

- **WHEN** `shouldClose()` is called and SDL events include only key presses and mouse motion
- **THEN** all events SHALL be forwarded to both ImGui and `Sdl3InputState`, and `shouldClose()` SHALL return `false`

### Requirement: Window exposes native handle

The `Window` interface (`src/core/platform/window.hpp`) SHALL declare a pure virtual method:

```cpp
virtual void* getNativeHandle() const = 0;
```

The SDL implementation (`src/infra/window/sdl_window.cpp`) SHALL return the underlying `SDL_Window*`. The GLFW implementation (`src/infra/window/glfw_window.cpp`) SHALL return the underlying `GLFWwindow*`. The `core` layer SHALL NOT include any SDL or GLFW headers; the conversion from `void*` back to the concrete type SHALL happen only in backend/infra code that already depends on the corresponding library.

#### Scenario: SDL window returns SDL_Window pointer

- **WHEN** `getNativeHandle()` is called on an SDL-backed `Window`
- **THEN** the returned pointer SHALL be the same address returned by the internal `SDL_CreateWindow` call

#### Scenario: GLFW window returns GLFWwindow pointer

- **WHEN** `getNativeHandle()` is called on a GLFW-backed `Window`
- **THEN** the returned pointer SHALL be the same address returned by the internal `glfwCreateWindow` call

#### Scenario: core layer does not include SDL or GLFW headers

- **WHEN** compiling a translation unit that includes only `core/platform/window.hpp`
- **THEN** compilation SHALL succeed without SDL or GLFW headers being available on the include path

### Requirement: Close callback registration
The window system SHALL support registering a callback function that is invoked when the window is closed.

#### Scenario: Register close callback
- **WHEN** `onClose(std::function<void()> cb)` is called
- **THEN** the provided callback SHALL be stored and invoked when `shouldClose()` returns `true`

### Requirement: Static initialization
The window system SHALL provide a static `Initialize()` method to set up the windowing subsystem before creating windows.

#### Scenario: Initialize window system
- **WHEN** `WindowImpl::Initialize()` is called with SDL backend
- **THEN** `SDL_Init(SDL_INIT_VIDEO)` SHALL be called and succeed without throwing

### Requirement: GLFW backend support
The window system SHALL compile and work identically when `USE_GLFW` is enabled instead of SDL.

#### Scenario: GLFW window creation
- **WHEN** `USE_GLFW` is defined and `Window` is constructed
- **THEN** a GLFW window SHALL be created with Vulkan support and resize handling

### Requirement: Window provides input state access
The `Window` interface (`src/core/platform/window.hpp`) SHALL declare a pure virtual method:

```cpp
virtual InputStateSharedPtr getInputState() const = 0;
```

The same `Window` instance SHALL return the same shared input state object on every call. `LX_infra::Window` SHALL override this method. The SDL implementation SHALL return a `Sdl3InputState`; the GLFW implementation SHALL return a `DummyInputState`.

#### Scenario: getInputState returns non-null
- **WHEN** calling `getInputState()` on any `Window` implementation
- **THEN** the returned `InputStateSharedPtr` SHALL NOT be null

#### Scenario: getInputState returns same object
- **WHEN** calling `getInputState()` twice on the same `Window` instance
- **THEN** both calls SHALL return the same pointer

#### Scenario: SDL window returns real input state
- **WHEN** calling `getInputState()` on the SDL window implementation
- **THEN** the returned object SHALL be a `Sdl3InputState` that responds to SDL events

#### Scenario: GLFW window returns DummyInputState
- **WHEN** calling `getInputState()` on the GLFW window implementation
- **THEN** the returned object SHALL be a `DummyInputState` with all-zero semantics
