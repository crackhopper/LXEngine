## 1. EngineLoop API

- [x] 1.1 Add `EngineLoop` module and public header with `initialize`, `startScene`, `setUpdateHook`, `tickFrame`, `run`, `stop`, and clock accessors
- [x] 1.2 Decide and implement the concrete module location/naming in the source tree so it sits above `gpu::Renderer` without leaking backend specifics

## 2. Runtime Implementation

- [x] 2.1 Implement `EngineLoop` state management for `Window`, `Renderer`, `Clock`, active scene, running flag, and optional update hook
- [x] 2.2 Implement `startScene(scene)` so it stores the scene and calls `renderer->initScene(scene)` exactly once per start/rebuild
- [x] 2.3 Implement `tickFrame()` with the required order: `clock.tick() -> update hook -> uploadData() -> draw()`
- [x] 2.4 Implement `run()` and `stop()` so applications can use a canonical loop without handwritten `while (...) { uploadData(); draw(); }`
- [x] 2.5 Implement explicit rebuild semantics for structural scene changes, either through a dedicated request API or a clearly documented restart path

## 3. Integration And Docs

- [x] 3.1 Migrate the current runnable entry path (`test_render_triangle`; future `demo_scene_viewer`) to use `EngineLoop` as its runtime entry point
- [x] 3.2 Update tutorial examples to present handwritten loops only as expanded teaching form and `EngineLoop` as the recommended production-facing shape
- [x] 3.3 Add or update tests that verify scene start is not per-frame, the update hook runs before upload/draw, and `run()` exits on stop/window close
