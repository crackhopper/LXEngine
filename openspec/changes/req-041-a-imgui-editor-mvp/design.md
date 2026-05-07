## Context

REQ-041-a is broad, so implementation should land in bounded, testable slices. The safest first slice is SceneTreePanel because it depends only on Scene path lookup + EditorState + CommandBus and can be exercised with a CPU-only ImGui context.

## Decisions

### 1. Split the editor MVP into panel/overlay modules

Keep editor UI responsibilities separated:
- `SceneTreePanel`: hierarchy browsing, path jump, selection/remove dispatch
- `InspectorPanel`: selected-node field inspection/edit submission
- `ViewportOverlay`: gizmo + picking + debug-draw orchestration
- existing `ConsolePanel`: reused as-is from REQ-040

This keeps each surface independently testable and limits merge risk.

### 2. Keep command bus as the only mutation path

All selection/removal/preview/transform edits still dispatch text commands. Panels may read scene/editor state directly for presentation, but they do not mutate the scene directly.

### 3. Prefer headless ImGui smoke coverage for panel contracts

Panels with CPU-side behavior (scene tree path jump, command dispatch triggers, input buffer handling) should gain headless tests by creating a minimal ImGui context. GPU/window-bound behavior remains for later manual verification in the demo.

## Increment Scope

This turn starts the change, lands SceneTreePanel, and adds focused headless verification for its command-dispatch contract. Inspector/gizmo/preview/demo layout follow in later increments of the same change.
