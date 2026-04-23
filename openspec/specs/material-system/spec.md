## Purpose

Define the current material system contract, including material templates, material instances, reflection-driven UBO access, and descriptor resources.
## Requirements
### Requirement: MaterialInstance is the sole material type
The system SHALL provide exactly one concrete material type, named `MaterialInstance`. All material pointers held by scene objects, render queues, and backend code MUST be `MaterialInstancePtr` values. The legacy `DrawMaterial` class and the legacy `BlinnPhongMaterialUBO` struct MUST NOT exist in the codebase after this change.

#### Scenario: Scene constructs materials via MaterialInstance
- **WHEN** a loader constructs a material for a `RenderableSubMesh`
- **THEN** the returned `MaterialInstancePtr` points to a `MaterialInstance` and the concrete type `DrawMaterial` is not referenced anywhere in `src/`

#### Scenario: MaterialInstance public surface is preserved
- **WHEN** rendering code calls `getShaderInfo(pass)`, `getPassFlag()`, `getRenderState(pass)`, `getDescriptorResources()`, or `getRenderSignature(pass)` on a `MaterialInstance`
- **THEN** each call returns a value consistent with the `MaterialTemplate`'s configuration and the instance's per-object state

### Requirement: MaterialTemplate requires a shader at construction
`MaterialTemplate::create(name, IShaderPtr shader)` SHALL require a non-null compiled shader. A template MUST expose `getShader()` so `MaterialInstance` can read reflection bindings during initialization. `MaterialTemplate` SHALL hold exactly one name-keyed binding cache (`std::unordered_map<StringID, ShaderResourceBinding>`) populated by `buildBindingCache()`, and MUST NOT carry a separate per-pass hash cache.

#### Scenario: Template construction requires a shader
- **WHEN** `MaterialTemplate::create("blinnphong_0", shader)` is called with a valid compiled `IShaderPtr`
- **THEN** the returned template's `getShader()` returns that shader and `findBinding(StringID("baseColor"))` returns the reflection binding for the `MaterialUBO`'s `baseColor` member after `buildBindingCache()` is called

#### Scenario: Template has no duplicate cache fields
- **WHEN** inspecting the `MaterialTemplate` class definition
- **THEN** `m_bindingCache` is declared exactly once and no `m_passHashCache` field exists

### Requirement: MaterialInstance allocates UBO buffer from reflection
`MaterialInstance::create(template)` SHALL walk material-owned buffer bindings from enabled passes using `MaterialTemplate::getMaterialBindings(pass)` and `isSystemOwnedBinding()`. For each unique material-owned buffer binding (by name), it SHALL create a `MaterialParameterData` object with a zero-initialized byte buffer sized to the binding's `size`.

Cross-pass consistency for same-name buffer bindings SHALL be verified: if two passes declare the same binding name, the size and member layout MUST match; the system SHALL assert on mismatch.

Shaders without any material-owned buffer binding MUST produce a `MaterialInstance` with an empty slot collection; legacy convenience setter calls on such an instance MUST assert in debug builds.

`MaterialParameterData` SHALL accept the binding's reflected layout and resource type at construction (`UniformBuffer` or `StorageBuffer`) and return the corresponding `ResourceType` from `getType()`.

#### Scenario: Construction creates slots from reflection using ownership query
- **WHEN** `MaterialInstance::create(tmpl)` is called and the shader declares `SurfaceParams` (UBO, 48 bytes) and `DetailParams` (UBO, 32 bytes)
- **THEN** two `MaterialParameterData` slots are created with sizes 48 and 32, each zero-initialized

#### Scenario: MaterialUBO name still works as an ordinary material-owned binding
- **WHEN** a shader declares `uniform MaterialUBO { vec3 baseColor; float shininess; }` and reflection reports a `UniformBuffer` binding named `MaterialUBO`
- **THEN** the system creates one `MaterialParameterData` slot named `MaterialUBO` and the material instance functions identically to before this change

#### Scenario: Shader without material-owned buffers produces empty slot collection
- **WHEN** a shader's reflection bindings contain only `Texture2D` entries and system-owned `CameraUBO`
- **THEN** the resulting `MaterialInstance` has an empty buffer slot collection

#### Scenario: Setter type mismatch is an assertion failure
- **WHEN** `setParameter(StringID("MaterialUBO"), StringID("baseColor"), 1.0f)` is called and reflection reports `baseColor` with type `Vec3`
- **THEN** an assertion fires in debug builds and the buffer is not modified

