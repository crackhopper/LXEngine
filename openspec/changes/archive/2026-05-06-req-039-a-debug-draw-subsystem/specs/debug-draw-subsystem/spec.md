## ADDED Requirements

### Requirement: DebugDraw exposes one-line world-space primitive helpers
The system SHALL provide a `DebugDraw` public API for transient world-space debug visualization. The API SHALL include helpers for line, triangle-edge, wire sphere, wire circle, axis-aligned and oriented wire box, cone, arrow, axis, and frustum drawing. All calls SHALL return immediately and SHALL append geometry into internal per-frame accumulation state instead of performing synchronous rendering work.

#### Scenario: Drawing a line only appends debug geometry
- **WHEN** caller code invokes `DebugDraw::drawLine(a, b, color)`
- **THEN** the call returns without blocking on GPU work
- **AND** the subsystem stores two line vertices in its current-frame accumulation state

### Requirement: DebugDraw provides built-in color constants and scoped layer override
The system SHALL expose built-in color helpers for common debug colors and SHALL provide a scope object that temporarily overrides the visibility layer mask used for newly emitted debug geometry. The default layer for debug output SHALL be the editor-overlay visibility layer.

#### Scenario: Scoped layer override restores the previous mask
- **WHEN** code enters a `LayerScope` with mask `Layer_All`, emits debug geometry, and leaves the scope
- **THEN** geometry emitted inside the scope uses `Layer_All`
- **AND** subsequent geometry uses the prior default mask again

### Requirement: DebugDraw batches and flushes once per frame
The system SHALL expose explicit `beginFrame()` and `endFrame()` lifecycle hooks. `beginFrame()` SHALL clear the previous frame's accumulated geometry. `endFrame()` SHALL publish the current frame's line data through one transient renderable path that targets the debug overlay pass and uses the current debug visibility mask.

#### Scenario: Frame reset removes prior debug geometry
- **WHEN** one frame accumulates debug lines, calls `endFrame()`, then the next frame calls `beginFrame()` before any new draws
- **THEN** the next frame starts with an empty accumulation buffer

### Requirement: DebugDraw enforces a per-frame line limit
The system SHALL enforce a default maximum of 100000 accepted lines per frame. Once that limit is reached, additional line submissions for the same frame SHALL be dropped and the system SHALL emit at most one warning for that frame.

#### Scenario: Over-limit line submissions are clipped
- **WHEN** caller code submits more than 100000 lines during one frame
- **THEN** only the first 100000 lines are retained for that frame
- **AND** the subsystem emits one warning about the clipped debug output

### Requirement: DebugDraw uses a dedicated overlay pass contract
The flushed debug renderable path SHALL target a dedicated `Pass_DebugOverlay` pass and SHALL use a line-list pipeline contract with position and color vertex attributes. The debug-line shaders SHALL transform world-space positions by the camera view-projection matrix and output vertex color directly. Debug overlay rendering SHALL enable depth testing, disable depth writes, and enable alpha blending.

#### Scenario: Debug overlay geometry uses the unlit line contract
- **WHEN** `endFrame()` publishes accumulated debug geometry
- **THEN** the resulting renderable participates only in `Pass_DebugOverlay`
- **AND** its pipeline contract uses line-list topology with world-space position plus color vertex data
