## Why

The Vulkan backend still keys pipelines with hard-coded strings such as `blinnphong_0` and ships Blinn-Phong–specific types (`VkPipelineBlinnPhong`, `MaterialBlinnPhong`), so pipeline choice cannot track real `RenderingItem` state as shader variants, vertex layouts, and render state diversify. REQ-002 defines a `PipelineKey` based on `GlobalStringTable` / `StringID`; implementing it and removing the legacy Blinn-Phong pipeline path aligns the backend with dynamic, data-driven pipeline creation.

## What Changes

- Add core `PipelineKey` (`StringID` identity, `Hash` for maps, `build(...)` from shader set, vertex layout, render state, topology, skeleton flag) per `docs/requirements/002-pipeline-key.md`.
- Extend `RenderingItem` with `pipelineKey`; fill it in `Scene::buildRenderingItem()` via `PipelineKey::build(...)`.
- Change backend pipeline cache from `std::string` to `PipelineKey` (or equivalent `StringID`-keyed map); on draw, look up or create pipeline from `item.pipelineKey`.
- **BREAKING**: Remove Blinn-Phong–specific pipeline and material stub types (`VkPipelineBlinnPhong`, related slot tables, `MaterialBlinnPhong` / UBO stub where only serving that path), and delete or replace call sites (`VulkanResourceManager` pre-registration, `vk_renderer` string keys, tests).
- Adjust push-constant / descriptor binding paths that assumed `PC_BlinnPhong` only, using reflection or generic material data as already used elsewhere.
- Update integration tests (`test_vulkan_pipeline`, `test_vulkan_command_buffer`, `test_render_triangle`, etc.) to build pipelines through the generic/dynamic path keyed by `PipelineKey`.

## Capabilities

### New Capabilities

- `pipeline-key`: Core `PipelineKey` type, normalized string format, `build()` composition using `getPipelineHash()` on participating resources, `RenderingItem.pipelineKey`, and `Scene::buildRenderingItem()` responsibility.

### Modified Capabilities

- `renderer-backend-vulkan`: Pipeline creation and caching SHALL be driven by `PipelineKey` from `RenderingItem`; remove normative coupling to a fixed Blinn-Phong pipeline scenario and hard-coded pipeline id strings in resource manager / renderer.

## Impact

- **Core**: New `pipeline_key.hpp` (and includes), `scene.hpp` / `RenderingItem`, possible includes from `shader.hpp`, mesh/render-state types used in `build()`.
- **Backend**: `vk_resource_manager`, `vk_renderer`, command buffer bind/draw, removal of `vkp_blinnphong.*`, `blinn_phong_material_stub.hpp`, CMake sources.
- **Tests**: Vulkan pipeline / command-buffer / renderer tests; `test_render_triangle` material creation path.
- **Optional follow-up**: `PC_BlinnPhong` in `render_resource.hpp` may shrink or move if push constants become fully shader-driven (only if unused after migration).
