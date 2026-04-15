## Why

The code already separates scene initialization from per-frame rendering, but the public mental model does not. `initScene/buildFromScene/preloadPipelines` are still easy to misread as frame-time work, and app code is forced to hand-roll `Window + Renderer + while (...)` orchestration in every demo or tutorial.

Now that the project has `Clock`, input, camera controllers, and debug UI, it needs a higher-level runtime entry point above the backend. Without that layer, lifecycle policy, update-hook timing, and scene rebuild rules remain scattered and hard to reuse.

## What Changes

- Introduce a new `EngineLoop` runtime abstraction above `gpu::Renderer`.
- Define explicit scene-start lifecycle separate from the per-frame loop.
- Standardize the frame order as `clock.tick() -> update hook -> uploadData() -> draw()`.
- Define how `EngineLoop` coordinates `Window`, `Renderer`, `Clock`, and the active `Scene`.
- Define explicit scene rebuild semantics for structural changes instead of implying per-frame `initScene`.
- Update architecture/tutorial/demo-facing guidance to converge on `EngineLoop` as the preferred application entry point.

## Capabilities

### New Capabilities
- `engine-loop`: Defines the engine-level runtime orchestration contract for scene startup, per-frame update hooks, render invocation, stop/run behavior, and explicit scene rebuild requests.

### Modified Capabilities

## Impact

- Affected code will include a new runtime module above the backend, likely in `src/core/` or a nearby engine-facing layer, plus tutorial/demo integration points.
- `demo_scene_viewer` becomes the first full consumer of `EngineLoop` instead of manually orchestrating the frame loop.
- Documentation and tutorial flow will change to distinguish scene startup from frame execution.
- Existing backend contracts (`initScene/uploadData/draw`) remain in place and are consumed by `EngineLoop` rather than removed.
