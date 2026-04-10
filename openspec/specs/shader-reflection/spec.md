## ADDED Requirements

### Requirement: Extract descriptor bindings from SPIR-V
ShaderReflector SHALL parse SPIR-V bytecode and extract all descriptor resource bindings. Each binding MUST populate `ShaderResourceBinding` fields: `name`, `set`, `binding`, `type`, `descriptorCount`, `size` (for buffers), and `stageFlags`.

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
ShaderReflector SHALL merge bindings from multiple shader stages (vertex + fragment). If the same (set, binding) pair appears in multiple stages, the resulting `ShaderResourceBinding` MUST have `stageFlags` combining all relevant stages via bitwise OR.

#### Scenario: Binding used in both vertex and fragment stages
- **WHEN** a uniform buffer at (set=0, binding=0) is used in both the vertex and fragment SPIR-V
- **THEN** the merged `ShaderResourceBinding` has `stageFlags = ShaderStage::Vertex | ShaderStage::Fragment`

#### Scenario: Binding used in only one stage
- **WHEN** a texture sampler at (set=1, binding=1) is used only in the fragment SPIR-V
- **THEN** the merged `ShaderResourceBinding` has `stageFlags = ShaderStage::Fragment`

### Requirement: findBinding by set and binding index
ShaderImpl SHALL support `findBinding(set, binding)` that returns the matching `ShaderResourceBinding` in O(1) or O(log n) time.

#### Scenario: Binding found
- **WHEN** `findBinding(0, 1)` is called and a binding at set=0, binding=1 exists
- **THEN** the corresponding `ShaderResourceBinding` reference is returned

#### Scenario: Binding not found
- **WHEN** `findBinding(3, 0)` is called and no binding at set=3, binding=0 exists
- **THEN** `std::nullopt` is returned

### Requirement: findBinding by name
ShaderImpl SHALL support `findBinding(name)` that returns the matching `ShaderResourceBinding` by its GLSL variable name.

#### Scenario: Find by name
- **WHEN** `findBinding("CameraUBO")` is called
- **THEN** the `ShaderResourceBinding` whose `name` field is `"CameraUBO"` is returned
