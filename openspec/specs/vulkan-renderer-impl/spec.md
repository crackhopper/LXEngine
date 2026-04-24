# Vulkan Renderer Impl

## Requirements

### Requirement: VulkanRendererImpl is a private implementation detail
`VulkanRendererImpl` SHALL act as an internal implementation type rather than a second public renderer abstraction layer.

#### Scenario: outer renderer owns the public renderer contract
- **WHEN** code interacts with the Vulkan renderer through its supported public interface
- **THEN** the implementation type remains an internal detail rather than a parallel public abstraction

### Requirement: Renderer implementation state is encapsulated
Renderer implementation state such as device, swapchain, resource manager, scene linkage, and GUI integration SHALL be encapsulated behind private implementation members or explicit internal accessors.

#### Scenario: implementation members are not directly public
- **WHEN** internal renderer state is inspected in the implementation
- **THEN** it is not exposed as broad public member data on the implementation object

### Requirement: Shared renderer constants have one source of truth
Renderer implementation constants such as frames-in-flight SHALL have one authoritative definition rather than repeated literals in multiple functions.

#### Scenario: frames in flight is defined once
- **WHEN** the renderer initializes and draws frames
- **THEN** both paths rely on the same authoritative frames-in-flight value
