## ADDED Requirements

### Requirement: LightBase abstract interface with pass mask
`LX_core::LightBase` SHALL be an abstract interface declaring at least:
- `virtual ~LightBase() = default;`
- `virtual ResourcePassFlag getPassMask() const = 0;`
- `virtual IRenderResourcePtr getUBO() const = 0;` (returning `nullptr` is allowed for lights that do not need a UBO)
- `virtual bool supportsPass(StringID pass) const;` with a default implementation that returns `(getPassMask() & passFlagFromStringID(pass)) != 0`.

A `using LightBaseSharedPtr = std::shared_ptr<LightBase>;` alias SHALL be provided in the same header.

#### Scenario: Default supportsPass honors the pass mask
- **WHEN** a `LightBase` subclass returns `getPassMask() == ResourcePassFlag::Forward | ResourcePassFlag::Deferred` and `light->supportsPass(Pass_Shadow)` is called
- **THEN** the method returns `false`

#### Scenario: DirectionalLight implements LightBase
- **WHEN** a `DirectionalLight` is constructed
- **THEN** it IS-A `LightBase` (passes a `dynamic_pointer_cast<LightBase>`), and `getUBO()` returns the directional light's UBO resource

### Requirement: Camera holds optional RenderTarget with deferred default
`LX_core::Camera` SHALL expose a target association via:
- `const std::optional<RenderTarget> &getTarget() const`
- `void setTarget(RenderTarget target)`
- `void clearTarget()`
- `bool matchesTarget(const RenderTarget &target) const` — returns `true` if and only if `m_target.has_value() && *m_target == target`. A `nullopt` target SHALL NOT match any concrete target; backfilling with a real value is the caller's responsibility.

The initial value of `m_target` SHALL be `std::nullopt`.

#### Scenario: nullopt camera does not match a concrete target
- **WHEN** a `Camera` is constructed without calling `setTarget` and `cam.matchesTarget(RenderTarget{})` is called
- **THEN** the method returns `false`

#### Scenario: Camera matches its set target
- **WHEN** `cam.setTarget(RenderTarget{ImageFormat::BGRA8, ImageFormat::D32Float, 1})` is called and `cam.matchesTarget(RenderTarget{ImageFormat::BGRA8, ImageFormat::D32Float, 1})` is invoked
- **THEN** the method returns `true`

### Requirement: RenderTarget equality by field
`LX_core::RenderTarget` SHALL define `bool operator==(const RenderTarget &other) const` that compares `colorFormat`, `depthFormat`, and `sampleCount` field by field. `operator!=` SHALL be the inverse. Hash equality (`getHash()`) is NOT a substitute for this operator.

#### Scenario: Targets equal when all three fields match
- **WHEN** two `RenderTarget` values are constructed with identical `colorFormat` / `depthFormat` / `sampleCount`
- **THEN** `operator==` returns `true`

#### Scenario: Targets differ on sampleCount
- **WHEN** two `RenderTarget` values differ only in `sampleCount`
- **THEN** `operator==` returns `false`

### Requirement: Scene multi-camera / multi-light container
`LX_core::Scene` SHALL hold:
- `std::vector<IRenderableSharedPtr> m_renderables` (unchanged from REQ-008)
- `std::vector<CameraSharedPtr> m_cameras` (NEW — replaces the single `CameraSharedPtr camera` field)
- `std::vector<LightBaseSharedPtr> m_lights` (NEW — replaces the single `DirectionalLightSharedPtr directionalLight` field)

Scene SHALL provide:
- `void addRenderable(IRenderableSharedPtr)` (unchanged)
- `const std::vector<IRenderableSharedPtr> &getRenderables() const` (unchanged)
- `void addCamera(CameraSharedPtr camera)`
- `const std::vector<CameraSharedPtr> &getCameras() const`
- `void addLight(LightBaseSharedPtr light)`
- `const std::vector<LightBaseSharedPtr> &getLights() const`

The public `CameraSharedPtr camera` and `DirectionalLightSharedPtr directionalLight` fields SHALL be removed.

#### Scenario: Scene holds multiple cameras
- **WHEN** `scene.addCamera(camA)` then `scene.addCamera(camB)` is called
- **THEN** `scene.getCameras()` returns a vector with exactly two elements in insertion order

#### Scenario: Scene holds multiple lights
- **WHEN** two `LightBaseSharedPtr` instances are added via `addLight`
- **THEN** `scene.getLights()` returns a vector with exactly two elements in insertion order

## MODIFIED Requirements

### Requirement: Scene exposes scene-level descriptor resources
`LX_core::Scene` SHALL provide `std::vector<IRenderResourcePtr> getSceneLevelResources(StringID pass, const RenderTarget &target) const`. The method SHALL:
1. Iterate `m_cameras` and push each camera's UBO (via `cam->getUBO()`) into the output IF `cam->matchesTarget(target)` returns `true` and the UBO is non-null. Ordering within the camera section follows insertion order.
2. After all cameras, iterate `m_lights` and push each light's UBO (via `light->getUBO()`) into the output IF `light->supportsPass(pass)` returns `true` and the UBO is non-null. Ordering within the light section follows insertion order.
3. Camera UBOs SHALL appear before light UBOs in the returned vector.

