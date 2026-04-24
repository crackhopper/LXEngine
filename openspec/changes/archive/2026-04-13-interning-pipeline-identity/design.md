## Context

Pipeline identity today flows through `PipelineKey::build(const ShaderProgramSet &, const Mesh &, const RenderState &, const SkeletonSharedPtr &)` in `src/core/resources/pipeline_key.cpp`. It produces a string of the form `"blinnphong_0|HAS_NORMAL_MAP|ml:0x3a2f1b7c|rs:0x7c1de4a0|sk:0x0"` by mixing `getPipelineHash()` from each resource. `StringID` wraps that string, and `Scene::buildRenderingItem()` sets it on the `RenderingItem` that's fed to `vk_resource_manager::getOrCreateRenderPipeline`.

Three concrete pain points the current scheme has:

1. **Debug opacity** — `toString(pipelineKey)` prints hex hashes. You can't tell if two pipelines differ because of a layout, a variant, or a cull mode.
2. **`Mesh::getPipelineHash()` opaqueness** — it `hash_combine`s layout + topology into a single `size_t`. If only topology changes, you can't observe that locally.
3. **No pass dimension** — the same mesh + material under Forward vs. Shadow currently collide into the same `PipelineKey`. REQ-003b's FrameGraph needs per-pass keys.

REQ-006 already landed in `change/extend-string-table-compose`: `GlobalStringTable` has `TypeTag` + `compose` + `decompose` + `toDebugString`. REQ-005 also already landed: `MaterialInstance` is `IMaterial`'s only implementation.

REQ-007 is the payoff. Every resource's pipeline contribution becomes a **structurally interned `StringID`**; `PipelineKey::build` is a two-argument `compose(TypeTag::PipelineKey, {objSig, matSig})`; `toDebugString` renders the full tree:

```
PipelineKey(
  ObjectRender(
    MeshRender(VertexLayout(0_inPos_Float3_Vertex_0, ..., 48), tri),
    Skn1),
  MaterialRender(
    RenderPassEntry(
      ShaderProgram(blinnphong_0, HAS_NORMAL_MAP),
      RenderState(Back, DepthTest, DepthWrite, LessEqual, NoBlend, One, Zero))))
```

## Goals / Non-Goals

**Goals:**

- Every leaf/aggregate resource that contributes to pipeline identity exposes a `getRenderSignature(...)` method returning `StringID`
- Pipeline identity is built bottom-up via `compose(TypeTag::<kind>, fields)` — no more `getPipelineHash()`
- `Pass_Forward` / `Pass_Deferred` / `Pass_Shadow` become `inline const` StringIDs in `src/core/scene/pass.hpp`
- `Scene::buildRenderingItem(StringID pass)` accepts a pass parameter; `RenderingItem.pass` stores it
- `PipelineKey::build(objSig, matSig)` replaces the old four-arg overload; old overload is **deleted**
- `MaterialTemplate::m_passes` key type migrates from `std::string` to `StringID`
- `toDebugString(key.id)` produces a human-readable pipeline tree suitable for logs and test assertions
- Every existing caller (`blinnphong_material_loader`, `test_material_instance`, `vk_renderer`, `test_vulkan_*`) is migrated in the same commit — no transitional shims

**Non-Goals:**

- Not changing `vk_resource_manager::getOrCreateRenderPipeline` lookup semantics (it still hashes `PipelineKey::id`)
- Not introducing FrameGraph / RenderQueue — that's REQ-003b
- Not changing `ShaderProgramSet::getHash()` semantics — that map key is still needed internally
- Not implementing per-pass mesh attribute culling — `Mesh::getRenderSignature(pass)` currently ignores `pass`
- Not re-writing `docs/requirements/finished/001-*.md` or `.../002-*.md` — only adding the top-line "Superseded by REQ-007" banner

## Decisions

### Decision 1: Introduce a new `render-signature` capability rather than extending `pipeline-key`

**Choice**: Create `openspec/specs/render-signature/` to own the `getRenderSignature(...)` contracts across resources. `pipeline-key` gets MODIFIED to describe the new `PipelineKey::build(StringID, StringID)` signature and the `pass` field on `RenderingItem`. `resource-pipeline-hash` gets its requirements REMOVED (all five of them) and `skeleton-resource` gets the skeleton-specific identity requirement MODIFIED.

**Alternatives**:
- (a) Extend `pipeline-key` to host all signature requirements. Rejected — pipeline-key would balloon with per-resource contracts that are logically separate from the key-construction contract.
- (b) Make `render-signature` a delta on `string-interning`. Rejected — `string-interning` is about generic table/compose mechanics; per-resource signature contracts don't belong there.

**Rationale**: Separation of concerns. `string-interning` owns the table mechanics, `render-signature` owns the per-resource contract, `pipeline-key` owns the assembly. Archiving REQ-007 later cleanly updates three specs without cross-pollution.

### Decision 2: `Mesh::getRenderSignature(StringID pass)` takes `pass` but ignores it today

