# Reflection Probe Capture And Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add local reflection probes, probe capture, probe cache output, and shared Forward/Deferred environment lighting.

**Architecture:** Add `ReflectionProbeComponent`, a `ReflectionCapture` render path, scene-level probe GPU resources, and a shared GLSL environment lighting include. Extend `bakeScene` so each local probe runs capture plus filter, writes a cache entry, and updates `SceneResourceTable`.

**Tech Stack:** C++20, CMake/Ninja, yaml-cpp, Vulkan, GLSL, existing scene component model, `RenderPathGraph`, `SceneResourceTable`, Forward and Deferred render paths.

---

## Dependency

Complete these plans first:

- `docs/superpowers/plans/2026-06-15-reflection-filter-brdf-graph.md`
- `docs/superpowers/plans/2026-06-15-scene-bake-cache.md`

## File Structure

- Create `src/core/scene/components/reflection_probe_component.*`.
- Modify scene YAML parser/saver in `src/infra/scene_io/scene_document.*`.
- Create `src/test/integration/test_reflection_probe_scene_document.cpp`.
- Modify `src/test/CMakeLists.txt`: add `test_reflection_probe_scene_document`.
- Add `assets/render_paths/reflection_capture.render-path.yaml`.
- Add `assets/render_paths/reflection_filter_cubemap.render-path.yaml`.
- Add shaders under `assets/shaders/glsl/render_paths/ReflectionCapture/`.
- Modify `src/core/asset/render_effect.hpp`: add `ReflectionCapture` and
  `cube-face-capture`.
- Modify `src/core/frame_graph/render_work_compiler.cpp`: expand capture inputs.
- Modify `src/core/scene/scene_resource_table.*`: expose probe set resources.
- Create `assets/shaders/glsl/common/environment_lighting.glsl`.
- Modify `assets/shaders/glsl/render_paths/Forward/pbr.frag`.
- Modify `assets/shaders/glsl/render_paths/Deferred/deferred_lighting.frag`.
- Modify `assets/render_paths/forward_main.render-path.yaml`,
  `assets/render_paths/forward_bloom.render-path.yaml`,
  `assets/render_paths/deferred_main.render-path.yaml`, and
  `assets/render_paths/deferred_bloom.render-path.yaml`.
- Extend `src/editor/runtime/scene_bake_service.*`.
- Add test cases to `src/test/integration/test_reflection_probe_scene_document.cpp`,
  `src/test/integration/test_render_resource_parsers.cpp`,
  `src/test/integration/test_render_work_compiler.cpp`,
  `src/test/integration/test_shader_compiler.cpp`, and
  `src/test/integration/test_vulkan_reflection_probe_bake.cpp`.

## Task 1: Add ReflectionProbe Scene Parser Tests

**Files:**
- Create: `src/test/integration/test_reflection_probe_scene_document.cpp`
- Modify: `src/test/CMakeLists.txt`
- Modify: `src/infra/scene_io/scene_document.*`

- [ ] **Step 1: Add parser tests**

Add tests:

```cpp
void testSceneParsesReflectionProbeComponent();
void testSceneRejectsUnknownReflectionProbeField();
void testReflectionProbeIsNotCollectedAsCamera();
```

Valid YAML:

```yaml
components:
  reflectionProbe:
    global: false
    capture:
      projection: cubemap
      resolution: 256
      nearClip: 0.1
      farClip: 50.0
      includeSky: true
    influence:
      shape: sphere
      radius: 12.0
      blendDistance: 2.0
    projection:
      mode: sphere
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
cmake --build build --target test_reflection_probe_scene_document
./build/src/test/test_reflection_probe_scene_document
```

Expected: tests fail before the component is implemented.

## Task 2: Implement ReflectionProbeComponent

**Files:**
- Create: `src/core/scene/components/reflection_probe_component.hpp`
- Create: `src/core/scene/components/reflection_probe_component.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/infra/scene_io/scene_document.*`
- Modify: `src/test/integration/test_reflection_probe_scene_document.cpp`

- [ ] **Step 1: Add component data**

