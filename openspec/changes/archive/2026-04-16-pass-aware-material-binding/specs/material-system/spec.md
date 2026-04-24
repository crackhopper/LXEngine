## ADDED Requirements

### Requirement: MaterialTemplate builds per-pass material-owned binding interface
`MaterialTemplate::buildBindingCache()` SHALL, in addition to populating the flattened name-keyed cache, build a per-pass material-owned binding list for each configured pass. For each pass, the list SHALL contain only bindings classified as material-owned by `isSystemOwnedBinding()`. The per-pass list SHALL preserve the reflection order from that pass's shader.

`MaterialTemplate` SHALL expose `getMaterialBindings(StringID pass)` returning the material-owned binding list for a given pass. If the pass is not defined, it SHALL return an empty list.

#### Scenario: Per-pass material bindings exclude system-owned
- **WHEN** a shader declares `CameraUBO`, `MaterialUBO`, and `albedoMap` bindings and the template is built with one forward pass
- **THEN** `getMaterialBindings(Pass_Forward)` returns only `MaterialUBO` and `albedoMap`, not `CameraUBO`

#### Scenario: Different passes may have different material bindings
- **WHEN** a forward pass shader declares `MaterialUBO` and `albedoMap`, and a shadow pass shader declares only `MaterialUBO`
- **THEN** `getMaterialBindings(Pass_Forward)` includes both, and `getMaterialBindings(Pass_Shadow)` includes only `MaterialUBO`

### Requirement: Cross-pass same-name binding conflict detection
During `buildBindingCache()`, if two passes contain a material-owned binding with the same name, the system SHALL verify that the bindings are consistent in:
- descriptor type (`ShaderPropertyType`)
- buffer size (for buffer-typed bindings)
- member layout (for `UniformBuffer` bindings)

If inconsistent, the system SHALL emit a warning log with the binding name, conflicting pass names, and the specific inconsistency. The system SHALL NOT fail — pass-aware queries resolve correctly regardless.

#### Scenario: Consistent same-name bindings across passes produce no warning
- **WHEN** forward and shadow passes both declare `MaterialUBO` as a `UniformBuffer` with identical size and members
- **THEN** no warning is emitted

#### Scenario: Inconsistent same-name bindings produce a warning
- **WHEN** forward declares `MaterialUBO` with size 32 and shadow declares `MaterialUBO` with size 48
- **THEN** the system emits a warning log identifying the size mismatch

### Requirement: First-version supported material-owned descriptor types
The material system SHALL formally support the following descriptor types for material-owned bindings in the first version:
- `UniformBuffer`
- `StorageBuffer`
- `Texture2D`
- `TextureCube`

For buffer types (`UniformBuffer`, `StorageBuffer`), `MaterialInstance` SHALL create a buffer slot with independent byte buffer, dirty state, and `IRenderResource` wrapper.

For texture types (`Texture2D`, `TextureCube`), the existing `setTexture(StringID, CombinedTextureSamplerSharedPtr)` mechanism SHALL continue to apply.

#### Scenario: StorageBuffer binding creates a buffer slot
- **WHEN** a shader declares a non-system-owned `StorageBuffer` binding named `ParticleData` with size 256
- **THEN** `MaterialInstance` creates a buffer slot for it with a 256-byte zero-initialized buffer

#### Scenario: TextureCube binding handled by setTexture
- **WHEN** a shader declares a `TextureCube` binding named `envMap`
- **THEN** `setTexture(StringID("envMap"), cubeTex)` stores the resource under that name

### Requirement: Unsupported descriptor types fail fast
If a material-owned binding has a descriptor type not in the supported set (e.g., `Sampler` without combined image, storage image, input attachment), the system SHALL emit a `FATAL` log with binding name and type and terminate immediately during `MaterialInstance` construction.

#### Scenario: Standalone Sampler binding terminates
- **WHEN** a shader declares a material-owned `Sampler` binding named `customSampler`
- **THEN** the system emits a `FATAL` log and terminates during material instance construction

### Requirement: MaterialInstance supports multiple material-owned buffer slots
`MaterialInstance` SHALL replace the single `m_uboBuffer` / `m_uboBinding` / `m_uboResource` with a collection of buffer slots, each keyed by binding name. Each slot SHALL have:
- an independent `std::vector<uint8_t>` byte buffer
- a non-owning pointer to the `ShaderResourceBinding`
- an independent `IRenderResourcePtr` wrapper
- an independent dirty flag

The slot collection SHALL be built during construction by iterating material-owned buffer bindings from the enabled passes.

#### Scenario: Two material-owned UBOs each get a buffer slot
- **WHEN** a shader declares `SurfaceParams` (UBO, 16 bytes) and `DetailParams` (UBO, 32 bytes)
- **THEN** `MaterialInstance` holds two buffer slots with sizes 16 and 32 respectively

#### Scenario: Single-buffer materials continue to work
- **WHEN** a shader declares only one material-owned UBO named `MaterialUBO`
- **THEN** `MaterialInstance` holds exactly one buffer slot and all existing setters work unchanged

### Requirement: setParameter API with bindingName and memberName
`MaterialInstance` SHALL provide a parameter write API addressed by `(bindingName, memberName)`:

```
setParameter(StringID bindingName, StringID memberName, float value)
setParameter(StringID bindingName, StringID memberName, int32_t value)
setParameter(StringID bindingName, StringID memberName, const Vec3f& value)
setParameter(StringID bindingName, StringID memberName, const Vec4f& value)
```

Each call SHALL locate the buffer slot by `bindingName`, then locate the member within that slot's reflection, verify the type, and write the value. If `bindingName` does not match any buffer slot, the system SHALL assert in debug builds.