**Choice**: Preserve the `pass` parameter in the signature even though the implementation only composes `vertexLayout + topology`, never touching `pass`.

**Alternatives**:
- (a) Omit `pass` from `Mesh::getRenderSignature()` (no arg). Rejected — forces callers at `RenderableSubMesh::getRenderSignature(pass)` to special-case Mesh vs. Material.
- (b) Have `Mesh::getRenderSignature(pass)` actually bake `pass` into the compose tree. Rejected — it's a no-op today and would bloat pipeline cache with semantically equivalent entries.

**Rationale**: Uniformity at the call site. Future "mesh attribute culling per pass" can drop the attribute before compose without breaking the signature.

### Decision 3: `Skeleton::getRenderSignature()` returns a fixed `Intern("Skn1")` regardless of bone count

**Choice**: Skinning is currently a boolean concern — either a `Skeleton` participates or it doesn't. `Skn1` is just a token that says "skinning on." Absence of a skeleton is represented by `StringID{}` (id 0) at the `RenderableSubMesh` call site, not by a different skeleton signature.

**Rationale**: The vertex shader cares about "has bones in the layout" vs. "no bones." It doesn't care about bone *count* at pipeline compile time (count is a UBO-side concern bounded by `MAX_BONE_COUNT`). Matches the spirit of REQ-001's `kSkeletonPipelineHashTag`, reimagined as an interned string.

### Decision 4: `MaterialTemplate::m_passes` uses `unordered_map<StringID, RenderPassEntry, StringID::Hash>`

**Choice**: Migrate the map key from `std::string` to `StringID`. `setPass(StringID, RenderPassEntry)` replaces `setPass(const std::string &, ...)`.

**Migration cost**: Two call sites — `blinnphong_material_loader.cpp:60` and `test_material_instance.cpp:73`, both of which pass literal `"Forward"` and get replaced by `Pass_Forward`.

**Rationale**: Consistency with REQ-007's direction. Once `pass` is a `StringID` at the Scene/Renderable layer, the MaterialTemplate lookup should be too. Saves one string hash per `getEntry` call.

### Decision 5: `PrimitiveTopology` uses a free function `topologySignature`, not a member

**Choice**: Enum class can't carry methods. Add `StringID topologySignature(PrimitiveTopology)` as a free function in `index_buffer.hpp` (inline) or a new `index_buffer.cpp` (out-of-line). Implementation is a `switch` returning `Intern("tri") / Intern("line") / ...`.

**Alternatives**:
- (a) Inline in header. Acceptable; `GlobalStringTable` is already included via `string_table.hpp` transitively.
- (b) New `index_buffer.cpp`. Matches the REQ-007 document's intent but adds a TU.

**Pick**: Inline in header, keeping `index_buffer.{hpp}` header-only since it already is. New TU would force a cmake globre-run for little benefit. (If out-of-line ends up cleaner because of include-order issues, we can promote it.)

### Decision 6: `IMaterial` / `IRenderable` add a non-default pure virtual

`virtual StringID getRenderSignature(StringID pass) const = 0` on both. All concrete implementations (`MaterialInstance`, `RenderableSubMesh`) must override or the build breaks. There's exactly one concrete implementation of each today per REQ-005 and the current renderable topology, so the migration is bounded.

### Decision 7: Variant signature — sort before compose

`ShaderProgramSet::getRenderSignature()` collects `enabled` variants' `macroName`, **sorts them lexically**, then composes. This mirrors the existing `ShaderProgramSet::getHash()` sort-before-hash logic and is necessary because `compose` is order-sensitive but variant declaration order in source files is arbitrary.

### Decision 8: `toString(enum)` helpers for `CullMode` / `CompareOp` / `BlendFactor` / `DataType` / `VertexInputRate`

Small free functions (or static methods) that turn each enum into a stable tag string (`"Back"`, `"LessEqual"`, `"SrcAlpha"`, `"Float3"`, `"Instance"`). Placed next to the enum declarations in `material.hpp` / `vertex_buffer.hpp`. Used by `getRenderSignature()` to produce leaf strings via `Intern`.

### Decision 9: Retention of `getHash()` / `getLayoutHash()` — non-deletion

