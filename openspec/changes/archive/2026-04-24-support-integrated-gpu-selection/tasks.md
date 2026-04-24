## 1. Device Selection Logic

- [x] 1.1 Split physical-device selection into suitability checks and preference ranking.
- [x] 1.2 Remove the hard requirement for `VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`.

## 2. Diagnostics

- [x] 2.1 Update logging or comments so fallback-to-integrated behavior is explicit.
- [x] 2.2 Align Linux/headless test notes with the new adapter-selection behavior.

## 3. Verification

- [x] 3.1 Build and run focused Vulkan initialization tests on the current environment.
- [x] 3.2 Confirm the engine still prefers a discrete GPU when both discrete and integrated adapters are suitable.
