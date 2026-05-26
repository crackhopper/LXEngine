# PBR IBL Active Requirements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the active `notes/requirements` queue from procedural cleanup through standard HDR post-processing, HDR/cubemap resources, static IBL baking, PBR IBL materials, the metal-sphere scene, and the final tutorial.

**Architecture:** Treat `REQ-046-a` as the rendering pipeline pivot: Forward writes linear HDR scene color, standard PostProcess writes swapchain, and the old `FullscreenProcedural` branch is deleted rather than completed. `REQ-047-a` supplies HDR/cubemap texture resource shapes, `REQ-048-a` produces static IBL resources, `REQ-049-a` makes PBR materials consume scene-level IBL resources, `REQ-050-a` creates the visual validation scene, and `REQ-051-a` documents the factual workflow after implementation.

**Tech Stack:** C++20, CMake/Ninja, Vulkan, GLSL/glslc, yaml-cpp, stb_image, LXEngine `src/core`, `src/infra`, `src/backend/vulkan`, `src/demos/lxe_editor`, `assets/`, `notes/`.

---

## Coordination Rules

- Do not implement on `main`. Use an isolated worktree or branch before code changes.
- Do not run multiple workers on overlapping files. The 046 -> 051 chain is mostly sequential.
- Use parallel agents only for independent read-only investigation or disjoint small fixes.
- Do not preserve compatibility with `FramePassKind::FullscreenProcedural`; migrate useful procedural audio/texture pieces and delete the old fullscreen branch.
- Use current code, current `notes/requirements`, and current specs as authority.

## Active Requirements And Dependency Order

| REQ | Status | Dependency Position |
|---|---|---|
| `045-a` | Mostly implemented; small test/status gap | Pre-flight cleanup |
| `045-b` | Partially implemented; small non-blocking gaps | Pre-flight cleanup |
| `045-c` | Audio/dynamic texture implemented; old fullscreen branch unfinished | Must be migrated by `046-a` |
| `046-a` | Draft | First major implementation |
| `047-a` | Draft | After/alongside early `046-a` HDR format decisions |
| `048-a` | Draft | Requires `047-a` texture/cubemap resource shape |
| `049-a` | Draft | Requires `046-a`, `047-a`, `048-a` |
| `050-a` | Draft | Requires `049-a` |
| `051-a` | Draft | Last, after scene and commands are factual |

## File Structure

### Pre-flight `045` Cleanup

- Modify: `src/test/integration/test_scene_runtime.cpp`
  - Add explicit procedural material preset coverage.
- Modify: `src/test/integration/test_scene_runtime.cpp` or `src/test/integration/test_scene_document.cpp`
  - Add negative runtime parameter type mismatch coverage.
- Modify: `notes/requirements/045-a-procedural-shader-gallery-foundation.md`
- Modify: `notes/requirements/045-b-procedural-runtime-parameter-stream.md`
- Modify: `notes/requirements/045-c-procedural-audio-and-framegraph-integration.md`
  - Update implementation status after cleanup and mark old fullscreen branch as migrated by `046-a`.

### `046-a` Standard Post-process Stack

- Modify: `src/core/frame_graph/pass.hpp`
  - Add `Pass_PostProcess`.
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`
- Modify: `src/test/integration/test_frame_graph.cpp`
  - Remove `FramePassKind::FullscreenProcedural` and `fullscreenMaterial`.
  - Add Forward HDR -> PostProcess graph tests.
- Modify: `src/core/rhi/image_format.hpp`
- Modify: `src/backend/vulkan/vulkan_renderer.cpp`
- Modify: `src/backend/vulkan/details/resource_manager.cpp`
  - Add `RGBA16Float` / `VK_FORMAT_R16G16B16A16_SFLOAT`.
- Create: `src/backend/vulkan/details/post_process/post_process_pass.hpp`
- Create: `src/backend/vulkan/details/post_process/post_process_pass.cpp`
  - Own renderer-side post material, descriptor resources, and fullscreen pipeline build desc.
- Modify: `src/backend/vulkan/details/commands/command_buffer.hpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
  - Add fullscreen triangle draw command.
- Create: `assets/shaders/glsl/post_tonemap.vert`
- Create: `assets/shaders/glsl/post_tonemap.frag`
- Create: `assets/materials/post_tonemap.material`
- Later in this REQ: add bloom pass shaders/materials if baseline tone map is stable.

### `047-a` HDR Texture And Cubemap Resource

- Modify: `src/core/asset/texture.hpp`
  - Add `TextureDimension`, float formats, mip/layer fields, byte-count helpers.
- Modify: `src/core/asset/audio_spectrum_texture.hpp`
  - Preserve existing RGBA8 dynamic texture behavior with new desc fields.
- Modify/Create: `src/infra/texture_loader/hdr_texture_loader.hpp`
- Modify/Create: `src/infra/texture_loader/hdr_texture_loader.cpp`
  - Load `.hdr` via `stbi_loadf`.
- Modify: `src/backend/vulkan/details/device_resources/texture.hpp`
- Modify: `src/backend/vulkan/details/device_resources/texture.cpp`
  - Support cube-compatible images, mip levels, array layers, cube image views.
- Modify: `src/backend/vulkan/details/resource_manager.cpp`
  - Map float formats and upload multi-layer/mip resources.
- Test: `src/test/integration/test_audio_spectrum_texture.cpp`
- Test: `src/test/integration/test_shader_compiler.cpp`
- Test: `src/test/integration/test_vulkan_texture.cpp`

### `048-a` IBL GPU Bake Pipeline

