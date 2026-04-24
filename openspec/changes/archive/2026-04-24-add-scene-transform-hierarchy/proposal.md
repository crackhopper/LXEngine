## Why

The review still calls out the lack of transform hierarchy as a core scene-model gap: model matrices are effectively fed from outside each frame, and parent-child spatial relationships cannot be expressed in the current flat scene model. That blocks ordinary gameplay-style composition and future culling work.

This change introduces a formal scene transform hierarchy instead of relying on external per-object matrix injection alone.

## What Changes

- Add parent-child transform hierarchy support to scene objects.
- Define local/world transform update semantics.
- Integrate hierarchy-aware world transforms with the current renderable validation path.
- Document hierarchy scope and limits for the first version.

## Capabilities

### New Capabilities
- `scene-transform-hierarchy`: Parent-child transform hierarchy contract for scene objects and renderable world transforms.

### Modified Capabilities

## Impact

- Affected code: `src/core/scene/`, renderable validation, demo/test scene assembly
- Affected APIs: scene object transform ownership and update flow
- Affected systems: gameplay composition, future culling, render transform correctness