#### Scenario: Write to named buffer and member
- **WHEN** `setParameter(StringID("SurfaceParams"), StringID("roughness"), 0.5f)` is called
- **THEN** the float 0.5 is written to the `roughness` member offset within the `SurfaceParams` buffer slot

#### Scenario: Wrong binding name asserts
- **WHEN** `setParameter(StringID("NonExistent"), StringID("x"), 1.0f)` is called and no buffer slot named `NonExistent` exists
- **THEN** an assertion fires in debug builds

### Requirement: Legacy convenience setters remain for single-buffer compatibility
The existing `setFloat(memberName, value)`, `setVec3(memberName, value)`, `setVec4(memberName, value)`, and `setInt(memberName, value)` convenience setters SHALL remain available. They SHALL search across all buffer slots for a member with the given name:
- If exactly one slot contains a matching member, write to it.
- If multiple slots contain a member with the same name, assert in debug builds with a diagnostic message.
- If no slot contains the member, assert in debug builds.

#### Scenario: Single-buffer convenience setter works
- **WHEN** one buffer slot exists containing member `shininess` and `setFloat(StringID("shininess"), 32.0f)` is called
- **THEN** the value is written to the correct offset in that slot

#### Scenario: Ambiguous member name asserts
- **WHEN** two buffer slots both contain a member named `opacity` and `setFloat(StringID("opacity"), 0.5f)` is called
- **THEN** an assertion fires in debug builds with a diagnostic about ambiguous member resolution

## MODIFIED Requirements

### Requirement: MaterialInstance allocates UBO buffer from reflection
`MaterialInstance::create(template)` SHALL walk material-owned buffer bindings from enabled passes using `MaterialTemplate::getMaterialBindings(pass)` and `isSystemOwnedBinding()`. For each unique material-owned buffer binding (by name), it SHALL create a `MaterialBufferSlot` with a zero-initialized byte buffer sized to the binding's `size`.

Cross-pass consistency for same-name buffer bindings SHALL be verified: if two passes declare the same binding name, the size and member layout MUST match; the system SHALL assert on mismatch.

Shaders without any material-owned buffer binding MUST produce a `MaterialInstance` with an empty slot collection; legacy convenience setter calls on such an instance MUST assert in debug builds.

The `MaterialParameterDataResource` wrapper SHALL accept the binding's resource type at construction (`UniformBuffer` or `StorageBuffer`) and return the corresponding `ResourceType` from `getType()`.

#### Scenario: Construction creates slots from reflection using ownership query
- **WHEN** `MaterialInstance::create(tmpl)` is called and the shader declares `SurfaceParams` (UBO, 48 bytes) and `DetailParams` (UBO, 32 bytes)
- **THEN** two buffer slots are created with sizes 48 and 32, each zero-initialized

#### Scenario: MaterialUBO name still works as an ordinary material-owned binding
- **WHEN** a shader declares `uniform MaterialUBO { vec3 baseColor; float shininess; }` and reflection reports a `UniformBuffer` binding named `MaterialUBO`
- **THEN** the system creates one buffer slot named `MaterialUBO` and the material instance functions identically to before this change

#### Scenario: Shader without material-owned buffers produces empty slot collection
- **WHEN** a shader's reflection bindings contain only `Texture2D` entries and system-owned `CameraUBO`
- **THEN** the resulting `MaterialInstance` has an empty buffer slot collection

#### Scenario: Setter type mismatch is an assertion failure
- **WHEN** `setParameter(StringID("MaterialUBO"), StringID("baseColor"), 1.0f)` is called and reflection reports `baseColor` with type `Vec3`
- **THEN** an assertion fires in debug builds and the buffer is not modified

### Requirement: getDescriptorResources returns UBO + textures in deterministic order
`MaterialInstance::getDescriptorResources(StringID pass)` SHALL return a vector of material-owned descriptor resources scoped to the target pass. The resolution SHALL:

1. Query `MaterialTemplate::getMaterialBindings(pass)` for the pass's material-owned bindings
2. For each binding, find the corresponding resource (buffer slot or texture) by binding name
3. Return resources sorted by ascending `(set << 16 | binding)` from the pass's reflection

The no-argument `getDescriptorResources()` SHALL be removed. All callers MUST provide a pass argument.

#### Scenario: Forward and shadow passes return different resource sets
- **WHEN** forward pass has `MaterialUBO` + `albedoMap` and shadow pass has only `MaterialUBO`
- **THEN** `getDescriptorResources(Pass_Forward)` returns 2 resources and `getDescriptorResources(Pass_Shadow)` returns 1

#### Scenario: Resources sorted by set/binding within a pass
- **WHEN** a pass has `MaterialUBO` at (set=2, binding=0) and `albedoMap` at (set=2, binding=1)
- **THEN** `getDescriptorResources(pass)` returns MaterialUBO first, then albedoMap

#### Scenario: Missing texture is skipped
- **WHEN** a pass reflection includes `albedoMap` but `setTexture` has not been called for it
- **THEN** that entry is omitted from the result

### Requirement: UBO GPU sync via cached IRenderResource wrapper
`MaterialInstance` SHALL construct one `MaterialParameterDataResource` per buffer slot during construction. `syncGpuData()` SHALL iterate all buffer slots and call `setDirty()` on each slot's resource that has its dirty flag set.

#### Scenario: syncGpuData propagates dirty state for all modified slots
- **WHEN** two buffer slots exist, one is modified via `setParameter`, and `syncGpuData()` is called
- **THEN** only the modified slot's resource has `setDirty()` invoked

#### Scenario: Buffer resource identity is stable
- **WHEN** `getDescriptorResources(pass)` is called twice on the same `MaterialInstance`
- **THEN** both calls return the same `IRenderResource` pointers for buffer entries (address equality)
