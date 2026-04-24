## ADDED Requirements

### Requirement: passFlagFromStringID helper translates pass identity to pass flag
The header `src/core/scene/pass.hpp` SHALL declare `LX_core::ResourcePassFlag passFlagFromStringID(StringID pass)` and the implementation SHALL live in a new `src/core/scene/pass.cpp` translation unit. The function SHALL map:
- `Pass_Forward` → `ResourcePassFlag::Forward`
- `Pass_Deferred` → `ResourcePassFlag::Deferred`
- `Pass_Shadow` → `ResourcePassFlag::Shadow`
- Any other `StringID` → `ResourcePassFlag{0}` (all bits clear)

This helper bridges the scene-layer pass identity (`StringID`) and the resource-layer pass flag (`ResourcePassFlag`). Core-layer code SHALL use this helper rather than hard-coded string comparisons when translating a pass `StringID` to a flag.

#### Scenario: Known pass constants map to the matching flag bit
- **WHEN** `passFlagFromStringID(Pass_Forward)` is called
- **THEN** the returned `ResourcePassFlag` equals `ResourcePassFlag::Forward`, and similarly for `Pass_Deferred` and `Pass_Shadow`

#### Scenario: Unknown StringID yields zero flag
- **WHEN** `passFlagFromStringID(Intern("CustomPass"))` is called for a pass identifier that is none of the three known constants
- **THEN** the returned `ResourcePassFlag` equals `ResourcePassFlag{0}` (no bits set)

## REMOVED Requirements

### Requirement: Scene::buildRenderingItem accepts a pass parameter
**Reason**: `Scene::buildRenderingItem(StringID pass)` and its per-renderable sibling `Scene::buildRenderingItemForRenderable(const IRenderableSharedPtr &, StringID pass)` are deleted. `RenderingItem` construction — including the `PipelineKey::build(sub->getRenderSignature(pass), sub->material->getRenderSignature(pass))` computation — moves into `RenderQueue::buildFromScene(scene, pass)` (see the `frame-graph` capability delta in this change). The pipeline-identity invariant is preserved; only the host of the construction code changes.

**Migration**: Callers that previously wrote `auto item = scene->buildRenderingItem(Pass_Forward);` SHALL now go through `RenderQueue`:

```cpp
LX_core::RenderQueue q;
q.buildFromScene(*scene, Pass_Forward);
auto &item = q.getItems().front();  // or iterate
```

Integration tests MAY use the new `src/test/integration/scene_test_helpers.hpp` helper `firstItemFromScene(scene, pass)`, which wraps this pattern with an assertion that the queue is non-empty.
