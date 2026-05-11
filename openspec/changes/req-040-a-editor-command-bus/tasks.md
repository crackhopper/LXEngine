## 1. OpenSpec And Core Contracts

- [x] 1.1 Create the `req-040-a-editor-command-bus` OpenSpec change skeleton (proposal/design/tasks/spec stub).
- [x] 1.2 Add `src/core/editor/command_bus.hpp/.cpp` with tokenizer, verb registry, dispatch history, and script dispatch.
- [x] 1.3 Add `src/core/editor/editor_state.hpp/.cpp` with non-owning selected-node state.

## 2. Headless Verification

- [x] 2.1 Add `src/test/integration/test_command_bus.cpp` covering parser, history, unknown commands, exception handling, script execution, and `EditorState` weak selection behavior.
- [x] 2.2 Run targeted build/test for `test_command_bus` and fix compile/runtime issues.

## 3. Follow-up REQ Scope

- [x] 3.1 Add built-in editor verbs (`help/select/move/...`) and registration helpers.
- [x] 3.2 Add ImGui console panel and history navigation/autocomplete.
- [x] 3.3 Wire the command bus into `lxe_editor`, then run full REQ build/test matrix.
