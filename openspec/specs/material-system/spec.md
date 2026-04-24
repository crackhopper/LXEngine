## Purpose

Define the current material system contract, including material templates, material instances, reflection-driven UBO access, and descriptor resources.
## Requirements
### Requirement: MaterialInstance is the sole material type
The system SHALL provide exactly one concrete material type, named `MaterialInstance`. All material pointers held by scene objects, render queues, and backend code MUST be `MaterialInstanceSharedPtr` values. The legacy `DrawMaterial` class and the legacy `BlinnPhongMaterialUBO` struct MUST NOT exist in the codebase after this change.

#### Scenario: Scene constructs materials via MaterialInstance
- **WHEN** a loader constructs a material for a `SceneNode` (or another `IRenderable` implementation)
- **THEN** the returned `MaterialInstanceSharedPtr` points to a `MaterialInstance` and the concrete type `DrawMaterial` is not referenced anywhere in `src/`

#### Scenario: MaterialInstance public surface is preserved
- **WHEN** rendering code calls `getPassShader(pass)`, `getPassRenderState(pass)`, `getDescriptorResources(pass)`, or `getPipelineSignature(pass)` on a `MaterialInstance`
- **THEN** each call returns a value consistent with the `MaterialTemplate`'s configuration and the instance's per-object state

### Requirement: MaterialTemplate canonicalizes the material interface
`MaterialTemplate::create(name)` SHALL construct an empty template shell without requiring a shader at construction time. Shaders belong to individual `MaterialPassDefinition::shaderProgram` values, and `MaterialTemplate::rebuildMaterialInterface()` SHALL derive the material-facing structure from those pass definitions.

The template SHALL own:

- one canonical material binding table keyed by `StringID(binding.name)`
- one `pass -> ordered vector<StringID>` view telling each pass which canonical bindings it uses

The template SHALL be the sole owner of cross-pass structural validation for material-owned bindings.

#### Scenario: Template builds canonical bindings from pass shaders
- **WHEN** `MaterialTemplate::create("blinnphong_0")` is called, pass definitions are populated with valid shaders, and `rebuildMaterialInterface()` is called
- **THEN** `findCanonicalMaterialBinding(StringID("MaterialUBO"))` returns the canonical reflected binding and `getPassMaterialBindingIds(pass)` returns the ordered pass-local binding-id view

#### Scenario: Template owns canonical material-binding metadata
- **WHEN** inspecting the `MaterialTemplate` class definition
- **THEN** it exposes one canonical material binding table plus one per-pass binding-id map, and no duplicate flattened cache fields exist

### Requirement: MaterialInstance allocates canonical parameter data from the template
`MaterialInstance::create(template)` SHALL walk the template's canonical material binding table. For each canonical material-owned buffer binding, it SHALL create a `ParameterBuffer` object with a zero-initialized byte buffer sized to the binding's `size`.

Cross-pass consistency for same-name bindings SHALL be verified during `MaterialTemplate::rebuildMaterialInterface()`: if two passes declare the same material-owned binding name, descriptor type, descriptor count, set/binding location, buffer size, and reflected member layout MUST match. Structural mismatch SHALL be treated as a fatal authoring error.

Shaders without any material-owned buffer binding MUST produce a `MaterialInstance` with an empty canonical parameter-data collection.

`ParameterBuffer` SHALL accept the binding's reflected layout and resource type at construction (`UniformBuffer` or `StorageBuffer`) and return the corresponding `ResourceType` from `getType()`.

#### Scenario: Construction creates slots from reflection using ownership query
- **WHEN** `MaterialInstance::create(tmpl)` is called and the shader declares `SurfaceParams` (UBO, 48 bytes) and `DetailParams` (UBO, 32 bytes)
- **THEN** two `ParameterBuffer` objects are created with sizes 48 and 32, each zero-initialized

#### Scenario: MaterialUBO name still works as an ordinary material-owned binding
- **WHEN** a shader declares `uniform MaterialUBO { vec3 baseColor; float shininess; }` and reflection reports a `UniformBuffer` binding named `MaterialUBO`
- **THEN** the system creates one canonical `ParameterBuffer` object named `MaterialUBO` and the material instance functions identically to before this change

