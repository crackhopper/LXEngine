## Why

REQ-040-a needs one command entrypoint that both in-editor UI and future MCP/agent shims can share. Current lxe_editor only has ad-hoc hotkeys/UI callbacks; there is no text protocol, command registry, or command history surface.

## What Changes

- Add a core `CommandBus` with text tokenization, verb registry, dispatch history, and script dispatch.
- Add a minimal `EditorState` selection holder so command handlers can share editor-local selection state without introducing ownership ambiguity.
- Add focused integration tests for parser, dispatch, history, unknown-command/error handling, and script execution.
- Follow-up increments will add built-in scene/camera verbs, ImGui console integration, and demo wiring.

## Capabilities

### New Capabilities
- `editor-command-bus`: text-command dispatch, registry, history, and script execution for editor/agent workflows.

## Impact

- Adds new code under `src/core/editor/`.
- Adds integration coverage in `src/test/integration/test_command_bus.cpp`.
- Future REQ-041-a editor UI work can reuse the same bus instead of inventing another callback path.