- Create: `src/core/scene/environment.hpp`
- Create: `src/core/scene/environment.cpp`
  - Define `EnvironmentSettings` and scene-level IBL resource ownership.
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/scene/scene.cpp`
  - Store active environment and expose scene-level IBL descriptor resources.
- Create: `src/backend/vulkan/details/ibl/ibl_baker.hpp`
- Create: `src/backend/vulkan/details/ibl/ibl_baker.cpp`
  - Build equirectangular -> skybox, irradiance, prefiltered radiance, and BRDF LUT resources.
- Create: `assets/shaders/glsl/ibl_equirect_to_cube.vert`
- Create: `assets/shaders/glsl/ibl_equirect_to_cube.frag`
- Create: `assets/shaders/glsl/ibl_irradiance.frag`
- Create: `assets/shaders/glsl/ibl_prefilter.frag`
- Create: `assets/shaders/glsl/ibl_brdf_lut.vert`
- Create: `assets/shaders/glsl/ibl_brdf_lut.frag`
- Test: `src/test/integration/test_shader_compiler.cpp`
- Test: `src/test/integration/test_vulkan_frame_graph.cpp` or a new `src/test/integration/test_vulkan_ibl_baker.cpp`.

### `049-a` PBR IBL Material Contract

- Modify: `src/core/asset/shader_binding_ownership.hpp`
  - Add system-owned IBL bindings: `IrradianceMap`, `PrefilteredEnvMap`, `BrdfLut`, `EnvironmentUBO`.
- Modify: `assets/shaders/glsl/pbr.frag`
  - Remove final tone mapping/gamma and fixed ambient.
  - Add diffuse irradiance and specular split-sum IBL.
- Modify: `assets/shaders/glsl/pbr.vert` only if tangent/normal outputs need alignment.
- Modify: `assets/materials/pbr_gold.material`
  - Keep material-owned parameters/resources only.
- Modify: `src/demos/lxe_editor/scene_builder.cpp`
  - Add PBR bridge path for glTF metadata and PBR test materials.
- Test: `src/test/integration/test_shader_compiler.cpp`
- Test: `src/test/integration/test_generic_material_loader.cpp`
- Test: `src/test/integration/test_material_instance.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`

### `050-a` IBL Metal Sphere Test Scene

- Modify: `src/demos/lxe_editor/scene_document.hpp`
- Modify: `src/demos/lxe_editor/scene_document.cpp`
  - Add environment/HDR scene config round-trip.
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
  - Load environment config, trigger IBL bake/default resources, and inject into scene.
- Create: `assets/scenes/ibl_metal_sphere.scene.yaml`
- Modify: `src/demos/lxe_editor/project_catalog.cpp` or project/session loading path if discoverability requires it.
- Test: `src/test/integration/test_scene_document.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`
- Test: Vulkan screenshot/use-case path when display is available.

### `051-a` PBR IBL Tutorial

- Create: `notes/tutorial/pbr-ibl-metal-sphere.md`
- Modify: `notes/nav.yml`
- Modify: `notes/concepts-design/rendering-pipeline/index.md` or `notes/concepts/material/index.md` only if a short current-fact link is useful.
- Run: `scripts/notes/serve_site.sh --build` or `python3 scripts/notes/generate_site_config.py`.

## Task 0: Set Up Isolated Execution Workspace

**Files:**
- Read: `AGENTS.md`
- Read: `openspec/specs/cpp-style-guide/spec.md`
- Read: active requirements under `notes/requirements/`
- Modify conditionally: `.gitignore`

- [ ] **Step 1: Detect current workspace isolation**

Run:

```bash
GIT_DIR=$(cd "$(git rev-parse --git-dir)" && pwd -P)
GIT_COMMON=$(cd "$(git rev-parse --git-common-dir)" && pwd -P)
git rev-parse --show-superproject-working-tree 2>/dev/null
git branch --show-current
git status --short
```

Expected: report whether the checkout is already a linked worktree. If it is not isolated and the branch is `main`, do not implement code in place.

- [ ] **Step 2: Preserve current requirement-doc work**

Run:

```bash
git status --short
git diff -- .codex/skills/draft-req/SKILL.md notes/requirements/README.md notes/requirements/index.md
```

Expected: identify the requirement-document changes that exist before implementation. Commit or stash them only with explicit user approval; otherwise create the implementation worktree from the current HEAD and copy/apply the approved requirement docs there before coding.

- [ ] **Step 3: Create isolated implementation worktree**

Run only after approval to create a worktree:

```bash
git check-ignore -q .worktrees || printf '\n.worktrees/\n' >> .gitignore
git worktree add .worktrees/pbr-ibl-active-reqs -b pbr-ibl-active-reqs
```

Expected: implementation happens under `.worktrees/pbr-ibl-active-reqs`, not on `main`.

- [ ] **Step 4: Configure baseline**

Run in the implementation worktree:

```bash
cmake -S . -B build -G Ninja
cmake --build build --target BuildTest CompileShaders -j2
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
```

Expected: baseline headless tests pass, or pre-existing failures are recorded before development.

## Task 1: Close Cheap `045` Gaps Before Post Migration

**Files:**
- Modify: `src/test/integration/test_scene_runtime.cpp`
- Modify: `notes/requirements/045-a-procedural-shader-gallery-foundation.md`
- Modify: `notes/requirements/045-b-procedural-runtime-parameter-stream.md`
- Modify: `notes/requirements/045-c-procedural-audio-and-framegraph-integration.md`

- [ ] **Step 1: Add material preset coverage**

In `src/test/integration/test_scene_runtime.cpp`, extend the existing `materialPresets()` test near its current assertions so it checks for:

```cpp
const auto presets = runtime.materialPresets();
EXPECT(std::find(presets.begin(), presets.end(),
                 "assets/materials/rtr_shadertoy_quantum_core.material") !=
           presets.end(),
       "procedural material should be available as a material preset");
