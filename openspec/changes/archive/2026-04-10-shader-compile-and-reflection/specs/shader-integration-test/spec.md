## ADDED Requirements

### Requirement: PBR shader with conditional compilation macros
A PBR shader pair (`pbr.vert` / `pbr.frag`) SHALL be provided in `assets/shaders/glsl/`. The fragment shader MUST use `#ifdef` guards for at least `HAS_NORMAL_MAP` and `HAS_METALLIC_ROUGHNESS`, each enabling additional texture sampling and computation paths.

#### Scenario: Base PBR shader compiles without macros
- **WHEN** `pbr.vert` and `pbr.frag` are compiled with no variant macros defined
- **THEN** compilation succeeds and produces valid SPIR-V for both stages

#### Scenario: PBR shader compiles with all macros enabled
- **WHEN** both `HAS_NORMAL_MAP` and `HAS_METALLIC_ROUGHNESS` are enabled
- **THEN** compilation succeeds and the reflection output includes additional texture bindings for normal map and metallic-roughness map

### Requirement: Integration test loads, compiles, and reflects PBR shader
An integration test (`test_shader_compiler`) SHALL load the PBR shader, compile it with various variant combinations, extract reflection data, and print all `ShaderResourceBinding` entries.

#### Scenario: Full pipeline with no variants
- **WHEN** the test compiles PBR shader with no variants
- **THEN** reflection bindings are printed showing at minimum: camera UBO, material UBO, and albedo texture bindings

#### Scenario: Full pipeline with all variants enabled
- **WHEN** the test compiles PBR shader with `HAS_NORMAL_MAP` and `HAS_METALLIC_ROUGHNESS` enabled
- **THEN** reflection bindings include additional entries for normal map and metallic-roughness textures compared to the no-variant case

### Requirement: Demonstrate descriptor set layout creation from reflection
The integration test SHALL print how each `ShaderResourceBinding` maps to a `VkDescriptorSetLayoutBinding`, showing: `binding`, `descriptorType`, `descriptorCount`, `stageFlags`. This demonstrates that the reflection output contains sufficient information for creating Vulkan descriptor set layouts.

#### Scenario: Print descriptor set layout mapping
- **WHEN** reflection bindings are extracted from compiled PBR shader
- **THEN** the test prints each binding's mapping in a format showing: set number, binding index, Vulkan descriptor type (e.g., `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`), descriptor count, and stage flags — without calling actual Vulkan API