#### Scenario: Shader without material-owned buffers produces empty canonical parameter data
- **WHEN** a shader's reflection bindings contain only `Texture2D` entries and system-owned `CameraUBO`
- **THEN** the resulting `MaterialInstance` has an empty canonical parameter-data collection

#### Scenario: Setter type mismatch is an assertion failure
- **WHEN** `setParameter(StringID("MaterialUBO"), StringID("baseColor"), 1.0f)` is called and reflection reports `baseColor` with type `Vec3`
- **THEN** an assertion fires in debug builds and the buffer is not modified

### Requirement: Reflection-driven parameter writes are addressed by binding and member
`MaterialInstance` SHALL expose only the explicit parameter-write API:

- `setParameter(StringID bindingName, StringID memberName, float)`
- `setParameter(StringID bindingName, StringID memberName, int32_t)`
- `setParameter(StringID bindingName, StringID memberName, const Vec3f&)`
- `setParameter(StringID bindingName, StringID memberName, const Vec4f&)`

Each overload SHALL locate the canonical `ParameterBuffer` object by `bindingName`, find the reflected member in that binding, verify the member type, and write into the byte buffer at the reflected offset. `ParameterBuffer::writeBindingMember(...)` SHALL encapsulate the lookup + type check + memcpy logic.

#### Scenario: setParameter writes Vec4 at the reflected offset
- **WHEN** `setParameter(StringID("MaterialUBO"), StringID("customColor"), Vec4f{1,0,0,1})` is called and reflection reports `customColor` at offset 16 with type `Vec4`
- **THEN** bytes 16..31 of that binding's parameter buffer equal the little-endian encoding of `{1.0f, 0.0f, 0.0f, 1.0f}`

#### Scenario: setParameter writes Vec3 and leaves the trailing 4 bytes untouched
- **WHEN** `setParameter(StringID("MaterialUBO"), StringID("baseColor"), Vec3f{0.5f, 0.5f, 0.5f})` is called, reflection reports `baseColor` at offset 0, and the next member `shininess` at offset 12 holds a previously written float
- **THEN** only bytes 0..11 are overwritten and the `shininess` value at offset 12..15 is preserved

#### Scenario: scalar setParameter writes 4 bytes with type checking
- **WHEN** `setParameter(StringID("MaterialUBO"), StringID("shininess"), 32.0f)` is called and reflection reports `shininess` with type `Float` at offset 12
- **THEN** bytes 12..15 of that binding's parameter buffer equal the little-endian encoding of `32.0f`

#### Scenario: Setter type mismatch is an assertion failure
- **WHEN** `setParameter(StringID("MaterialUBO"), StringID("baseColor"), 1.0f)` is called and reflection reports `baseColor` with type `Vec3`
- **THEN** an assertion fires in debug builds and the buffer is not modified

#### Scenario: Unknown member name asserts and is ignored
- **WHEN** `setParameter(StringID("MaterialUBO"), StringID("doesNotExist"), Vec4f{1,0,0,1})` is called and no member with that name exists in the reflected binding members
- **THEN** an assertion fires in debug builds and the parameter buffer is unchanged

### Requirement: Texture bindings by StringID
`MaterialInstance::setTexture(StringID id, CombinedTextureSamplerSharedPtr tex)` SHALL look up `id` via `MaterialTemplate::findCanonicalMaterialBinding(id)`, assert that the resulting binding's type is `Texture2D` or `TextureCube`, and store the sampler in `m_textureBindingsByName[id]`. `MaterialInstance` MUST NOT expose a setter that takes a raw `uint32_t` set/binding pair — callers use the shader-declared name only. `CombinedTextureSamplerSharedPtr` (rather than raw `TextureSharedPtr`) is used because the concrete resource passed to the backend descriptor layer must already implement `IGpuResource`, which `CombinedTextureSampler` does and `Texture` does not.

#### Scenario: Texture bound to a reflected sampler name
- **WHEN** `setTexture(StringID("albedoMap"), tex)` is called and `findCanonicalMaterialBinding(StringID("albedoMap"))` returns a `Texture2D` binding
- **THEN** the texture is stored under that `StringID` in `m_textureBindingsByName`

