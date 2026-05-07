## Context

REQ-041-b spans core command infrastructure and multiple editor panels. To keep risk bounded, implementation should start from the lowest-level shared state change: multi-selection in `EditorState`. Once callers can distinguish full selection vs. primary selection, command parsing, undo/redo, and completion can layer on without reworking panel assumptions again.

## Decisions

### 1. Land multi-selection state before command grammar changes

`EditorState` becomes the canonical source of truth for:
- full selected-node collection
- primary selected node (last live insertion)
- additive/removal selection mutations

Inspector/gizmo/scene tree will consume `getPrimarySelected()` for anchor-style behavior while batch operations use `getSelected()`.

### 2. Keep primary-selection semantics explicit

The old `getSelected()` contract hid the distinction between "one selected" and "anchor among many". V2 makes this explicit so panels can stay single-anchor without blocking batch transforms.

### 3. Preserve the existing command line surface while upgrading internals incrementally

The change will keep verb names and single-target call forms stable. New parsing/completion/history features should be additive and verified with regression coverage.

## Increment Scope

Change fully landed. `EditorState` multi-selection, command-bus arg completion / undo / redo, add-remove inverse recovery, and viewport gizmo multi-selection delta commit are all implemented and covered by regression tests.
