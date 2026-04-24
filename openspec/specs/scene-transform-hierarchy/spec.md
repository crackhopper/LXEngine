# Scene Transform Hierarchy

## Requirements

### Requirement: Scene objects support parent-child transform relationships
The scene layer SHALL support parent-child transform relationships so object transforms can be composed hierarchically.

#### Scenario: child transform depends on parent
- **WHEN** a child object is attached under a parent with a non-identity transform
- **THEN** the child's world transform reflects the composition of parent and child local transforms

### Requirement: Local and world transform semantics are explicit
The engine SHALL distinguish stored local transform state from derived world transform state for hierarchical scene objects.

#### Scenario: updating local transform updates world result
- **WHEN** a scene object's local transform changes
- **THEN** its derived world transform and any dependent child world transforms update according to the hierarchy contract

### Requirement: Renderable world transforms come from the hierarchy contract
Renderable submission data SHALL consume world transforms derived from the scene hierarchy rather than requiring external per-frame world-matrix injection as the only composition mechanism.

#### Scenario: renderer uses hierarchy-derived world transform
- **WHEN** a hierarchical scene is rendered
- **THEN** the render path uses the world transform produced by the scene hierarchy contract
