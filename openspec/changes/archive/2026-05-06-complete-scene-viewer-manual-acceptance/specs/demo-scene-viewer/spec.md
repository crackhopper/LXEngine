## ADDED Requirements

### Requirement: Scene viewer manual acceptance checklist is explicit
The project SHALL define an explicit manual acceptance checklist for `demo_scene_viewer` covering the maintained user-facing behaviors required for requirement closure.

#### Scenario: checklist enumerates observable demo behavior
- **WHEN** a developer prepares to manually validate `demo_scene_viewer`
- **THEN** the required observations are listed explicitly rather than inferred from scattered notes

### Requirement: Manual acceptance records closure evidence
Successful scene-viewer manual acceptance SHALL produce recorded evidence sufficient to mark the remaining REQ-019 work complete.

#### Scenario: successful validation can be archived
- **WHEN** the manual acceptance checklist passes in a suitable environment
- **THEN** the result includes enough recorded evidence to support requirement/archive closure
