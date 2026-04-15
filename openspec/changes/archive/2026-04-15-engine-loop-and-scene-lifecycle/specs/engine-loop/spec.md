## ADDED Requirements

### Requirement: EngineLoop orchestrates renderer lifecycle above the backend
The system SHALL provide an `EngineLoop` runtime abstraction above `gpu::Renderer`. `EngineLoop` SHALL coordinate a `Window`, a `Renderer`, a `Clock`, and the active `Scene` without moving frame-graph or Vulkan-specific execution policy into the backend.

#### Scenario: EngineLoop initialized with window and renderer
- **WHEN** application code constructs an `EngineLoop` and calls `initialize(window, renderer)`
- **THEN** the loop stores the provided runtime dependencies and becomes ready to start a scene

### Requirement: EngineLoop starts a scene explicitly
`EngineLoop` SHALL provide an explicit scene-start entry point that is separate from per-frame execution. Starting a scene SHALL hand the scene to the renderer through `renderer->initScene(scene)` exactly once per scene start or rebuild.

#### Scenario: Scene startup triggers renderer initialization
- **WHEN** `startScene(scene)` is called on an initialized `EngineLoop`
- **THEN** the active scene becomes `scene`
- **AND** `renderer->initScene(scene)` is invoked before any subsequent frame is executed

#### Scenario: Scene startup is not part of the frame loop
- **WHEN** a scene has already been started and `tickFrame()` is called repeatedly
- **THEN** `renderer->initScene(scene)` is not re-invoked unless an explicit rebuild/start action occurs

### Requirement: EngineLoop runs a deterministic per-frame order
For each frame, `EngineLoop` SHALL execute the runtime phases in this order:
1. `Clock::tick()`
2. user update hook, if registered
3. `renderer->uploadData()`
4. `renderer->draw()`

No renderer upload or draw call SHALL happen before `Clock::tick()` for that frame.

#### Scenario: Update hook runs before upload and draw
- **WHEN** a frame is executed through `tickFrame()`
- **THEN** the update hook observes the current frame's clock values before `renderer->uploadData()` is called
- **AND** `renderer->draw()` occurs after `renderer->uploadData()`

### Requirement: EngineLoop exposes a per-frame update hook
`EngineLoop` SHALL allow application code to register a per-frame update callback. The callback SHALL receive access to the active scene and current clock state so it can mutate CPU-side scene data before upload.

#### Scenario: Hook mutates scene state before dirty upload
- **WHEN** an update hook modifies camera matrices or material/light CPU-side values and marks the affected resources dirty
- **THEN** those mutations occur before `renderer->uploadData()` in the same frame

### Requirement: EngineLoop maintains Clock as frame-time source
`EngineLoop` SHALL own or coordinate a `Clock` instance and expose read access to the current clock state for runtime consumers such as debug UI or application hooks.

#### Scenario: Debug consumer reads clock from EngineLoop
- **WHEN** UI code queries `EngineLoop` for the current clock after one or more frames have run
- **THEN** it can read `deltaTime`, `totalTime`, or equivalent frame-time values from the loop-owned clock

### Requirement: EngineLoop supports stop and run control
`EngineLoop` SHALL provide a convenience `run()` mode that repeatedly executes frames until the loop is stopped. The loop SHALL stop when `stop()` is called or when the application/window close condition is met.

#### Scenario: Explicit stop exits run loop
- **WHEN** `run()` is active and `stop()` is called
- **THEN** the run loop exits without executing additional frames afterward

#### Scenario: Window close exits run loop
- **WHEN** `run()` is active and the window reports a close request
- **THEN** the run loop exits without requiring application code to hand-roll a separate while-loop

### Requirement: EngineLoop separates dirty updates from structural scene rebuilds
`EngineLoop` SHALL distinguish between per-frame data mutation and structural scene changes. Structural changes that affect renderer initialization products, such as render queues or preloaded pipelines, SHALL require an explicit rebuild/start action rather than being implicitly recomputed every frame.

Structural changes include at minimum:
- adding or removing renderables
- changing pass participation
- changing shader/material structure in a way that alters pipeline identity
- changing camera target routing that affects scene-level resource selection

#### Scenario: Dirty-only update does not rebuild the scene
- **WHEN** the update hook only changes camera transforms or light/material parameter values
- **THEN** `tickFrame()` performs upload and draw for that frame without re-running scene initialization

#### Scenario: Structural change requires explicit rebuild
- **WHEN** application code adds a new renderable to the active scene
- **THEN** the runtime requires an explicit rebuild/start action before the renderer is expected to consume the new scene structure