#### Scenario: Texture bound to a non-sampler name asserts
- **WHEN** `setTexture(StringID("baseColor"), tex)` is called and `baseColor` is a UBO scalar member
- **THEN** an assertion fires in debug builds and `m_textureBindingsByName` is unchanged

### Requirement: getDescriptorResources returns UBO + textures in deterministic order
`MaterialInstance::getDescriptorResources(StringID pass)` SHALL return a vector of material-owned descriptor resources scoped to the target pass. The resolution SHALL:

1. Query `MaterialTemplate::getPassMaterialBindingIds(pass)` for the pass's ordered canonical binding ids
2. Resolve each id through the template's canonical material binding table
3. For each binding, find the corresponding runtime resource (parameter data or texture) by binding name
4. Return resources sorted by ascending `(set << 16 | binding)` from the pass's reflection

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
`MaterialInstance` SHALL construct one `ParameterBuffer` per canonical material-owned buffer binding during construction. `syncGpuData()` SHALL iterate all buffer-binding objects and call `setDirty()` on each object whose internal dirty flag is set.

#### Scenario: syncGpuData propagates dirty state for all modified slots
- **WHEN** two canonical parameter bindings exist, one is modified via `setParameter`, and `syncGpuData()` is called
- **THEN** only the modified binding's resource has `setDirty()` invoked

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
The file-shader loader for `blinnphong_0` SHALL be named `loadBlinnPhongMaterial` (or similar, not containing `DrawMaterial`) and SHALL return a `MaterialInstanceSharedPtr`. It SHALL compile the shader, reflect bindings, create a `MaterialTemplate`, configure at least one `MaterialPassDefinition`, call `rebuildMaterialInterface()`, create a `MaterialInstance`, and seed reasonable default uniform values via `setParameter(bindingName, memberName, value)`. The legacy file `blinnphong_draw_material_loader.{hpp,cpp}` MUST be removed or rewritten in place.

#### Scenario: Loader produces a ready-to-render MaterialInstance
- **WHEN** `loadBlinnPhongMaterial()` is called
- **THEN** the returned `MaterialInstanceSharedPtr` has non-empty canonical parameter data, a resolvable `getPassShader()`, and default uniform values written via `setParameter(...)`

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
Shader variants that change shader code shape or pipeline identity SHALL belong to `MaterialTemplate` / loader output, not to `MaterialInstance`. For each configured pass, the loader MUST determine the enabled variant set, pass that set into shader compilation, and persist the same set in `MaterialPassDefinition::shaderProgram.variants`.

`MaterialInstance` SHALL continue to own only runtime instance parameters such as UBO values, textures, and pass enable state. Instance-level parameter writes MUST NOT introduce a new variant identity dimension.

#### Scenario: Loader persists a skinning variant on the template pass
- **WHEN** a loader creates a material template for a pass that enables skinning
- **THEN** the pass's shader compilation input and stored `MaterialPassDefinition::shaderProgram.variants` both include the skinning variant

#### Scenario: Runtime parameter writes do not create variants
- **WHEN** a `MaterialInstance` updates UBO values or textures for an existing pass
- **THEN** the template-owned variant set for that pass remains unchanged

### Requirement: Forward material loader validates and persists the variant contract
The loader for `blinnphong_0` SHALL be the authority for forward-shader variant-set construction. For every configured pass, it MUST:

- declare the enabled subset of `USE_VERTEX_COLOR`, `USE_UV`, `USE_LIGHTING`, `USE_NORMAL_MAP`, and `USE_SKINNING`
- pass that exact subset into shader compilation
- persist that same subset in `MaterialPassDefinition::shaderProgram.variants`
- validate the logical dependencies of the variant set before returning a material/template result

If the enabled subset violates any mandatory dependency, the loader MUST emit a `FATAL` log and terminate the process immediately.

#### Scenario: Loader rejects normal map without lighting
- **WHEN** the loader constructs a `blinnphong_0` pass with `USE_NORMAL_MAP=1` and `USE_LIGHTING=0`
- **THEN** the loader emits a `FATAL` validation error and terminates instead of returning a material/template

#### Scenario: Loader stores the validated variant set in compile input and pass metadata
- **WHEN** the loader constructs a valid `blinnphong_0` pass with `USE_VERTEX_COLOR=1`, `USE_UV=1`, and `USE_LIGHTING=1`
- **THEN** the shader compilation input and `MaterialPassDefinition::shaderProgram.variants` contain the same validated variant subset

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

