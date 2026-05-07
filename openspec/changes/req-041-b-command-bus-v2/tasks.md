## 1. OpenSpec And Multi-Selection Foundation

- [x] 1.1 Create the `req-041-b-command-bus-v2` OpenSpec change skeleton (proposal/design/tasks).
- [x] 1.2 Upgrade `EditorState` to multi-selection + primary-selection semantics, and migrate direct panel/test callers.
- [x] 1.3 Add/refresh headless tests covering `EditorState` multi-selection behavior.

## 2. Command Bus V2 Core

- [x] 2.1 Extend `CommandBus` registration/dispatch contracts with arg completers and undo/redo metadata.
- [x] 2.2 Implement builtin multi-target/select/deselect/undo/redo flows plus scene path completers.
- [x] 2.3 Add `src/test/integration/test_command_bus_v2.cpp` for completion, undo/redo, and multi-target regression coverage.

## 3. UI Wiring And Full Verification

- [x] 3.1 Update console/viewport/editor panels to consume command-bus v2 completion, shortcuts, and batch selection behavior.
- [x] 3.2 Run required configure/build/ctest matrix, then commit `Implement REQ-041-b: command-bus-v2`.
