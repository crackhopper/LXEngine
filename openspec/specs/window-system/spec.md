## ADDED Requirements

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
The window system SHALL provide a way to detect when the user has requested the window to close (e.g., pressing the close button).

#### Scenario: Detect close request via polling
- **WHEN** `shouldClose()` is called
- **WHEN** a `SDL_QUIT` event has been received since the last call
- **THEN** `true` SHALL be returned

#### Scenario: No close request pending
- **WHEN** `shouldClose()` is called
- **WHEN** no `SDL_QUIT` event has been received
- **THEN** `false` SHALL be returned

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