### Requirement: Enabled-pass queries are derived from template definitions
`MaterialInstance` SHALL derive pass participation from:

- passes defined by the template
- passes currently enabled on the instance

The implementation MUST NOT treat a separate manually maintained pass bitmask as the authoritative truth if that value can diverge from the instance's enabled pass set.

#### Scenario: Disabled template pass is absent from enabled-pass queries
- **WHEN** a template defines `Pass_Forward` and `Pass_Shadow`, the instance disables `Pass_Shadow`, and `getEnabledPasses()` is queried
- **THEN** the returned pass set includes `Pass_Forward` and excludes `Pass_Shadow`

### Requirement: Material render-state queries are pass-aware
Material render-state queries SHALL be pass-aware. The system MUST provide a material render-state lookup path keyed by `StringID pass`, and `MaterialInstance` render-state access MUST resolve the `RenderState` from the corresponding template-defined `MaterialPassDefinition` for that pass.

The implementation MUST NOT preserve a Forward-only transitional meaning as the authoritative material render-state contract.

#### Scenario: Forward and shadow passes return different render states
- **WHEN** a template defines distinct `RenderState` values for `Pass_Forward` and `Pass_Shadow`
- **THEN** querying the material render state for `Pass_Forward` returns the forward state and querying it for `Pass_Shadow` returns the shadow state

### Requirement: Ordinary material parameter updates are not structural pass changes
Instance-level runtime parameter updates such as `setParameter(...)`, `setTexture(...)`, and `syncGpuData()` SHALL NOT be treated as structural pass-state changes. These operations MUST continue to affect only runtime material data and resource-dirty propagation.

Only pass enable/disable mutations are structural changes within `MaterialInstance` for the purposes of pass participation and `SceneNode` revalidation.

#### Scenario: Parameter writes leave enabled pass set unchanged
- **WHEN** `setParameter(...)` and `syncGpuData()` are called on a `MaterialInstance`
- **THEN** the instance's enabled pass set and derived `getEnabledPasses()` result remain unchanged

### Requirement: MaterialTemplate builds per-pass material-owned binding interface
`MaterialTemplate::rebuildMaterialInterface()` SHALL build a canonical material binding table plus a per-pass ordered binding-id list for each configured pass. For each pass, the list SHALL contain only bindings classified as material-owned by `isSystemOwnedBinding()`. The per-pass list SHALL preserve the reflection order from that pass's shader.

`MaterialTemplate` SHALL expose `getPassMaterialBindingIds(StringID pass)` returning the material-owned binding ids for a given pass. If the pass is not defined, it SHALL return an empty list.

#### Scenario: Per-pass material bindings exclude system-owned
- **WHEN** a shader declares `CameraUBO`, `MaterialUBO`, and `albedoMap` bindings and the template is built with one forward pass
- **THEN** `getPassMaterialBindingIds(Pass_Forward)` returns only the ids for `MaterialUBO` and `albedoMap`, not `CameraUBO`

#### Scenario: Different passes may have different material bindings
- **WHEN** a forward pass shader declares `MaterialUBO` and `albedoMap`, and a shadow pass shader declares only `MaterialUBO`
- **THEN** `getPassMaterialBindingIds(Pass_Forward)` includes both, and `getPassMaterialBindingIds(Pass_Shadow)` includes only `MaterialUBO`

### Requirement: Cross-pass same-name binding conflict detection
During `rebuildMaterialInterface()`, if two passes contain a material-owned binding with the same name, the system SHALL verify that the bindings are consistent in:
- descriptor type (`ShaderPropertyType`)
- descriptor count
- set/binding location
- buffer size (for buffer-typed bindings)
- member layout (for `UniformBuffer` bindings)

If inconsistent, the system SHALL emit a `FATAL` log with the binding name, pass name, and the specific inconsistency, then terminate immediately.

#### Scenario: Consistent same-name bindings across passes produce one canonical binding
- **WHEN** forward and shadow passes both declare `MaterialUBO` as a `UniformBuffer` with identical size and members
- **THEN** one canonical binding is created and both passes reference it by the same `StringID`