```cpp
enum class ReflectionProbeInfluenceShape {
  Sphere,
  Box,
};

struct ReflectionProbeCaptureSettings final {
  u32 resolution = 256;
  float nearClip = 0.1f;
  float farClip = 50.0f;
  bool includeSky = true;
};

class ReflectionProbeComponent final : public IComponent {
public:
  bool global = false;
  ReflectionProbeCaptureSettings capture;
  ReflectionProbeInfluenceShape influenceShape = ReflectionProbeInfluenceShape::Sphere;
  float radius = 10.0f;
  float blendDistance = 1.0f;
};
```

- [ ] **Step 2: Parse and save strict YAML**

Accept only the YAML fields from Task 1. Reject unknown fields.

- [ ] **Step 3: Run scene tests**

Run:

```bash
cmake --build build --target test_reflection_probe_scene_document
./build/src/test/test_reflection_probe_scene_document
```

Expected: parser tests pass.

## Task 3: Add ReflectionCapture Graph Contracts

**Files:**
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/infra/resource_parsers/render_path_graph_resource_parser.cpp`
- Modify: `src/infra/resource_parsers/render_pass_node_parser.cpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

- [ ] **Step 1: Add render path and input kind**

Add:

```cpp
RenderPath::ReflectionCapture
RenderPassInputKind::CubeFaceCapture
```

- [ ] **Step 2: Parse `input.capture`**

Accept:

```yaml
input:
  kind: cube-face-capture
  capture:
    target: capture.radiance
    faces: 6
    camera: probe-capture
```

Reject `input.capture` on every other input kind.

- [ ] **Step 3: Run parser tests**

Run:

```bash
cmake --build build --target test_render_resource_parsers
./build/src/test/test_render_resource_parsers
```

Expected: ReflectionCapture parser tests pass.

## Task 4: Add ReflectionCapture Graph Asset And Compiler Expansion

**Files:**
- Create: `assets/render_paths/reflection_capture.render-path.yaml`
- Create: `assets/shaders/glsl/render_paths/ReflectionCapture/capture_radiance.vert`
- Create: `assets/shaders/glsl/render_paths/ReflectionCapture/capture_radiance.frag`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`

- [ ] **Step 1: Add graph asset**

Use shader URI:

```text
render_paths/ReflectionCapture/capture_radiance
```

The graph writes `capture.radiance` cubemap.

- [ ] **Step 2: Add compiler test**

Add:

```cpp
void testCubeFaceCaptureExpandsToSixDrawInputs();
```

Assert six `RenderDrawInput` values with `bakeIteration.faceLayer` from 0 to 5.

- [ ] **Step 3: Implement expansion**

Use the same cubemap iteration metadata pattern as `cube-face-filter`, but set
the source to scene renderables and target to `capture.radiance`.

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target CompileShaders test_render_work_compiler
./build/src/test/test_render_work_compiler
```

Expected: shader compile succeeds and capture expansion passes.

## Task 5: Add Probe Runtime Resource Set

**Files:**
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/rhi/gpu_resource_table.hpp`
- Modify: `src/backend/vulkan/vulkan_gpu_resource_table.*`
- Modify: `src/test/integration/test_scene_resource_table.cpp`

- [ ] **Step 1: Add resource record**

```cpp
struct ReflectionProbeGpuRecord final {
  Vec4f positionAndRadius;
  Vec4f blendAndFlags;
  u32 prefilteredEnvMapIndex = 0;
  u32 diffuseSh9Index = 0;
  u32 brdfLutIndex = 0;
  u32 _padding = 0;
};
```

- [ ] **Step 2: Add table view**

Expose a scene-level `ReflectionProbeSetResource` only when all referenced
`PrefilteredEnvMap`, `DiffuseSH9`, and `BrdfLut` payloads are live.

- [ ] **Step 3: Add negative test**

Assert metadata-only probe assets do not satisfy `scene.reflectionProbes`.

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target test_scene_resource_table
./build/src/test/test_scene_resource_table
```

Expected: live resource checks pass.

## Task 6: Share Environment Lighting Shader Code

