# Reflection Filter And BRDF Graph Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the private `IBLBake` renderer with graph-authored `ReflectionFilter` and `BrdfLutBake` runtime paths.

**Architecture:** Extend the existing `RenderPathGraph` and `RenderWorkCompiler` model with filter resource declarations, cubemap face/mip draw inputs, SH9 compute output metadata, and graph-owned shader URIs. Keep execution in the current Vulkan backend, but remove hardcoded old bake shader names.

**Tech Stack:** C++20, CMake/Ninja, yaml-cpp, Vulkan, GLSL, existing `RenderPathGraph`, `FrameGraph`, `RenderWorkCompiler`, `SceneResourceTable`, and integration tests.

---

## Dependency

Read `docs/superpowers/specs/2026-06-15-reflection-filter-brdf-graph-design.md`
before executing this plan.

## File Structure

- Modify `src/core/asset/render_effect.hpp`: add `ReflectionFilter`,
  `BrdfLutBake`, filter input contracts, and graph resource declarations.
- Modify `src/core/frame_graph/render_input.hpp`: add bake iteration metadata
  to draw inputs and SH9 output metadata to compute inputs.
- Modify `src/infra/resource_parsers/render_path_graph_resource_parser.cpp`:
  parse strict root `resources`.
- Modify `src/infra/resource_parsers/render_pass_node_parser.cpp`: parse
  `cube-face-filter`, `cube-mip-face-filter`, and `input.filter`.
- Modify `src/core/frame_graph/graph_resource_registry.*`: build a registry
  from graph-declared resources.
- Modify `src/core/frame_graph/render_work_compiler.cpp`: expand filter inputs.
- Create `assets/render_paths/reflection_filter_spherical.render-path.yaml`.
- Create `assets/render_paths/brdf_lut_bake.render-path.yaml`.
- Move/rename root IBL bake shaders into
  `assets/shaders/glsl/render_paths/ReflectionFilter/` and
  `assets/shaders/glsl/render_paths/BrdfLutBake/`.
- Modify or replace `src/backend/vulkan/details/ibl_bake_renderer.*` with a
  graph executor.
- Modify tests in `src/test/integration/test_render_resource_parsers.cpp`,
  `src/test/integration/test_render_work_compiler.cpp`,
  `src/test/integration/test_shader_compiler.cpp`, and
  `src/test/integration/test_vulkan_ibl_bake.cpp`.

## Task 1: Add Graph Parser Negative Tests

**Files:**
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

- [ ] **Step 1: Add failing tests**

Add tests named:

```cpp
void testRejectsLegacyIblBakeRenderPath();
void testReflectionFilterRequiresDeclaredResources();
void testBrdfLutBakeRejectsRootShaderUri();
void testFilterInputRejectsDirectShaderSourceUri();
void testFilterContractRejectedOnFullscreenInput();
```

Use graph snippets that include:

```yaml
renderPath: IBLBake
```

and:

```yaml
shader: ibl_brdf_lut
```

and:

```yaml
shader: assets/shaders/glsl/render_paths/BrdfLutBake/integrate_standard_ggx.frag
```

Expected diagnostics must mention the rejected field or URI.

- [ ] **Step 2: Run the parser test**

Run:

```bash
cmake --build build --target test_render_resource_parsers
./build/src/test/test_render_resource_parsers
```

Expected: the new tests fail before implementation because the new render path
values and resources are not modeled.

## Task 2: Add Core Graph Contracts

**Files:**
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/core/asset/render_effect.cpp`
- Modify: `src/core/frame_graph/render_input.hpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`

- [ ] **Step 1: Add enum values and structs**

Add:

```cpp
enum class RenderPath {
  Forward,
  Deferred,
  OfflineRT,
  ReflectionFilter,
  BrdfLutBake,
};

enum class RenderPassInputKind {
  SceneRenderables,
  FullscreenTriangle,
  ComputeDispatch,
  CubeFaceFilter,
  CubeMipFaceFilter,
};