An empty return value is a valid result — some `(pass, target)` combinations legitimately have no scene-level resources.

The REQ-008 parameterless `getSceneLevelResources()` overload SHALL NOT coexist with this signature.

#### Scenario: Camera UBO filtered by target
- **WHEN** a scene has two cameras `camA` / `camB` with distinct targets `targetA` / `targetB`, and `scene.getSceneLevelResources(Pass_Forward, targetA)` is called
- **THEN** the returned vector contains exactly `camA->getUBO()` (camB is excluded, and no light is added because the scene has no lights)

#### Scenario: Light UBO filtered by pass mask
- **WHEN** a scene has three lights — one with pass mask `Forward` only, one with `Shadow` only, and one with `Forward | Shadow` — plus one camera matching `RenderTarget{}`, and `getSceneLevelResources(Pass_Forward, RenderTarget{})` is called
- **THEN** the returned vector contains exactly three elements: the camera UBO, the `Forward`-only light's UBO, and the `Forward | Shadow` light's UBO. The `Shadow`-only light is excluded.

#### Scenario: Empty result for no matching resources
- **WHEN** a scene has one camera with target X, no lights, and `getSceneLevelResources(Pass_Forward, target Y)` is called (where X ≠ Y)
- **THEN** the returned vector is empty

### Requirement: RenderQueue builds items from a Scene per pass and target
`LX_core::RenderQueue::buildFromScene(const Scene &scene, StringID pass, const RenderTarget &target)` SHALL construct the queue's `RenderingItem` set from the scene. The method SHALL:
1. Call `clearItems()`.
2. Retrieve `scene.getSceneLevelResources(pass, target)` once before iterating renderables.
3. For each `IRenderableSharedPtr` in `scene.getRenderables()`, skip null pointers and skip renderables for which `renderable->supportsPass(pass)` returns `false`.
4. Construct a `RenderingItem` for each matching renderable (`vertexBuffer`, `indexBuffer`, `objectInfo`, `descriptorResources`, `shaderInfo`, `passMask`, `pass`, and — for `RenderableSubMesh` with non-null `mesh` and `material` — `material` and `pipelineKey = PipelineKey::build(sub->getRenderSignature(pass), sub->material->getRenderSignature(pass))`).
5. Append the scene-level resources from step 2 to each item's `descriptorResources`, after the renderable's own resources.
6. Push each item into `m_items` and call `sort()` at the end.

Renderable participation is decided by pass alone (via `supportsPass(pass)`); the `target` parameter is passed through purely for the scene-level-resource query.

The REQ-008 two-argument `buildFromScene(scene, pass)` overload SHALL NOT coexist with this signature.

#### Scenario: Queue rebuilt from scratch on each call
- **WHEN** `queue.buildFromScene(scene, Pass_Forward, RenderTarget{})` is called twice in a row on the same scene
- **THEN** the second call produces a queue with the same items as the first (not double-populated)

#### Scenario: Renderable filtering ignores target
- **WHEN** a scene has a renderable with `supportsPass(Pass_Forward) == true` and two cameras bound to distinct targets, and `buildFromScene(scene, Pass_Forward, targetA)` is called
- **THEN** the item for that renderable is in the queue regardless of which target was passed; only the scene-level resources (camera UBO) change between `targetA` and `targetB`

### Requirement: FrameGraph buildFromScene delegates with pass and target
`FrameGraph::buildFromScene(const Scene &)` SHALL iterate every configured `FramePass` in the frame graph and for each pass SHALL call `pass.queue.buildFromScene(scene, pass.name, pass.target)`. The method SHALL NOT construct `RenderingItem`s itself. Multiple calls in sequence are idempotent — each call rebuilds every queue from scratch.

#### Scenario: Populating a single forward pass with target-scoped camera
- **WHEN** `FrameGraph` contains one `FramePass{Pass_Forward, swapchainTarget, {}}` and a scene whose single camera has been backfilled to `swapchainTarget` and whose renderable supports `Pass_Forward`
- **THEN** after `buildFromScene(scene)`, the forward pass's `queue.getItems()` contains exactly one item whose `descriptorResources` includes the camera's UBO

#### Scenario: FramePass target is threaded through to the queue
- **WHEN** `FrameGraph` has two `FramePass` entries with targets `targetA` and `targetB`, each with a dedicated camera bound to that target via `setTarget`, and `buildFromScene(scene)` is called
- **THEN** pass A's queue items carry camera A's UBO in their `descriptorResources`, pass B's queue items carry camera B's UBO, and the two UBO identities do not mix across passes
