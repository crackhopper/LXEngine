## ADDED Requirements

### Requirement: MaterialInstance is the sole IMaterial implementation
The system SHALL provide exactly one concrete implementation of `IMaterial`, named `MaterialInstance`. All `MaterialPtr` values held by scene objects, render queues, and backend code MUST refer to `MaterialInstance` instances. The legacy `DrawMaterial` class and the legacy `BlinnPhongMaterialUBO` struct MUST NOT exist in the codebase after this change.

#### Scenario: Scene constructs materials via MaterialInstance
- **WHEN** a loader constructs a material for a `RenderableSubMesh`
- **THEN** the returned `MaterialPtr` points to a `MaterialInstance` and the concrete type `DrawMaterial` is not referenced anywhere in `src/`

#### Scenario: IMaterial virtual surface is preserved
- **WHEN** rendering code calls `getShaderInfo()`, `getPassFlag()`, `getRenderState()`, `getDescriptorResources()`, or `getRenderSignature(pass)` on a `MaterialInstance`
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
`MaterialInstance::create(template, passFlag)` SHALL walk the template shader's reflection bindings, locate the `ShaderPropertyType::UniformBuffer` whose `name` is exactly `"MaterialUBO"`, and allocate `m_uboBuffer` sized to that binding's `size`, zero-initialized. The binding pointer SHALL be cached as `m_uboBinding` (non-owning). Shaders without a `MaterialUBO` block MUST produce a `MaterialInstance` with an empty buffer and a null `m_uboBinding`; all setter calls on such an instance MUST assert in debug builds.

> **Convention**: scene-level UBOs such as `LightUBO`, `CameraUBO`, and skinning `Bones` also appear in the same reflection list but belong to other owners (the scene / camera / skeleton). Matching on the name `"MaterialUBO"` is how `MaterialInstance` identifies the block it owns without taking an explicit configuration key. Shaders authored for the renderer are expected to follow this naming convention.

#### Scenario: Construction sizes buffer from reflection
- **WHEN** `MaterialInstance::create(tmpl)` is called and the shader's reflection reports a `UniformBuffer` binding with `size = 32`
- **THEN** `m_uboBuffer.size() == 32` and every byte is zero

#### Scenario: Shader without UBO produces empty buffer
- **WHEN** a shader's reflection bindings contain only `Texture2D` entries
- **THEN** the resulting `MaterialInstance` has `m_uboBuffer.empty() == true` and `m_uboBinding == nullptr`

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
`MaterialInstance::setTexture(StringID id, CombinedTextureSamplerPtr tex)` SHALL look up `id` via `MaterialTemplate::findBinding(id)`, assert that the resulting binding's type is `Texture2D` or `TextureCube`, and store the sampler in `m_textures[id]`. `MaterialInstance` MUST NOT expose a setter that takes a raw `uint32_t` set/binding pair — callers use the shader-declared name only. `CombinedTextureSamplerPtr` (rather than raw `TexturePtr`) is used because the concrete resource passed to the backend descriptor layer must already implement `IRenderResource`, which `CombinedTextureSampler` does and `Texture` does not.

#### Scenario: Texture bound to a reflected sampler name
- **WHEN** `setTexture(StringID("albedoMap"), tex)` is called and `findBinding(StringID("albedoMap"))` returns a `Texture2D` binding
- **THEN** the texture is stored under that `StringID` in `m_textures`

#### Scenario: Texture bound to a non-sampler name asserts
- **WHEN** `setTexture(StringID("baseColor"), tex)` is called and `baseColor` is a UBO scalar member
- **THEN** an assertion fires in debug builds and `m_textures` is unchanged

### Requirement: getDescriptorResources returns UBO + textures in deterministic order
`MaterialInstance::getDescriptorResources()` SHALL return a vector containing (1) the single UBO `IRenderResource` wrapper when `m_uboBinding` is non-null and `m_uboBuffer` is non-empty, followed by (2) every texture in `m_textures` sorted by ascending `(set << 16 | binding)` derived from `MaterialTemplate::findBinding(id)`. Textures whose `StringID` key does not resolve to a reflection binding MUST be skipped.