struct RenderFilterInputContract final {
  std::string source;
  std::string target;
  u32 faces = 6;
  float sampleCount = 64.0f;
  bool roughnessFromMip = false;
};
```

- [ ] **Step 2: Add render input metadata**

Add to `RenderDrawInput`:

```cpp
std::optional<RenderBakeIteration> bakeIteration;
```

where:

```cpp
struct RenderBakeIteration final {
  StringID source;
  StringID target;
  u32 mipLevel = 0;
  u32 faceLayer = 0;
  u32 extent = 1;
  float roughness = 0.0f;
  float sourceMipCount = 1.0f;
  float sampleCount = 64.0f;
};
```

- [ ] **Step 3: Run core contract tests**

Run:

```bash
cmake --build build --target test_render_path_graph_pass_contract
./build/src/test/test_render_path_graph_pass_contract
```

Expected: existing tests pass after signature updates include the new input
kind names and filter contract fields.

## Task 3: Parse Resources And Filter Inputs

**Files:**
- Modify: `src/infra/resource_parsers/render_path_graph_resource_parser.cpp`
- Modify: `src/infra/resource_parsers/render_pass_node_parser.cpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

- [ ] **Step 1: Parse render path values**

Accept only:

```text
Forward
Deferred
OfflineRT
ReflectionFilter
BrdfLutBake
```

Reject `IBLBake`.

- [ ] **Step 2: Parse root resources**

Accept strict `resources.imports` and `resources.outputs` entries with:

```yaml
type: texture2d | cubemap | sh9
binding: <non-empty string>
format: RGBA16Float | RGB32Float
extent: <integer or supported settings key>
mips: <integer or supported settings key>
projection: equirectangular
optional: true
coefficients: 9
```

Reject unknown keys.

- [ ] **Step 3: Parse filter input kinds**

Accept:

```yaml
input:
  kind: cube-mip-face-filter
  filter:
    source: filter.radiance
    target: filter.prefiltered
    roughness: from-mip
    sampleCount: settings.specularSampleCount
```

Reject `input.filter` on every non-filter kind.

- [ ] **Step 4: Run parser tests**

Run:

```bash
cmake --build build --target test_render_resource_parsers
./build/src/test/test_render_resource_parsers
```

Expected: parser tests pass.

## Task 4: Add Graph Assets And Move Shaders

**Files:**
- Create: `assets/render_paths/reflection_filter_spherical.render-path.yaml`
- Create: `assets/render_paths/brdf_lut_bake.render-path.yaml`
- Move: root IBL bake shaders into render-path folders.
- Modify: `src/test/integration/test_shader_compiler.cpp`
- Modify: `assets/shaders/README.md`
- Modify: `notes/concepts/material/shader-catalog.md`

- [ ] **Step 1: Move and rename shaders**

Run:

```bash
mkdir -p assets/shaders/glsl/render_paths/ReflectionFilter
mkdir -p assets/shaders/glsl/render_paths/BrdfLutBake
mv assets/shaders/glsl/equirect_to_cubemap.vert assets/shaders/glsl/render_paths/ReflectionFilter/spherical_to_cubemap.vert
mv assets/shaders/glsl/equirect_to_cubemap.frag assets/shaders/glsl/render_paths/ReflectionFilter/spherical_to_cubemap.frag
mv assets/shaders/glsl/ibl_prefilter_env.vert assets/shaders/glsl/render_paths/ReflectionFilter/prefilter_specular_env.vert
mv assets/shaders/glsl/ibl_prefilter_env.frag assets/shaders/glsl/render_paths/ReflectionFilter/prefilter_specular_env.frag
mv assets/shaders/glsl/ibl_brdf_lut.vert assets/shaders/glsl/render_paths/BrdfLutBake/integrate_standard_ggx.vert
mv assets/shaders/glsl/ibl_brdf_lut.frag assets/shaders/glsl/render_paths/BrdfLutBake/integrate_standard_ggx.frag
```

Delete the old `ibl_irradiance_convolve.*` files or keep them only as a source
for the new `project_diffuse_sh9.comp` implementation. They must not remain as
root shader assets.

- [ ] **Step 2: Add graph assets**

Create the two YAML files exactly under `assets/render_paths/` and use shader
URIs:

```text
render_paths/ReflectionFilter/spherical_to_cubemap
render_paths/ReflectionFilter/prefilter_specular_env
render_paths/ReflectionFilter/project_diffuse_sh9
render_paths/BrdfLutBake/integrate_standard_ggx
```

- [ ] **Step 3: Compile shaders**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler
./build/src/test/test_shader_compiler
```

Expected: all shader compiler tests pass and no root IBL shader outputs are
regenerated.

## Task 5: Compile Graph Resources Into Work Inputs

**Files:**
- Modify: `src/core/frame_graph/graph_resource_registry.*`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`

