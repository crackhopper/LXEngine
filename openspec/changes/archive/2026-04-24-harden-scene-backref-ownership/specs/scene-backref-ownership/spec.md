## ADDED Requirements

### Requirement: Scene-node back-reference uses an ownership-safe contract
Scene nodes SHALL refer back to their parent scene through an ownership-safe contract rather than an unchecked raw pointer.

#### Scenario: scene destruction does not leave an unchecked raw back-reference
- **WHEN** a scene is detached or destroyed
- **THEN** scene-node back-reference semantics remain defined and do not rely on a dangling raw pointer

### Requirement: Attach and detach semantics are explicit
The engine SHALL define how scene-node back-references are established, cleared, and observed during attach/detach operations.

#### Scenario: detaching a node updates parent linkage
- **WHEN** a node is detached from its scene
- **THEN** the back-reference state changes according to an explicit contract rather than incidental caller discipline