Those methods continue to exist because they still key internal `unordered_map`s (`VertexFactory`'s layout-hash map, `ShaderProgramSet::operator==`). REQ-007's deletions are limited to the `getPipelineHash()` family (which were specifically introduced by REQ-001/002 for pipeline identity).

### Decision 10: Archive banner in `finished/001-*.md` and `.../002-*.md`

Add exactly one blockquote at the very top:

```markdown
> **Superseded by REQ-007** — R6/R7 (001) and R3/R4 (002) about `getPipelineHash()` / `PipelineKey::build()` signatures are replaced by REQ-007 "Structured Interning Pipeline Identity." Historical context retained; implementation follows REQ-007.
```

Rule: **do not touch the body**. This is the only allowed modification to `finished/`.

## Risks / Trade-offs

- **[Risk] String explosion in `GlobalStringTable`** — every `VertexLayoutItem` becomes a leaf string like `"0_inPos_Float3_Vertex_0"`. For common vertex types this caps around 10–20 strings per vertex format. Add perhaps 5–10 new vertex formats × 8 items = 160 strings in the worst case, well under the current table's capacity.
  **Mitigation**: none needed. Monitored via `GlobalStringTable` internal counters if they ever surface.

- **[Risk] Signature divergence from behavior** — `RenderState::getRenderSignature` must cover every field that affects pipeline compilation. If a new field is added to `RenderState` and its `getRenderSignature` is not updated, two distinct configurations can collapse to the same key.
  **Mitigation**: Add a code-review checklist item. Consider a `static_assert(sizeof(RenderState) == X)` tripwire in `material.cpp` alongside the signature implementation; bumping the struct forces a visit to the signature impl.

- **[Risk] `PipelineKey::build` compile-break in downstream call sites** — removing the old overload means every caller (`scene.cpp`, possibly tests) must be migrated atomically in the same change. Miss one → build breaks.
  **Mitigation**: `grep -rn "PipelineKey::build"` before committing. Current callers are exactly `scene.cpp:17` and `pipeline_key.cpp:28` (the definition). Manageable.

- **[Trade-off] `m_passes` key migration is a mini-breaking change** — call sites using `setPass("Forward", ...)` break. Two known sites.
  **Mitigation**: migrate them in the same change; verify with `grep -n 'setPass(' src/`.

- **[Trade-off] Pass parameter threads through `IRenderable::getRenderSignature`** — adds a virtual dispatch argument that today's implementation ignores for `Mesh` but is architecturally significant.
  **Mitigation**: The API ergonomics are worth it. REQ-003b will immediately consume `pass` in `FrameGraph::buildFromScene()`.

- **[Trade-off] `toString(enum)` introduces duplication with any existing name-to-enum switch** — if `CullMode` already has a stringifier elsewhere, we'd have two.
  **Mitigation**: `grep` first; if a stringifier exists, reuse it. If not, the new one lives next to the enum and is the single source of truth.

- **[Risk] `variantSegment()` removal misses callers** — the private helper in `pipeline_key.cpp` may be referenced from elsewhere.
  **Mitigation**: it's in an anonymous namespace, so it can't be. Safe to delete.

## Migration Plan

1. **Foundation**: new header `src/core/scene/pass.hpp` with `Pass_Forward/Deferred/Shadow` constants
2. **Leaf signatures**: `VertexLayoutItem/VertexLayout::getRenderSignature()`, `topologySignature(PrimitiveTopology)`, `toString(enum)` helpers
3. **Mid-level signatures**: `Mesh::getRenderSignature(pass)`, `Skeleton::getRenderSignature()`, `ShaderProgramSet::getRenderSignature()`, `RenderState::getRenderSignature()`, `RenderPassEntry::getRenderSignature()`
4. **Template/material**: `MaterialTemplate::m_passes` key migration, `setPass(StringID, ...)`, `getRenderPassSignature(StringID)`; `IMaterial::getRenderSignature(pass)` pure virtual, `MaterialInstance` override
5. **Renderable**: `IRenderable::getRenderSignature(pass)` pure virtual, `RenderableSubMesh` override
6. **Pipeline key rewrite**: `PipelineKey::build(StringID, StringID)` replaces old overload; `pipeline_key.cpp` shrinks to a one-liner
7. **Scene**: `Scene::buildRenderingItem(StringID pass)`, `RenderingItem.pass` field
8. **Deletions**: five `getPipelineHash()` methods + `kSkeletonPipelineHashTag` + `variantSegment()`
9. **Caller migration**: `blinnphong_material_loader`, `test_material_instance`, `vk_renderer`, three `test_vulkan_*`
10. **Archive banner**: `finished/001-*.md`, `.../002-*.md`
11. **Test**: `test_pipeline_identity.cpp` covering equality, topology sensitivity, variant sensitivity, skeleton presence, pass differentiation
12. **Regression**: full cmake build + ctest (including test_render_triangle end-to-end)

**Rollback strategy**: This is a destructive rename + restructure. `git revert` the single commit. No data migration, no persisted state.

## Open Questions

- Should `RenderingItem.pass == StringID{}` be allowed (i.e. pass-less rendering)? Current answer: yes for backward-compat during the transition, since backend doesn't inspect `item.pass`. FrameGraph (REQ-003b) will make it mandatory.
- Should `test_pipeline_identity.cpp` also verify `toDebugString()` output literally, or just structural equality? Start with structural equality (compose id comparisons) + a single `toDebugString` smoke assertion. Full string-literal assertions are brittle.
- Is `Pass_Forward` enough, or should we also add `Pass_Opaque` / `Pass_Transparent` / `Pass_UI`? Defer to REQ-003b; REQ-007 only introduces the three mandatory ones.
