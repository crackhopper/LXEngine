## ADDED Requirements

### Requirement: Extract descriptor bindings from SPIR-V
ShaderReflector SHALL parse SPIR-V bytecode and extract all descriptor resource bindings. Each binding MUST populate `ShaderResourceBinding` fields: `name`, `set`, `binding`, `type`, `descriptorCount`, `size` (for buffers), `stageFlags`, and—for `UniformBuffer` bindings—`members` describing the std140-laid-out contents of the block.

#### Scenario: Extract uniform buffer binding
- **WHEN** SPIR-V contains a `uniform` block at set=0, binding=0
- **THEN** a `ShaderResourceBinding` is returned with `type = ShaderPropertyType::UniformBuffer`, correct `set` and `binding`, and `size` reflecting the buffer's total byte size

#### Scenario: Extract combined image sampler binding
- **WHEN** SPIR-V contains a `sampler2D` at set=1, binding=0
- **THEN** a `ShaderResourceBinding` is returned with `type = ShaderPropertyType::Texture2D` and correct set/binding

#### Scenario: Extract sampler binding
- **WHEN** SPIR-V contains a standalone `sampler` resource
- **THEN** a `ShaderResourceBinding` is returned with `type = ShaderPropertyType::Sampler`

### Requirement: Merge reflection across shader stages
ShaderReflector SHALL merge bindings from multiple shader stages (vertex + fragment). If the same (set, binding) pair appears in multiple stages, the resulting `ShaderResourceBinding` MUST have `stageFlags` combining all relevant stages via bitwise OR. For `UniformBuffer` bindings, the `members` vector MUST be preserved from the first stage that produces a non-empty value, and subsequent stages' `members` MUST be structurally identical (same count, same names, same offsets, same types).

#### Scenario: Binding used in both vertex and fragment stages
- **WHEN** a uniform buffer at (set=0, binding=0) is used in both the vertex and fragment SPIR-V
- **THEN** the merged `ShaderResourceBinding` has `stageFlags = ShaderStage::Vertex | ShaderStage::Fragment`

#### Scenario: Binding used in only one stage
- **WHEN** a texture sampler at (set=1, binding=1) is used only in the fragment SPIR-V
- **THEN** the merged `ShaderResourceBinding` has `stageFlags = ShaderStage::Fragment`

#### Scenario: UBO members preserved during stage merge
- **WHEN** the same UBO at (set=2, binding=0) appears in both vertex and fragment stages and both declare identical members
- **THEN** the merged binding retains the full `members` vector with every member's `name`, `type`, `offset`, and `size`

### Requirement: findBinding by set and binding index
CompiledShader SHALL support `findBinding(set, binding)` that returns the matching `ShaderResourceBinding` in O(1) or O(log n) time.

#### Scenario: Binding found
- **WHEN** `findBinding(0, 1)` is called and a binding at set=0, binding=1 exists
- **THEN** the corresponding `ShaderResourceBinding` reference is returned

#### Scenario: Binding not found
- **WHEN** `findBinding(3, 0)` is called and no binding at set=3, binding=0 exists
- **THEN** `std::nullopt` is returned

### Requirement: findBinding by name
CompiledShader SHALL support `findBinding(name)` that returns the matching `ShaderResourceBinding` by its GLSL variable name.

#### Scenario: Find by name
- **WHEN** `findBinding("CameraUBO")` is called
- **THEN** the `ShaderResourceBinding` whose `name` field is `"CameraUBO"` is returned

### Requirement: Extract UBO member layout
ShaderReflector SHALL walk the declared member list of every `UniformBuffer` binding and populate `ShaderResourceBinding::members` with one `StructMemberInfo` entry per top-level member. Each entry MUST record the GLSL member name, a `ShaderPropertyType` tag matching the member's scalar/vector/matrix shape, the std140 byte offset as reported by `spv::DecorationOffset`, and the std140 declared byte size as reported by `get_declared_struct_member_size`.

#### Scenario: Flat UBO with mixed scalar and vector members
- **WHEN** a uniform block declares `vec3 baseColor; float shininess; int enableAlbedo;` at set=2, binding=0
- **THEN** the binding's `members` vector contains three entries with names `"baseColor"`, `"shininess"`, `"enableAlbedo"`; types `Vec3`, `Float`, `Int`; and std140 offsets reported by `spv::DecorationOffset` — note that `float shininess` packs into the trailing 4 bytes of the `vec3` 16-byte bucket (offset 12), and `enableAlbedo` follows at offset 16

#### Scenario: Non-UBO binding has empty members
- **WHEN** a `sampler2D` binding is extracted
- **THEN** its `members` vector is empty

#### Scenario: Deterministic member ordering
- **WHEN** the same UBO is reflected twice from the same SPIR-V
- **THEN** the `members` vector appears in the same order both times, matching the GLSL declaration order

### Requirement: Int member type tag
The `ShaderPropertyType` enum SHALL include an `Int` value so that `int`-typed UBO members are classified distinctly from `Float`. `StructMemberInfo::type` MUST use `Int` for GLSL `int` members.

#### Scenario: Integer UBO member
- **WHEN** a UBO declares `int enableAlbedo;`
- **THEN** the corresponding `StructMemberInfo::type` equals `ShaderPropertyType::Int`, not `ShaderPropertyType::Float`

### Requirement: Graceful fallback for unsupported UBO shapes
When a `UniformBuffer` block contains a nested struct or an array-of-struct member, ShaderReflector SHALL produce the binding with `members` left empty and SHALL emit a log entry identifying the unsupported shape. The binding's `size` MUST still reflect the full std140 size of the block so that GPU-side allocation remains correct.

#### Scenario: Nested struct falls back to empty members
- **WHEN** a UBO contains `struct Light { vec3 pos; float intensity; }; Light lights[4];`
- **THEN** the binding is returned with correct `size` but `members` is empty and a warning is logged

### Requirement: Synthesized names for stripped SPIR-V
When spirv-cross returns an empty member name (e.g. from stripped SPIR-V), ShaderReflector SHALL synthesize a deterministic fallback name of the form `"_memberN"` where `N` is the zero-based member index, so that downstream `StringID`-based lookups remain stable.

#### Scenario: Stripped UBO receives fallback names
- **WHEN** spirv-cross returns an empty string for a UBO member at index 2
- **THEN** the resulting `StructMemberInfo::name` equals `"_member2"`
