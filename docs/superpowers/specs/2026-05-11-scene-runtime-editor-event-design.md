# Scene Runtime And Editor Event Design

Date: 2026-05-11

## Goal

Introduce a layered event mechanism that fixes stale editor UI after scene-node state changes and establishes a reusable runtime event foundation for future gameplay systems.

The immediate bug is:

- a command such as `move /helmet 0 1 0` mutates the selected node
- the scene node actually moves
- editor UI state such as the inspector draft does not update because it only resyncs on selection-path changes

The design must therefore:

- emit events from the real scene-state write points, not only from editor commands
- keep runtime scene events distinct from editor-session events
- support synchronous in-process listeners for same-frame UI consistency
- support queued mirroring for API / WebSocket / MCP consumers

## Context

Current repository facts:

- `SceneNode` mutators such as `setLocalTransform`, `setTranslation`, `setRotation`, `setScale`, `setName`, `setParent`, and `clearParent` update internal state directly in `src/core/scene/object.cpp`
- `InspectorPanel` in `src/core/editor/inspector_panel.cpp` only calls `syncDraftFromSnapshot()` when the selected path changes
- `LxeEditorApiService` currently derives API events mostly by observing command history and diffing captured editor state, not by subscribing to engine-level scene change notifications
- existing events are effectively editor/API-facing events, not a reusable runtime scene event layer

## Non-Goals

This first design explicitly does not introduce:

- a cross-thread engine-wide message bus
- gameplay scripting integration
- network replication
- persisted event logs
- full before/after object snapshots in each event
- a full refactor of every editor panel

## Design Summary

The system is split into two domains:

- `Runtime`: world and scene-graph facts that gameplay/runtime code and editor code can both observe
- `Editor`: editor-session facts such as selection, preview mode, toolbar mode, and document dirty state

The first implementation adds a runtime scene event hub owned by `Scene`. `SceneNode` and `Scene` emit runtime events from their real mutation points. Editor UI and API layers subscribe to that hub. API layers may mirror synchronous runtime events into their own buffered event queues.

This preserves a clean boundary:

- runtime events describe scene changes
- editor events describe editor session changes

## Architecture

### Core objects

Add a new scene event subsystem under `src/core/scene/`, for example:

- `scene_events.hpp`
- `scene_events.cpp`

Recommended core types:

- `enum class SceneEventDomain { Runtime, Editor }`
- `enum class SceneEventType { SceneNodeChanged, SceneNodeAdded, SceneNodeRemoved }`
- `enum class SceneNodeAspect { Transform, Identity, Hierarchy, Visibility, RenderableStructure }`
- `struct SceneEvent`
- `class SceneEventHub`
- `class SceneEventSubscription`

`Scene` owns one `SceneEventHub`.

`SceneNode` does not own listeners or a separate hub. When attached to a scene, it emits through the owning scene hub. Detached nodes keep working as standalone objects but do not broadcast scene runtime events.

### Why hub ownership belongs to Scene

Owning the hub at scene scope avoids fragmented subscription management.

Benefits:

- listeners can subscribe once to the current scene instead of wiring every node
- hierarchy changes, rename changes, and node replacement stay observable through one stable entry point
- runtime and editor code share the same scene-level subscription surface

## Event Model

### Event envelope

`SceneEvent` should be a compact semantic envelope rather than a full state snapshot.

Recommended fields:

- `SceneEventDomain domain`
- `SceneEventType type`
- `u64 sequence`
- `std::string path`
- `std::string stableNodeName`
- `std::vector<SceneNodeAspect> aspects`

This design intentionally keeps payloads moderate:

- enough information to explain what changed
- not so much information that the schema becomes coupled to one consumer

### Runtime events

First-version runtime events:

- `SceneNodeChanged`
- `SceneNodeAdded`
- `SceneNodeRemoved`

First-version aspects for `SceneNodeChanged`:

- `Transform`
- `Identity`
- `Hierarchy`
- `Visibility`
- `RenderableStructure`

Examples:

- `move /helmet 0 1 0` emits `Runtime + SceneNodeChanged + /helmet + [Transform]`
- `set /helmet.name visor` emits `Runtime + SceneNodeChanged + /helmet + [Identity]`
- `setParent()` emits `Runtime + SceneNodeChanged + <path> + [Hierarchy]`
- component add/remove or equivalent structural renderable changes emit `Runtime + SceneNodeChanged + <path> + [RenderableStructure]`

### Editor events stay separate

Editor-session events remain editor-specific and are not pushed down into runtime domain.

Examples:

- `SelectionChanged`
- `PreviewChanged`
- `ModeChanged`
- `DirtyChanged`

Rationale:

- selection is editor UI state, not world state
- preview mode is editor camera/session state, not runtime scene state
- mixing these into runtime would make gameplay-facing consumers depend on editor concepts

## Mutation Sources And Emission Points

The event system is bound to real state write points instead of command wrappers.

First-version emission points in `SceneNode`:

- `setLocalTransform`
- `setTranslation`
- `setRotation`
- `setScale`
- `setName`
- `setParent`
- `clearParent`
- `setVisibilityLayerMask`

First-version emission points in `Scene`:

- `addRenderable`
- `removeRenderable`
- `addCamera`
- `removeCamera`

Future expansion may also include:

- component add/remove
- camera component field mutations
- light-related scene-node property mutations

This write-point binding guarantees consistent behavior for:

