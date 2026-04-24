## ADDED Requirements

### Requirement: RenderQueue builds items from a Scene per pass
`LX_core::RenderQueue` SHALL provide `void buildFromScene(const Scene &scene, StringID pass)`. This method SHALL:
1. Call `clearItems()` to reset the queue.
2. Retrieve `scene.getSceneLevelResources()` once before iteration.
3. For each `IRenderableSharedPtr` in `scene.getRenderables()`, skip null pointers and skip renderables for which `renderable->supportsPass(pass)` returns `false`.
4. For each matching renderable, construct a `RenderingItem` populated with the renderable's `vertexBuffer`, `indexBuffer`, `objectInfo`, `descriptorResources`, `shaderInfo`, `passMask`, and `pass`. When the renderable resolves to a `RenderableSubMesh` with non-null `mesh` and `material`, the item's `material` and `pipelineKey = PipelineKey::build(sub->getRenderSignature(pass), sub->material->getRenderSignature(pass))` SHALL be set.
5. Append the scene-level resources from step 2 to the item's `descriptorResources` (after the renderable's own resources, preserving descriptor binding order).
6. Push each item into the queue and call `sort()` at the end.

#### Scenario: Queue is rebuilt from scratch on each call
- **WHEN** `queue.buildFromScene(scene, Pass_Forward)` is called twice in a row on the same scene
- **THEN** the second call produces a queue with the same items as the first call (not double-populated), because `buildFromScene` calls `clearItems()` before iterating

#### Scenario: Scene-level resources merge into every item
- **WHEN** a scene has one camera UBO and one light UBO exposed via `getSceneLevelResources()`, and two renderables each with their own descriptor resources
- **THEN** each resulting `RenderingItem` has its own descriptor resources **followed by** the camera UBO and the light UBO in the same order returned by `getSceneLevelResources()`

#### Scenario: Pass-mask filter excludes non-matching renderables
- **WHEN** a scene contains renderable A with pass mask `Forward | Shadow` and renderable B with pass mask `Forward`, and `buildFromScene(scene, Pass_Shadow)` is called
- **THEN** the resulting queue contains exactly one item (from renderable A)

### Requirement: IRenderable supportsPass filter predicate
`IRenderable` SHALL declare `virtual bool supportsPass(StringID pass) const`. The default implementation SHALL compute `passFlagFromStringID(pass)` and return `(getPassMask() & flag) != 0`. Subclasses MAY override to implement more granular filtering (e.g., filtering based on whether the material has a configured `Pass_Shadow` entry).

#### Scenario: Default implementation honors pass mask bits
- **WHEN** a renderable has `getPassMask() == (ResourcePassFlag::Forward | ResourcePassFlag::Shadow)` and `supportsPass(Pass_Deferred)` is called
- **THEN** the method returns `false`

#### Scenario: Renderable with Forward mask supports Forward pass
- **WHEN** a renderable has `getPassMask() == ResourcePassFlag::Forward` and `supportsPass(Pass_Forward)` is called
- **THEN** the method returns `true`

### Requirement: Scene exposes scene-level descriptor resources
`LX_core::Scene` SHALL provide `std::vector<IRenderResourcePtr> getSceneLevelResources() const`. The method SHALL return a vector containing, in order: the camera's UBO (if `camera != nullptr` and its UBO is non-null), followed by the directional light's UBO (if `directionalLight != nullptr` and its UBO is non-null). The ordering — camera first, light second — is normative so that backends can rely on a stable merge order when `RenderQueue::buildFromScene` appends scene-level resources to each item.

This is the REQ-008 parameterless form. A future `getSceneLevelResources(StringID pass, const RenderTarget &target)` overload is planned under REQ-009 and will replace this form.

#### Scenario: Non-null camera and light both contribute
- **WHEN** a scene has a non-null camera with a non-null UBO and a non-null directional light with a non-null UBO, and `getSceneLevelResources()` is called
- **THEN** the returned vector contains exactly two elements, with the camera UBO at index 0 and the light UBO at index 1

#### Scenario: Null camera is skipped
- **WHEN** a scene has `camera == nullptr` and a non-null directional light, and `getSceneLevelResources()` is called
- **THEN** the returned vector contains exactly one element — the light UBO — at index 0

## MODIFIED Requirements

### Requirement: FrameGraph buildFromScene populates queues per pass
`FrameGraph::buildFromScene(const Scene &)` SHALL iterate every configured `FramePass` in the frame graph and for each pass SHALL call `pass.queue.buildFromScene(scene, pass.name)`. It SHALL NOT construct `RenderingItem`s itself and SHALL NOT call any method on `Scene` other than the getters consumed by `RenderQueue::buildFromScene`. Multiple calls in sequence are idempotent — each call rebuilds every queue from scratch.

#### Scenario: Populating a single forward pass
- **WHEN** `FrameGraph` contains exactly one `FramePass` with `name == Pass_Forward` and the scene contains one renderable whose pass mask includes `Forward`
- **THEN** after `buildFromScene(scene)`, that pass's `queue.getItems()` contains exactly one element whose `pass == Pass_Forward`

#### Scenario: Delegation to RenderQueue
- **WHEN** `FrameGraph::buildFromScene(scene)` is called
- **THEN** each `FramePass::queue.buildFromScene(scene, pass.name)` is called exactly once with the pass's name and the scene reference, and no per-renderable factory method on `Scene` is invoked

#### Scenario: Idempotent rebuild
- **WHEN** `FrameGraph::buildFromScene(scene)` is called twice on the same scene
- **THEN** every pass's queue contains the same items after the second call as after the first (items are not duplicated)