### Requirement: Reflection-driven UBO setters
`MaterialInstance` SHALL expose `setVec4(StringID, Vec4f)`, `setVec3(StringID, Vec3f)`, `setFloat(StringID, float)`, and `setInt(StringID, int32_t)`. Each setter SHALL look up the target member by matching `StringID(member.name) == id` in the cached `m_uboBinding->members` vector, verify the member's `ShaderPropertyType` matches the setter's expected type, and `memcpy` the value into `m_uboBuffer.data() + member.offset` using the byte width of the source type. A shared private helper `writeUboMember(StringID, const void*, size_t, ShaderPropertyType)` SHALL encapsulate the lookup + type check + memcpy logic.

#### Scenario: setVec4 writes 16 bytes at the reflected offset
- **WHEN** `setVec4(StringID("customColor"), Vec4f{1,0,0,1})` is called and reflection reports `customColor` at offset 16 with type `Vec4`
- **THEN** bytes 16..31 of `m_uboBuffer` equal the little-endian encoding of `{1.0f, 0.0f, 0.0f, 1.0f}`

#### Scenario: setVec3 writes 12 bytes and leaves the trailing 4 bytes untouched
- **WHEN** `setVec3(StringID("baseColor"), Vec3f{0.5f, 0.5f, 0.5f})` is called, reflection reports `baseColor` at offset 0, and the next member `shininess` at offset 12 holds a previously written float
- **THEN** only bytes 0..11 are overwritten and the `shininess` value at offset 12..15 is preserved

#### Scenario: setFloat and setInt write 4 bytes with type checking
- **WHEN** `setFloat(StringID("shininess"), 32.0f)` is called and reflection reports `shininess` with type `Float` at offset 12
- **THEN** bytes 12..15 of `m_uboBuffer` equal the little-endian encoding of `32.0f`

#### Scenario: Setter type mismatch is an assertion failure
- **WHEN** `setFloat(StringID("baseColor"), 1.0f)` is called and reflection reports `baseColor` with type `Vec3`
- **THEN** an assertion fires in debug builds and the buffer is not modified

#### Scenario: Unknown member name asserts and is ignored
- **WHEN** `setVec4(StringID("doesNotExist"), …)` is called and no member with that name exists in `m_uboBinding->members`
- **THEN** an assertion fires in debug builds and `m_uboBuffer` is unchanged

### Requirement: Texture bindings by StringID
`MaterialInstance::setTexture(StringID id, CombinedTextureSamplerPtr tex)` SHALL look up `id` via `MaterialTemplate::findBinding(id)`, assert that the resulting binding's type is `Texture2D` or `TextureCube`, and store the sampler in `m_textures[id]`. `MaterialInstance` MUST NOT expose a setter that takes a raw `uint32_t` set/binding pair — callers use the shader-declared name only. `CombinedTextureSamplerPtr` (rather than raw `TexturePtr`) is used because the concrete resource passed to the backend descriptor layer must already implement `IGpuResource`, which `CombinedTextureSampler` does and `Texture` does not.

#### Scenario: Texture bound to a reflected sampler name
- **WHEN** `setTexture(StringID("albedoMap"), tex)` is called and `findBinding(StringID("albedoMap"))` returns a `Texture2D` binding
- **THEN** the texture is stored under that `StringID` in `m_textures`

#### Scenario: Texture bound to a non-sampler name asserts
- **WHEN** `setTexture(StringID("baseColor"), tex)` is called and `baseColor` is a UBO scalar member
- **THEN** an assertion fires in debug builds and `m_textures` is unchanged

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

### Requirement: UBO GPU sync via cached IGpuResource wrapper
`MaterialInstance` SHALL construct one `MaterialParameterData` per material-owned buffer binding during construction. `syncGpuData()` SHALL iterate all parameter slots and call `setDirty()` on each slot whose internal dirty flag is set.

#### Scenario: syncGpuData propagates dirty state for all modified slots
- **WHEN** two buffer slots exist, one is modified via `setParameter`, and `syncGpuData()` is called
- **THEN** only the modified slot's resource has `setDirty()` invoked

#### Scenario: Buffer resource identity is stable
- **WHEN** `getDescriptorResources(pass)` is called twice on the same `MaterialInstance`
- **THEN** both calls return the same `IGpuResource` pointers for buffer entries (address equality)

