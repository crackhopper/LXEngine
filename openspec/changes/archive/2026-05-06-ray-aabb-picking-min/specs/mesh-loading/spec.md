## ADDED Requirements

### Requirement: Loaded meshes preserve local-space bounds
Mesh ingestion paths SHALL compute and preserve a local-space `BoundingBox` for loaded geometry. The computed bounds SHALL reflect the loaded vertex positions in mesh-local space and SHALL be stored on the resulting `Mesh`.

This requirement SHALL apply to both OBJ and GLTF mesh-loading flows, and SHALL be satisfied during the existing position-reading pass rather than by an editor-only post-process.

#### Scenario: OBJ mesh load computes bounds
- **WHEN** an OBJ mesh is loaded successfully
- **THEN** the resulting `Mesh` SHALL carry a valid local-space `BoundingBox` spanning the loaded positions

#### Scenario: GLTF mesh load computes bounds
- **WHEN** a GLTF mesh is loaded successfully
- **THEN** the resulting `Mesh` SHALL carry a valid local-space `BoundingBox` spanning the loaded positions
