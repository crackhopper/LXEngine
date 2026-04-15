## ADDED Requirements

### Requirement: Forward material loader validates and persists the variant contract
The loader for `blinnphong_0` SHALL be the authority for forward-shader variant-set construction. For every configured pass, it MUST:

- declare the enabled subset of `USE_VERTEX_COLOR`, `USE_UV`, `USE_LIGHTING`, `USE_NORMAL_MAP`, and `USE_SKINNING`
- pass that exact subset into shader compilation
- persist that same subset in `RenderPassEntry::shaderSet.variants`
- validate the logical dependencies of the variant set before returning a material/template result

If the enabled subset violates any mandatory dependency, the loader MUST emit a `FATAL` log and terminate the process immediately.

#### Scenario: Loader rejects normal map without lighting
- **WHEN** the loader constructs a `blinnphong_0` pass with `USE_NORMAL_MAP=1` and `USE_LIGHTING=0`
- **THEN** the loader emits a `FATAL` validation error and terminates instead of returning a material/template

#### Scenario: Loader stores the validated variant set in compile input and pass metadata
- **WHEN** the loader constructs a valid `blinnphong_0` pass with `USE_VERTEX_COLOR=1`, `USE_UV=1`, and `USE_LIGHTING=1`
- **THEN** the shader compilation input and `RenderPassEntry::shaderSet.variants` contain the same validated variant subset
