## Why

Window backends currently diverge on two core contracts: SDL does not forward `updateSize()`, and GLFW returns a heap pointer where SDL returns a Vulkan surface handle value. This makes resize behavior inconsistent and leaks backend-specific handle semantics into device creation.

This change re-establishes one window contract for both backends and one graphics-handle contract for renderer initialization.

## What Changes

- Make SDL window size updates flow through the same public contract already used by GLFW.
- Unify graphics handle creation/destruction semantics across SDL and GLFW.
- Remove heap-allocated Vulkan surface indirection from the GLFW path.
- Document the cross-backend contract in window and renderer specs.

## Capabilities

### New Capabilities

### Modified Capabilities
- `window-system`: Window backends expose uniform resize and graphics-handle behavior.
- `renderer-backend-vulkan`: Vulkan initialization consumes one consistent surface-handle contract from window backends.

## Impact

- Affected code: `src/infra/window/`, `src/backend/vulkan/details/device.cpp`
- Affected APIs: `IWindow::updateSize`, `createGraphicsHandle`, `destroyGraphicsHandle`
- Affected systems: resize handling, Vulkan surface lifetime, backend interchangeability