```

- [ ] **Step 2: Run the focused test**

Run:

```bash
cmake --build build --target test_scene_runtime -j2
./build/src/test/test_scene_runtime
```

Expected: fail if the preset is filtered out; pass if already included.

- [ ] **Step 3: Fix preset filtering only if the test fails**

If the test fails, update the preset list source in `src/demos/lxe_editor/scene_runtime.cpp` so `assets/materials/rtr_shadertoy_quantum_core.material` remains visible while invalid hidden materials remain filtered. Do not change unrelated preset behavior.

- [ ] **Step 4: Add a type-mismatch negative test for procedural runtime parameters**

In `src/test/integration/test_scene_runtime.cpp`, add a focused test that loads a procedural node whose `proceduralMaterial.timeMember` points to a non-float reflected member, calls `SceneRuntime::updateProceduralMaterials(...)`, and asserts the call reports a stable diagnostic while `nodeMaterialOverrides` remains unchanged. Reuse existing scene YAML string fixtures around the current procedural runtime tests.

- [ ] **Step 5: Run focused tests**

Run:

```bash
cmake --build build --target test_scene_runtime test_scene_document test_audio_spectrum_texture -j2
./build/src/test/test_scene_runtime
./build/src/test/test_scene_document
./build/src/test/test_audio_spectrum_texture
```

Expected: all pass.

- [ ] **Step 6: Update 045 requirement statuses**

Update the implementation status sections:

- `045-a`: mark remaining preset test gap closed if Step 1-5 pass.
- `045-b`: mark type-mismatch test closed if Step 4-5 pass; keep Inspector/CommandBus UI items as downstream if not implemented.
- `045-c`: keep audio/dynamic texture pieces complete; state the old fullscreen branch is intentionally migrated/deleted by `REQ-046-a`.

- [ ] **Step 7: Commit**

Run:

```bash
git add src/test/integration/test_scene_runtime.cpp notes/requirements/045-a-procedural-shader-gallery-foundation.md notes/requirements/045-b-procedural-runtime-parameter-stream.md notes/requirements/045-c-procedural-audio-and-framegraph-integration.md
git commit -m "test: close procedural requirement gaps"
```

Expected: one focused pre-flight commit.

## Task 2: Add PostProcess Pass And Remove Old Fullscreen Branch

**Files:**
- Modify: `src/core/frame_graph/pass.hpp`
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`
- Modify: `src/test/integration/test_frame_graph.cpp`
- Modify: `notes/requirements/045-c-procedural-audio-and-framegraph-integration.md`

- [ ] **Step 1: Write failing graph tests**

Replace the existing `FullscreenProcedural` preservation test in `src/test/integration/test_frame_graph.cpp` with a test named `testPostProcessReadsForwardHdrColor`.

Use this structure:

```cpp
void testPostProcessReadsForwardHdrColor() {
  using namespace LX_core;
  FrameGraph graph;
  const auto hdr = FrameGraphResourceRef::colorAttachment(StringID("scene.hdrColor"));
  const auto depth = FrameGraphResourceRef::depthAttachment(StringID("scene.depth"));
  const auto swap = FrameGraphResourceRef::colorAttachment(StringID("swapchain.color"));

  graph.addPass(FramePass{Pass_Forward,
                          RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float),
                          {},
                          {},
                          {FrameGraphWrite{hdr}, FrameGraphWrite{depth}}});
  graph.addPass(FramePass{Pass_PostProcess,
                          RenderTargetDesc::swapchain(ImageFormat::BGRA8,
                                                      ImageFormat::D32Float),
                          {},
                          {FrameGraphRead::sampled(StringID("scene.hdrColor"),
                                                   StringID("SceneColor"))},
                          {FrameGraphWrite{swap}}});

  const auto compiled = graph.compile();
  EXPECT(compiled.isValid(), "post pass should read forward HDR color");
  EXPECT(compiled.getPasses().size() == 2,
         "compiled graph should keep forward and post passes");
  EXPECT(compiled.getPasses()[1].name == Pass_PostProcess,
         "second pass should be PostProcess");
}
```

Also add a search-style compile test or assertion that no production header exposes `FramePassKind`.

- [ ] **Step 2: Run test and verify failure**

Run:

```bash
cmake --build build --target test_frame_graph -j2
./build/src/test/test_frame_graph
```

Expected: fail to compile because `Pass_PostProcess` and/or `ImageFormat::RGBA16Float` do not exist and `FramePassKind` still exists.

- [ ] **Step 3: Add pass constant**

Add to `src/core/frame_graph/pass.hpp`:

```cpp
inline const StringID Pass_PostProcess = StringID("PostProcess");
```

- [ ] **Step 4: Remove old fullscreen fields**

In `src/core/frame_graph/frame_graph.hpp`, delete:

```cpp
enum class FramePassKind { ... };
MaterialInstanceSharedPtr fullscreenMaterial;
```

from both `FramePass` and `CompiledFrameGraphPass`. In `src/core/frame_graph/frame_graph.cpp`, update `CompiledFrameGraphPass` construction to copy only name, target, reads, and writes.

- [ ] **Step 5: Run frame graph tests**

Run:

```bash
cmake --build build --target test_frame_graph -j2
./build/src/test/test_frame_graph
```

Expected: pass once `RGBA16Float` is added in Task 3; if Task 3 is not yet done, keep this test as the red test for Task 3.

- [ ] **Step 6: Commit**

Run:

```bash
git add src/core/frame_graph/pass.hpp src/core/frame_graph/frame_graph.hpp src/core/frame_graph/frame_graph.cpp src/test/integration/test_frame_graph.cpp notes/requirements/045-c-procedural-audio-and-framegraph-integration.md
git commit -m "refactor: replace fullscreen procedural hook with post pass"
```

Expected: core cleanup committed.

## Task 3: Add HDR Render Target Format

**Files:**
- Modify: `src/core/rhi/image_format.hpp`
- Modify: `src/core/frame_graph/render_target.hpp`
- Modify: `src/backend/vulkan/vulkan_renderer.cpp`
- Modify: `src/backend/vulkan/details/resource_manager.cpp`
- Test: `src/test/integration/test_frame_graph.cpp`
- Test: `src/test/integration/test_pipeline_identity.cpp`

- [ ] **Step 1: Add failing format tests**

Add assertions that `RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float)` has a distinct pipeline signature from `ImageFormat::RGBA8` and swapchain `BGRA8`.

- [ ] **Step 2: Add core enum**

Add `RGBA16Float` to `ImageFormat` in `src/core/rhi/image_format.hpp`.

- [ ] **Step 3: Map Vulkan formats**

Map `ImageFormat::RGBA16Float` to `VK_FORMAT_R16G16B16A16_SFLOAT` in both Vulkan format conversion helpers:

- `src/backend/vulkan/vulkan_renderer.cpp`
- `src/backend/vulkan/details/resource_manager.cpp`

Also map reverse `VK_FORMAT_R16G16B16A16_SFLOAT` to `ImageFormat::RGBA16Float`.

- [ ] **Step 4: Run focused tests**

Run:

```bash
cmake --build build --target test_frame_graph test_pipeline_identity -j2
./build/src/test/test_frame_graph
./build/src/test/test_pipeline_identity
```

