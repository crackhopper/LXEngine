## MODIFIED Requirements

### Requirement: IComponent and base header are removed
The legacy scene-component base contract at `src/core/scene/components/base.hpp` SHALL remain removed. However, the codebase MAY introduce new scene-component infrastructure under current paths such as `src/core/scene/component.hpp` and `src/core/scene/components/` for node-local composition, provided that this infrastructure does not move `Skeleton` out of `src/core/asset/` and does not make `Skeleton` inherit `IComponent`.

#### Scenario: scene-component infrastructure returns without changing skeleton ownership
- **WHEN** the codebase introduces new node-local component files
- **THEN** `Skeleton` still lives under `src/core/asset/skeleton.hpp` and does not inherit `IComponent`
