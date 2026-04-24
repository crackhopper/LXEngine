# Design: Multi-camera / multi-light with RenderTarget-driven filtering

## Context

REQ-008 shipped the data-flow fix but deliberately froze two shortcuts:
1. `Scene` keeps single `CameraSharedPtr camera` / `DirectionalLightSharedPtr directionalLight` fields.
2. `Scene::getSceneLevelResources()` is parameterless — every scene-level UBO is appended to every `RenderingItem` regardless of which pass or target.

Both shortcuts were acceptable for the current single-`Pass_Forward`-to-swapchain scenario because there is exactly one camera and one light. But every non-trivial feature queued after REQ-008 — shadow map, deferred G-buffer, overlay UI pass, reflection probe, multiple directional lights — immediately breaks against these shortcuts. This change replaces them with two filter axes (camera-by-target, light-by-pass) so `RenderQueue::buildFromScene` can produce the correct per-pass resource set from a general `Scene`.

A second thread pulled along is `FramePass.target`: REQ-008's `defaultForwardTarget()` returned a default-constructed `RenderTarget{}` placeholder, and no code read it. REQ-009 must promote this to a real value derived from the Vulkan device's surface/depth format, because the new camera-filter logic uses `RenderTarget::operator==` against it.

## Goals / Non-Goals

**Goals**:
- Two filter axes on `getSceneLevelResources`: camera filtered by `matchesTarget(target)`, light filtered by `supportsPass(pass)`. Independent dimensions — camera doesn't care about pass, light doesn't care about target.
- `Camera` owns an `std::optional<RenderTarget>`. `nullopt` is a deferred default: `VulkanRenderer::initScene` backfills it with the real swapchain target before `m_frameGraph.buildFromScene`. Tests that want a specific target set it explicitly.
- `LightBase` becomes a proper interface; `DirectionalLight` implements it. Future `PointLight` / `SpotLight` / `AreaLight` can be added purely by implementing `LightBase` without touching the scene or queue code.
- `RenderQueue::buildFromScene(scene, pass, target)` is the single threaded entry. `FrameGraph::buildFromScene` is a trivial dispatch that reads `pass.target` from each `FramePass`.
- Backend provides a `toImageFormat(VkFormat)` translation covering the formats the swapchain actually picks, so `makeSwapchainTarget()` returns a `RenderTarget` whose `operator==` matches what test code sets via `setTarget(RenderTarget{})`.

**Non-Goals**:
- **Not** scheduling multiple `FramePass`es in parallel. `initScene` still hard-codes a single `Pass_Forward` pass. The architecture supports additional passes now; wiring them up is downstream work.
- **Not** implementing `PointLight` / `SpotLight` / `AreaLight` — they are unblocked by this change but out of scope. Only `DirectionalLight` is migrated onto `LightBase`.
- **Not** supporting dynamic scene mutation — runtime `addCamera` / `addLight` will still require re-running `initScene` to take effect. The framegraph is built once.
- **Not** implementing shadow map target derivation (constructing a `RenderTarget` from a shadow atlas image view) — that's a later shadow-pass-specific change.
- **Not** filtering `Renderable` by target — a renderable's pass mask alone decides participation. `RenderingItem` construction has no target awareness; only `getSceneLevelResources` does.
- **Not** filtering `Light` by target — a light can influence shading on any target it reaches. Target picks "where to draw"; pass picks "which shading stage the light participates in".
- **Not** building a complete `VkFormat → ImageFormat` translation table — only the handful of formats the Vulkan swapchain picks in this project need to round-trip.

## Decisions

### D1: Camera filters by target, light filters by pass — independent axes

**Decision**: `Scene::getSceneLevelResources(pass, target)` applies two independent filters:
- Camera: `cam->matchesTarget(target)` — pass parameter is ignored.
- Light: `light->supportsPass(pass)` — target parameter is ignored.

**Alternatives considered**:
- **Both axes filter both dimensions** (camera by `(target, pass)`, light by `(target, pass)`). Rejected: semantically wrong — a single camera drawing to a target typically participates in every pass that draws to that target (forward + overlay of the same swapchain, for instance), so the pass filter on camera would either be a no-op or incorrect.
- **Single axis** (just target, or just pass). Rejected: camera and light have fundamentally different scoping — camera is target-scoped, light is pass-scoped. Collapsing to one axis loses one of the two.

**Rationale**: The two concepts are orthogonal. "Which attachment am I writing pixels to" (target) and "which shading stage am I part of" (pass) are separate operator dimensions. Letting each light / camera filter on its own axis keeps the model composable and the mental load low.

### D2: `Camera::matchesTarget(nullopt) == false`, with explicit backfill in `initScene`

**Decision**: `Camera::matchesTarget(target)` returns `false` if `m_target == nullopt`, regardless of `target`. `VulkanRenderer::initScene` is responsible for explicitly setting every `nullopt` camera's target to the swapchain target **before** `m_frameGraph.buildFromScene(*scene)` runs.

