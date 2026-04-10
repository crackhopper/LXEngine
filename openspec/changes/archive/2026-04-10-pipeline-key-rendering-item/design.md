## Context

- `docs/requirements/002-pipeline-key.md` defines `PipelineKey` as a core `StringID` wrapper, built from shader program set, vertex layout, render state, primitive topology, and skeleton participation, using existing `getPipelineHash()` on each contributor (`openspec/specs/resource-pipeline-hash/spec.md`).
- Today `RenderingItem` (`src/core/scene/scene.hpp`) carries shader, buffers, descriptors, and pass mask but no stable pipeline identity; `VulkanResourceManager` pre-registers `blinnphong_0` and `vk_renderer.cpp` falls back to string keys.
- Blinn-Phong–specific code (`vkp_blinnphong.*`, `blinn_phong_material_stub.hpp`) duplicates what dynamic shader reflection + material instances can express once pipelines are keyed by real draw state.

## Goals / Non-Goals

**Goals:**

- Implement `PipelineKey` in core and thread it through `RenderingItem` and `Scene::buildRenderingItem()`.
- Key the Vulkan pipeline cache by `PipelineKey` (via `PipelineKey::Hash` / `StringID`) and create pipelines on cache miss from the item’s shader and layout data.
- Remove dedicated Blinn-Phong pipeline and material stub classes; keep `blinnphong_0` GLSL as an example shader reachable through the generic path.
- Preserve existing integration test coverage by exercising the generic pipeline factory and command recording paths.

**Non-Goals:**

- Redesigning the full material template system or ImGui.
- Adding MSAA or new pipeline factors beyond REQ-002 (future extension only noted in `build()`).
- Cross-backend (non-Vulkan) abstractions for pipeline cache.

## Decisions

1. **`PipelineKey` location and shape** — Add `src/core/resources/pipeline_key.hpp` with `struct PipelineKey { StringID id; ... Hash; static PipelineKey build(...) }`, matching REQ-002. Rationale: keeps identity in core; backend stays free of string parsing.

2. **String canonicalization** — Use the REQ-002 format `{shader}|{sorted variants}|vl:{hash}|rs:{hash}|{topology}[|+skel]`, interned via `StringID`. Rationale: matches existing design doc and `resource-pipeline-hash` expectations.

3. **`RenderingItem` contents** — Add `PipelineKey pipelineKey` only; do not remove fields needed for binding until a follow-up proves redundant. Rationale: minimal churn; backend can use key for cache and existing fields for recording.

4. **Remove `VkPipelineBlinnPhong` / `MaterialBlinnPhong`** — Replace with `VulkanPipeline` instances created through the same code path used for other shaders (layout from mesh + reflection). Rationale: one pipeline creation path; avoids duplicate slot metadata.

5. **Push constants** — Prefer sizes and layouts from shader reflection / material binding cache rather than `PC_BlinnPhong` at call sites where generic drawing applies; keep `PC_BlinnPhong` only if still required for a specific test shader until migrated.

6. **Resource manager API** — Expose `getOrCreatePipeline(PipelineKey, ...)` (or equivalent) instead of `getRenderPipeline("blinnphong_0")`. Rationale: explicit cache semantics and testability.

## Risks / Trade-offs

- **[Risk] Key string growth / intern table churn** — Many variant combinations create many `StringID`s → Mitigation: keys only built when items change; document debugging via `getName(id)`.
- **[Risk] First-frame pipeline compile stalls** — Cache misses compile pipelines during draw → Mitigation: acceptable for demo scope; optional warmup pass can be a later task.
- **[Risk] Tests tightly coupled to Blinn-Phong class names** — Mitigation: rewrite tests to assert on pipeline creation success and draw validity using generic types.

## Migration Plan

1. Land `PipelineKey` + `RenderingItem` + `Scene::buildRenderingItem()` updates; keep backend compiling with temporary dual path if needed (short-lived).
2. Switch `VulkanResourceManager` / `VulkanRenderer` to `PipelineKey` cache; delete Blinn-Phong-specific types and includes.
3. Update CMake sources and integration tests; run `ninja BuildTest` and main `Renderer` smoke path.
4. Remove dead symbols (`PC_BlinnPhong` usages) once no references remain.

## Open Questions

- Whether `initScene` should pre-warm pipelines for the current `RenderingItem` or rely entirely on lazy creation during `draw()`.
- Exact fate of `PC_BlinnPhong` in `render_resource.hpp` once all call sites use generic push-constant sizing.
