## MODIFIED Requirements

### Requirement: Local and world transform semantics are explicit
The engine SHALL distinguish stored local transform state from derived world transform state for hierarchical scene objects. `SceneNode` SHALL store local transform state as an explicit `Transform { translation, rotation, scale }` value type rather than as a raw local matrix. Derived world transform state SHALL remain a `Mat4f` produced from the hierarchy contract so existing render submission and per-draw matrix consumers continue to operate on matrices.

`SceneNode` SHALL expose local transform mutation through:

- `setLocalTransform(const Transform&)`
- `setTranslation(...)`
- `setRotation(...)`
- `setScale(...)`

`SceneNode` SHALL NOT expose `setLocalTransform(const Mat4f&)` as a public compatibility API once this change lands. Existing matrix-based callers MUST migrate to TRS setters or to explicit caller-side `Transform::fromMat4(...)` conversion.

#### Scenario: updating local transform updates world result
- **WHEN** a scene object's local transform changes
- **THEN** its derived world transform and any dependent child world transforms update according to the hierarchy contract

#### Scenario: matrix setter compatibility is removed
- **WHEN** existing demo, test, or scene-construction code needs to change a node transform after this change
- **THEN** it uses `Transform` or TRS-specific setters rather than a public `setLocalTransform(const Mat4f&)` overload

## ADDED Requirements

### Requirement: Transform bridge preserves migration path with explicit approximation semantics
The engine SHALL provide a `Transform` value type that can bridge to and from matrix form through `toMat4()` and `fromMat4(const Mat4f&)`. `fromMat4(...)` SHALL extract translation from the matrix translation column and SHALL reconstruct rotation and scale from the upper 3x3 basis using an approximation strategy that ignores shear when the input is not a strict TRS matrix.

For inputs containing negative scale or reflection, `fromMat4(...)` SHALL normalize the returned scale/sign convention consistently so downstream code receives a stable `Transform` representation rather than arbitrary sign flips between axes.

#### Scenario: strict TRS input round-trips through Transform
- **WHEN** `fromMat4(...)` receives a matrix produced from a strict TRS transform without shear
- **THEN** the reconstructed translation, rotation, and scale match the original transform within numeric tolerance

#### Scenario: historical matrix callers convert explicitly
- **WHEN** a caller only has a historical matrix constant and still needs to construct a local transform
- **THEN** the caller performs an explicit `Transform::fromMat4(...)` conversion instead of relying on an implicit `SceneNode` matrix setter
