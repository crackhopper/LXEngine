## 1. Math Foundation

- [x] 1.1 Add `src/core/math/transform.{hpp,cpp}` with `Transform` storage, `identity()`, `toMat4()`, and `fromMat4(...)`
- [x] 1.2 Implement `fromMat4(...)` decomposition behavior for strict TRS, negative-scale normalization, and non-strict TRS warning emission
- [x] 1.3 Extend focused math tests to cover identity, translation-only, strict TRS round-trip, and `WARN` cases for shear / reflection repair

## 2. SceneNode API Migration

- [x] 2.1 Change `SceneNode` local transform storage from `Mat4f` to `Transform` while preserving world-matrix cache and dirty propagation
- [x] 2.2 Replace public matrix local-transform setter/getter surface with `Transform` and TRS-specific accessors
- [x] 2.3 Update world transform recomposition and per-draw model sync to consume `m_localTransform.toMat4()`

## 3. Call Site Refactor

- [x] 3.1 Migrate demo, test, and scene-construction call sites away from `setLocalTransform(Mat4f)`
- [x] 3.2 Prefer direct `setTranslation(...)` or explicit `Transform{...}` construction for readable migrated call sites
- [x] 3.3 Keep explicit caller-side `Transform::fromMat4(...)` only where a historical matrix constant is still the clearest source form

## 4. Verification And Docs

- [x] 4.1 Extend scene hierarchy integration tests to cover migrated TRS setters and unchanged dirty-propagation behavior
- [x] 4.2 Update source-analysis / subsystem notes for `SceneNode` local transform semantics after implementation lands
- [x] 4.3 Run targeted build and test commands for math and scene hierarchy regressions before close-out
