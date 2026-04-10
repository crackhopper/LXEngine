## ADDED Requirements

### Requirement: GUI initialization with Vulkan
The GUI system SHALL initialize Dear ImGui with Vulkan rendering backend using SDL3 for input.

#### Scenario: Initialize ImGui with Vulkan
- **WHEN** `Gui::init()` is called with `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `QueueFamilyIndices`, `VkQueue`, and `VkSurfaceKHR`
- **THEN** ImGui context SHALL be created and `ImGui_ImplVulkan_Init` and `ImGui_ImplSDL3_Init` SHALL be called with the provided Vulkan objects

#### Scenario: Initialize ImGui twice
- **WHEN** `init()` is called when already initialized
- **THEN** a `std::runtime_error` SHALL be thrown indicating already initialized

### Requirement: GUI frame management
The GUI system SHALL provide `beginFrame()` and `endFrame()` methods to delimit ImGui frame rendering.

#### Scenario: Start new ImGui frame
- **WHEN** `beginFrame()` is called after initialization
- **THEN** `ImGui_ImplSDL3_NewFrame()` and `ImGui_ImplVulkan_NewFrame()` SHALL be called followed by `ImGui::NewFrame()`

#### Scenario: End and render ImGui frame
- **WHEN** `endFrame()` is called
- **THEN** `ImGui::Render()` SHALL be called and the draw data SHALL be rendered via `ImGui_ImplVulkan_RenderDrawData()`

### Requirement: GUI shutdown
The GUI system SHALL properly shut down ImGui and release resources.

#### Scenario: Shutdown ImGui
- **WHEN** `shutdown()` is called
- **THEN** `ImGui_ImplVulkan_Shutdown()` and `ImGui_ImplSDL3_Shutdown()` SHALL be called and ImGui context SHALL be destroyed

### Requirement: PImpl hiding of ImGui internals
The GUI system SHALL use the PImpl idiom to hide ImGui context and backend-specific details from external consumers.

#### Scenario: Gui implementation details hidden
- **WHEN** a consumer includes `gui.hpp`
- **THEN** no ImGui types SHALL be visible in the public API
