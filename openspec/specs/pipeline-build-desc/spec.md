## ADDED Requirements

### Requirement: PipelineBuildDesc captures all pipeline construction inputs
The system SHALL provide `LX_core::PipelineBuildDesc`, a core-layer struct that aggregates all data a backend needs to construct a graphics pipeline. It MUST contain at minimum:

- `PipelineKey key` — identity produced by the `pipeline-key` capability
- `std::vector<ShaderStageCode> stages` — SPIR-V bytecode for every shader stage
- `std::vector<ShaderResourceBinding> bindings` — descriptor binding reflection (set/binding/type/name/stage/members)
- `VertexLayout vertexLayout`
- `RenderState renderState`
- `PrimitiveTopology topology`
- `PushConstantRange pushConstant` (engine-wide convention, set by the factory)

The struct MUST be backend-agnostic: no Vulkan symbols in its definition. Backends SHALL translate it to their own types (`VkVertexInputAttributeDescription`, `VkDescriptorSetLayoutBinding`, etc.).

#### Scenario: PipelineBuildDesc fields populated from reflection
- **WHEN** `PipelineBuildDesc::fromRenderingItem(item)` is called for an item whose shader's `IShader::getReflectionBindings()` contains N bindings
- **THEN** the returned `PipelineBuildDesc::bindings` SHALL contain exactly those N bindings, in the same order

#### Scenario: PipelineBuildDesc is independent of shader name
- **WHEN** `fromRenderingItem` is called for a shader whose `getShaderName()` returns an unknown string
- **THEN** the call SHALL succeed and produce a valid `PipelineBuildDesc` without any hardcoded lookup table

### Requirement: PipelineBuildDesc::fromRenderingItem is the single factory
`PipelineBuildDesc` SHALL expose a static factory `static PipelineBuildDesc fromRenderingItem(const RenderingItem &item)`. The factory MUST derive `bindings` from `item.shaderInfo->getReflectionBindings()`, `stages` from `item.shaderInfo->getAllStages()`, `vertexLayout` from the item's vertex buffer, `topology` from the item's index buffer, and `renderState` from the item's material (via a path introduced for this purpose, not re-reading `MaterialTemplate::getEntry` unnecessarily). The `pushConstant` field SHALL be filled with the engine-wide convention (128-byte range visible to vertex + fragment stages) until a future requirement introduces shader-declared push constants.

#### Scenario: Key matches input
- **WHEN** `fromRenderingItem(item)` is evaluated
- **THEN** the returned `PipelineBuildDesc::key` equals `item.pipelineKey`

#### Scenario: Factory is deterministic
- **WHEN** `fromRenderingItem` is called twice on the same `RenderingItem`
- **THEN** both returned `PipelineBuildDesc` values are field-wise equal (identical stages, bindings, layout, state, topology)

### Requirement: PipelineBuildDesc retains ShaderResourceBinding ordering and names
The `bindings` vector inside `PipelineBuildDesc` SHALL preserve the order produced by `IShader::getReflectionBindings()` and MUST carry every binding's `name` field verbatim. Backend code that builds descriptor set layouts or routes resources to descriptors SHALL consume `bindings` by name (for resource routing) or by `(set, binding)` pair (for layout creation), never by a hardcoded enum.

#### Scenario: Binding names survive the factory
- **WHEN** a shader exposes a uniform buffer named `"CameraUBO"` and a sampler named `"albedoTex"`
- **THEN** the corresponding entries in `PipelineBuildDesc::bindings` carry exactly those names
