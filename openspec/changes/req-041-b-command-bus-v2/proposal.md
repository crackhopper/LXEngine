## Why

REQ-041-b closes the three command-bus/editor gaps intentionally deferred by REQ-040-a/041-a: argument completion, undo/redo, and multi-selection. Current editor flows still require hand-typed full paths, have no reversible command history, and force single-node selection even when the UI already exposes batch-edit style workflows.

## What Changes

- Create the `req-041-b-command-bus-v2` OpenSpec change and implement it in bounded slices.
- Extend `EditorState` from single weak selection to a live multi-selection set with primary-selection semantics for inspector/gizmo anchoring.
- Extend `CommandBus`/builtin commands with arg completers plus undo/redo metadata/execution.
- Update console/viewport/editor tests to cover the new completion, history, and multi-target behavior.

## Capabilities

### New Capabilities
- `editor-command-bus-v2`: argument completion, undo/redo history replay, and multi-selection-aware editor command flows.

## Impact

- Updates core editor contracts under `src/core/editor/`.
- Adds new integration coverage for command-bus v2 and refreshes affected editor tests.
- Keeps REQ-040-a text protocol surface while broadening capability behind the same command entrypoint.
