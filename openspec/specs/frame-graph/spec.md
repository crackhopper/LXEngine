## Purpose

Define the current frame-graph, render-target, and render-queue contracts used to build rendering work from a scene and preload pipelines.

## Requirements

### Requirement: ImageFormat enum enumerates core-supported texel formats
The system SHALL provide `LX_core::ImageFormat`, a `uint8_t`-backed enum covering at minimum `RGBA8`, `BGRA8`, `R8`, `D32Float`, `D24UnormS8`, and `D32FloatS8`. Backends SHALL provide a translation to their native format type (e.g., `VkFormat toVkFormat(ImageFormat)` in the Vulkan backend). Core layer code SHALL NOT reference backend-specific format types.

#### Scenario: Round-trip to backend format
- **WHEN** `toVkFormat(ImageFormat::BGRA8)` is called in the Vulkan backend
- **THEN** it returns `VK_FORMAT_B8G8R8A8_UNORM` (or the equivalent project-chosen BGRA8 variant)

### Requirement: RenderTarget describes a render pass attachment set
`LX_core::RenderTarget` SHALL be a core-layer struct containing at minimum `ImageFormat colorFormat`, `ImageFormat depthFormat`, and `uint8_t sampleCount`. It SHALL expose a stable `size_t getHash() const` suitable for use as an `unordered_map` key. `RenderTarget` membership SHALL NOT be part of `PipelineKey` at this stage of the project; the field is reserved for future multi-target support.

#### Scenario: Default target
- **WHEN** a default-constructed `RenderTarget` is inspected
- **THEN** it has a reasonable default (project picks — e.g., `BGRA8 + D32Float + sampleCount=1`) that matches today's forward pass

#### Scenario: Hash distinguishes different targets
- **WHEN** two `RenderTarget` values differ only in `sampleCount`
- **THEN** their `getHash()` values SHALL differ

### Requirement: RenderQueue collects and deduplicates pipeline build infos
`LX_core::RenderQueue` SHALL provide `void addItem(RenderingItem)`, `void sort()` (ordering by `PipelineKey` to reduce pipeline switches), `const std::vector<RenderingItem> &getItems() const`, and `std::vector<PipelineBuildDesc> collectUniquePipelineBuildDescs() const`. Deduplication MUST be performed by `PipelineKey` equality; items with the same key MUST contribute exactly one `PipelineBuildDesc` to the result.

#### Scenario: Deduplication on equal keys
- **WHEN** two `RenderingItem`s with equal `pipelineKey` are added to a queue
- **THEN** `collectUniquePipelineBuildDescs()` returns exactly one `PipelineBuildDesc`

#### Scenario: Sort is stable for equal keys
- **WHEN** `sort()` is called on a queue that contains interleaved items
- **THEN** items sharing the same `pipelineKey` are contiguous in `getItems()` afterward

### Requirement: FrameGraph models one pass per output target
`LX_core::FrameGraph` SHALL contain a sequence of `FramePass` entries. `FramePass` SHALL have at least `StringID name` (matching REQ-007 pass constants `Pass_Forward` / `Pass_Shadow` / `Pass_Deferred`), `RenderTarget target`, and `RenderQueue queue`. The `name` field MUST be `StringID`, not `std::string`, to align with `RenderQueue::buildFromScene(scene, pass)` and `IRenderable::supportsPass(pass)`.

#### Scenario: FramePass name is a StringID
- **WHEN** a `FramePass` is constructed with `Pass_Forward`
- **THEN** `pass.name == Pass_Forward` compares true and does not allocate a new string

### Requirement: RenderQueue builds items from a Scene per pass
`LX_core::RenderQueue::buildFromScene(const Scene &scene, StringID pass, const RenderTarget &target)` SHALL construct the queue's `RenderingItem` set from the scene. The method SHALL:
1. Call `clearItems()`.
2. Retrieve `scene.getSceneLevelResources(pass, target)` once before iterating renderables.
3. For each `IRenderablePtr` in `scene.getRenderables()`, skip null pointers and skip renderables for which `renderable->supportsPass(pass)` returns `false`.
4. For each remaining renderable, consume its already-validated structural result for `pass` and construct a `RenderingItem` from that cached data, filling `vertexBuffer`, `indexBuffer`, `objectInfo`, `descriptorResources`, `shaderInfo`, `passMask`, `pass`, `material`, and `pipelineKey`.
5. Append the scene-level resources from step 2 to each item's `descriptorResources`, after the renderable's own resources.
6. Push each item into `m_items` and call `sort()` at the end.

