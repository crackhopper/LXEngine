## Context

The engine has two active window backends. SDL already contains correct resize logic in its implementation layer but does not expose it through the public method. GLFW exposes a different Vulkan surface ownership shape entirely, which forces device initialization to rely on backend quirks.

## Goals / Non-Goals

**Goals:**
- Make resize/update behavior uniform across backends.
- Make graphics-handle ownership and destruction uniform across backends.
- Keep renderer/device code backend-agnostic.

**Non-Goals:**
- No new window backend.
- No redesign of event polling beyond the current resize/minimize contract.

## Decisions

- Keep `updateSize()` as the public contract and forward SDL to its existing implementation.
- Standardize `createGraphicsHandle()` on returning a handle value with a matching destroy path, because that matches SDL and Vulkan device expectations.
- Update Vulkan device consumption code to assume one contract only, not branch on backend quirks.

## Risks / Trade-offs

- [Backends may have implicit ownership differences] → Make ownership explicit in specs and pair creation with destruction in both implementations.
- [Tests may need real video devices] → Reuse current Linux/Xvfb guidance and keep smoke tests focused on initialization.
