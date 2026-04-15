## ADDED Requirements

### Requirement: Reflected vertex input contract matches forward variant-driven declarations
When `blinnphong_0` is compiled with a specific forward variant subset, `ShaderReflector` SHALL report only the vertex-stage inputs actually declared by that compiled shader variant. Inputs disabled by variant-controlled GLSL declarations MUST NOT appear in the reflected vertex input contract.

This requirement applies at minimum to the forward-family-controlled inputs `inColor`, `inUV`, `inNormal`, `inTangent`, `inBoneIDs`, and `inBoneWeights`.

#### Scenario: UV-disabled variant omits UV input from reflection
- **WHEN** `blinnphong_0` is compiled with `USE_UV=0`
- **THEN** the reflected vertex input contract does not contain `inUV`

#### Scenario: Normal-mapped variant reflects tangent input
- **WHEN** `blinnphong_0` is compiled with `USE_NORMAL_MAP=1`
- **THEN** the reflected vertex input contract contains `inTangent`

#### Scenario: Non-skinned variant omits skinning inputs
- **WHEN** `blinnphong_0` is compiled with `USE_SKINNING=0`
- **THEN** the reflected vertex input contract does not contain `inBoneIDs` or `inBoneWeights`
