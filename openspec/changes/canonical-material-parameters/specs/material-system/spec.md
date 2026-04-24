## ADDED Requirements

### Requirement: MaterialInstance stores a single canonical material parameter set
`MaterialInstance` SHALL store exactly one canonical set of material-owned buffer slots and texture bindings for the entire instance. Material-owned pass data MUST be modeled as a usage view over that canonical set, not as per-pass copies or override state.

The implementation MUST NOT keep a second pass-scoped source of truth for material parameter bytes or textures.

#### Scenario: Two passes reuse the same canonical buffer slot
- **WHEN** forward and shadow both declare a compatible material-owned `UniformBuffer` named `MaterialUBO`
- **THEN** `MaterialInstance` creates exactly one canonical `MaterialParameterData` slot for `MaterialUBO` and both passes resolve that same slot

#### Scenario: Pass-scoped setter APIs do not exist
- **WHEN** inspecting the `MaterialInstance` public API
- **THEN** there is no `setParameter(pass, ...)` or `setTexture(pass, ...)` overload that accepts a pass argument

## MODIFIED Requirements

### Requirement: MaterialInstance allocates UBO buffer from reflection
`MaterialInstance::create(template)` SHALL walk material-owned buffer bindings from the template's per-pass usage lists. For each unique material-owned buffer binding name, it SHALL create exactly one canonical `MaterialParameterData` object with a zero-initialized byte buffer sized to the binding's `size`.

Cross-pass reuse for same-name buffer bindings SHALL be treated as canonical binding reuse: if two passes declare the same binding name, the descriptor type, buffer size, and member layout MUST match exactly. The system SHALL fail fast on mismatch during template construction or material instance initialization.

Shaders without any material-owned buffer binding MUST produce a `MaterialInstance` with an empty canonical slot collection; legacy convenience setter calls on such an instance MUST assert in debug builds.

`MaterialParameterData` SHALL accept the binding's reflected layout and resource type at construction (`UniformBuffer` or `StorageBuffer`) and return the corresponding `ResourceType` from `getType()`.

#### Scenario: Construction creates canonical slots from reflection using ownership query
- **WHEN** `MaterialInstance::create(tmpl)` is called and the enabled pass shaders declare `SurfaceParams` (UBO, 48 bytes) and `DetailParams` (UBO, 32 bytes)
- **THEN** two canonical `MaterialParameterData` slots are created with sizes 48 and 32, each zero-initialized

#### Scenario: Same-name buffer reused across passes creates one canonical slot
- **WHEN** forward and shadow both declare a compatible `UniformBuffer` binding named `MaterialUBO`
- **THEN** `MaterialInstance` creates one canonical slot named `MaterialUBO`, not one slot per pass

#### Scenario: Incompatible same-name binding fails fast
- **WHEN** forward declares `MaterialUBO` as a 32-byte buffer and shadow declares `MaterialUBO` as a 48-byte buffer
- **THEN** the system fails fast instead of creating material slots with ambiguous layout

#### Scenario: Shader without material-owned buffers produces empty slot collection
- **WHEN** a shader's reflection bindings contain only `Texture2D` entries and system-owned `CameraUBO`
- **THEN** the resulting `MaterialInstance` has an empty canonical buffer slot collection

### Requirement: Reflection-driven UBO setters
`MaterialInstance` SHALL expose instance-level `setVec4(StringID, Vec4f)`, `setVec3(StringID, Vec3f)`, `setFloat(StringID, float)`, and `setInt(StringID, int32_t)` convenience setters, plus the primary instance-level `setParameter(bindingName, memberName, value)` overload family. These setters SHALL write into the canonical material-owned buffer slots stored on the instance.

Each setter SHALL look up the target member by matching `StringID(member.name)` against the canonical binding/member metadata, verify the member's `ShaderPropertyType` matches the setter's expected type, and `memcpy` the value into the canonical slot buffer at the reflected offset using the byte width of the source type. A shared private helper SHALL encapsulate the lookup + type check + memcpy logic.

The system MUST NOT expose pass-scoped setter overloads that write a different value for a specific pass.

#### Scenario: setVec4 writes into the canonical slot at the reflected offset
- **WHEN** `setParameter(StringID("MaterialUBO"), StringID("customColor"), Vec4f{1,0,0,1})` is called and reflection reports `customColor` at offset 16 with type `Vec4`
- **THEN** bytes 16..31 of the canonical `MaterialUBO` slot equal the little-endian encoding of `{1.0f, 0.0f, 0.0f, 1.0f}`

#### Scenario: setVec3 writes 12 bytes and leaves the trailing 4 bytes untouched
- **WHEN** `setVec3(StringID("baseColor"), Vec3f{0.5f, 0.5f, 0.5f})` is called, reflection reports `baseColor` at offset 0 in the unique canonical slot, and the next member `shininess` at offset 12 holds a previously written float
- **THEN** only bytes 0..11 are overwritten and the `shininess` value at offset 12..15 is preserved

#### Scenario: Setter type mismatch is an assertion failure
- **WHEN** `setFloat(StringID("baseColor"), 1.0f)` is called and reflection reports `baseColor` with type `Vec3`
- **THEN** an assertion fires in debug builds and the canonical slot buffer is not modified

#### Scenario: Unknown member name asserts and is ignored
- **WHEN** `setVec4(StringID("doesNotExist"), …)` is called and no member with that name exists in the canonical slot metadata
- **THEN** an assertion fires in debug builds and the canonical slot buffer is unchanged

