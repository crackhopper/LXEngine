# material-asset-loader Specification

## Purpose
TBD - created by archiving change generic-material-asset. Update Purpose after archive.
## Requirements
### Requirement: YAML material asset format
The system SHALL define a YAML-based material definition file format (`.material`). The format SHALL support:

- `shader`: required string identifying the default shader family name
- `variants`: optional map of global shader variant names to boolean values
- `parameters`: optional map of `bindingName.memberName` to scalar or array values (global defaults)
- `resources`: optional map of binding name to file path or built-in placeholder name (global defaults)
- `passes`: optional map of pass name to pass-level configuration, each containing:
  - `shader`: optional per-pass shader name (overrides global default)
  - `renderState`: optional render state fields (`cullMode`, `depthTest`, `depthWrite`, `blendEnable`)
  - `variants`: optional pass-level variant overrides (merged with global variants)
  - `parameters`: optional pass-level parameter overrides
  - `resources`: optional pass-level resource overrides

Each pass MAY specify its own `shader` field to use a completely different shader from the global default. Per-pass variants are merged with global variants (pass-level wins on conflict).

If `passes` is omitted, a single Forward pass with default render state SHALL be assumed.

Parameter keys SHALL use `bindingName.memberName` format consistent with the `setParameter(bindingName, memberName, value)` runtime API from REQ-032.

#### Scenario: Minimal material asset with only shader
- **WHEN** a yaml file contains only `shader: blinnphong_0`
- **THEN** the generic loader creates a single-pass Forward material with default render state and no parameter overrides

#### Scenario: Full material asset with passes and defaults
- **WHEN** a material file declares shader, global parameters, global resources, and per-pass overrides
- **THEN** the generic loader creates a multi-pass material with global defaults applied and per-pass values overwriting where specified

#### Scenario: Per-pass shader override
- **WHEN** a material file specifies `shader: shadow_depth_only` under `passes.Shadow`
- **THEN** the Shadow pass compiles and uses `shadow_depth_only.vert/.frag` while other passes use the global shader

### Requirement: Generic material loader
The system SHALL provide a `loadGenericMaterial(materialPath)` function that:

1. Parses the YAML material definition file
2. For each pass, resolves the shader (per-pass override or global) and compiles with merged variants
3. Reflects bindings and vertex inputs
4. Builds a `MaterialTemplate` with pass entries
5. Creates a `MaterialInstance`
6. Applies global default parameters via `setParameter` or legacy setters
7. Applies per-pass parameter overrides (overwriting global defaults in shared buffer slots)
8. Loads and binds default resources (textures from file paths or placeholders)
9. Returns the `MaterialInstanceSharedPtr`

The generic loader SHALL NOT require material-type-specific C++ code. Any shader that follows the engine's binding conventions SHALL be loadable via this path.

#### Scenario: Generic loader produces a ready-to-render instance
- **WHEN** `loadGenericMaterial("assets/materials/blinnphong_lit.mat.yaml")` is called with a valid yaml
- **THEN** the returned `MaterialInstanceSharedPtr` has populated buffer slots, bound textures, and valid pass configuration

#### Scenario: Generic loader rejects invalid shader
- **WHEN** the yaml references a shader that cannot be found or compiled
- **THEN** the system emits a `FATAL` log and terminates

### Requirement: Parameter and resource names validated against reflection
All parameter binding names and member names declared in the YAML material asset file MUST exist in the shader reflection. All resource binding names MUST exist in the shader reflection as texture-type bindings.

If a parameter or resource name in the yaml does not match any reflected binding/member, the system SHALL emit a `FATAL` log with the unresolved name and terminate.

#### Scenario: Unknown parameter name terminates
- **WHEN** a yaml declares `NonExistent.foo: 1.0` and no binding named `NonExistent` exists in reflection
- **THEN** the system emits a `FATAL` log and terminates

#### Scenario: Unknown resource name terminates
- **WHEN** a yaml declares `resources: { noSuchTex: "path.png" }` and no texture binding named `noSuchTex` exists in reflection
- **THEN** the system emits a `FATAL` log and terminates

#### Scenario: Valid parameter names pass validation
- **WHEN** all parameter and resource names in the yaml match reflected bindings
- **THEN** the loader proceeds without error

### Requirement: Material asset file does not participate in ownership
The YAML material asset file SHALL NOT contain any fields that declare or override binding ownership. Ownership is determined solely by `shader-binding-ownership` (REQ-031) and shader reflection.

#### Scenario: Yaml cannot override system-owned status
- **WHEN** a yaml attempts to treat `CameraUBO` as a material parameter binding
- **THEN** the parameter write is rejected because `CameraUBO` is system-owned and has no corresponding buffer slot

### Requirement: Built-in placeholder textures
The system SHALL provide built-in placeholder textures accessible by name:

- `white`: 1x1 RGBA pixel (255, 255, 255, 255)
- `black`: 1x1 RGBA pixel (0, 0, 0, 255)
- `normal`: 1x1 RGBA pixel (128, 128, 255, 255) — flat tangent-space normal

When a resource value in the yaml matches a placeholder name, the loader SHALL bind the corresponding placeholder texture instead of attempting file loading.

Placeholder textures SHALL be lazily created singletons.

#### Scenario: Placeholder name resolves to built-in texture
- **WHEN** a yaml declares `resources: { normalMap: normal }`
- **THEN** the loader binds the built-in flat-normal placeholder texture to `normalMap`

#### Scenario: File path loads from disk
- **WHEN** a yaml declares `resources: { albedoMap: "textures/brick.png" }`
- **THEN** the loader loads the texture from disk and binds it to `albedoMap`

### Requirement: Global defaults and per-pass overrides
The material asset file SHALL support two layers of defaults:

- Global defaults (top-level `parameters` and `resources`) apply to all passes
- Per-pass overrides (`passes.<pass>.parameters` and `passes.<pass>.resources`) overwrite global defaults for the target pass

In the first version, since buffer slots are shared across passes, per-pass parameter overrides simply overwrite the global value in the shared buffer. The last-applied pass override wins.

#### Scenario: Global default applies to all passes
- **WHEN** a yaml declares `parameters: { MaterialUBO.shininess: 32.0 }` and no per-pass override exists
- **THEN** the shininess value is 32.0 for rendering in any pass

#### Scenario: Per-pass override overwrites global
- **WHEN** a yaml declares global `MaterialUBO.enableNormal: 0` and `passes.Forward.parameters.MaterialUBO.enableNormal: 1`
- **THEN** the final value of `enableNormal` is 1 (last write wins from Forward pass override)

### Requirement: Yaml parameters are not a whitelist constraint
Parameters and resources listed in the yaml SHALL be treated as defaults and metadata, not as a whitelist. If a binding/member exists in shader reflection but is not listed in the yaml, it SHALL remain a valid material-owned slot with zero-initialized defaults.

#### Scenario: Unlisted parameter remains valid
- **WHEN** a shader reflects `MaterialUBO.specularIntensity` but the yaml does not mention it
- **THEN** `specularIntensity` is zero-initialized and can be set at runtime via `setParameter` or `setFloat`

