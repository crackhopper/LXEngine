## 1. Visibility Data Model

- [x] 1.1 Add layer-mask state to renderables or their validated scene-facing representation.
- [x] 1.2 Add culling-mask state to cameras with clear defaults.

## 2. Queue Filtering

- [x] 2.1 Filter render-queue construction by camera/renderable mask intersection.
- [x] 2.2 Keep scene-level camera resource collection independent from visibility filtering.

## 3. Verification

- [x] 3.1 Add focused tests covering visible and filtered-out renderables.
- [x] 3.2 Update scene/queue docs to describe the new mask contract.
