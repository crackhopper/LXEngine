# Scene Resource Table Owned Render Resources Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove `MaterialInstance::getDescriptorResources(pass)` from render paths and migrate render-resource ownership toward `SceneResourceTable` as the single source for realtime/offline uploads.

**Architecture:** Keep Vulkan descriptor binding name-based for now, but make `RenderWorkQueue::build()` populate `RenderWorkItem::descriptorResources` through a scene/resource-table resolver instead of through `MaterialInstance`. Then replace material texture ownership with `TextureHandle` bindings resolved by `SceneResourceTable::buildUploadView()`, and finally move component-held material/mesh resources to handles.

**Tech Stack:** C++20, LXEngine core scene/material/frame-graph systems, Vulkan backend descriptor binding, GLSL shader reflection, CMake/Ninja integration tests.

---

### Task 1: Cut Render Path Off `MaterialInstance::getDescriptorResources`

**Files:**
- Create: `src/core/frame_graph/scene_descriptor_resource_resolver.hpp`
- Create: `src/core/frame_graph/scene_descriptor_resource_resolver.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `src/core/scene/object.hpp`
- Modify: `src/core/scene/object.cpp`
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/core/asset/material_instance.hpp`
- Modify: `src/core/asset/material_instance.cpp`
- Test: `src/test/integration/test_scene_node_validation.cpp`
- Test: `src/test/integration/test_frame_graph.cpp`
- Test: `src/test/integration/test_material_instance.cpp`

- [ ] Step 1: Add a failing search/test guard that rejects `getDescriptorResources(` calls in `src/core/scene/object.cpp`, `src/core/frame_graph/render_queue.cpp`, and `src/backend/vulkan/vulkan_realtime_renderer.cpp` except post-process temporary material builders.
- [ ] Step 2: Remove `descriptorResources` from `ValidatedRenderablePassData`.
- [ ] Step 3: Add `SceneDescriptorResourceResolver` with `buildRenderableResources(scene, data, pass, target)` that appends material-owned resources by reading shader reflection and `MaterialInstance` binding accessors, then appends skeleton resources, scene-level resources, and IBL resources.
- [ ] Step 4: Move material-owned resource validation from `SceneNode::rebuildValidatedCache()` into resolver-owned validation helpers so render queue build is the only descriptor construction point.
- [ ] Step 5: Change `RenderWorkQueue::build()` to call the resolver and populate `RenderWorkItem::descriptorResources`.
- [ ] Step 6: Keep `MaterialInstance` low-level binding accessors only: `getParameterResource(bindingName)` and `getTexture(bindingName)`. Do not expose pass-scoped descriptor lists.
- [ ] Step 7: Delete `MaterialInstance::getDescriptorResources(pass)` and update tests to validate low-level binding access plus resolver output.
- [ ] Step 8: Run `cmake --build build --target test_material_instance test_scene_node_validation test_frame_graph test_scene_runtime lxe_editor -j2` and the corresponding test binaries.

### Task 2: Move Material Texture Bindings To `TextureHandle`