**Alternatives considered**:
- **Treat `nullopt` as "matches any target"** — less code in `initScene`, more permissive default. Rejected: quietly swallows real configuration bugs (a camera that was never assigned becomes invisible to the filter because it silently matches everything). The tight contract surfaces mistakes loudly.
- **Treat `nullopt` as "matches the swapchain target by convention"** — still implicit, still swallows errors when the user intended a different target.

**Rationale**: The explicit backfill makes the "default to swapchain" semantics a one-line contract in `initScene`, gives test code a way to opt out by calling `setTarget` explicitly, and keeps `Camera::matchesTarget` a pure function of its state (no hidden "current swapchain" dependency). R8's `testNullOptCameraBeforeAndAfterFill` locks this behavior in.

### D3: Backfill runs **before** `buildFromScene`, not as part of it

**Decision**: `VulkanRenderer::Impl::initScene` performs the nullopt-camera backfill between `addPass` and `buildFromScene`:

```cpp
void initScene(SceneSharedPtr _scene) override {
    scene = _scene;
    RenderTarget swapchain = makeSwapchainTarget();
    for (auto &cam : scene->getCameras()) {
        if (cam && !cam->getTarget().has_value()) cam->setTarget(swapchain);
    }
    m_frameGraph = LX_core::FrameGraph{};
    m_frameGraph.addPass(FramePass{Pass_Forward, swapchain, {}});
    m_frameGraph.buildFromScene(*scene);
    // ... sync, preload, ...
}
```

**Alternative considered**: Fold the backfill into `FrameGraph::buildFromScene` by detecting nullopt cameras and using `pass.target` as the fill value.

**Rationale**: `FrameGraph::buildFromScene` is a pure core-layer function and should not mutate scene state. Mutation of camera state is a backend-level responsibility (only the backend knows what the "real default" target is). Keeping the mutation in `initScene` preserves the layering: core stays read-only on `Scene`, backend takes ownership of the contextual default.

### D4: `RenderTarget::operator==` is a by-field comparison, not a hash comparison

**Decision**: `operator==` compares `colorFormat`, `depthFormat`, `sampleCount` field by field. Hash (`getHash()`) exists for `unordered_map` keying but is not used for equality.

**Alternative considered**: Equality via `getHash()`.

**Rationale**: Hash collisions are rare but real. For a small struct like `RenderTarget` the by-field comparison is clearer, zero-cost, and matches the exact equality semantics the `Camera::matchesTarget` path needs. `getHash` stays for map keying only.

### D5: `firstItemFromScene` gets a **defaulted** target parameter, not a separate overload

**Decision**: The REQ-008 helper signature

```cpp
inline RenderingItem firstItemFromScene(Scene &scene, StringID pass);
```

becomes

```cpp
inline RenderingItem firstItemFromScene(Scene &scene, StringID pass,
                                        const RenderTarget &target = {});
```

**Alternative considered**: Add a separate overload `firstItemFromScene(scene, pass, target)` and keep the old one.

**Rationale**: The default argument keeps all five REQ-008-migrated call sites source-compatible with zero edits on the call side. What **must** change is the test *setup*: every test that previously set `scene->camera = cam` must now also call `cam->setTarget(RenderTarget{})` (or use the new `makeDefaultCameraWithTarget` helper), otherwise `matchesTarget` returns false and the queue is empty. The failure mode is loud (`assert(!q.getItems().empty())` fires), so the migration surfaces clearly in CI.

### D6: `toImageFormat` lives next to `toVkFormat` in the Vulkan backend, not in core

**Decision**: `LX_core::ImageFormat toImageFormat(VkFormat)` is declared in `src/backend/vulkan/details/vk_resource_manager.hpp` and implemented in the matching `.cpp`, right next to the existing `VkFormat toVkFormat(ImageFormat)`. Core does not gain any dependency on `VkFormat`.

**Alternative considered**: Put the function in `src/core/gpu/image_format.hpp`.

**Rationale**: Core must not reference `VkFormat` (cpp-style-guide rule). The reverse translation is backend-side by construction. Pairing it with `toVkFormat` keeps both directions in one place.

### D7: Minimal format coverage for `toImageFormat`

**Decision**: `toImageFormat` only handles the formats the swapchain actually chooses (`VK_FORMAT_B8G8R8A8_SRGB`, `VK_FORMAT_B8G8R8A8_UNORM`, `VK_FORMAT_D32_SFLOAT`, `VK_FORMAT_D24_UNORM_S8_UINT`). Any other input maps to a default (likely `ImageFormat::RGBA8`) and logs a debug warning; the error path is not thrown because `initScene` must not crash on an unfamiliar format during normal operation.

**Rationale**: A full bi-directional map is speculative now. Shadow map and G-buffer formats will be added when the corresponding passes are implemented — at that point the new format is added to both `toVkFormat` and `toImageFormat` in a single edit. YAGNI.

## Risks / Trade-offs

