## Context

`src/core/resources/material.hpp` currently holds two unrelated material designs side by side:

1. **Used**: `IMaterial` (virtual interface) ← `DrawMaterial` (concrete, holds `BlinnPhongMaterialUBO ubo; CombinedTextureSamplerSharedPtr albedoSampler; CombinedTextureSamplerSharedPtr normalSampler;`). Every render path in the project constructs `DrawMaterial` and pushes typed UBO fields directly into the struct.
2. **Unused**: `MaterialTemplate` + `MaterialInstance`. Designed as a template/instance pair where templates carry shader + pass configs and instances carry per-object uniform values. Exists in the header but has no callers.

The used path hard-codes the shader's UBO shape in C++. Adding a parameter to `MaterialUBO` in GLSL requires rebuilding `BlinnPhongMaterialUBO`, its loader, and every callsite that writes to it. REQ-004 extended `ShaderReflector` so `ShaderResourceBinding::members` now carries std140 offsets for every UBO top-level field; the machinery is there, but `MaterialInstance` doesn't consume it yet.

Additionally, the inherited `MaterialInstance` draft stored per-uniform `std::unordered_map<StringID, Vec4f>` / `<StringID, float>` values that duplicated the UBO byte buffer, and `MaterialTemplate` had a duplicate `m_bindingCache` field plus a stale `m_passHashCache` that REQ-001's hash convention relied on (REQ-007 will replace hashing entirely). Both are dead weight we want to clean up as part of unification.

## Goals / Non-Goals

**Goals:**
- Single concrete `IMaterial` class (`MaterialInstance`) that replaces `DrawMaterial` and holds all per-instance state.
- Zero hand-written std140 structs for any material UBO — `MaterialInstance` builds its byte buffer from reflection at construction time.
- `MaterialTemplate` becomes a thin carrier for "name + shader + pass entries + name-keyed binding cache", with no stale hash cache and no duplicate fields.
- Preserve the existing `IMaterial` virtual surface (`getDescriptorResources / getShaderInfo / getPassFlag / getShaderProgramSet / getRenderState`) so `Scene::buildRenderingItem` and the backend pipeline cache keep working without touching render-graph code.
- Loader API migrates from `DrawMaterial::SharedPtr` return to `MaterialInstance::SharedPtr` return with the same shape (one factory per shader file).

**Non-Goals:**
- `IMaterial::getRenderSignature(pass)` / pass-aware material identity — that is REQ-007's job and would conflate two large refactors.
- Multi-UBO materials. `MaterialInstance` stores a single `const ShaderResourceBinding*` for the first `UniformBuffer` in reflection. The data structure will be promoted to a vector when a shader actually needs two UBOs.
- Push-constant reflection. Push-constant handling stays whatever `DrawMaterial` currently does (engine convention), out of scope.
- `RenderPassEntry` field restructuring. We touch only `MaterialTemplate` internals; `RenderPassEntry` keeps `renderState`, `shaderSet`, `bindingCache` exactly as today.
- Hot-reload / runtime shader swap on a live `MaterialInstance`.

## Decisions

### Decision 1: `MaterialInstance` stores a single flat byte buffer, not typed maps
- **Choice**: `std::vector<uint8_t> m_uboBuffer;` sized from reflection. Setters `memcpy` into offsets looked up by member name. Optional cached `std::unordered_map<StringID, size_t>` can be built on demand if profiling shows the per-set linear scan is hot.
- **Alternative**: Keep `m_vec4s` / `m_floats` maps and flush them into a temporary UBO buffer during `updateUBO()`.
- **Rationale**: The maps duplicate state (the buffer is already the source of truth) and force `updateUBO()` to walk every key even when nothing changed. Writing straight into the buffer matches how the GPU sees the data and makes `getRawData()` a zero-copy pointer return. This is also how `SkeletonUBO` already works in-repo, so the pattern is familiar.

### Decision 2: Centralize setter logic in a `writeUboMember` helper
- **Choice**: `void writeUboMember(StringID id, const void* src, size_t nbytes, ShaderPropertyType expected)`. Each public setter (`setVec4`, `setVec3`, `setFloat`, `setInt`) is a one-line wrapper.
- **Alternative**: Duplicate the loop-to-find-offset-then-memcpy logic in each setter.
- **Rationale**: The four setters differ only in the expected type tag and byte count. Centralizing means the `StringID(member.name) != id` comparison, the `assert(m.type == expected)` guard, and the `memcpy` call live in one place. This makes it trivial to add `setMat4` / `setUInt` / etc. later without risking divergence between setters.

### Decision 3: std140 `vec3` writes copy only 12 bytes, not 16
- **Choice**: `setVec3` passes `sizeof(Vec3f) == 12` to `writeUboMember`. If the next member packs into the trailing 4 bytes (e.g. `float shininess` in `blinnphong_0.frag`), we must not clobber it.
- **Alternative**: Always write 16 bytes for alignment.
- **Rationale**: REQ-004's test already confirmed that spirv-cross reports `vec3 baseColor @ 0` followed by `float shininess @ 12`. Writing 16 bytes for a `vec3` would overwrite `shininess` every time someone called `setVec3(baseColor, ...)`. The earlier REQ-005 draft incorrectly said "always 16 bytes" — we're correcting that here.

### Decision 4: `MaterialInstance` holds a stable `IRenderResource` wrapper, not a per-call wrapper
- **Choice**: Construct the UBO `IRenderResource` wrapper once in the constructor (via `wrapAsUboResource(m_uboBuffer, *m_uboBinding, passFlag)`) and cache it as `m_uboResource`. `getDescriptorResources()` appends the cached pointer; `updateUBO()` calls `m_uboResource->setDirty()`.
- **Alternative**: Build a fresh `IRenderResource` inside each `getDescriptorResources()` call.
- **Rationale**: The backend's `VulkanResourceManager::syncResource()` keys its GPU-side cache on the `IRenderResource` identity. A fresh wrapper every frame would defeat that cache. The cached wrapper also gives us a safe place to flip dirty flags from setter calls.