**Files:**
- Create: `assets/shaders/glsl/common/environment_lighting.glsl`
- Modify: `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- Modify: `assets/shaders/glsl/render_paths/Deferred/deferred_lighting.frag`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add common include**

The include exposes:

```glsl
vec3 lxEvaluateEnvironmentDiffuse(vec3 worldPos, vec3 normal, vec3 baseColor);
vec3 lxEvaluateEnvironmentSpecular(vec3 worldPos, vec3 normal, vec3 viewDir,
                                   vec3 f0, float roughness);
```

When probe count is zero, both functions return `vec3(0.0)`.

- [ ] **Step 2: Replace duplicated IBL math**

Forward and DeferredLighting call the common functions instead of duplicating
`IrradianceMap` / `PrefilteredEnvMap` / `BrdfLut` sampling logic.

- [ ] **Step 3: Run shader tests**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler
./build/src/test/test_shader_compiler
```

Expected: shader reflection exposes the probe lighting resource set in both
runtime paths.

## Task 7: Wire RenderPathGraph Dependencies

**Files:**
- Modify: `assets/render_paths/forward_main.render-path.yaml`
- Modify: `assets/render_paths/forward_bloom.render-path.yaml`
- Modify: `assets/render_paths/deferred_main.render-path.yaml`
- Modify: `assets/render_paths/deferred_bloom.render-path.yaml`
- Modify: `src/core/frame_graph/graph_resource_registry.cpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

- [ ] **Step 1: Add graph resource**

Register:

```text
scene.reflectionProbes
```

as a scene-level resource.

- [ ] **Step 2: Add pass sources**

Add `scene.reflectionProbes` to Forward and DeferredLighting pass sources.

- [ ] **Step 3: Run graph parser tests**

Run:

```bash
cmake --build build --target test_render_resource_parsers
./build/src/test/test_render_resource_parsers
```

Expected: graph parser accepts the new source only when the registry knows it.

## Task 8: Extend bakeScene For Local Probes

**Files:**
- Modify: `src/editor/runtime/scene_bake_service.*`
- Modify: scene bake cache writer/loader files.
- Add Vulkan smoke test for local probe bake.

- [ ] **Step 1: Iterate probes**

For each `ReflectionProbeComponent`, run:

```text
ReflectionCapture -> reflection_filter_cubemap -> write probe cache -> register asset
```

- [ ] **Step 2: Write probe cache**

Write local probe manifests under:

```text
.lxe-bake/<scene-stem>/probes/<node-id>/
```

- [ ] **Step 3: Add smoke test**

The test creates a tiny scene with one probe, calls `bakeScene`, and asserts
that `SceneResourceTable` exposes one local probe plus one global probe when a
global sky environment is present.

- [ ] **Step 4: Run smoke**

Run:

```bash
cmake --build build --target test_vulkan_reflection_probe_bake
xvfb-run -a ./build/src/test/test_vulkan_reflection_probe_bake
```

Expected: smoke exits 0, or skips with exit 0 only for explicit video device
initialization failure.

## Task 9: Final Verification

**Files:**
- Modify: `assets/shaders/README.md`
- Modify: `notes/concepts/material/shader-catalog.md`
- Modify: `notes/subsystems/scene.md`

- [ ] **Step 1: Run final commands**

Run:

```bash
cmake --build build --target CompileShaders CompileMaterialSourceShaderVariants test_render_resource_parsers test_render_work_compiler test_shader_compiler test_scene_resource_table test_vulkan_reflection_probe_bake lxe_editor
./build/src/test/test_render_resource_parsers
./build/src/test/test_render_work_compiler
./build/src/test/test_shader_compiler
./build/src/test/test_scene_resource_table
xvfb-run -a ./build/src/test/test_vulkan_reflection_probe_bake
git diff --check
```

Expected: all commands exit 0 and `git diff --check` has no output.

- [ ] **Step 2: Run audits**

Run:

```bash
rg -n "ReflectionProbeComponent|ReflectionCapture|scene.reflectionProbes|DiffuseSH9|PrefilteredEnvMap" src assets notes docs
rg -n "IrradianceMap" src assets/shaders/glsl/render_paths/Forward assets/shaders/glsl/render_paths/Deferred
```

Expected: probe terms appear in implementation/tests/docs; `IrradianceMap`
does not remain in Forward or Deferred runtime lighting shaders.