### Requirement: Texture bindings by StringID
`MaterialInstance::setTexture(StringID id, CombinedTextureSamplerSharedPtr tex)` SHALL look up `id` via the template's material binding metadata, assert that the resulting binding's type is `Texture2D` or `TextureCube`, and store the sampler in the instance's canonical texture map under that binding name.

`MaterialInstance` MUST NOT expose a setter that takes a pass argument or a raw `uint32_t` set/binding pair — callers use the shader-declared binding name only. `CombinedTextureSamplerSharedPtr` (rather than raw `TextureSharedPtr`) is used because the concrete resource passed to the backend descriptor layer must already implement `IGpuResource`, which `CombinedTextureSampler` does and `Texture` does not.

#### Scenario: Texture bound to a reflected sampler name updates canonical storage
- **WHEN** `setTexture(StringID("albedoMap"), tex)` is called and the template metadata classifies `albedoMap` as a `Texture2D` binding
- **THEN** the texture is stored under `StringID("albedoMap")` in the canonical texture map

#### Scenario: Texture bound to a non-sampler name asserts
- **WHEN** `setTexture(StringID("baseColor"), tex)` is called and `baseColor` is a UBO scalar member rather than a texture binding
- **THEN** an assertion fires in debug builds and the canonical texture map is unchanged

### Requirement: getDescriptorResources returns UBO + textures in deterministic order
`MaterialInstance::getDescriptorResources(StringID pass)` SHALL return a vector of material-owned descriptor resources scoped to the target pass by filtering the instance's canonical parameter/resource set through that pass's declared material binding usage list.

The resolution SHALL:

1. Query `MaterialTemplate::getMaterialBindings(pass)` for the pass's material-owned bindings
2. For each binding in that pass usage list, find the corresponding canonical resource (buffer slot or texture) by binding name
3. Return resources sorted by ascending `(set << 16 | binding)` from the pass's reflection

The no-argument `getDescriptorResources()` SHALL remain absent. All callers MUST provide a pass argument.

#### Scenario: Forward and shadow passes return different resource subsets from shared canonical storage
- **WHEN** forward pass declares `MaterialUBO` + `albedoMap`, shadow pass declares only `MaterialUBO`, and the instance has canonical resources for both bindings
- **THEN** `getDescriptorResources(Pass_Forward)` returns 2 resources and `getDescriptorResources(Pass_Shadow)` returns 1, both resolved from the same canonical instance data

#### Scenario: Resources sorted by set/binding within a pass
- **WHEN** a pass has `MaterialUBO` at (set=2, binding=0) and `albedoMap` at (set=2, binding=1)
- **THEN** `getDescriptorResources(pass)` returns `MaterialUBO` first, then `albedoMap`

#### Scenario: Missing canonical texture is skipped
- **WHEN** a pass usage list includes `albedoMap` but `setTexture` has not been called for that canonical binding
- **THEN** that entry is omitted from the result

### Requirement: MaterialTemplate builds per-pass material-owned binding interface
`MaterialTemplate::buildBindingCache()` SHALL build a per-pass material-owned binding list for each configured pass. For each pass, the list SHALL contain only bindings classified as material-owned by `isSystemOwnedBinding()`. The per-pass list SHALL preserve the reflection order from that pass's shader and represent that pass's usage view over the instance-wide canonical material parameter set.

`MaterialTemplate` SHALL expose `getMaterialBindings(StringID pass)` returning the material-owned binding list for a given pass. If the pass is not defined, it SHALL return an empty list.

#### Scenario: Per-pass material bindings exclude system-owned
- **WHEN** a shader declares `CameraUBO`, `MaterialUBO`, and `albedoMap` bindings and the template is built with one forward pass
- **THEN** `getMaterialBindings(Pass_Forward)` returns only `MaterialUBO` and `albedoMap`, not `CameraUBO`

#### Scenario: Different passes may use different subsets of canonical material bindings
- **WHEN** a forward pass shader declares `MaterialUBO` and `albedoMap`, and a shadow pass shader declares only `MaterialUBO`
- **THEN** `getMaterialBindings(Pass_Forward)` includes both, and `getMaterialBindings(Pass_Shadow)` includes only `MaterialUBO`

### Requirement: Cross-pass same-name binding conflict detection
During `buildBindingCache()`, if two passes contain a material-owned binding with the same name, the system SHALL treat them as declarations of the same canonical material binding and SHALL verify that they are consistent in:
- descriptor type (`ShaderPropertyType`)
- buffer size (for buffer-typed bindings)
- member layout (for buffer-typed bindings with reflected members)

If inconsistent, the system SHALL fail fast with a diagnostic that includes the binding name, conflicting pass names, and the specific inconsistency. The system SHALL NOT continue with a warning-only path.

#### Scenario: Consistent same-name bindings across passes are accepted as canonical reuse
- **WHEN** forward and shadow both declare `MaterialUBO` as a `UniformBuffer` with identical size and members
- **THEN** template construction succeeds and both passes refer to the same canonical binding definition

#### Scenario: Inconsistent same-name bindings fail fast
- **WHEN** forward declares `MaterialUBO` with size 32 and shadow declares `MaterialUBO` with size 48
- **THEN** the system fails fast and reports the size mismatch instead of emitting a warning and continuing