- [ ] **Step 1: Build registry from graph resources**

Add:

```cpp
[[nodiscard]] static GraphResourceRegistry
fromRenderPathGraph(const RenderPathGraph &graph);
```

It registers imports and outputs from graph declarations. Non-bake realtime
graphs without declarations continue using `makeDefault()`.

- [ ] **Step 2: Add compiler tests**

Add:

```cpp
void testCubeFaceFilterExpandsToSixDrawInputs();
void testCubeMipFaceFilterExpandsToMipFaceDrawInputs();
```

Assertions:

```cpp
EXPECT(result.inputs.size() == 6, "cube-face-filter should emit six inputs");
EXPECT(result.inputs.size() == 24, "four mip cube filter should emit 24 inputs");
```

- [ ] **Step 3: Implement expansion**

Emit one `RenderDrawInput` per face and per mip/face pair, filling
`bakeIteration.source`, `target`, `mipLevel`, `faceLayer`, `extent`, and
`roughness`.

- [ ] **Step 4: Run compiler tests**

Run:

```bash
cmake --build build --target test_render_work_compiler
./build/src/test/test_render_work_compiler
```

Expected: new expansion tests pass.

## Task 6: Replace Private IBL Bake Renderer

**Files:**
- Create: `src/backend/vulkan/details/reflection_filter_graph_executor.hpp`
- Create: `src/backend/vulkan/details/reflection_filter_graph_executor.cpp`
- Create: `src/backend/vulkan/details/brdf_lut_bake_graph_executor.hpp`
- Create: `src/backend/vulkan/details/brdf_lut_bake_graph_executor.cpp`
- Modify: `src/backend/vulkan/details/ibl_bake_renderer.*`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_vulkan_ibl_bake.cpp`

- [ ] **Step 1: Add graph executor interfaces**

```cpp
class VulkanReflectionFilterGraphExecutor final {
public:
  [[nodiscard]] ReflectionProbeBakeAsset
  execute(const LX_core::RenderPathGraph &graph,
          const ReflectionFilterSettings &settings);
};

class VulkanBrdfLutBakeGraphExecutor final {
public:
  [[nodiscard]] BrdfLutAsset
  execute(const LX_core::RenderPathGraph &graph,
          const BrdfLutBakeSettings &settings);
};
```

- [ ] **Step 2: Move valid backend helpers**

Keep cubemap allocation, subresource views, framebuffer creation, layout
tracking, and readback helpers. Remove shader-name-driven helpers that build old
private bake work items.

- [ ] **Step 3: Migrate the Vulkan smoke**

Change `test_vulkan_ibl_bake.cpp` so it loads the two graph assets and executes
the graph executors. Keep readback checks for `PrefilteredEnvMap`, `DiffuseSH9`,
and `BrdfLut`.

- [ ] **Step 4: Run smoke**

Run:

```bash
cmake --build build --target test_vulkan_ibl_bake
xvfb-run -a ./build/src/test/test_vulkan_ibl_bake
```

Expected: smoke exits 0 through graph executors.

## Task 7: Final Hard-Cut Audit

**Files:**
- Modify: `assets/shaders/README.md`
- Modify: `notes/concepts/material/shader-catalog.md`

- [ ] **Step 1: Run build and tests**

Run:

```bash
cmake --build build --target CompileShaders CompileMaterialSourceShaderVariants test_render_resource_parsers test_render_work_compiler test_shader_compiler test_vulkan_ibl_bake lxe_editor
./build/src/test/test_render_resource_parsers
./build/src/test/test_render_work_compiler
./build/src/test/test_shader_compiler
xvfb-run -a ./build/src/test/test_vulkan_ibl_bake
```

- [ ] **Step 2: Run audits**

Run:

```bash
rg -n "IBLBake|iblbake|IblBakeRenderer|bakeStaticEnvironment" src assets docs notes
rg -n "\"(equirect_to_cubemap|ibl_irradiance_convolve|ibl_prefilter_env|ibl_brdf_lut)\"" src assets/render_paths assets/shaders/glsl
find assets/shaders/glsl -maxdepth 1 -type f \( -name 'equirect_to_cubemap.*' -o -name 'ibl_*' \) -print
git diff --check
```

Expected: no production backend hit for old bake names, no root IBL shader files,
and no whitespace errors.