- command bus mutations
- gizmo-driven edits
- direct engine/runtime code mutations
- future gameplay systems that bypass editor commands

## Delivery Model

### In-process delivery

`SceneEventHub` uses synchronous in-process dispatch.

Rules:

- listeners subscribe through a handle object
- the handle unregisters automatically on destruction
- `emit(...)` synchronously invokes matching listeners
- listeners receive a read-only event object

This matches current C++ and RAII constraints in the repo and solves the UI freshness problem without adding a frame-later dependency.

### Transport mirroring

External transports should not become the primary bus. Instead:

- runtime mutations synchronously emit scene events
- editor/API layers subscribe to those scene events
- API layers enqueue mirrored transport events for polling / streaming clients

This gives the requested hybrid model:

- synchronous for in-process UI
- queued for HTTP/WebSocket/MCP-style consumers

### Threading

First version is single-threaded and main-thread-oriented.

No cross-thread safety is promised in this design. If the engine later needs cross-thread event delivery, that should be a separate requirement and should not complicate this first implementation.

## Subscriber Behavior

### InspectorPanel

`InspectorPanel` should subscribe to runtime scene events rather than infer external changes indirectly from selection-path changes.

Recommended behavior:

- when a relevant event targets the currently selected node and contains relevant aspects, mark the panel snapshot/draft as stale
- on the next safe `draw()` cycle, rebuild the snapshot and resync draft state

Relevant aspects for inspector refresh:

- `Transform`
- `Identity`
- `Visibility`
- `Hierarchy` when the path display or path-dependent lookups matter
- `RenderableStructure` when camera/material/mesh/skeleton availability in the inspector can change

Important rule:

- the event callback should mark dirty
- the UI should resync during `draw()`

This avoids overloading event callbacks with direct ImGui state mutations and reduces re-entrancy risk.

### LxeEditorApiService

`LxeEditorApiService` should subscribe to runtime scene events and mirror them into the API event stream.

It may continue using state-diff logic for editor-session events such as:

- selection
- preview
- mode
- dirty

But runtime scene-node changes should no longer depend only on command history or coarse state diffing.

This enables external clients to receive true node-change notifications rather than guessing from unrelated state changes.

## Subscription Lifecycle

The system should use RAII subscriptions.

Recommended contract:

- `subscribe(...)` returns a subscription object
- destroying that object automatically unregisters the listener
- no fragile manual unregister call is required for normal ownership paths

This reduces lifetime bugs for UI panels, API services, and scene-session helpers.

## Re-entrancy And Safety Constraints

The first implementation should keep rules simple:

- listener code must treat incoming events as notifications, not as a place for complex scene mutation pipelines
- nested emits may happen, but the hub implementation must tolerate listener self-removal safely
- listener collections must remain safe if a subscription is destroyed during dispatch

The design does not forbid all nested mutation, but it also does not attempt to define a complex transactional event model in version one.

## Path And Identity Considerations

`path` alone is not enough as a long-term identity because rename and reparent operations can change it.

Therefore each event should include both:

- current `path`
- stable `nodeName`

This does not create a globally stable persistent GUID system. It only preserves better diagnostic and lookup information for the current architecture.

If a true persistent node identity is needed later, that should be a separate capability.

## Integration Plan Boundary

The first implementation scope should cover:

- scene event type definitions
- scene-owned synchronous runtime event hub
- runtime event emission from scene-node mutation points
- inspector subscription and stale-draft fix
- API service subscription and runtime-event mirroring
- tests for event emission, inspector refresh, and API event forwarding

The first implementation should not expand into:

- generic gameplay scripting buses
- inter-scene routing
- network transports redesign
- event persistence
- broad editor-wide refactors beyond affected consumers

## Testing Strategy

At minimum, add tests in three categories.

### 1. Scene event emission tests

Verify that direct mutations emit the correct semantic event:

- `setTranslation` -> `SceneNodeChanged` with `Transform`
- `setName` -> `SceneNodeChanged` with `Identity`
- `setParent` / `clearParent` -> `SceneNodeChanged` with `Hierarchy`

Assertions should cover:

- domain
- type
- path
- aspects

### 2. Inspector regression test

Reproduce the real bug:

- create a selected node
- build an `InspectorPanel`
- mutate the node through a command or direct scene-node API
- verify that the inspector’s next safe refresh uses updated node state rather than stale draft values

This is the regression that justifies the feature.

### 3. API service runtime-event test

Execute a transform-changing operation and verify that:

- `CommandExecuted` still appears when applicable
- a runtime scene-node event also appears

This proves the runtime event system is not trapped inside local UI-only behavior.

## Trade-off Summary

### Rejected approach: command-driven events only

This would be smaller initially but would fail for direct runtime writes and would force a second redesign later.

### Rejected approach: listener lists on every SceneNode

This would cover write points but would fragment subscriptions and couple broad scene observers to per-node wiring.

### Chosen approach: scene-owned layered hub

This best matches the requested long-term direction:

- runtime events reusable by gameplay and editor
- editor events remain separate
- synchronous same-process updates
- queued transport mirroring

## OpenSpec / Requirements Follow-up

Implementation work after this spec should create a concrete plan that breaks the change into small steps:

- add event types and hub
- wire `Scene` ownership
- emit from scene-node mutators
- subscribe inspector
- subscribe API service
- add tests

That plan should be written before implementation begins.
