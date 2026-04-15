# forward-shader-variant-contract Specification

## Purpose
TBD - created by archiving change forward-shader-variant-contract. Update Purpose after archive.
## Requirements
### Requirement: blinnphong_0 defines a fixed forward shader variant family
`blinnphong_0` SHALL be treated as a generic forward shader family rather than a fixed hard-coded Blinn-Phong shader. The only supported short-term variants for this family SHALL be:

- `USE_VERTEX_COLOR`
- `USE_UV`
- `USE_LIGHTING`
- `USE_NORMAL_MAP`
- `USE_SKINNING`

Any forward material pass that uses `blinnphong_0` MUST express its feature shape exclusively through this variant set.

#### Scenario: Forward pass uses only the supported variant names
- **WHEN** a forward material pass is configured for `blinnphong_0`
- **THEN** its variant set contains only `USE_VERTEX_COLOR`, `USE_UV`, `USE_LIGHTING`, `USE_NORMAL_MAP`, and `USE_SKINNING`

### Requirement: Forward shader baseColor composition follows the variant contract
The forward shader family SHALL always retain a material constant named `baseColor` as the default multiplier and fallback color source.

Final base color composition MUST follow these rules:

- if `USE_VERTEX_COLOR` is enabled, vertex color participates in the product
- if `USE_UV` is enabled, the sampled texture color participates in the product
- if both are enabled, the result is `baseColor * vertexColor * texture`
- if both are disabled, the result is `baseColor`

When `USE_LIGHTING` is disabled, the fragment shader MUST output exactly this base-color composition result and MUST NOT apply ambient, diffuse, specular, or any other Blinn-Phong lighting term.

#### Scenario: Constant-only base color path
- **WHEN** `USE_VERTEX_COLOR=0`, `USE_UV=0`, and `USE_LIGHTING=0`
- **THEN** the fragment output is derived only from the material constant `baseColor`

#### Scenario: Vertex color and texture both modulate baseColor
- **WHEN** `USE_VERTEX_COLOR=1` and `USE_UV=1`
- **THEN** the final base color multiplies the material constant, vertex color, and sampled texture color together before any enabled lighting step

### Requirement: Forward shader variant dependencies are fixed and mandatory
The forward shader family SHALL enforce the following logical dependencies:

- `USE_NORMAL_MAP => USE_LIGHTING`
- `USE_NORMAL_MAP => USE_UV`
- `USE_SKINNING => USE_LIGHTING`

`USE_VERTEX_COLOR` and `USE_UV` MAY be enabled independently or together. `USE_LIGHTING` SHALL NOT require either `USE_VERTEX_COLOR` or `USE_UV`.

#### Scenario: Normal map requires lighting and UV
- **WHEN** a forward shader variant set enables `USE_NORMAL_MAP`
- **THEN** the same variant set also enables both `USE_LIGHTING` and `USE_UV`

#### Scenario: Skinning requires lighting
- **WHEN** a forward shader variant set enables `USE_SKINNING`
- **THEN** the same variant set also enables `USE_LIGHTING`

### Requirement: Shader inputs and resources shrink strictly by enabled variants
The compiled `blinnphong_0` shader interface SHALL expose only the inputs and shader resources required by the enabled variants.

At minimum, the contract MUST satisfy all of the following:

- disabled `USE_VERTEX_COLOR` => shader does not require `inColor`
- enabled `USE_VERTEX_COLOR` => shader requires `inColor`
- disabled `USE_UV` => shader does not require `inUV`
- enabled `USE_UV` => shader requires `inUV`
- disabled `USE_LIGHTING` => shader does not require `inNormal` and does not execute Blinn-Phong lighting code
- enabled `USE_LIGHTING` => shader requires `inNormal`
- disabled `USE_NORMAL_MAP` => shader does not require `inTangent`
- enabled `USE_NORMAL_MAP` => shader requires `inTangent` and `inUV`
- disabled `USE_SKINNING` => shader does not declare skinning vertex attributes, does not declare the `Bones` UBO, and does not execute skinning math
- enabled `USE_SKINNING` => shader declares skinning vertex attributes, declares the `Bones` UBO, and executes skinning math

#### Scenario: Unlit variant removes lighting-only inputs
- **WHEN** `USE_LIGHTING=0`
- **THEN** the compiled shader contract excludes lighting-only requirements such as `inNormal` and any `Bones` UBO requirement that would only be introduced by `USE_SKINNING`

#### Scenario: Skinned normal-mapped variant expands to the full contract
- **WHEN** `USE_LIGHTING=1`, `USE_UV=1`, `USE_NORMAL_MAP=1`, and `USE_SKINNING=1`
- **THEN** the compiled shader contract requires `inNormal`, `inUV`, `inTangent`, `inBoneIDs`, `inBoneWeights`, and the `Bones` UBO

