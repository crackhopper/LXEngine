## MODIFIED Requirements

### Requirement: RenderQueue builds items from a Scene per pass
`LX_core::RenderQueue::buildFromScene(const Scene &scene, StringID pass, const RenderTarget &target)` SHALL construct the queue's `RenderingItem` set from the scene. The method SHALL:
1. Call `clearItems()`.
2. Retrieve `scene.getSceneLevelResources(pass, target)` once before iterating renderables.
3. For each `IRenderableSharedPtr` in `scene.getRenderables()`, skip null pointers and skip renderables for which `renderable->supportsPass(pass)` returns `false`.
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