### Requirement: Core-layer UBO byte-buffer resource wrapper
The core layer SHALL provide a `UboByteBufferResource` class that implements `IGpuResource` over a non-owning reference to a `std::vector<uint8_t>`. Its `getRawData()` MUST return a pointer computed from the referenced vector at call time (not a stale copy captured at construction), `getByteSize()` MUST return the byte count passed at construction, `getType()` MUST return `ResourceType::UniformBuffer`, and `setDirty()` MUST mark the resource for upload through the existing `VulkanResourceManager::syncResource()` path. `MaterialInstance` SHALL construct exactly one such wrapper for its `m_uboBuffer` during its own construction.

> Rationale: this wrapper is the same shape as the existing `SkeletonUBO` in `src/core/asset/skeleton.hpp` — both live in core because `IGpuResource` is a core contract and no backend-specific code is required to adapt a raw byte buffer into that contract.

#### Scenario: Wrapper exposes buffer bytes without copy
- **WHEN** a `UboByteBufferResource` is created over a 48-byte vector
- **THEN** `wrapper.getRawData()` returns a pointer whose dereferenced content matches `buffer.data()` byte-for-byte and `wrapper.getByteSize() == 48`

#### Scenario: Modifying the source buffer is visible through the wrapper
- **WHEN** bytes in the source buffer change after wrapping and `getRawData()` is read again
- **THEN** the wrapper returns the updated bytes (no stale copy)

### Requirement: Loader returns MaterialInstance
The file-shader loader for `blinnphong_0` SHALL be named `loadBlinnPhongMaterial` (or similar, not containing `DrawMaterial`) and SHALL return a `MaterialInstancePtr`. It SHALL compile the shader, reflect bindings, create a `MaterialTemplate`, configure at least one `MaterialPassDefinition`, call `buildBindingCache()`, create a `MaterialInstance`, and seed reasonable default uniform values via the reflection-driven setters. The legacy file `blinnphong_draw_material_loader.{hpp,cpp}` MUST be removed or rewritten in place.

#### Scenario: Loader produces a ready-to-render MaterialInstance
- **WHEN** `loadBlinnPhongMaterial()` is called
- **THEN** the returned `MaterialInstancePtr` has a non-empty `m_uboBuffer`, a resolvable `getShaderInfo()`, and default uniform values written via `setVec3` / `setFloat` / `setInt`

#### Scenario: No DrawMaterial references remain
- **WHEN** searching `src/` (excluding `openspec/changes/archive/`) for the symbol `DrawMaterial`
- **THEN** no matches are found

### Requirement: Engine-wide draw push constant ABI is model-only
The renderer SHALL use a single engine-wide draw push constant ABI consisting only of the model transform payload:

`struct alignas(16) PC_Base { Mat4f model; };`

If `PC_Draw` remains in the codebase as a compatibility alias or extension point, it MUST NOT add `enableLighting`, `enableSkinning`, or any other field that changes shader interface or pipeline shape. Shader-side push constant blocks used by the forward material path MUST match this model-only ABI exactly.

#### Scenario: Forward shader uses model-only push constant
- **WHEN** the forward material path compiles `blinnphong_0` shaders
- **THEN** the push constant block layout matches the engine-wide model-only ABI and contains no skinning or lighting feature flags

### Requirement: MaterialTemplate owns shader variants per pass
Shader variants that change shader code shape or pipeline identity SHALL belong to `MaterialTemplate` / loader output, not to `MaterialInstance`. For each configured pass, the loader MUST determine the enabled variant set, pass that set into shader compilation, and persist the same set in `MaterialPassDefinition::shaderSet.variants`.

`MaterialInstance` SHALL continue to own only runtime instance parameters such as UBO values, textures, and pass enable state. Instance-level parameter writes MUST NOT introduce a new variant identity dimension.

#### Scenario: Loader persists a skinning variant on the template pass
- **WHEN** a loader creates a material template for a pass that enables skinning
- **THEN** the pass's shader compilation input and stored `MaterialPassDefinition::shaderSet.variants` both include the skinning variant

#### Scenario: Runtime parameter writes do not create variants
- **WHEN** a `MaterialInstance` updates UBO values or textures for an existing pass
- **THEN** the template-owned variant set for that pass remains unchanged

### Requirement: Forward material loader validates and persists the variant contract
The loader for `blinnphong_0` SHALL be the authority for forward-shader variant-set construction. For every configured pass, it MUST:

- declare the enabled subset of `USE_VERTEX_COLOR`, `USE_UV`, `USE_LIGHTING`, `USE_NORMAL_MAP`, and `USE_SKINNING`
- pass that exact subset into shader compilation
- persist that same subset in `MaterialPassDefinition::shaderSet.variants`
- validate the logical dependencies of the variant set before returning a material/template result

