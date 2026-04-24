## ADDED Requirements

### Requirement: Physical-device suitability is capability-based
The Vulkan backend SHALL determine physical-device suitability from required queue families, required device extensions, and swapchain viability. GPU type alone MUST NOT make an otherwise suitable device ineligible.

#### Scenario: integrated GPU can be selected
- **WHEN** a non-discrete adapter satisfies all required Vulkan runtime checks
- **THEN** the backend treats it as suitable rather than rejecting it only because of device type

### Requirement: Physical-device preference remains separate from suitability
If multiple suitable Vulkan devices exist, the backend SHALL apply preference ranking after suitability checks and MAY prefer discrete GPUs ahead of equally suitable integrated adapters.

#### Scenario: discrete GPU remains preferred but not mandatory
- **WHEN** both a discrete GPU and an integrated GPU satisfy suitability requirements
- **THEN** the backend may rank the discrete GPU first without making discrete type a hard requirement
