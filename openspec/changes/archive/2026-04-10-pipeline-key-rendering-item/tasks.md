## 1. Core PipelineKey and RenderingItem

- [x] 1.1 Add `src/core/resources/pipeline_key.hpp` (and `.cpp` if needed) with `PipelineKey`, `Hash`, and `build(ShaderProgramSet, VertexLayout, RenderState, PrimitiveTopology, bool hasSkeleton)` implementing the REQ-002 canonical string and `StringID` interning via `getPipelineHash()` on each contributor
- [x] 1.2 Extend `RenderingItem` with `PipelineKey pipelineKey` in `src/core/scene/scene.hpp`
- [x] 1.3 Update `Scene::buildRenderingItem()` to gather `ShaderProgramSet`, `VertexLayout`, `RenderState`, `PrimitiveTopology`, and skeleton presence from the active renderable (e.g. `RenderableSubMesh` / material + mesh) and assign `pipelineKey`
- [x] 1.4 Add or adjust `IRenderable` accessors only if `buildRenderingItem()` cannot obtain required inputs without breaking layering rules

## 2. Vulkan backend: cache and draw path

- [x] 2.1 Replace `std::unordered_map<std::string, VulkanPipelineUniquePtr>` (or equivalent) with `std::unordered_map<PipelineKey, VulkanPipelineUniquePtr, PipelineKey::Hash>` in `VulkanResourceManager`
- [x] 2.2 Implement get-or-create pipeline creation on cache miss using `RenderingItem` fields (shader, vertex layout, render state, topology), reusing the generic `VulkanPipeline` construction path
- [x] 2.3 Update `vk_renderer.cpp` to resolve pipelines via `renderItem.pipelineKey` and remove hard-coded `blinnphong_0` / string fallbacks
- [x] 2.4 Update `VulkanCommandBuffer` bind/draw paths if signatures or assumptions referenced Blinn-Phong-only types

## 3. Remove Blinn-Phong-specific types

- [x] 3.1 Delete `src/backend/vulkan/details/pipelines/vkp_blinnphong.hpp` and `vkp_blinnphong.cpp` and remove them from CMake sources
- [x] 3.2 Delete or inline-replace `src/backend/vulkan/details/blinn_phong_material_stub.hpp`; migrate call sites to existing material / UBO types used by dynamic shaders
- [x] 3.3 Remove `MaterialBlinnPhong::create` usage from tests and app code; use materials compatible with `blinnphong_0` shaders through the generic path
- [x] 3.4 Remove `PC_BlinnPhong` from `render_resource.hpp` (and all references) once push constants are driven by shader/material reflection or `ObjectPC` generic buffer

## 4. Tests and verification

- [x] 4.1 Rewrite `test_vulkan_pipeline.cpp` to create a pipeline via the `PipelineKey` / resource-manager path instead of `VkPipelineBlinnPhong::create`
- [x] 4.2 Update `test_vulkan_command_buffer.cpp`, `test_vulkan_resource_manager.cpp`, and `test_render_triangle.cpp` for new material/pipeline APIs
- [x] 4.3 Run `ninja BuildTest` and fix compile or runtime issues; smoke `ninja Renderer` if applicable

## 5. Spec archive readiness

- [x] 5.1 After implementation, run OpenSpec validate/archive flow for this change per project workflow (`openspec archive` when tests pass)
