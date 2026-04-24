## Why

`src/core/resources/material.hpp` currently ships two parallel material designs: `IMaterial` + `DrawMaterial` + `BlinnPhongMaterialUBO` (used by all call sites today) and `MaterialTemplate` + `MaterialInstance` (defined but unreferenced). `DrawMaterial` holds a hand-written std140 struct that must be kept in lockstep with the GLSL declaration — any shader edit requires a matching C++ edit, and we can't add a new shader without writing new C++. REQ-004's UBO member reflection now gives us the layout metadata needed to eliminate the hand-written structs, but nothing consumes it until `MaterialInstance` grows real UBO management and `DrawMaterial` is retired.

## What Changes

- Make `MaterialInstance` a concrete `IMaterial` implementation that owns a std140 byte buffer initialized from shader reflection (`ShaderResourceBinding::members`).
- Add reflection-driven setters: `setVec4` / `setVec3` / `setFloat` / `setInt` / `setTexture` — all keyed by `StringID` member name, dispatched through a shared `writeUboMember` helper for offset lookup + type check + `memcpy`.
- **BREAKING**: remove `DrawMaterial` and `BlinnPhongMaterialUBO` from `material.hpp`. All `MaterialPtr` values now point to `MaterialInstance`.
- Require `MaterialTemplate::create(name, IShaderSharedPtr)` to take a shader up-front; drop `m_passHashCache` (dead after REQ-007) and keep a single `m_bindingCache` keyed by `StringID`.
- Rename / rewrite `blinnphong_draw_material_loader` → `blinnphong_material_loader`, returning `MaterialInstance::SharedPtr` and seeding default uniforms via the new setters.
- Introduce an infra-layer `wrapAsUboResource(buffer, binding, passFlag)` helper that lifts the byte buffer into an `IRenderResource` implementing `getRawData()` / `getByteSize()` / `setDirty()`, following the `SkeletonUBO` pattern.
- Migrate every test and call site that constructed `DrawMaterial` to use `loadBlinnPhongMaterial()` or an equivalent `MaterialInstance` construction.

## Capabilities

### New Capabilities
- `material-system`: material template/instance lifecycle, reflection-driven UBO management, `IMaterial` contract, and loader construction. No prior capability covered this — historically `DrawMaterial` was documented only as an example inside `renderer-backend-vulkan`.

### Modified Capabilities
<!-- None — this change introduces a new capability rather than amending existing ones. -->

## Impact

- **Code**:
  - `src/core/resources/material.hpp` / `material.cpp` — delete `DrawMaterial`, delete `BlinnPhongMaterialUBO`, expand `MaterialInstance`, tighten `MaterialTemplate`
  - `src/infra/loaders/blinnphong_draw_material_loader.{hpp,cpp}` — rename + rewrite
  - `src/infra/gpu/ubo_resource.{hpp,cpp}` (new) — `wrapAsUboResource` helper
  - `src/core/scene/object.hpp` — update comment about `RenderableSubMesh::material` type (functional type unchanged)
  - `src/test/integration/test_material_instance.cpp` (new) — covers UBO byte-level writes and `findBinding`
  - `src/test/test_render_triangle.cpp`, `src/test/integration/test_vulkan_*.cpp` — replace `DrawMaterial` construction
- **Specs**: new `material-system` capability doc
- **Dependencies**: REQ-004 (`ShaderResourceBinding::members`) — already landed
- **Breaking changes**: `DrawMaterial` and `BlinnPhongMaterialUBO` removed; any out-of-tree caller must switch to `MaterialInstance::create(template)` + setter calls
- **Not in scope**: `IMaterial::getRenderSignature(pass)` (REQ-007), `RenderPassEntry` field restructuring, multi-UBO materials, push-constant reflection
