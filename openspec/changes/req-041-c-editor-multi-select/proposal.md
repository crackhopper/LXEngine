## Why

REQ-041-c connects the multi-selection state and multi-target command protocol from REQ-041-b to actual editor UI. Today the scene tree still behaves like single-select only, and the viewport still only supports click picking plus one selected wireframe.

## What Changes

- Create the `req-041-c-editor-multi-select` OpenSpec change and implement it in bounded slices.
- Add scene tree modifier-key selection behavior on top of the existing command bus.
- Add viewport drag-box selection, multi-selected debug visualization, and large-box confirmation.
- Add focused integration coverage for scene tree multi-select and viewport box-select flows.

## Capabilities

### New Capabilities

- `editor-multi-select`: scene tree additive/range selection and viewport box selection for the ImGui editor.

## Impact

- Updates editor UI code under `src/core/editor/`.
- Adds/refreshes integration coverage under `src/test/integration/`.
- Keeps selection mutations going through the command bus instead of direct `EditorState` writes.
