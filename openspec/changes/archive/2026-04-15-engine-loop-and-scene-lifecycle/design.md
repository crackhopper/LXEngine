## Context

The Vulkan backend already has the right internal split:

- `Renderer::initScene(scene)` performs scene-bound setup
- `Renderer::uploadData()` performs dirty-resource synchronization
- `Renderer::draw()` records and submits rendering commands

The problem is not in the backend implementation. The problem is that the project still exposes application code through handwritten loops, so lifecycle policy is duplicated in demos and tutorials. The new runtime layer must sit above `gpu::Renderer`, reuse the existing three-step renderer contract, and avoid smearing gameplay/runtime concerns back into `backend/vulkan`.

This is a cross-cutting change because it touches architecture boundaries, future demo integration, and how later runtime-facing features will be composed.

## Goals / Non-Goals

**Goals:**

- Introduce `EngineLoop` as the preferred engine-facing runtime entry point.
- Keep backend responsibility narrow: scene handoff, upload, and draw only.
- Make scene startup explicit and separate from frame execution.
- Provide a deterministic update hook before `uploadData()`.
- Define explicit rebuild semantics for structural scene changes.
- Make `demo_scene_viewer` and tutorial flow converge on the same lifecycle shape.

**Non-Goals:**

- Not introducing ECS, scripting, physics, or a full gameplay framework.
- Not removing `Renderer::initScene/uploadData/draw`; `EngineLoop` composes them.
- Not specifying fixed-step simulation, interpolation, or pause/time-scale policy beyond the minimal `Clock` integration.
- Not changing the backend frame graph construction rules themselves.

## Decisions

### Decision 1: `EngineLoop` lives above `gpu::Renderer` and composes it

`EngineLoop` is a new runtime abstraction that owns or coordinates:

- `WindowSharedPtr`
- `RendererSharedPtr`
- `Clock`
- current `SceneSharedPtr`
- running/stopped state
- optional per-frame update callback

Representative API:

```cpp
class EngineLoop {
public:
  void initialize(WindowSharedPtr window, gpu::RendererSharedPtr renderer);
  void startScene(SceneSharedPtr scene);
  void setUpdateHook(std::function<void(Scene &, const Clock &)> hook);
  void tickFrame();
  void run();
  void stop();
  const Clock &getClock() const;
};
```

This preserves the backend contract while giving the application a single higher-level entry point.

Alternatives considered:

- Put update-hook logic into `VulkanRenderer`.
  Rejected: that would mix runtime policy with rendering execution.
- Keep handwritten loops in demos/tutorials only.
  Rejected: that preserves duplication and leaves no reusable engine-facing interface.

### Decision 2: Scene startup is a one-time phase driven by `startScene`

`EngineLoop::startScene(scene)` is the engine-level lifecycle boundary for beginning or restarting a scene.

Its core responsibility is to hand the scene to the renderer:

```cpp
m_scene = scene;
m_renderer->initScene(scene);
```

The renderer remains free to build frame graphs, preload pipelines, backfill camera targets, and prepare GPU resources during `initScene()`. `EngineLoop` does not duplicate those details; it defines when they happen.

This keeps scene startup separate from the frame loop and avoids the incorrect idea that `buildFromScene` is expected every frame.

### Decision 3: Frame execution order is fixed and narrow

`EngineLoop::tickFrame()` executes in this order:

```cpp
clock.tick();
if (updateHook) updateHook(*scene, clock);
renderer->uploadData();
renderer->draw();
window->nextFrame();   // if the window/input implementation requires it
```

This order is deliberate:

- user code must run after time advances
- user code must run before dirty resources are uploaded
- renderer upload must complete before draw submission

The update hook is allowed to mutate camera transforms, lights, material parameters, and other CPU-side values that participate in the dirty-resource path.

Alternatives considered:

- Upload first, then update.
  Rejected: that would delay user changes by one frame or require side-channel uploads.
- Allow hook injection both before and after draw.
  Rejected for now: too much policy surface for the first runtime abstraction.

### Decision 4: Structural scene changes use explicit rebuild semantics

`EngineLoop` must distinguish between:

- data changes that only require dirty upload
- structural changes that require renderer re-initialization

Examples of structural changes:

- adding/removing renderables
- pass participation changes
- material/shader changes that alter `PipelineKey`
- camera target changes that alter pass/target routing

The runtime API will therefore include an explicit rebuild path, either:

```cpp
void requestSceneRebuild();
```

or by reusing:

```cpp
startScene(m_scene);
```

The proposal does not force one exact public method name, but the implementation must make rebuild intent explicit instead of hiding it inside per-frame execution.

### Decision 5: `EngineLoop::run()` is a thin default loop, not a framework

`run()` is a convenience wrapper around repeated `tickFrame()` calls until:

- `stop()` is called, or
- the window requests close

It is not meant to become a full scheduler. This keeps the abstraction useful for demos and applications without prematurely defining a full engine runtime framework.

## Risks / Trade-offs

- [Risk] `EngineLoop` may become a dumping ground for future gameplay concerns.  
  Mitigation: keep the initial contract narrow and centered on lifecycle orchestration only.

- [Risk] Rebuild semantics may be underspecified if the implementation does not clearly separate dirty updates from structural changes.  
  Mitigation: document rebuild triggers in code comments/tests and expose explicit rebuild API.

- [Trade-off] `run()` duplicates a loop that applications could write themselves.  
  Mitigation: that duplication is the point; it centralizes the canonical loop and reduces drift.

- [Trade-off] Window/input stepping may not be uniformly owned by the current window abstraction.  
  Mitigation: keep `window->nextFrame()` as a runtime integration detail in the first implementation and refine later if needed.

## Migration Plan

1. Add `EngineLoop` type and minimal API surface.
2. Implement `initialize`, `startScene`, `setUpdateHook`, `tickFrame`, `run`, and `stop`.
3. Route `Clock` ownership into `EngineLoop`.
4. Update at least one end-to-end consumer (`demo_scene_viewer`) to use `EngineLoop`.
5. Update tutorial and architecture notes so scene startup and frame execution are described separately.

Rollback is low-risk because the backend contract remains unchanged. Reverting the new runtime layer would mostly involve removing `EngineLoop` call sites and restoring handwritten loops.

## Open Questions

- Should `EngineLoop` own polling/input retrieval directly, or just rely on `Window` side effects plus `nextFrame()`?
- Should explicit rebuild be public as `requestSceneRebuild()` or remain an internal flag driven by `startScene(scene)` reuse?
- Does `EngineLoop` belong in `src/core/engine/`, `src/core/runtime/`, or a similarly named top-level module?