Expected: pass.

- [ ] **Step 5: Commit**

Run:

```bash
git add src/core/rhi/image_format.hpp src/core/frame_graph/render_target.hpp src/backend/vulkan/vulkan_renderer.cpp src/backend/vulkan/details/resource_manager.cpp src/test/integration/test_frame_graph.cpp src/test/integration/test_pipeline_identity.cpp
git commit -m "feat: add HDR render target format"
```

## Task 4: Implement Baseline Post-process Material And Fullscreen Draw

**Files:**
- Create: `assets/shaders/glsl/post_tonemap.vert`
- Create: `assets/shaders/glsl/post_tonemap.frag`
- Create: `assets/materials/post_tonemap.material`
- Create: `src/backend/vulkan/details/post_process/post_process_pass.hpp`
- Create: `src/backend/vulkan/details/post_process/post_process_pass.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.hpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
- Modify: `src/backend/vulkan/CMakeLists.txt`
- Test: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add shader compiler test for post tonemap**

In `test_shader_compiler.cpp`, add a test that compiles `post_tonemap.vert/frag` and asserts reflection contains:

- `PostProcessUBO` as `UniformBuffer`
- `SceneColor` as `Texture2D`

- [ ] **Step 2: Create fullscreen triangle vertex shader**

Create `assets/shaders/glsl/post_tonemap.vert`:

```glsl
#version 450
layout(location = 0) out vec2 vUV;
void main() {
    vec2 pos[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 uv[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );
    gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
    vUV = uv[gl_VertexIndex];
}
```

- [ ] **Step 3: Create baseline tone-map fragment shader**

Create `assets/shaders/glsl/post_tonemap.frag`:

```glsl
#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform PostProcessUBO {
    float exposure;
    int toneMapMode;
    float gamma;
    float bloomIntensity;
} postParams;

layout(set = 1, binding = 1) uniform sampler2D SceneColor;

vec3 reinhard(vec3 color) {
    return color / (color + vec3(1.0));
}

