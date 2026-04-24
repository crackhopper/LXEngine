## 1. Entry Contract

- [x] 1.1 Decide and document the role of the root `Renderer` executable relative to `scene_viewer`.
- [x] 1.2 Align CMake target comments/docs with that chosen role.

## 2. Helper Cleanup

- [x] 2.1 Remove or consolidate unused renderer bootstrap/debug helper functions identified by the review.
- [x] 2.2 Centralize duplicated environment toggle reads where they support the same behavior.

## 3. Verification

- [x] 3.1 Build the root executable and `scene_viewer` after the cleanup.
- [x] 3.2 Confirm docs and executable behavior no longer send conflicting signals.
