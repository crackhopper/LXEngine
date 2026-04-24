## Why

Scene rendering currently has no camera-layer visibility mask even though REQ-034 still tracks it as one of the remaining scene/render-queue gaps. Without that filter, cameras cannot express selective visibility without mutating the scene itself.

This change adds explicit layer-mask filtering between renderables and cameras while keeping scene-level resource collection independent from visibility filtering.

## What Changes

- Add layer masks to renderables.
- Add culling masks to cameras.
- Filter queue construction by camera/renderable mask intersection.
- Keep camera scene-level resource collection separate from visibility-mask decisions.

## Capabilities

### New Capabilities
- `camera-visibility-mask`: Layer-mask visibility filtering between cameras and renderables during queue construction.

### Modified Capabilities

## Impact

- Affected code: `src/core/scene/`, `src/core/frame-graph/` or queue assembly path
- Affected APIs: camera/renderable visibility settings
- Affected systems: scene filtering, draw submission, future culling