**Files:**
- Modify: `src/core/asset/material_instance.hpp`
- Modify: `src/core/asset/material_instance.cpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Modify: `src/infra/scene_asset/gltf_scene_asset_loader.cpp`
- Modify: `src/infra/offline/offline_scene_loader.cpp`
- Test: `src/test/integration/test_scene_resource_table.cpp`
- Test: `src/test/integration/test_generic_material_loader.cpp`
- Test: `src/test/integration/test_offline_scene_loader.cpp`

- [ ] Step 1: Add failing tests proving `MaterialInstance` no longer stores `CombinedTextureSamplerSharedPtr` and that `buildUploadView()` resolves texture table indices from `TextureHandle`.
- [ ] Step 2: Add `MaterialTextureBinding { StringID bindingName; TextureHandle texture; }` or equivalent handle map to `MaterialInstance`.
- [ ] Step 3: Replace `setTexture(bindingName, CombinedTextureSamplerSharedPtr)` with `setTextureHandle(bindingName, TextureHandle)` for renderable materials.
- [ ] Step 4: Add scene/table registration helpers used by loaders: register texture once, bind the returned handle to the material.
- [ ] Step 5: Change `SceneResourceTable::buildUploadView()` to call `registerUploadTexture(TextureHandle)` and resolve the table-owned texture resource.
- [ ] Step 6: Keep descriptor wrappers transient and non-owning; texture pixel data remains owned by `SceneResourceTable`.
- [ ] Step 7: Run texture/material/offline loader tests.

### Task 3: Move Material Components To Handles

**Files:**
- Modify: `src/core/scene/components/material_component.hpp`
- Modify: `src/core/scene/components/material_component.cpp`
- Modify: `src/core/scene/scene.cpp`
- Modify: `src/core/scene/object.cpp`
- Modify: scene loader/editor material creation call sites
- Test: `src/test/integration/test_scene_runtime.cpp`
- Test: `src/test/integration/test_scene_resource_table.cpp`

- [ ] Step 1: Add failing tests proving `MaterialComponent` stores active/tagged `MaterialHandle` values and no active `MaterialInstanceSharedPtr`.
- [ ] Step 2: Register material instances into `SceneResourceTable` at scene attach/load time and store handles in the component tag map.
- [ ] Step 3: Update pass-state revalidation to resolve the active material through the scene table.
- [ ] Step 4: Update `SceneNode::rebuildValidatedCache()` to use the resolved material reference from the table.
- [ ] Step 5: Remove material shared ownership from components.

### Task 4: Move Mesh Components To Handles

**Files:**
- Modify: `src/core/scene/components/mesh_component.hpp`
- Modify: `src/core/scene/components/mesh_component.cpp`
- Modify: `src/core/scene/scene.cpp`
- Modify: `src/core/scene/object.cpp`
- Modify: mesh/gltf scene loader call sites
- Test: `src/test/integration/test_scene_runtime.cpp`
- Test: `src/test/integration/test_scene_resource_table.cpp`

- [ ] Step 1: Add failing tests proving `MeshComponent` stores only `GeometryStorageHandle`, `MeshHandle`, and `ObjectHandle`.
- [ ] Step 2: Register mesh data into `SceneResourceTable` during scene attach/load.
- [ ] Step 3: Update vertex/index access in `SceneNode` validation to resolve mesh data through the table.
- [ ] Step 4: Remove mesh shared ownership from components.

### Task 5: Spec And Guard Cleanup

**Files:**
- Modify: `openspec/specs/material-system/spec.md`
- Modify: `openspec/specs/frame-graph/spec.md`
- Modify: `openspec/specs/renderer-backend-vulkan/spec.md`
- Modify: `notes/requirements/056-a-offline-pbr-texture-material-support.md`
- Test: add or update a source-level guard test under `src/test/integration`

- [ ] Step 1: Remove spec language requiring `MaterialInstance::getDescriptorResources(pass)`.
- [ ] Step 2: Add spec language requiring descriptor resources to be built from scene/resource-table resolver output.
- [ ] Step 3: Add a guard test that fails if `MaterialInstance::getDescriptorResources` is reintroduced.
- [ ] Step 4: Run `rg -n "getDescriptorResources" src openspec notes/requirements/056-a-offline-pbr-texture-material-support.md` and verify only removed-history references are gone.

### Task 6: Realtime/Offline Equivalence Verification

**Files:**
- Existing high-res Helmet scenes and profile output targets
- Existing offline/realtime compare targets

- [ ] Step 1: Build `lxe_editor` and relevant comparison targets.
- [ ] Step 2: Generate 1024x1024 realtime PBR, realtime Blinn-Phong, and offline PBR images.
- [ ] Step 3: Run EXR comparison for realtime/offline PBR direct-light profiles.
- [ ] Step 4: Record output paths and comparison summary.