- **[Risk]** Backfill ordering is subtle: if any code path calls `buildFromScene` before `initScene` runs (e.g., a test that directly constructs a `FrameGraph` and calls it), nullopt cameras will silently be filtered out and the queue will be empty.
  → **Mitigation**: the new `testNullOptCameraBeforeAndAfterFill` scenario documents the contract. `firstItemFromScene`'s non-empty assertion will fire loudly in any test that forgets the backfill. The REQ-008 integration test migrations in R8.1 explicitly call `setTarget(RenderTarget{})` on the camera they add.

- **[Risk]** `Scene` callers scattered across the codebase still use the deleted `scene->camera` / `scene->directionalLight` access syntax.
  → **Mitigation**: `grep -rn 'scene->camera\b\|scene->directionalLight\b\|scene\.camera\b\|scene\.directionalLight\b' src/` returns zero hits after the R4 migration lands. Any miss is a hard compile error.

- **[Risk]** The migration from REQ-008's `Scene::getSceneLevelResources()` to REQ-009's `(pass, target)` version will silently change behavior for existing call sites that previously passed an implicit "all resources". Specifically: a test that expects two resources (camera + light) may now get zero if the camera's target isn't set.
  → **Mitigation**: test migrations (task group 6) explicitly re-set each test's camera target. The `firstItemFromScene` helper's `assert(!getItems().empty())` is the backstop.

- **[Risk]** `RenderTarget::operator==` is tight (by-field). If a future refactor adds fields to `RenderTarget` without updating `operator==`, two targets that should compare equal will silently compare unequal.
  → **Mitigation**: document the requirement in the `RenderTarget` header next to `operator==`. Not a hard mitigation — this is a "gotcha" to live with until a real bug forces structural invariants.

- **[Trade-off]** Tight camera filter (`nullopt → false`) means every test that constructs a scene must explicitly set a target on every camera. This is more boilerplate per test but catches configuration bugs earlier.

- **[Trade-off]** Two filter axes (target for camera, pass for light) is an asymmetry some readers will find surprising. The asymmetry is intentional and documented in D1, but it is the kind of design choice that will need to be explained in onboarding.

## Migration Plan

1. **Additive foundation** (R1, R2, R3, R4 adds): add `LightBase` virtual interface alongside the existing `class LightBase {}`, add `std::optional<RenderTarget>` to `Camera`, add `RenderTarget::operator==`, add `addCamera` / `addLight` / `getCameras` / `getLights` to `Scene`. These can land without touching any call site.
2. **DirectionalLight onto LightBase**: implement the virtuals; existing `scene->directionalLight->...` access still compiles because `Scene` still holds the legacy field at this step.
3. **`getSceneLevelResources(pass, target)` overload**: add the new signature alongside the REQ-008 parameterless one. Zero callers use it yet.
4. **`RenderQueue::buildFromScene(scene, pass, target)` overload**: same — coexists with the REQ-008 signature.
5. **Switch `FrameGraph::buildFromScene` over**: now `pass.queue.buildFromScene(scene, pass.name, pass.target)` — the old 2-arg queue entry is dead from the frame-graph path.
6. **Backend adopts**: `makeSwapchainTarget` + `toImageFormat` land; `initScene` does the nullopt backfill; `FramePass` is constructed with the real swapchain target.
7. **Test migration**: migrated REQ-008 tests switch to `addCamera` / `addLight`; each camera gets `setTarget(RenderTarget{})` in setup.
8. **Delete legacy surfaces**: `Scene::camera` / `Scene::directionalLight` fields removed; REQ-008's parameterless `getSceneLevelResources()` removed; REQ-008's `RenderQueue::buildFromScene(scene, pass)` 2-arg version removed. Tree must still build — any missed caller fails loudly.
9. **New test scenarios (R8.2 / R8.3 / R8.4)** added to `test_frame_graph.cpp`.
10. **REQ-008 finished doc** gets a "Partial supersede by REQ-009" banner scoped to R3 / R4 / R6.

Each numbered step leaves the build green. A bisect between steps isolates any regression to a single edit.

**Rollback**: Revert the change commit. Because every step is additive-before-breaking, a single-commit revert is sufficient; there is no data migration.

## Open Questions

- **Does `DirectionalLight` own or borrow its pass mask?** The current proposal keeps `m_passMask` as a member. A constructor parameter with a `Forward | Deferred` default covers all existing call sites without touching the loader. If this turns out to be inflexible, a separate `PassMaskPolicy` strategy object can be added later — not now.
- **Should `Camera` default pass mask to the swapchain target lazily (on first `matchesTarget` call) instead of explicit `initScene` backfill?** Tempting because it removes the "ordering matters" footgun, but it requires `Camera` to know about a thread-local "current default". The explicit backfill is the lesser evil; revisit only if the ordering footgun actually bites.
- **Will the `toImageFormat` default mapping (unknown format → `RGBA8`) ever silently match a real test setup wrong?** Only if a test manually constructs a `RenderTarget` with a format that isn't in the explicit set, and separately the swapchain happens to pick a different format. The test migration in R8.1 explicitly uses `RenderTarget{}` (default construction), so both sides agree on the default. We accept the mismatch as "test explicitly uses default; backend picks whatever Vulkan exposes; if they disagree the test catches it via the empty-queue assertion".