`RenderQueue` MUST NOT perform first-time mesh/material/skeleton legality checks, variant interpretation, or structural descriptor validation during queue build. Those responsibilities belong to the validated renderable model.

Renderable participation is decided by pass alone (via `supportsPass(pass)`); the `target` parameter is passed through purely for the scene-level-resource query.

The REQ-008 two-argument `buildFromScene(scene, pass)` overload SHALL NOT coexist with this signature.

#### Scenario: Queue rebuilt from scratch on each call
- **WHEN** `queue.buildFromScene(scene, Pass_Forward, RenderTarget{})` is called twice in a row on the same scene
- **THEN** the second call produces a queue with the same items as the first (not double-populated)

#### Scenario: Queue consumes validated entry without revalidating
- **WHEN** a `SceneNode` already has a validated forward-pass cache entry and `buildFromScene(scene, Pass_Forward, target)` is called
- **THEN** `RenderQueue` builds the item from that cached structural result and only appends scene-level resources for `target`

### Requirement: IRenderable supportsPass filter predicate
`IRenderable` SHALL declare `virtual bool supportsPass(StringID pass) const`. The primary implementation, `SceneNode`, SHALL answer from the material instance's pass-enable state and the node's pass-level validated-entry cache. `supportsPass(pass)` MUST return `false` for unknown, absent, or disabled passes and MUST NOT trigger ad-hoc structural revalidation while answering the query.

Other `IRenderable` implementations MAY provide equivalent semantics, but they MUST preserve the contract that pass support is a read-only query over already-established structural state.

#### Scenario: Disabled pass returns false
- **WHEN** a node's material instance has `Pass_Shadow` disabled
- **THEN** `supportsPass(Pass_Shadow)` returns `false`

#### Scenario: Cached enabled pass returns true
- **WHEN** a node has a validated entry for `Pass_Forward` and that pass remains enabled on the material instance
- **THEN** `supportsPass(Pass_Forward)` returns `true`

### Requirement: Scene exposes scene-level descriptor resources
`LX_core::Scene` SHALL provide `std::vector<IGpuResourcePtr> getSceneLevelResources(StringID pass, const RenderTarget &target) const`. The method SHALL:
1. Iterate `m_cameras` and push each camera's UBO (via `cam->getUBO()`) into the output IF `cam->matchesTarget(target)` returns `true` and the UBO is non-null. Ordering within the camera section follows insertion order.
2. After all cameras, iterate `m_lights` and push each light's UBO (via `light->getUBO()`) into the output IF `light->supportsPass(pass)` returns `true` and the UBO is non-null. Ordering within the light section follows insertion order.
3. Camera UBOs SHALL appear before light UBOs in the returned vector.

An empty return value is a valid result — some `(pass, target)` combinations legitimately have no scene-level resources.

The REQ-008 parameterless `getSceneLevelResources()` overload SHALL NOT coexist with this signature.

#### Scenario: Camera UBO filtered by target
- **WHEN** a scene has two cameras `camA` / `camB` with distinct targets `targetA` / `targetB`, no light in the scene supports `Pass_Shadow`, and `scene.getSceneLevelResources(Pass_Shadow, targetA)` is called
- **THEN** the returned vector contains exactly `camA->getUBO()` (camB is excluded, and no light is added because none supports `Pass_Shadow`)

#### Scenario: Light UBO filtered by pass mask
- **WHEN** a scene contains the constructor-seeded default directional light (`Forward | Deferred`), plus three additional lights — one with pass mask `Forward` only, one with `Shadow` only, and one with `Forward | Shadow` — and one camera matching `RenderTarget{}`, and `getSceneLevelResources(Pass_Forward, RenderTarget{})` is called
- **THEN** the returned vector contains exactly four elements in order: the camera UBO, the default directional light's UBO, the `Forward`-only light's UBO, and the `Forward | Shadow` light's UBO. The `Shadow`-only light is excluded.

#### Scenario: Empty result for no matching resources
- **WHEN** a scene has one camera with target X, no light in the scene supports `Pass_Shadow`, and `getSceneLevelResources(Pass_Shadow, target Y)` is called (where X ≠ Y)
- **THEN** the returned vector is empty

### Requirement: FrameGraph buildFromScene populates queues per pass
`FrameGraph::buildFromScene(const Scene &)` SHALL iterate every configured `FramePass` in the frame graph and for each pass SHALL call `pass.queue.buildFromScene(scene, pass.name, pass.target)`. The method SHALL NOT construct `RenderingItem`s itself. Multiple calls in sequence are idempotent — each call rebuilds every queue from scratch.

