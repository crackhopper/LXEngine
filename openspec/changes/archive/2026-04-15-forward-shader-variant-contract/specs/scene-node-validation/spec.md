## ADDED Requirements

### Requirement: SceneNode validates forward-shader resource requirements from variants
For any enabled pass backed by the `blinnphong_0` forward shader family, `SceneNode` SHALL validate mesh and node resources against the active shader variant set before the node is considered structurally valid.

At minimum, the validation rules SHALL be:

- `USE_VERTEX_COLOR => mesh` provides `inColor`
- `USE_UV => mesh` provides `inUV`
- `USE_LIGHTING => mesh` provides `inNormal`
- `USE_NORMAL_MAP => mesh` provides `inTangent` and `inUV`
- `USE_SKINNING => mesh` provides `inBoneIDs` and `inBoneWeights`, and the node provides `Skeleton/Bones`

Any mismatch between the enabled variant set and the available mesh/skeleton resources MUST be treated as a structural validation failure and handled as `FATAL + terminate`.

#### Scenario: Missing vertex color attribute terminates
- **WHEN** a pass enables `USE_VERTEX_COLOR` and the mesh vertex layout does not provide `inColor`
- **THEN** `SceneNode` logs a `FATAL` structural validation failure for that pass and terminates immediately

#### Scenario: Missing UV for textured forward pass terminates
- **WHEN** a pass enables `USE_UV` and the mesh vertex layout does not provide `inUV`
- **THEN** `SceneNode` logs a `FATAL` structural validation failure for that pass and terminates immediately

#### Scenario: Missing tangent for normal-mapped pass terminates
- **WHEN** a pass enables `USE_NORMAL_MAP` and the mesh vertex layout does not provide `inTangent`
- **THEN** `SceneNode` logs a `FATAL` structural validation failure for that pass and terminates immediately

#### Scenario: Missing skeleton resources for skinned pass terminates
- **WHEN** a pass enables `USE_SKINNING` and the node lacks a `Skeleton` or `Bones` resource
- **THEN** `SceneNode` logs a `FATAL` structural validation failure for that pass and terminates immediately
