## ADDED Requirements

### Requirement: Vulkan device initialization consumes a uniform window surface contract
The Vulkan backend SHALL consume window-provided graphics handles through one backend-independent contract. Device creation and teardown MUST NOT depend on SDL-specific versus GLFW-specific handle interpretation.

#### Scenario: backend-specific surface wrappers are unnecessary
- **WHEN** Vulkan device creation receives a window graphics handle
- **THEN** it treats the handle as one consistent contract regardless of which window backend produced it