#### Scenario: Populating a single forward pass with target-scoped camera
- **WHEN** `FrameGraph` contains one `FramePass{Pass_Forward, swapchainTarget, {}}` and a scene whose single camera has been backfilled to `swapchainTarget` and whose renderable supports `Pass_Forward`
- **THEN** after `buildFromScene(scene)`, the forward pass's `queue.getItems()` contains exactly one item whose `descriptorResources` includes the camera's UBO

#### Scenario: FramePass target is threaded through to the queue
- **WHEN** `FrameGraph` has two `FramePass` entries with targets `targetA` and `targetB`, each with a dedicated camera bound to that target via `setTarget`, and `buildFromScene(scene)` is called
- **THEN** pass A's queue items carry camera A's UBO in their `descriptorResources`, pass B's queue items carry camera B's UBO, and the two UBO identities do not mix across passes

#### Scenario: Idempotent rebuild
- **WHEN** `FrameGraph::buildFromScene(scene)` is called twice on the same scene
- **THEN** every pass's queue contains the same items after the second call as after the first (items are not duplicated)

### Requirement: FrameGraph collectAllPipelineBuildDescs deduplicates across passes
`FrameGraph::collectAllPipelineBuildDescs()` SHALL iterate every pass's `RenderQueue`, concatenate each queue's `collectUniquePipelineBuildDescs()`, and deduplicate the combined set by `PipelineKey`. The returned vector is the input to the backend's pipeline preload step.

#### Scenario: Duplicate across passes collapses to one
- **WHEN** the same mesh+material appears in two passes whose `PipelineKey` values are identical (e.g., a trivial pass remapping)
- **THEN** `collectAllPipelineBuildDescs()` returns exactly one `PipelineBuildDesc` for it

#### Scenario: Different passes keep distinct entries
- **WHEN** the same mesh+material appears under `Pass_Forward` and `Pass_Shadow` with distinct per-pass render state
- **THEN** `collectAllPipelineBuildDescs()` returns two distinct `PipelineBuildDesc` entries

### Requirement: Scene exposes a renderable collection
`LX_core::Scene` SHALL provide `const std::vector<IRenderablePtr> &getRenderables() const` returning every renderable currently part of the scene. The previously single `IRenderablePtr mesh` member SHALL be replaced (or wrapped) by a `std::vector<IRenderablePtr> m_renderables` member so `FrameGraph::buildFromScene` can iterate.

#### Scenario: Single renderable scene
- **WHEN** a scene is constructed with one renderable (matching today's tests)
- **THEN** `getRenderables()` returns a vector with exactly one element

### Requirement: LightBase abstract interface with pass mask
`LX_core::LightBase` SHALL be an abstract interface declaring at least:
- `virtual ~LightBase() = default;`
- `virtual ResourcePassFlag getPassMask() const = 0;`
- `virtual IGpuResourcePtr getUBO() const = 0;` (returning `nullptr` is allowed for lights that do not need a UBO)
- `virtual bool supportsPass(StringID pass) const;` with a default implementation that returns `(getPassMask() & passFlagFromStringID(pass)) != 0`.

A `using LightBasePtr = std::shared_ptr<LightBase>;` alias SHALL be provided in the same header.

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
- `std::vector<IRenderablePtr> m_renderables` (unchanged from REQ-008)
- `std::vector<CameraPtr> m_cameras` (NEW — replaces the single `CameraPtr camera` field)
- `std::vector<LightBasePtr> m_lights` (NEW — replaces the single `DirectionalLightPtr directionalLight` field)

Scene SHALL provide:
- `void addRenderable(IRenderablePtr)` (unchanged)
- `const std::vector<IRenderablePtr> &getRenderables() const` (unchanged)
- `void addCamera(CameraPtr camera)`
- `const std::vector<CameraPtr> &getCameras() const`
- `void addLight(LightBasePtr light)`
- `const std::vector<LightBasePtr> &getLights() const`

The public `CameraPtr camera` and `DirectionalLightPtr directionalLight` fields SHALL be removed.

#### Scenario: Scene holds multiple cameras
- **WHEN** `scene.addCamera(camA)` then `scene.addCamera(camB)` is called
- **THEN** `scene.getCameras()` returns a vector with exactly two elements in insertion order

#### Scenario: Scene holds multiple lights
- **WHEN** two `LightBasePtr` instances are added via `addLight`
- **THEN** `scene.getLights()` returns a vector with exactly two elements in insertion order
