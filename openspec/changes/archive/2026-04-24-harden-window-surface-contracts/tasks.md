## 1. Resize Contract

- [x] 1.1 Forward SDL `Window::updateSize()` to the existing implementation logic.
- [x] 1.2 Verify GLFW and SDL report resize/minimize state through the same public method shape.

## 2. Surface Handle Contract

- [x] 2.1 Remove GLFW heap allocation around `VkSurfaceKHR` and align create/destroy semantics with SDL.
- [x] 2.2 Update Vulkan device initialization and teardown to consume the unified handle contract.

## 3. Verification

- [x] 3.1 Run focused window/Vulkan smoke tests in a Linux environment with a video device or `xvfb-run`.
- [x] 3.2 Update docs/specs to reflect the unified contract.
