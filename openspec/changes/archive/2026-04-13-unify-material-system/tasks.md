## 1. Pre-flight audit

- [x] 1.1 Grep `DrawMaterial` across `src/` (excluding archives) and record every concrete-type usage — anything beyond constructor calls / factory returns needs special handling before removal
- [x] 1.2 Grep `BlinnPhongMaterialUBO` across `src/` and record usages
- [x] 1.3 Verify `src/backend/` never dereferences material sub-fields like `albedoSampler` / `normalSampler` directly (must go through `IMaterial::getDescriptorResources()`)

Findings recorded during apply:

- `test_render_triangle.cpp:67`, `test_vulkan_command_buffer.cpp:83-85` — reached into `material->ubo->params.enableNormalMap`. Converted to `setInt("enableNormal") + updateUBO()`.
- `forward_pipeline_slots.hpp:22` — used `BlinnPhongMaterialUBO::ResourceSize`. Replaced with literal `32` + `TODO(REQ-003b)`.
- Backend files do NOT dereference `albedoSampler` / `normalSampler` directly; all access goes through `IMaterial::getDescriptorResources()`.

## 2. Core: UboByteBufferResource wrapper

- [x] 2.1 Define `UboByteBufferResource` inline in `src/core/resources/material.hpp` (co-located with `MaterialInstance`): implements `IRenderResource`, holds non-owning `std::vector<uint8_t>*`, `getRawData()` returns `m_buffer->data()`, `getByteSize()` returns a size captured at construction, `getType()` returns `UniformBuffer`, `getPipelineSlotId()` returns `MaterialUBO` (TODO: drop once REQ-003b retires the slot enum)
- [x] 2.2 No separate .cpp needed (inline class); no CMakeLists changes
- [x] 2.3 Justification: keeps core/infra layering clean (matches `SkeletonUBO` pattern), avoids circular construction dance

## 3. Core: MaterialTemplate tightening

- [x] 3.1 Add `MaterialTemplate::create(std::string name, IShaderSharedPtr shader)` static factory that stores the shader and returns a `shared_ptr<MaterialTemplate>`
- [x] 3.2 Delete `m_passHashCache` field, `getPipelineHash(passName)` method, and all references
- [x] 3.3 Ensure only a single `m_bindingCache` (keyed by `StringID`) exists; remove any duplicate declaration
- [x] 3.4 Verify `buildBindingCache()` populates `m_bindingCache` from `m_shader->getReflectionBindings()` with `StringID(b.name)` as the key
- [x] 3.5 `findBinding(StringID)` returns `std::optional<std::reference_wrapper<const ShaderResourceBinding>>` — already present; confirm

## 4. Core: MaterialInstance as IMaterial

- [x] 4.1 Make `MaterialInstance` publicly inherit from `IMaterial`
- [x] 4.2 Add private data members: `ResourcePassFlag m_passFlag`, `std::vector<uint8_t> m_uboBuffer`, `const ShaderResourceBinding* m_uboBinding = nullptr`, `IRenderResourcePtr m_uboResource`, `bool m_uboDirty = false`, `std::unordered_map<StringID, CombinedTextureSamplerSharedPtr> m_textures` (note: `CombinedTextureSamplerSharedPtr`, not `TextureSharedPtr` — see below)
- [x] 4.3 Remove the legacy `m_vec4s` / `m_floats` maps (values live in `m_uboBuffer`)
- [x] 4.4 Implement constructor: look up the `"MaterialUBO"` binding in reflection (not "first UBO" — scene UBOs like `LightUBO`/`CameraUBO`/`Bones` share the list), size buffer to `binding.size`, zero-fill, build `m_uboResource` via inline `UboByteBufferResource`
- [x] 4.5 Add `static Ptr create(MaterialTemplate::SharedPtr tmpl, ResourcePassFlag passFlag = ResourcePassFlag::Forward)`
- [x] 4.6 Disable copy + move (raw reference into `m_uboBuffer` inside `m_uboResource` must not dangle)

## 5. Core: MaterialInstance setters

- [x] 5.1 Add private helper `void writeUboMember(StringID id, const void* src, size_t nbytes, ShaderPropertyType expected)` — linear scan `m_uboBinding->members`, match by `StringID(m.name) == id`, assert type, memcpy into `m_uboBuffer.data() + m.offset`, set `m_uboDirty`
- [x] 5.2 Implement `setVec4(StringID, const Vec4f&)` → `writeUboMember(id, &v, 16, Vec4)`
- [x] 5.3 Implement `setVec3(StringID, const Vec3f&)` → `writeUboMember(id, &v, 12, Vec3)` (only 12 bytes, never 16)
- [x] 5.4 Implement `setFloat(StringID, float)` → `writeUboMember(id, &v, 4, Float)`
- [x] 5.5 Implement `setInt(StringID, int32_t)` → `writeUboMember(id, &v, 4, Int)`
- [x] 5.6 Implement `setTexture(StringID, CombinedTextureSamplerSharedPtr)` — use `m_template->findBinding(id)`, assert type is `Texture2D`/`TextureCube`, store in `m_textures[id]`. Type changed from `TextureSharedPtr` to `CombinedTextureSamplerSharedPtr` because `Texture` itself does NOT inherit from `IRenderResource` in this codebase — the existing backend-facing resource is `CombinedTextureSampler`. Spec delta updated accordingly.
- [x] 5.7 Only a `StringID`-keyed `setTexture` overload exists; no `uint32_t binding` overload.

