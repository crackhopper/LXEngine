## Context

REQ-041-c spans two editor surfaces with different risks:

- scene tree selection semantics are local, deterministic, and easy to test headlessly
- viewport drag-box selection needs projection math, modal state, and input-priority interactions with ImGuizmo

To keep rollout bounded, implementation should land scene tree semantics first, then viewport box-select and visualization, then full regression/verification.

## Decisions

### 1. Reuse the existing `select <path...>` command surface

The command bus already supports replacing the whole selection with multiple paths. UI code should therefore compute the desired path set locally and dispatch a single `select ...` command, keeping undo/redo and history consistent.

### 2. Keep scene tree range selection sibling-scoped

Shift-range selection uses the current primary selection as the anchor. Range expansion only happens when anchor and target share the same parent/root level; otherwise the click falls back to single-node replacement.

### 3. Stage viewport work behind explicit config/state

Box-select needs temporary drag state, screen-space projection helpers, and confirmation modal state. Those should live in `ViewportOverlay`/`EditorConfig` instead of leaking into unrelated editor modules.

## Increment Scope

Current increment lands:

- OpenSpec change skeleton
- scene tree Ctrl/Shift selection dispatch logic
- focused integration coverage for scene tree multi-select helpers

Viewport box-select, multi-selected viewport visualization polish, confirmation modal, and full matrix verification remain for later increments.
