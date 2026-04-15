## 1. Spec And API Alignment

- [x] 1.1 Finalize the `material-system` and `scene-node-validation` spec deltas for template-owned pass definitions, instance-owned pass enable state, and scene-level propagation
- [x] 1.2 Update the level-1 material design docs and subsystem notes to reflect the new pass-selection model and shared-instance semantics

## 2. MaterialInstance Pass Model

- [x] 2.1 Refactor `MaterialInstance` so new instances default to all template-defined passes enabled and `setPassEnabled(...)` fatals on undefined passes
- [x] 2.2 Make `getPassFlag()` a derived view of the currently defined-and-enabled pass set instead of an independently maintained truth value
- [x] 2.3 Replace the Forward-only render-state access path with a pass-aware material render-state query

## 3. Scene Integration

- [x] 3.1 Update `SceneNode` validation and `supportsPass(pass)` so they consider only currently enabled material passes
- [x] 3.2 Add scene-level propagation such as `revalidateNodesUsing(materialInstance)` so shared instance pass-state changes revalidate every affected node
- [x] 3.3 Keep ordinary material parameter writes on the non-structural path so they do not trigger node revalidation

## 4. Verification

- [x] 4.1 Add tests covering default-enable-all behavior, undefined-pass `setPassEnabled(...)` fatal failures, and derived `getPassFlag()` correctness
- [x] 4.2 Add tests covering pass-aware render-state lookup and `supportsPass(pass)` behavior with enabled versus disabled passes
- [x] 4.3 Add tests covering shared `MaterialInstance` pass-state propagation across multiple `SceneNode` objects and confirming ordinary parameter writes do not revalidate nodes