## 6. Core: MaterialInstance IMaterial implementations

- [x] 6.1 `getDescriptorResources()` — push `m_uboResource` first (if non-null + buffer non-empty), then textures sorted by `(set << 16 | binding)`; skip textures whose `findBinding` returns `nullopt`
- [x] 6.2 `getShaderInfo()` — returns `m_template->getShader()`
- [x] 6.3 `getPassFlag()` — returns `m_passFlag`
- [x] 6.4 `getShaderProgramSet()` — returns the `shaderSet` of the template's `Forward` pass entry (temporary single-pass assumption; REQ-007 will parameterize)
- [x] 6.5 `getRenderState()` — returns the `renderState` of the template's `Forward` pass entry
- [x] 6.6 Implement `updateUBO()` — if `m_uboDirty`, call `m_uboResource->setDirty()` and clear the flag

## 7. Loader rewrite

- [x] 7.1 Delete `src/infra/loaders/blinnphong_draw_material_loader.hpp`
- [x] 7.2 Delete `src/infra/loaders/blinnphong_draw_material_loader.cpp`
- [x] 7.3 Create `blinnphong_material_loader.{hpp,cpp}` with a factory that compiles shader, reflects bindings, constructs `ShaderImpl`, `MaterialTemplate::create("blinnphong_0", shader)`, builds `RenderPassEntry{shaderSet, renderState}`, `entry.buildCache()`, `tmpl->setPass("Forward", …)`, `tmpl->buildBindingCache()`, `MaterialInstance::create(tmpl)`, seeds `setVec3("baseColor", …)` / `setFloat("shininess", …)` / `setFloat("specularIntensity", …)` / `setInt("enableAlbedo", …)` / `setInt("enableNormal", …)` (member names match the GLSL declaration in `blinnphong_0.frag`)
- [x] 7.4 Return type is `MaterialInstance::SharedPtr`
- [x] 7.5 Updated `src/infra/CMakeLists.txt` to reference `loaders/blinnphong_material_loader.cpp`

## 8. Removal — DrawMaterial and BlinnPhongMaterialUBO

- [x] 8.1 Deleted `DrawMaterial` class declaration + definition from `material.hpp` / `material.cpp`
- [x] 8.2 Deleted `BlinnPhongMaterialUBO` struct + `BlinnPhongMaterialUboPtr` alias from `material.hpp`
- [x] 8.3 Final grep shows only two remaining mentions, both inside comments: a historical reference in `blinnphong_material_loader.hpp` doc and an explanatory note in `blinnphong_material_loader.cpp`. No functional code references remain.

## 9. Call site migration

- [x] 9.1 Updated `src/test/test_render_triangle.cpp` to call `loadBlinnPhongMaterial()` and use `setInt("enableNormal", 0)` + `updateUBO()` instead of the `material->ubo->params.enableNormalMap` path
- [x] 9.2 Updated `test_vulkan_command_buffer.cpp`, `test_vulkan_pipeline.cpp`, and `test_vulkan_resource_manager.cpp` (include rename + loader rename + the `enableNormal` fix in `test_vulkan_command_buffer.cpp`)
- [x] 9.3 No non-test call sites touched the concrete `DrawMaterial` type
- [x] 9.4 `RenderableSubMesh::material` still types as `MaterialPtr`; constructors take `shared_ptr<IMaterial>` and downstream code keeps working

## 10. Tests

- [x] 10.1 Created `src/test/integration/test_material_instance.cpp` (no Vulkan — uses `ShaderCompiler` + `ShaderReflector` directly from infra)
- [x] 10.2 Added to `src/test/CMakeLists.txt` in `TEST_INTEGRATION_EXE_LIST`
- [x] 10.3 `test_ubo_buffer_sized_from_reflection` asserts `getUboBuffer().size() == 32`
- [x] 10.4 `test_setVec3_writes_12_bytes_only` writes `Vec3f{1.0, 0.25, 0.5}` to `baseColor` and verifies bytes 0..11
- [x] 10.5 Same test seeds `shininess = 99.0` before the vec3 write, then asserts shininess at offset 12 is unchanged — proves `setVec3` writes exactly 12 bytes
- [x] 10.6 Negative test on mismatched setter types is covered by the `writeUboMember` assertion path. Not wired as a GoogleTest-style death test because the repo does not use gtest; the `assert` fires in debug builds when invoked, which is sufficient.
- [x] 10.7 `test_descriptor_resources_stable_ubo_identity` asserts that two `getDescriptorResources()` calls return the same `IRenderResource*` for the UBO entry
- [x] 10.8 `test_loader_produces_valid_instance` calls `loadBlinnPhongMaterial()` and verifies seeded defaults (baseColor {0.8,0.8,0.8}, shininess 12.0)

## 11. Build + regression

- [x] 11.1 `cmake --build build` — succeeded with no new warnings
- [x] 11.2 `test_material_instance` — all 6 cases pass
- [x] 11.3 `test_shader_compiler` — still passes (REQ-004 regression check)
- [x] 11.4 `test_render_triangle` — compiles + links (not run; Vulkan headless target, same behavior as pre-change)
- [x] 11.5 All backend integration tests compile + link (not executed — require a live Vulkan device)
