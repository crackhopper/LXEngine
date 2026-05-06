## Context

`SceneNode` is the engine's primary high-level renderable, but its structural payload is still encoded as dedicated `mesh`, `materialInstance`, and optional `skeleton` fields plus matching setters. That shape works for the current renderable path, yet it forces every future attachable scene concern to become a new bespoke `SceneNode` field or side channel. The next editor-facing changes need a uniform node-local payload model without losing the current validation, pass-cache, and scene-level material propagation behavior.

Current repository facts also matter:

- `Skeleton` is already a core resource in `src/core/asset/` and must stay there.
- `scene-node-validation` currently treats mesh/material as required structural inputs and rebuilds validation eagerly.
- `skeleton-resource` currently bans scene-component infrastructure entirely, so this change must narrow that ban rather than regress skeleton-resource ownership boundaries.

## Goals / Non-Goals

**Goals:**

- Give `SceneNode` a uniform component container for structural scene payloads.
- Preserve current renderable validation semantics, pass-cache behavior, and scene-level shared-material revalidation.
- Migrate mesh/material/skeleton access to typed components without changing `Mesh`, `MaterialInstance`, or `Skeleton` resource APIs.
- Establish a foundation that future node-attached data such as camera can reuse.

**Non-Goals:**

- Introducing ECS archetypes, systems, queries, or serialization.
- Turning `Skeleton` itself into a scene component base type.
- Changing transform hierarchy, scene back-reference, or render-queue ownership models.
- Supporting multiple components of the same type on one node in this iteration.

## Decisions

### Decision: introduce a new `scene-node-components` capability instead of overloading `scene-node-validation`

Component storage and typed lookup are a new contract, not just a validation detail. A dedicated capability keeps the reusable node-component surface explicit, while `scene-node-validation` remains focused on renderable legality and cache behavior.

Alternatives considered:

- Only modify `scene-node-validation`: rejected because it hides the reusable component contract inside a rendering spec.
- Reuse `skeleton-resource` as the component home: rejected because that spec governs skeleton resource behavior, not generic node payload composition.

### Decision: keep component storage on `SceneNode` as `std::vector<std::unique_ptr<IComponent>>` with one instance per type

The expected per-node component count is small, and linear lookup keeps the first iteration simple while preserving deterministic iteration order for editor inspectors. One instance per concrete type avoids policy ambiguity for renderable structural data and matches current assumptions that a node has at most one mesh/material/skeleton payload.

Alternatives considered:

- `unordered_map<ComponentTypeId, unique_ptr<IComponent>>`: rejected because it adds indirection and loses stable insertion order without solving a demonstrated performance issue.
- Allow duplicate component types from the start: rejected because the validation/render path assumes a single structural mesh/material/skeleton tuple.

### Decision: keep `Skeleton` as a resource and wrap it with `SkeletonComponent`

This preserves the current asset-layer boundary and existing skeleton APIs while still letting node structure move to a component model. The component owns only node attachment semantics; the resource keeps owning bones, UBO data, and pass-flag behavior.

Alternatives considered:

- Make `Skeleton` inherit `IComponent`: rejected because it collapses asset and scene responsibilities and conflicts with current skeleton-resource direction.
- Keep skeleton as a dedicated optional field while only componentizing mesh/material: rejected because it would preserve the split model this change is trying to remove.

### Decision: component ownership back-reference is non-owning and owner-safe

Components need owner access for cross-component queries and node-triggered revalidation, but the owner relationship is already guaranteed by `SceneNode` ownership of component objects. The component contract should therefore expose a non-owning owner handle with defined attach/detach semantics rather than a second ownership edge.

Alternatives considered:

- Shared ownership from component back to node: rejected because it creates cycles and fights current `SceneNode` lifetime.
- No owner back-reference: rejected because it pushes cross-component coordination into ad hoc helpers and weakens the future camera/editor model.

### Decision: preserve eager structural validation, but move structural mutations behind component-aware APIs

The current renderer assumes `SceneNode` is either structurally valid or fails immediately. That contract stays. The migration changes where mesh/material/skeleton come from, not when the node becomes valid. Construction shifts to `SceneNode(nodeName)` followed by component attachment, and validation is rebuilt once the node has the required structural components.

Alternatives considered:

- Delay all validation until scene insertion: rejected because it breaks the current independent-node contract.
- Keep deprecated mesh/material/skeleton setters as compatibility shims: rejected because it prolongs the old model and complicates future camera-on-node work.

### Decision: move material pass-listener lifecycle behind the material component

The pass-listener is logically tied to the active material binding, not to unrelated node fields. Moving that lifecycle into `MaterialComponent` localizes the ownership rule while preserving the observable requirement that pass enable/disable changes force revalidation of referencing nodes.

Alternatives considered:

- Leave listener bookkeeping in `SceneNode`: rejected because it leaves the node partially coupled to removed dedicated material state.

## Risks / Trade-offs

- `Source migration breadth` → Many call sites currently depend on constructor injection or direct getters/setters. Mitigation: make tasks explicitly cover demos, infra helpers, and integration tests in one migration wave.
- `Transient invalid node states during construction` → `SceneNode(nodeName)` plus incremental component attachment can create partially populated nodes. Mitigation: specify when validation is required and keep renderable-path queries fatal or false until required structural components exist.
- `Component API drift versus current specs` → Existing specs still encode field-based construction. Mitigation: ship delta specs for `scene-node-validation` and `skeleton-resource` in the same proposal.
- `Future overreach into ECS` → Component terminology can invite broader architecture creep. Mitigation: keep spec language explicit that this is node-local typed composition only, not a general ECS.