### Decision 5: `wrapAsUboResource` lives in infra, not core
- **Choice**: New file `src/infra/gpu/ubo_resource.hpp` (and `.cpp` if necessary) provides the factory. `MaterialInstance` (core) depends on the resulting `IRenderResource` interface but not on the concrete class.
- **Alternative**: Put it in `src/core/gpu/render_resource.hpp` alongside the interface.
- **Rationale**: `SkeletonUBO` lives in `src/core/resources/`, but it owns its own `Mat4f[]` storage. `wrapAsUboResource` needs a weak/non-owning reference back into `MaterialInstance::m_uboBuffer` — that's a lifetime contract specific to the material-layer setup, not a primitive. Keeping it in infra lets us import `<cstring>` / `<memory>` without polluting the core header and matches the project's existing layering (cpp-style-guide says core defines contracts, infra provides implementations).

### Decision 6: `MaterialTemplate` key stays `std::string`, not `StringID`
- **Choice**: `setPass(const std::string&, RenderPassEntry)` / `getEntry(const std::string&)` keep string keys. `m_bindingCache` is keyed by `StringID` because reflection binding names are interned by design.
- **Alternative**: Migrate pass keys to `StringID` now.
- **Rationale**: REQ-007 will globally migrate pass identifiers to `StringID` with `Pass_Forward` / `Pass_Shadow` constants. Doing it twice (once here, once there) would churn more call sites. Keep pass keys as `std::string` in this change; REQ-007 handles the final transition.

### Decision 7: Single-UBO assumption, fail-loud on multi-UBO shaders
- **Choice**: Constructor loops reflection bindings, captures the first `UniformBuffer`, and breaks. Any shader with two UBOs silently uses the first.
- **Alternative**: Support a vector of UBO bindings.
- **Rationale**: No shader in the project currently has more than one material-level UBO. Paying the complexity cost of multi-UBO now means adding a binding-number keyed map, dirty flags per buffer, and more wrapper resources — all unverified. Add `TODO(multi-ubo)` comment next to the single-buffer field so the shape is visible. When a real shader needs it, the vector refactor is mechanical.

## Risks / Trade-offs

- **[Risk]** The migration is visibly user-facing — every existing `DrawMaterial` construction is deleted or rewritten, and any missed call site fails to compile. → **Mitigation**: Grep-sweep `DrawMaterial` before committing and confirm zero hits outside `archive/`. The compiler will catch the rest.
- **[Risk]** `wrapAsUboResource` takes a non-owning reference into `MaterialInstance::m_uboBuffer`. If `MaterialInstance` is moved (e.g. shuffled inside a container), the pointer dangles. → **Mitigation**: Disable copy/move on `MaterialInstance` or only allow ownership through `shared_ptr` (which `create()` already enforces). Document the contract in the wrapper.
- **[Risk]** Hidden assumption in current backend: `DrawMaterial::albedoSampler` / `normalSampler` are dereferenced directly for `VkDescriptorImageInfo` writes. If backend code paths introspect the concrete type rather than going through `getDescriptorResources()`, those paths will break. → **Mitigation**: Before deleting `DrawMaterial`, grep for `DrawMaterial` in `src/backend/` and fix any concrete-type uses to go through `IMaterial`.
- **[Risk]** Reflection returns `_memberN` fallback names for stripped SPIR-V (REQ-004 behavior). Default seed calls like `setVec3(StringID("baseColor"), …)` would silently hit an `assert(false)` if the shipping shader is stripped. → **Mitigation**: All shaders in this repo go through GLSL → SPIR-V with names preserved; document the constraint and rely on the existing assert. If stripped shaders become a target, member lookup must fall back to set/binding + declared order, but that's out of scope.
- **[Trade-off]** Assertions-not-exceptions for unknown member names. Release builds that drop asserts will silently skip the write. Acceptable for now — rendering a wrong value is caught visually during development and an exception here would only replace one crash with another.

## Migration Plan

This is an in-tree breaking change with a straightforward forward path:

1. **Pre-work**: scan `src/backend/` and `src/test/` for direct `DrawMaterial` type references (not `IMaterial`). List them. Any found go on a fix list before deleting the class.
2. **Additive phase**: implement `wrapAsUboResource`, flesh out `MaterialInstance`, rename the loader, add the new test. At this point `DrawMaterial` still exists and still compiles; nothing is deleted yet.
3. **Cutover phase**: switch loader call sites to the new `loadBlinnPhongMaterial()` function. Rebuild — any compile error indicates a missed migration.
4. **Removal phase**: delete `DrawMaterial` / `BlinnPhongMaterialUBO` from `material.hpp` + `.cpp`. Delete the old loader file. Rebuild + run all tests.
5. **Rollback strategy**: each phase is a separate commit. Revert the removal commit to restore `DrawMaterial` without losing the new `MaterialInstance` work.

No data migration is required (materials are constructed fresh at load time, no on-disk format).

## Open Questions

- Should `updateUBO()` be called automatically by setters (push model) or explicitly by the render loop (pull model)? The draft assumes a dirty flag + explicit flush, matching `SkeletonUBO`. Leaving this as designed; revisit if the render loop ends up double-flushing.
- Does `RenderPassEntry::buildCache()` still belong to the per-entry object or should it move fully into `MaterialTemplate::buildBindingCache()`? Current code has both. This cleanup is deferred — safer to touch as part of REQ-007's broader `RenderPassEntry` churn.