#### Scenario: Single UBO followed by sorted textures
- **WHEN** a `MaterialInstance` has UBO binding at (set=2, binding=0), `albedoMap` at (set=2, binding=1), and `normalMap` at (set=2, binding=2), all populated
- **THEN** `getDescriptorResources()` returns exactly three entries in that order

#### Scenario: Missing binding is skipped
- **WHEN** `m_textures` contains a key whose `findBinding` returns `nullopt` (stale reference after template reload, etc.)
- **THEN** that texture is omitted from the result and no crash occurs

### Requirement: UBO GPU sync via cached IRenderResource wrapper
`MaterialInstance` SHALL construct a single `UboByteBufferResource` over `m_uboBuffer` during construction (when a UBO binding is present) and SHALL cache the resulting `shared_ptr<IRenderResource>` as `m_uboResource`. `updateUBO()` SHALL call `m_uboResource->setDirty()` so `VulkanResourceManager::syncResource()` pushes the bytes to the GPU on the next frame. Setter calls MAY set a dirty flag and defer the `setDirty()` propagation to `updateUBO()`.

#### Scenario: updateUBO propagates dirty state
- **WHEN** a setter is called followed by `updateUBO()`
- **THEN** `m_uboResource->setDirty()` has been invoked at least once since the last `updateUBO()` call completed

#### Scenario: UBO resource identity is stable
- **WHEN** `getDescriptorResources()` is called twice in succession on the same `MaterialInstance`
- **THEN** both calls return the same `IRenderResource` pointer for the UBO entry (address equality)

### Requirement: Core-layer UBO byte-buffer resource wrapper
The core layer SHALL provide a `UboByteBufferResource` class that implements `IRenderResource` over a non-owning reference to a `std::vector<uint8_t>`. Its `getRawData()` MUST return a pointer computed from the referenced vector at call time (not a stale copy captured at construction), `getByteSize()` MUST return the byte count passed at construction, `getType()` MUST return `ResourceType::UniformBuffer`, and `setDirty()` MUST mark the resource for upload through the existing `VulkanResourceManager::syncResource()` path. `MaterialInstance` SHALL construct exactly one such wrapper for its `m_uboBuffer` during its own construction.

> Rationale: this wrapper is the same shape as the existing `SkeletonUBO` in `src/core/asset/skeleton.hpp` — both live in core because `IRenderResource` is a core contract and no backend-specific code is required to adapt a raw byte buffer into that contract.

#### Scenario: Wrapper exposes buffer bytes without copy
- **WHEN** a `UboByteBufferResource` is created over a 48-byte vector
- **THEN** `wrapper.getRawData()` returns a pointer whose dereferenced content matches `buffer.data()` byte-for-byte and `wrapper.getByteSize() == 48`

#### Scenario: Modifying the source buffer is visible through the wrapper
- **WHEN** bytes in the source buffer change after wrapping and `getRawData()` is read again
- **THEN** the wrapper returns the updated bytes (no stale copy)

### Requirement: Loader returns MaterialInstance
The file-shader loader for `blinnphong_0` SHALL be named `loadBlinnPhongMaterial` (or similar, not containing `DrawMaterial`) and SHALL return a `MaterialInstance::Ptr`. It SHALL compile the shader, reflect bindings, create a `MaterialTemplate`, configure at least one `RenderPassEntry`, call `buildBindingCache()`, create a `MaterialInstance`, and seed reasonable default uniform values via the reflection-driven setters. The legacy file `blinnphong_draw_material_loader.{hpp,cpp}` MUST be removed or rewritten in place.

#### Scenario: Loader produces a ready-to-render MaterialInstance
- **WHEN** `loadBlinnPhongMaterial()` is called
- **THEN** the returned `MaterialInstance::Ptr` has a non-empty `m_uboBuffer`, a resolvable `getShaderInfo()`, and default uniform values written via `setVec3` / `setFloat` / `setInt`

#### Scenario: No DrawMaterial references remain
- **WHEN** searching `src/` (excluding `openspec/changes/archive/`) for the symbol `DrawMaterial`
- **THEN** no matches are found