If the enabled subset violates any mandatory dependency, the loader MUST emit a `FATAL` log and terminate the process immediately.

#### Scenario: Loader rejects normal map without lighting
- **WHEN** the loader constructs a `blinnphong_0` pass with `USE_NORMAL_MAP=1` and `USE_LIGHTING=0`
- **THEN** the loader emits a `FATAL` validation error and terminates instead of returning a material/template

#### Scenario: Loader stores the validated variant set in compile input and pass metadata
- **WHEN** the loader constructs a valid `blinnphong_0` pass with `USE_VERTEX_COLOR=1`, `USE_UV=1`, and `USE_LIGHTING=1`
- **THEN** the shader compilation input and `MaterialPassDefinition::shaderSet.variants` contain the same validated variant subset

### Requirement: MaterialInstance owns instance-level pass enable state
`MaterialTemplate` SHALL remain the owner of pass definitions through its `pass -> MaterialPassDefinition` mapping. `MaterialInstance` SHALL own only the instance-level enabled subset of those template-defined passes.

A newly created `MaterialInstance` MUST enable every pass defined by its template by default.

`MaterialInstance` SHALL provide instance-level APIs whose semantics cover at minimum:

- querying whether a pass is enabled for the instance
- enabling or disabling a specific pass for the instance
- querying the set of currently enabled passes

If a caller attempts to enable or disable a pass that is not defined by the template, the system MUST emit a `FATAL` log and terminate the process immediately.

#### Scenario: New instance starts with all template passes enabled
- **WHEN** a `MaterialTemplate` defines both `Pass_Forward` and `Pass_Shadow` and a `MaterialInstance` is created from that template
- **THEN** the new instance reports both passes as enabled before any explicit pass-state mutation

#### Scenario: Undefined pass enable request terminates
- **WHEN** `setPassEnabled(Pass_Deferred, false)` is called on a `MaterialInstance` whose template does not define `Pass_Deferred`
- **THEN** the system logs a `FATAL` error and terminates immediately

### Requirement: getPassFlag is derived from defined and enabled passes
`MaterialInstance::getPassFlag()` SHALL be derived from the intersection of:

- passes defined by the template
- passes currently enabled on the instance

The implementation MUST NOT treat a separate manually maintained bitmask as the authoritative truth if that value can diverge from the instance's enabled pass set.

#### Scenario: Disabled template pass is absent from getPassFlag
- **WHEN** a template defines `Pass_Forward | Pass_Shadow`, the instance disables `Pass_Shadow`, and `getPassFlag()` is queried
- **THEN** the returned `ResourcePassFlag` includes `Forward` and excludes `Shadow`

### Requirement: Material render-state queries are pass-aware
Material render-state queries SHALL be pass-aware. The system MUST provide a material render-state lookup path keyed by `StringID pass`, and `MaterialInstance` render-state access MUST resolve the `RenderState` from the corresponding template-defined `MaterialPassDefinition` for that pass.

The implementation MUST NOT preserve a Forward-only transitional meaning as the authoritative material render-state contract.

#### Scenario: Forward and shadow passes return different render states
- **WHEN** a template defines distinct `RenderState` values for `Pass_Forward` and `Pass_Shadow`
- **THEN** querying the material render state for `Pass_Forward` returns the forward state and querying it for `Pass_Shadow` returns the shadow state

### Requirement: Ordinary material parameter updates are not structural pass changes
Instance-level runtime parameter updates such as `setFloat`, `setInt`, `setVec*`, `setTexture`, and `updateUBO` SHALL NOT be treated as structural pass-state changes. These operations MUST continue to affect only runtime material data and resource-dirty propagation.

Only pass enable/disable mutations are structural changes within `MaterialInstance` for the purposes of pass participation and `SceneNode` revalidation.

#### Scenario: UBO write leaves enabled pass set unchanged
- **WHEN** `setFloat(...)` and `updateUBO()` are called on a `MaterialInstance`
- **THEN** the instance's enabled pass set and derived `getPassFlag()` value remain unchanged

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

For buffer types (`UniformBuffer`, `StorageBuffer`), `MaterialInstance` SHALL create a buffer slot with independent byte buffer, dirty state, and `IGpuResource` wrapper.

For texture types (`Texture2D`, `TextureCube`), the existing `setTexture(StringID, CombinedTextureSamplerPtr)` mechanism SHALL continue to apply.

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
- an independent `IGpuResourcePtr` wrapper
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