vec3 acesApprox(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) /
                 (color * (c * color + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(SceneColor, vUV).rgb * postParams.exposure;
    vec3 mapped = postParams.toneMapMode == 1 ? reinhard(hdr) : acesApprox(hdr);
    float gammaValue = max(postParams.gamma, 0.0001);
    mapped = pow(mapped, vec3(1.0 / gammaValue));
    outColor = vec4(mapped, 1.0);
}
```

- [ ] **Step 4: Create material asset**

Create `assets/materials/post_tonemap.material`:

```yaml
shader: post_tonemap

passes:
  PostProcess:
    renderState:
      cullMode: None
      depthTest: false
      depthWrite: false

parameters:
  PostProcessUBO.exposure: 1.0
  PostProcessUBO.toneMapMode: 0
  PostProcessUBO.gamma: 2.2
  PostProcessUBO.bloomIntensity: 0.0

resources:
  SceneColor: white
```

- [ ] **Step 5: Add fullscreen command**

Add to `VulkanCommandBuffer`:

```cpp
void drawFullscreenTriangle();
```

Implementation:

```cpp
void VulkanCommandBuffer::drawFullscreenTriangle() {
  vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
}
```

Use the actual command-buffer member name from the file.

- [ ] **Step 6: Add post pass helper**

Create `PostProcessPass` that loads `assets/materials/post_tonemap.material`, stores one `MaterialInstanceSharedPtr`, appends `FrameGraphSampledResource("scene.hdrColor", "SceneColor")` before draw, syncs descriptor resources, and exposes a method to build a fullscreen `PipelineBuildDesc`.

If `PipelineBuildDesc::fromRenderingItem()` cannot be reused without vertex/index buffers, add a dedicated static factory such as:

```cpp
static PipelineBuildDesc PipelineBuildDesc::fromFullscreenMaterial(
    const MaterialInstance &material,
    StringID pass,
    const RenderTargetDesc &target);
```

Implement only what the post pass needs.

- [ ] **Step 7: Run shader tests**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler -j2
./build/src/test/test_shader_compiler assets/shaders/glsl
```

Expected: post shaders compile and reflect required bindings.

- [ ] **Step 8: Commit**

Run:

```bash
git add assets/shaders/glsl/post_tonemap.vert assets/shaders/glsl/post_tonemap.frag assets/materials/post_tonemap.material src/backend/vulkan/details/post_process src/backend/vulkan/details/commands src/backend/vulkan/CMakeLists.txt src/test/integration/test_shader_compiler.cpp
git commit -m "feat: add standard tone mapping post pass assets"
```

## Task 5: Wire Forward HDR To PostProcess Swapchain

**Files:**
- Modify: `src/backend/vulkan/vulkan_renderer.cpp`
- Modify: `src/backend/vulkan/details/resource_manager.*`
- Modify: `src/core/frame_graph/frame_graph.*` if post pipeline desc collection needs a core hook.
- Test: `src/test/integration/test_vulkan_frame_graph.cpp`

- [ ] **Step 1: Add renderer pass-order test**

Extend `test_vulkan_frame_graph.cpp` or a headless renderer inspection test to assert compiled graph contains, in order:

```text
Shadow, Shadow, Shadow, Shadow, Forward, PostProcess, DebugOverlay
```

Expected failure before wiring.

- [ ] **Step 2: Update `initScene()` graph construction**

In `VulkanRenderer::Impl::initScene()`:

- derive `swapchainTarget` as now
- create `forwardHdrDesc = RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float)` and include depth if the current render-pass model requires it
- backfill cameras to `RenderTarget{forwardHdrDesc}` before `m_frameGraph.buildFromScene(*scene)`
- add Forward pass writing `scene.hdrColor` and `scene.depth`
- add PostProcess pass reading `scene.hdrColor` as `SceneColor` and writing `swapchain.color`
- add DebugOverlay after PostProcess on swapchain target

- [ ] **Step 3: Execute post pass in draw loop**

When the compiled pass name is `Pass_PostProcess`, draw the standard fullscreen post pass instead of `drawPassQueue(passIndex, cmd)`.

Required behavior:

- sync post material resources during `initScene()` and `uploadData()`
- bind post pipeline
- bind `SceneColor` via `FrameGraphSampledResource`
- call `drawFullscreenTriangle()`

- [ ] **Step 4: Preserve overlay output**

Keep PostProcess and DebugOverlay in the final contiguous swapchain pass group so DebugOverlay/ImGui do not clear post output.

- [ ] **Step 5: Run focused Vulkan tests**

Run:

```bash
cmake --build build --target test_frame_graph test_vulkan_frame_graph lxe_editor -j2
./build/src/test/test_frame_graph
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
```

Expected: frame graph pass order and Vulkan execution pass.

- [ ] **Step 6: Commit**

Run:

```bash
git add src/backend/vulkan/vulkan_renderer.cpp src/backend/vulkan/details/resource_manager.* src/core/frame_graph src/test/integration/test_vulkan_frame_graph.cpp
git commit -m "feat: route forward HDR through post process"
```

## Task 6: Add Bloom V1

**Files:**
- Create: `assets/shaders/glsl/post_bloom_threshold.vert`
- Create: `assets/shaders/glsl/post_bloom_threshold.frag`
- Create: `assets/shaders/glsl/post_bloom_blur.vert`
- Create: `assets/shaders/glsl/post_bloom_blur.frag`
- Modify: `src/backend/vulkan/details/post_process/post_process_pass.*`
- Test: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add shader reflection tests**

Add tests for bloom threshold and blur/composite shaders. Required bindings:

- `SceneColor`
- `BloomParamsUBO`

- [ ] **Step 2: Implement threshold shader**

Create threshold shader that samples scene color, computes luminance, and outputs only pixels above threshold.

- [ ] **Step 3: Implement blur/composite path**

Implement one downsample/blur/upsample or a one-pass separable blur if the first version needs to stay small. Keep `bloomIntensity = 0.0` as the default disabled state.

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler test_vulkan_frame_graph -j2
./build/src/test/test_shader_compiler assets/shaders/glsl
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
```

- [ ] **Step 5: Commit**

Run:

```bash
git add assets/shaders/glsl/post_bloom* src/backend/vulkan/details/post_process src/test/integration/test_shader_compiler.cpp
git commit -m "feat: add bloom post process baseline"
```

## Task 7: Implement HDR Texture And Cubemap Resource Shape

**Files:**
- Modify: `src/core/asset/texture.hpp`
- Modify: `src/core/asset/audio_spectrum_texture.hpp`
- Test: `src/test/integration/test_audio_spectrum_texture.cpp`

- [ ] **Step 1: Add failing texture metadata tests**

Add tests for:

- `TextureFormat::RGBA16F` bytes per pixel equals 8
- `TextureFormat::RGBA32F` bytes per pixel equals 16
- `TextureDimension::TextureCube` requires 6 layers
- mip chain byte count sums all mip levels
- existing RGBA8 audio texture desc remains valid

- [ ] **Step 2: Implement desc fields**

Update `TextureDesc`:

```cpp
enum class TextureDimension { Texture2D, TextureCube };

struct TextureDesc {
  u32 width = 0;
  u32 height = 0;
  TextureFormat format = TextureFormat::RGBA8;
  TextureDimension dimension = TextureDimension::Texture2D;
  u32 mipLevels = 1;
  u32 arrayLayers = 1;
};
```

Add `RGBA16F` and `RGBA32F`.

- [ ] **Step 3: Update validation helpers**

Make byte-count validation include all mip levels and layers. Throw `std::runtime_error("cubemap texture requires 6 layers")` when dimension is cube and layers != 6.

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target test_audio_spectrum_texture -j2
./build/src/test/test_audio_spectrum_texture
```

- [ ] **Step 5: Commit**

Run:

```bash
git add src/core/asset/texture.hpp src/core/asset/audio_spectrum_texture.hpp src/test/integration/test_audio_spectrum_texture.cpp
git commit -m "feat: extend texture metadata for HDR and cubemaps"
```

## Task 8: Add HDR Loader And Cube Reflection Coverage

**Files:**
- Create: `src/infra/texture_loader/hdr_texture_loader.hpp`
- Create: `src/infra/texture_loader/hdr_texture_loader.cpp`
- Modify: `src/infra/texture_loader/CMakeLists.txt` or owning CMake file.
- Modify: `src/test/integration/test_shader_compiler.cpp`
- Create: `assets/shaders/glsl/test_sampler_cube.vert`
- Create: `assets/shaders/glsl/test_sampler_cube.frag`
- Test: new or existing texture loader test.

- [ ] **Step 1: Add failing HDR loader test**

Create or extend a test to load `assets/env/studio_small_03_2k.hdr` and assert:

- width > 0
- height > 0
- format is `RGBA32F` or chosen float format
- data byte count equals `expectedTextureByteCount(desc)`

- [ ] **Step 2: Implement `HdrTextureLoader`**

Use `stbi_loadf(path.c_str(), &w, &h, &channels, 4)`, copy floats into `std::vector<u8>` bytes, and expose a `Texture` or `TextureDesc + bytes` result. Free with `stbi_image_free`.

- [ ] **Step 3: Add samplerCube shader fixture**

Create `test_sampler_cube.frag` with:

```glsl
#version 450
layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 0) uniform samplerCube EnvMap;
void main() {
    outColor = texture(EnvMap, vec3(0.0, 1.0, 0.0));
}
```

Use a minimal fullscreen/triangle vertex shader.

- [ ] **Step 4: Assert reflection returns `TextureCube`**

Extend `test_shader_compiler.cpp` to compile the fixture and assert binding `EnvMap` has `ShaderPropertyType::TextureCube`.

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler BuildTest -j2
./build/src/test/test_shader_compiler assets/shaders/glsl
```

- [ ] **Step 6: Commit**

Run:

```bash
git add src/infra/texture_loader assets/shaders/glsl/test_sampler_cube.* src/test/integration/test_shader_compiler.cpp
git commit -m "feat: load HDR textures and test cube reflection"
```

## Task 9: Extend Vulkan Texture For Cubemaps And Mips

**Files:**
- Modify: `src/backend/vulkan/details/device_resources/texture.hpp`
- Modify: `src/backend/vulkan/details/device_resources/texture.cpp`
- Modify: `src/backend/vulkan/details/resource_manager.cpp`
- Test: `src/test/integration/test_vulkan_texture.cpp`

- [ ] **Step 1: Add failing Vulkan cubemap smoke test**

In `test_vulkan_texture.cpp`, add a requires-video-device test that creates a cubemap texture with 6 layers and 1 mip level, then asserts:

- `getImageView()` is non-null
- `getSampler()` is non-null
- `getWidth()` / `getHeight()` match
- descriptor info layout is shader-read.

- [ ] **Step 2: Introduce Vulkan texture create info**

Add a struct:

```cpp
struct VulkanTextureCreateInfo {
  u32 width = 1;
  u32 height = 1;
  u32 mipLevels = 1;
  u32 arrayLayers = 1;
  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
  VkImageCreateFlags flags = 0;
  VkFilter filter = VK_FILTER_LINEAR;
  VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
};
```

Keep existing `create()` wrappers delegating to this struct.

- [ ] **Step 3: Use mip/layer fields in image creation**

Set image `mipLevels`, `arrayLayers`, and `flags`. For cubemap, use `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT` and `VK_IMAGE_VIEW_TYPE_CUBE`.

- [ ] **Step 4: Update barriers and copies**

Make transition and copy helpers use `m_mipLevels` and `m_arrayLayers`, or add explicit region helpers for upload. Preserve existing 2D behavior.

- [ ] **Step 5: Update resource manager**

Map:

- `TextureFormat::RGBA16F -> VK_FORMAT_R16G16B16A16_SFLOAT`
- `TextureFormat::RGBA32F -> VK_FORMAT_R32G32B32A32_SFLOAT`

Create cube views when `TextureDesc::dimension == TextureDimension::TextureCube`.

- [ ] **Step 6: Run Vulkan texture tests**

Run:

```bash
cmake --build build --target test_vulkan_texture -j2
xvfb-run -a ./build/src/test/test_vulkan_texture
```

- [ ] **Step 7: Commit**

Run:

```bash
git add src/backend/vulkan/details/device_resources/texture.* src/backend/vulkan/details/resource_manager.cpp src/test/integration/test_vulkan_texture.cpp
git commit -m "feat: support Vulkan cubemap textures"
```

## Task 10: Implement Static IBL Resource Model And Bake Shaders

**Files:**
- Create: `src/core/scene/environment.hpp`
- Create: `src/core/scene/environment.cpp`
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/scene/scene.cpp`
- Create: `assets/shaders/glsl/ibl_equirect_to_cube.vert`
- Create: `assets/shaders/glsl/ibl_equirect_to_cube.frag`
- Create: `assets/shaders/glsl/ibl_irradiance.frag`
- Create: `assets/shaders/glsl/ibl_prefilter.frag`
- Create: `assets/shaders/glsl/ibl_brdf_lut.vert`
- Create: `assets/shaders/glsl/ibl_brdf_lut.frag`
- Test: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add shader compiler tests for IBL bake shaders**

Assert all IBL bake shaders compile and expose expected bindings:

- HDR panorama texture
- source environment cubemap
- bake parameter UBO when present

- [ ] **Step 2: Define core environment resource names**

In `environment.hpp`, define constants:

```cpp
inline const StringID Binding_SkyboxMap = StringID("SkyboxMap");
inline const StringID Binding_IrradianceMap = StringID("IrradianceMap");
inline const StringID Binding_PrefilteredEnvMap = StringID("PrefilteredEnvMap");
inline const StringID Binding_BrdfLut = StringID("BrdfLut");
inline const StringID Binding_EnvironmentUBO = StringID("EnvironmentUBO");
```

Add a small environment resource holder that exposes `std::vector<IGpuResourceSharedPtr> getDescriptorResources() const`.

- [ ] **Step 3: Add shader assets**

Implement standard graphics-pipeline bake shaders. Keep BRDF LUT 2D and cubemap bake outputs separate.

- [ ] **Step 4: Run shader tests**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler -j2
./build/src/test/test_shader_compiler assets/shaders/glsl
```

- [ ] **Step 5: Commit**

Run:

```bash
git add src/core/scene/environment.* src/core/scene/scene.* assets/shaders/glsl/ibl_* src/test/integration/test_shader_compiler.cpp
git commit -m "feat: add static IBL resource model and bake shaders"
```

## Task 11: Implement Vulkan IBL Baker

**Files:**
- Create: `src/backend/vulkan/details/ibl/ibl_baker.hpp`
- Create: `src/backend/vulkan/details/ibl/ibl_baker.cpp`
- Modify: `src/backend/vulkan/CMakeLists.txt`
- Modify: `src/backend/vulkan/vulkan_renderer.cpp`
- Test: `src/test/integration/test_vulkan_frame_graph.cpp` or new `src/test/integration/test_vulkan_ibl_baker.cpp`

- [ ] **Step 1: Add Vulkan IBL baker smoke test**

Add a requires-video-device test that loads `assets/env/studio_small_03_2k.hdr`, invokes the baker, and asserts all four outputs are non-null:

- skybox cubemap
- irradiance cubemap
- prefiltered cubemap
- BRDF LUT

- [ ] **Step 2: Implement `IblBaker` skeleton**

The baker should own no scene state. It receives `VulkanDevice`, `VulkanResourceManager`, command buffer manager, and an HDR texture resource, then returns core environment descriptor resources.

- [ ] **Step 3: Implement equirectangular-to-cube pass**

Render six faces into a cube-compatible texture. Use deterministic face matrices and fixed output size.

- [ ] **Step 4: Implement irradiance, prefilter, and BRDF LUT passes**

Use graphics passes. Keep resolutions small in tests to reduce runtime.

- [ ] **Step 5: Add debug dump hook**

Expose a way to dump at least BRDF LUT or cubemap face through existing attachment dump tooling or a small test helper.

- [ ] **Step 6: Run Vulkan tests**

Run:

```bash
cmake --build build --target test_vulkan_frame_graph -j2
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
```

If a new test target is created, add it to `src/test/CMakeLists.txt` and run it under `xvfb-run`.

- [ ] **Step 7: Commit**

Run:

```bash
git add src/backend/vulkan/details/ibl src/backend/vulkan/CMakeLists.txt src/backend/vulkan/vulkan_renderer.cpp src/test/integration
git commit -m "feat: bake static IBL resources on GPU"
```

## Task 12: Add PBR IBL Material Contract

**Files:**
- Modify: `src/core/asset/shader_binding_ownership.hpp`
- Modify: `assets/shaders/glsl/pbr.frag`
- Modify: `assets/materials/pbr_gold.material`
- Modify: `src/test/integration/test_shader_compiler.cpp`
- Modify: `src/test/integration/test_generic_material_loader.cpp`
- Modify: `src/test/integration/test_material_instance.cpp`

- [ ] **Step 1: Add failing system-owned binding test**

In `test_material_instance.cpp` or a nearby material ownership test, assert:

```cpp
EXPECT(isSystemOwnedBinding("IrradianceMap"), "IrradianceMap is scene-owned");
EXPECT(isSystemOwnedBinding("PrefilteredEnvMap"), "PrefilteredEnvMap is scene-owned");
EXPECT(isSystemOwnedBinding("BrdfLut"), "BrdfLut is scene-owned");
EXPECT(isSystemOwnedBinding("EnvironmentUBO"), "EnvironmentUBO is scene-owned");
```

Expected failure before ownership update.

- [ ] **Step 2: Update ownership table**

Add expected types:

- `IrradianceMap -> TextureCube`
- `PrefilteredEnvMap -> TextureCube`
- `BrdfLut -> Texture2D`
- `EnvironmentUBO -> UniformBuffer`

- [ ] **Step 3: Update PBR shader**

In `pbr.frag`:

- remove fixed ambient
- remove final Reinhard/gamma
- add IBL bindings
- compute diffuse irradiance
- compute specular split-sum with `textureLod(PrefilteredEnvMap, R, lod)` and `BrdfLut`

- [ ] **Step 4: Update PBR material**

Keep only material-owned resources in `pbr_gold.material`. Do not list `IrradianceMap`, `PrefilteredEnvMap`, `BrdfLut`, or `EnvironmentUBO` under `.material resources`.

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler test_generic_material_loader test_material_instance -j2
./build/src/test/test_shader_compiler assets/shaders/glsl
./build/src/test/test_generic_material_loader
./build/src/test/test_material_instance
```

Expected: PBR shader reflects IBL bindings; material loader does not treat them as material-owned missing resources.

- [ ] **Step 6: Commit**

Run:

```bash
git add src/core/asset/shader_binding_ownership.hpp assets/shaders/glsl/pbr.frag assets/materials/pbr_gold.material src/test/integration/test_shader_compiler.cpp src/test/integration/test_generic_material_loader.cpp src/test/integration/test_material_instance.cpp
git commit -m "feat: add PBR IBL material contract"
```

## Task 13: Inject Scene-level IBL Resources

**Files:**
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/scene/scene.cpp`
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`
- Test: `src/test/integration/test_frame_graph.cpp`

- [ ] **Step 1: Add failing scene-level resource test**

Create a scene with a PBR sphere and environment resources, then assert the `Pass_Forward` rendering item contains descriptors named:

- `IrradianceMap`
- `PrefilteredEnvMap`
- `BrdfLut`
- `EnvironmentUBO`

- [ ] **Step 2: Store environment on `Scene`**

Add setters/getters for an active environment resource holder. `Scene::getSceneLevelResources(Pass_Forward, target)` should append environment resources for Forward only.

- [ ] **Step 3: Provide defaults**

If no environment exists, provide black/default resources or skip IBL contribution according to the shader contract. Do not crash.

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target test_scene_runtime test_frame_graph -j2
./build/src/test/test_scene_runtime
./build/src/test/test_frame_graph
```

- [ ] **Step 5: Commit**

Run:

```bash
git add src/core/scene src/demos/lxe_editor/scene_runtime.cpp src/test/integration/test_scene_runtime.cpp src/test/integration/test_frame_graph.cpp
git commit -m "feat: inject scene IBL resources"
```

## Task 14: Add glTF PBR Bridge

**Files:**
- Modify: `src/demos/lxe_editor/scene_builder.cpp`
- Modify: `src/demos/lxe_editor/scene_builder.hpp`
- Test: `src/test/integration/test_gltf_loader.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Add bridge-facing test**

Expose or test through runtime loading that DamagedHelmet can create a PBR material using:

- baseColor texture
- metallicRoughness texture
- AO texture
- emissive texture
- normal texture disabled because tangents are absent

- [ ] **Step 2: Implement PBR material helper**

Add a helper beside `makeHelmetMaterial()` that loads `assets/materials/pbr_gold.material` or a new `pbr_gltf.material`, binds glTF PBR textures by reflected names, and disables normal mapping when tangents are absent.

- [ ] **Step 3: Preserve Blinn-Phong fallback**

Do not remove existing Blinn-Phong bridge unless no caller needs it. The PBR demo path should use the PBR bridge explicitly.

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target test_gltf_loader test_scene_runtime -j2
./build/src/test/test_gltf_loader
./build/src/test/test_scene_runtime
```

- [ ] **Step 5: Commit**

Run:

```bash
git add src/demos/lxe_editor/scene_builder.* src/test/integration/test_gltf_loader.cpp src/test/integration/test_scene_runtime.cpp
git commit -m "feat: bridge glTF PBR metadata to PBR materials"
```

## Task 15: Add IBL Metal Sphere Scene

**Files:**
- Modify: `src/demos/lxe_editor/scene_document.hpp`
- Modify: `src/demos/lxe_editor/scene_document.cpp`
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Create: `assets/scenes/ibl_metal_sphere.scene.yaml`
- Test: `src/test/integration/test_scene_document.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Add failing scene document environment round-trip test**

Add YAML fixture:

```yaml
name: IBL Metal Sphere
environment:
  hdr: assets/env/studio_small_03_2k.hdr
  skybox: true
nodes:
  - path: /world/metal_sphere
    mesh:
      uri: builtin://lxe_editor/primitives/sphere
    material:
      uri: assets/materials/pbr_gold.material
```

Assert `environment.hdr` and `environment.skybox` round-trip.

- [ ] **Step 2: Implement document model**

Add `SceneEnvironmentDocument` with `std::optional<std::string> hdr` and `bool skybox = true`.

- [ ] **Step 3: Runtime loads environment**

When scene has environment HDR, load/bake environment resources and attach them to `Scene`.

- [ ] **Step 4: Create scene asset**

Create `assets/scenes/ibl_metal_sphere.scene.yaml` with:

- active camera
- directional light
- ground/reference object
- builtin sphere using PBR material
- environment HDR path

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build --target test_scene_document test_scene_runtime -j2
./build/src/test/test_scene_document
./build/src/test/test_scene_runtime
```

- [ ] **Step 6: Run visual smoke when available**

Run:

```bash
cmake --build build --target lxe_editor -j2
xvfb-run -a ./build/src/demos/lxe_editor/lxe_editor --scene assets/scenes/ibl_metal_sphere.scene.yaml
```

Expected: editor starts or exits with a documented skip/error if the current CLI does not accept `--scene`. If CLI lacks this option, document the actual launch path in this task and implement the smallest project/session entry needed.

- [ ] **Step 7: Commit**

Run:

```bash
git add src/demos/lxe_editor/scene_document.* src/demos/lxe_editor/scene_runtime.cpp assets/scenes/ibl_metal_sphere.scene.yaml src/test/integration/test_scene_document.cpp src/test/integration/test_scene_runtime.cpp
git commit -m "feat: add IBL metal sphere scene"
```

## Task 16: Write Tutorial And Update Notes

**Files:**
- Create: `notes/tutorial/pbr-ibl-metal-sphere.md`
- Modify: `notes/nav.yml`
- Modify: `notes/requirements/046-a-standard-post-process-stack.md`
- Modify: `notes/requirements/047-a-hdr-texture-cubemap-resource.md`
- Modify: `notes/requirements/048-a-ibl-gpu-bake-pipeline.md`
- Modify: `notes/requirements/049-a-pbr-ibl-material-contract.md`
- Modify: `notes/requirements/050-a-ibl-metal-sphere-test-scene.md`
- Modify: `notes/requirements/051-a-pbr-ibl-tutorial.md`
- Modify generated: `notes/requirements/index.md`, `mkdocs.gen.yml` if generated.

- [ ] **Step 1: Write tutorial**

Create `notes/tutorial/pbr-ibl-metal-sphere.md` with these sections:

- HDR environment as input
- GPU bake outputs
- Forward HDR scene color
- PostProcess tone mapping and bloom
- PBR material bindings
- Metal sphere scene YAML
- Build/run/screenshot commands
- Troubleshooting

Use current factual paths from the implemented code.

- [ ] **Step 2: Update nav**

Add tutorial entry to `notes/nav.yml` under the existing tutorial/concepts area.

- [ ] **Step 3: Update requirement statuses**

Mark implemented requirements with exact verification commands and remaining boundaries. Move finished requirements only if the project’s `finish-req` workflow is explicitly requested; otherwise keep status sections updated.

- [ ] **Step 4: Build notes**

Run:

```bash
python3 scripts/notes/generate_site_config.py
scripts/notes/serve_site.sh --build
```

Expected: notes build succeeds.

- [ ] **Step 5: Commit**

Run:

```bash
git add notes/tutorial/pbr-ibl-metal-sphere.md notes/nav.yml notes/requirements mkdocs.gen.yml
git commit -m "docs: add PBR IBL metal sphere tutorial"
```

## Task 17: Full Verification

**Files:**
- No intended source edits unless verification exposes defects.

- [ ] **Step 1: Build all required targets**

Run:

```bash
cmake --build build --target CompileShaders BuildTest lxe_editor -j2
```

Expected: build succeeds.

- [ ] **Step 2: Run headless tests**

Run:

```bash
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
```

Expected: all non-video tests pass.

- [ ] **Step 3: Run Vulkan/video tests**

Run:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

Expected: Vulkan/windowed tests pass or skip with documented environment reason.

- [ ] **Step 4: Run focused manual smoke**

Run the editor with the IBL metal sphere scene using the implemented scene launch path.

Expected:

- metal sphere visible
- background/skybox or environment preview visible
- no shader-side final tone mapping in `pbr.frag`
- post output appears tone mapped

- [ ] **Step 5: Final review**

Run:

```bash
rg -n "FullscreenProcedural|fullscreenMaterial" src assets notes/requirements
rg -n "color = color / \\(color \\+ vec3\\(1\\.0\\)\\)|pow\\(color, vec3\\(1\\.0 / 2\\.2\\)\\)" assets/shaders/glsl/pbr.frag
git status --short
```

Expected:

- no production use of old fullscreen procedural branch
- no final display mapping remains in `pbr.frag`
- worktree contains only intended changes

## Subagent Dispatch Strategy

Use subagents after the isolated worktree is ready:

1. Parallel pre-flight workers:
   - Worker A owns `045` test/status cleanup only.
   - Worker B may implement `Task 2` core FrameGraph cleanup only if Worker A does not touch frame graph files.
2. Sequential core renderer workers:
   - One worker at a time for `Task 3` through `Task 6`; these touch renderer/pipeline state and should not run in parallel.
3. Parallel texture/resource workers after `Task 3`:
   - Worker C can do `Task 7` core texture metadata.
   - Worker D can do `Task 8` HDR loader and cube reflection.
   - `Task 9` must wait for C and D.
4. Sequential IBL/PBR/scene/docs:
   - `Task 10` -> `Task 11` -> `Task 12` -> `Task 13` -> `Task 14` -> `Task 15` -> `Task 16`.
5. Every worker must:
   - edit only its assigned files
   - avoid reverting others’ changes
   - run the focused commands in its task
   - report changed files and verification output
6. After each worker:
   - run spec compliance review against the REQ section
   - run code quality review
   - only then mark the task complete.

## Self-review Checklist

- `045-a/b/c` current partial/completed state is represented.
- `046-a` deletes old fullscreen procedural branch instead of completing it.
- `047-a` uses Pasteur’s finding that `TextureCube` reflection already exists but Vulkan/resource shape is missing.
- `049-a` waits for `046/047/048` and removes shader-local final tone mapping.
- `050-a` includes scene environment round-trip because no environment block exists today.
- `051-a` is last and only documents implemented facts.