#### Scenario: Inconsistent same-name bindings fail fast
- **WHEN** forward declares `MaterialUBO` with size 32 and shadow declares `MaterialUBO` with size 48
- **THEN** the system emits a fatal log identifying the size mismatch and terminates

### Requirement: First-version supported material-owned descriptor types
The material system SHALL formally support the following descriptor types for material-owned bindings in the first version:
- `UniformBuffer`
- `StorageBuffer`
- `Texture2D`
- `TextureCube`

For buffer types (`UniformBuffer`, `StorageBuffer`), `MaterialInstance` SHALL create canonical parameter data with independent byte buffer, dirty state, and `IGpuResource` behavior.

For texture types (`Texture2D`, `TextureCube`), the existing `setTexture(StringID, CombinedTextureSamplerSharedPtr)` mechanism SHALL continue to apply.

#### Scenario: StorageBuffer binding creates canonical parameter data
- **WHEN** a shader declares a non-system-owned `StorageBuffer` binding named `ParticleData` with size 256
- **THEN** `MaterialInstance` creates canonical parameter data for it with a 256-byte zero-initialized buffer

#### Scenario: TextureCube binding handled by setTexture
- **WHEN** a shader declares a `TextureCube` binding named `envMap`
- **THEN** `setTexture(StringID("envMap"), cubeTex)` stores the resource under that name

### Requirement: Unsupported descriptor types fail fast
If a material-owned binding has a descriptor type not in the supported set (e.g., `Sampler` without combined image, storage image, input attachment), the system SHALL emit a `FATAL` log with binding name and type and terminate immediately during `MaterialTemplate::rebuildMaterialInterface()`.

#### Scenario: Standalone Sampler binding terminates
- **WHEN** a shader declares a material-owned `Sampler` binding named `customSampler`
- **THEN** the system emits a `FATAL` log and terminates during template interface rebuild

### Requirement: MaterialInstance supports multiple canonical parameter bindings
`MaterialInstance` SHALL replace the single `m_uboBuffer` / `m_uboBinding` / `m_uboResource` with a collection of canonical parameter bindings, each keyed by binding name. Each entry SHALL have:
- an independent `std::vector<uint8_t>` byte buffer
- a non-owning pointer to the `ShaderResourceBinding`
- `IGpuResource` behavior through `ParameterBuffer`
- an independent dirty flag

The parameter-data collection SHALL be built during construction by iterating canonical material-owned buffer bindings from the template.

#### Scenario: Two material-owned UBOs each get canonical parameter data
- **WHEN** a shader declares `SurfaceParams` (UBO, 16 bytes) and `DetailParams` (UBO, 32 bytes)
- **THEN** `MaterialInstance` holds two canonical parameter-data objects with sizes 16 and 32 respectively

#### Scenario: Single-buffer materials continue to work
- **WHEN** a shader declares only one material-owned UBO named `MaterialUBO`
- **THEN** `MaterialInstance` holds exactly one canonical parameter-data object and `setParameter(...)` works unchanged

### Requirement: setParameter API with bindingName and memberName
`MaterialInstance` SHALL provide a parameter write API addressed by `(bindingName, memberName)`:

```
setParameter(StringID bindingName, StringID memberName, float value)
setParameter(StringID bindingName, StringID memberName, int32_t value)
setParameter(StringID bindingName, StringID memberName, const Vec3f& value)
setParameter(StringID bindingName, StringID memberName, const Vec4f& value)
```

Each call SHALL locate the canonical parameter binding by `bindingName`, then locate the member within that binding's reflection, verify the type, and write the value. If `bindingName` does not match any canonical parameter binding, the system SHALL assert in debug builds.

#### Scenario: Write to named buffer and member
- **WHEN** `setParameter(StringID("SurfaceParams"), StringID("roughness"), 0.5f)` is called
- **THEN** the float 0.5 is written to the `roughness` member offset within the `SurfaceParams` parameter buffer

#### Scenario: Wrong binding name asserts
- **WHEN** `setParameter(StringID("NonExistent"), StringID("x"), 1.0f)` is called and no canonical parameter binding named `NonExistent` exists
- **THEN** an assertion fires in debug builds
