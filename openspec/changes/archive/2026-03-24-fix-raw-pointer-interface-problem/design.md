## Context

The `VulkanResourceManager` class in `src/backend/vulkan/details/vk_resource_manager.hpp` provides getter functions that return raw pointers to Vulkan objects (buffers, textures, shaders, render passes, pipelines). The C++ style guide mandates using references (`T&`) instead of raw pointers (`T*`) for non-owning object references.

The style guide rationale: raw pointers obscure ownership semantics, invite dangling pointer bugs, and make lifetime requirements unclear. In this case, the `VulkanResourceManager` owns the objects, so callers receive non-owning references - exactly the use case for `T&`.

## Goals / Non-Goals

**Goals:**
- Replace raw pointer return types with references in `VulkanResourceManager` getters
- Ensure all call sites are updated to use reference syntax

**Non-Goals:**
- This is not a ownership change - `VulkanResourceManager` still owns all objects
- Not changing the internal implementation or resource management logic
- Not adding new functionality

## Decisions

### Decision 1: Use references instead of pointers for getter return types

**Choice**: Change return types from `VulkanBuffer*` to `VulkanBuffer&` (and similar for other types)

**Rationale**: Per C++ style guide section "Required Replacements", `T*` for non-owning reference should be replaced with `T&`. The `VulkanResourceManager` owns the objects, so these getters provide access to owned objects - non-owning references.

**Alternatives considered**:
- `std::optional<T&>` - Would allow null returns, but the style guide reserves `std::optional` for nullable types that are not pointers. In this design, objects are expected to exist if registered.
- Keep pointers - Violates the explicit style guide rule.

### Decision 2: Keep `void*` for handle parameter

**Choice**: The `getBuffer(void* handle)` and similar functions keep `void*` parameter type

**Rationale**: Per style guide, `void*` is allowed as handle/identifier pointers where the pointer value itself is the identifier (not dereferenced to access object state). This is exactly the case here - the handle is a lookup key.

## Risks / Trade-offs

**[Risk] Callers using `->` syntax will break**
→ **Mitigation**: Search for all usages of these functions and update to `.` syntax

**[Risk] Some getters may legitimately return null in edge cases**
→ **Mitigation**: Review the implementation to ensure this cannot happen. If it can, the function may need redesign (but per style guide, null returns should use `std::optional<T&>`)

**[Risk] ABI change if these functions are exported in a DLL/API**
→ **Mitigation**: This appears to be an internal implementation detail, not a public API

## Migration Plan

1. Update `vk_resource_manager.hpp` - change return types from `T*` to `T&`
2. Update `vk_resource_manager.cpp` - change return statements from `return pointer;` to `return *pointer;`
3. Find all call sites using grep
4. Update each call site to use `.` instead of `->`
5. Build and verify

## Open Questions

None - the change is straightforward per the style guide specification.
