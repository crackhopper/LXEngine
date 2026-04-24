## Why

The Vulkan backend currently rejects integrated GPUs during suitability checks even though project notes and comments describe discrete preference, not discrete exclusivity. That blocks laptops, VMs, and some CI environments from even starting renderer initialization.

This change separates "device is usable" from "device is preferred" so the engine can start on any Vulkan-capable adapter that meets real runtime requirements.

## What Changes

- Redefine device suitability around queue, extension, and swapchain support instead of GPU type.
- Keep discrete-GPU preference as a ranking rule, not a hard gate.
- Document the selection behavior so Linux/CI/headless expectations are explicit.

## Capabilities

### New Capabilities

### Modified Capabilities
- `renderer-backend-vulkan`: Physical-device selection accepts any Vulkan-capable adapter that satisfies runtime requirements, while still preferring discrete GPUs when available.

## Impact

- Affected code: `src/backend/vulkan/details/device.cpp`
- Affected APIs: none expected
- Affected systems: startup portability, CI readiness, adapter selection
