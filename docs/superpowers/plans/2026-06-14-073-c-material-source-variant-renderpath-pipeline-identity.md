# 073-c Material Source Variant And RenderPath Pipeline Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `REQ-073-c` so material `type` owns the shader source variant, final shader reflection becomes the pipeline fact, `PipelineKey` is reduced to `MaterialTypeVariant + RenderPathNodeSignature`, and converted Helmet glTF metallic-roughness assets render non-black without old material fallback.

**Architecture:** Add `standard-pbr` as a first-class material type and require every material source shader to expose a common BSDF ABI. Parse RenderPathNode pipeline contracts explicitly, including `rendering`, geometry/topology requirements, sources/targets, and attachment formats. Compile material-source shader variants after scene resources are known, reflect only the final variant shader, and build pipeline identity from the material type variant plus RenderPathNode signature. Helmet validation uses generated `standard-pbr` material assets and must fail during preparation if the new path cannot render.

**Current facts from code:**

- `PipelineKey` currently composes `objectSig + materialSig + targetSig` in `src/core/pipeline/pipeline_key.cpp`.
- `PipelineBuildDesc` still stores `RenderTargetDesc target` and filters vertex layout from the concrete mesh buffer in `src/core/pipeline/pipeline_build_desc.cpp`.
- `RenderPassNode` has `stage`, `dispatch`, `sources`, `targets`, `renderState`, `writeMode`, and filters, but no `rendering`, geometry contract, attachment contract, or node signature in `src/core/asset/render_effect.hpp`.
- `RenderPassNodeResourceParser` rejects unsupported fields and currently requires only `shader/stage/dispatch/sources/targets/renderState`.
- `ShaderCompiler` can inject `LX_MATERIAL_CONTRACT_SOURCE`, but `assets/shaders/CMakeLists.txt` still naked-compiles all `.frag/.comp`, including variant-only PBR shaders.
- Forward/Deferred/OfflineRT shaders still consume `lxLoadMaterialSurface`; BSDF evaluate/sample ABI does not exist yet.
- glTF Helmet currently goes through `assets/materials/pbr.material` with `type: uber`; `GLTFPbrMaterial` lacks factors for base color, metallic, roughness, alpha mode, and alpha cutoff.
- Scene material loading still accepts `source: gltf` bridge in `src/infra/scene_asset/scene_material_loader.cpp`; `073-c` may leave the bridge for old scenes, but Helmet smoke must not depend on it.

---

## Work Packages

These packages are suitable for subagents after Task 0 test scaffolding lands:

| Package | Owner focus | Independent after |
|---|---|---|
| A | `standard-pbr` contract and glTF conversion data | Task 0 |
| B | BSDF shader ABI and variant-only shader build target | Task 0 |
| C | RenderPathNode contract/signature and resource vocabulary | Task 0 |
| D | PipelineKey hard cut and RenderWorkItem integration | C |
| E | Material type variant resolver and final reflection | A, B, C |
| F | Helmet conversion/smoke diagnostics | A, D, E |

Do not let subagents implement compatibility fallbacks. If a package cannot compile or render without old-path behavior, the required output is a failing diagnostic and a precise blocker, not a hidden workaround.

---

## Progress

- [x] Task 0: Add characterization tests for the 073-c contract.
- [x] Task 1: Add `standard-pbr` material contract and glTF metallic-roughness data model.
- [x] Task 2: Add shader-side BSDF ABI and update material contract sources.
- [x] Task 3: Make shader build targets variant-aware.
- [x] Task 4: Add RenderPathNode contract, built-in resource vocabulary, and node signature.
- [x] Task 5: Hard-cut `PipelineKey` to `MaterialTypeVariant + RenderPathNodeSignature`.
- [x] Task 6: Implement material type variant resolver and final shader reflection.
- [x] Task 7: Add dynamic/traditional rendering mode plumbing to pipeline build.
- [ ] Task 8: Implement Helmet material conversion tool and generated scene.
- [ ] Task 9: Add Helmet realtime smoke and diagnostics.
- [ ] Task 10: Close requirement status and notes site verification.

---

## Task 0: Add Characterization Tests

**Files:**

- Add: `src/test/integration/test_material_source_variant_pipeline.cpp`
- Modify: `src/test/CMakeLists.txt`
- Modify as needed: existing tests listed below

### Step 0.1: Add a focused 073-c test binary

- [ ] Add `test_material_source_variant_pipeline` to `TEST_INTEGRATION_EXE_LIST`.
- [ ] Add `LXE_SOURCE_DIR` compile definition for this target.
- [ ] Use the project’s simple `EXPECT` pattern from nearby integration tests.

The initial file should contain tests that fail until later tasks implement the behavior:

```cpp
void testPipelineKeyIgnoresObjectAndTargetAxes();
void testRenderPassNodeRequiresRenderingAndGeometry();
void testUnknownRenderPathResourceVocabularyFails();
void testSameTypeDifferentSourceFails();
void testStandardPbrContractReflectsRequiredFields();
void testVariantOnlyShaderNakedCompileFailsWithDiagnostic();
```

### Step 0.2: Run the failing test target

- [ ] Run:

```bash
cmake --build build --target test_material_source_variant_pipeline
./build/src/test/test_material_source_variant_pipeline
```

Expected: compile or runtime failures proving the new contract does not exist yet.

### Step 0.3: Keep existing 073-a/b coverage active

- [ ] Confirm these targets still build and run after every task:

```bash
cmake --build build --target test_material_source_contract test_scene_resource_upload_view_v2 test_bindless_indirect_contract
./build/src/test/test_material_source_contract
./build/src/test/test_scene_resource_upload_view_v2
./build/src/test/test_bindless_indirect_contract
```

---

## Task 1: Add `standard-pbr` Contract And glTF Data Model

**Files:**

- Add: `assets/shaders/glsl/common/materials/standard_pbr.contract.glsl`
- Modify: `src/infra/mesh_loader/gltf_mesh_loader.hpp/.cpp`
- Modify: `src/infra/scene_asset/gltf_scene_asset_loader.hpp/.cpp`
- Modify: `src/test/integration/test_material_source_contract.cpp`
- Modify: `src/test/integration/test_gltf_scene_asset_loader.cpp`

### Step 1.1: Add failing reflection tests

- [ ] Assert `standard_pbr.contract.glsl` reflects `declaredType == "standard-pbr"`.
- [ ] Assert storage fields cover:

```text
baseColor, baseColorTexture, metallic, metallicRoughnessTexture,
roughness, normalTexture, occlusionTexture, emissive, emissiveTexture,
alphaMode, alphaCutoff
```

- [ ] Assert source signature is stable when material values change.

### Step 1.2: Implement the contract source

- [ ] Add contract metadata for glTF metallic-roughness fields.
- [ ] Implement the current accessor entry point `lxLoadMaterialSurface` so existing shaders keep compiling during the transition.
- [ ] Add BSDF functions later in Task 2; do not fake BSDF metadata in this task.

### Step 1.3: Extend glTF metadata extraction

- [ ] Extend `GLTFPbrMaterial` with:

```cpp
LX_core::Vec4f baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
float metallicFactor = 1.0f;
float roughnessFactor = 1.0f;
std::string alphaMode = "OPAQUE";
float alphaCutoff = 0.5f;
```

- [ ] Extract those values from `cgltf_material`.
- [ ] Keep texture URIs relative to the glTF directory.

### Step 1.4: Add a standard-pbr material builder

- [ ] Add a helper that creates a `MaterialInstance` with:

```text
bsdf.type = standard-pbr
bsdf.source = assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
```

- [ ] Bind Helmet base color, metallic-roughness, normal, occlusion, and emissive textures as material envelopes.
- [ ] Do not use `assets/materials/pbr.material` for this new path.

### Step 1.5: Run focused tests

- [ ] Run:

```bash
cmake --build build --target test_material_source_contract test_gltf_scene_asset_loader
./build/src/test/test_material_source_contract
./build/src/test/test_gltf_scene_asset_loader
```

---

## Task 2: Add Shader-side BSDF ABI

**Files:**

- Add: `assets/shaders/glsl/common/material_bsdf.glsl`
- Modify: `assets/shaders/glsl/common/materials/*.contract.glsl`
- Modify: `assets/shaders/glsl/techniques/Forward/pbr.frag`
- Modify: `assets/shaders/glsl/techniques/Deferred/pbr_gbuffer.frag`
- Modify: `assets/shaders/glsl/techniques/OfflineRT/offline_pbr_direct_ray.comp`
- Modify: `src/infra/material_loader/material_contract_reflector.hpp/.cpp`
- Modify: `src/test/integration/test_material_source_contract.cpp`

### Step 2.1: Define the ABI header

- [ ] Add structs with stable field names:

```glsl
struct LxBsdfEvaluateInput { vec3 normal; vec3 wi; vec3 wo; vec3 baseColor; float metallic; float roughness; float ao; vec3 emissive; };
struct LxBsdfEvaluateOutput { vec3 value; };
struct LxBsdfSampleInput { vec3 normal; vec3 wo; vec2 xi; };
struct LxBsdfSampleOutput { vec3 wi; vec3 value; float pdf; };
```

- [ ] Required function names:

```glsl
LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput input);
LxBsdfSampleOutput lxSampleBsdf(LxBsdfSampleInput input);
```

- [ ] `lxEvaluateBsdf` receives both directions and returns only the BSDF value
      for that fixed pair.
- [ ] `lxSampleBsdf` receives `wo` plus random input and returns the sampled
      `wi`, the BSDF value for that sampled pair, and the sampling
      PDF.
- [ ] Do not add a required standalone `lxPdfBsdf` entry in this requirement.

### Step 2.2: Reflect required BSDF capability metadata

- [ ] Extend contract comment metadata with capability lines:

```text
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
```

- [ ] Store capability booleans/names in `MaterialContractReflection`.
- [ ] Add negative tests for missing evaluate/sample when a pass requires them.

### Step 2.3: Update supported material sources

- [ ] Implement evaluate/sample for `standard-pbr`, `uber`, `matte`, `metal`, and `substrate`.
- [ ] Unsupported PBRT contracts may keep `status: unsupported`, but diagnostics must say why they cannot satisfy a pass.

### Step 2.4: Update pass shaders to call BSDF functions

- [ ] Forward shader builds its direct-light result through `lxEvaluateBsdf` or a wrapper over it.
- [ ] Deferred GBuffer may still write `LxMaterialSurface`, but it must not be the only route for Forward/OfflineRT light transport.
- [ ] OfflineRT direct shader uses `lxSampleBsdf` and `lxEvaluateBsdf` instead of hardcoded PBR logic.
- [ ] Keep `#include LX_MATERIAL_CONTRACT_SOURCE` fail-fast.

### Step 2.5: Audit no type/source branching

- [ ] Run:

```bash
rg -n "materialType|materialSource|MATERIAL_TYPE_|type ==|switch .*source|MaterialUBO|debug material|fallback" assets/shaders/glsl/techniques src/core src/backend src/infra
```

- [ ] Any match on a positive 073-c path must be removed or marked as a later `073-f` legacy boundary.

---

## Task 3: Make Shader Build Targets Variant-aware

**Files:**

- Modify: `assets/shaders/CMakeLists.txt`
- Add: `scripts/shaders/compile_material_source_variants.py`
- Modify: `src/test/integration/test_shader_compiler.cpp`
- Modify: `src/test/CMakeLists.txt`

### Step 3.1: Stop naked-compiling variant-only base shaders

- [ ] Remove these files from the naked `CompileShaders` `.frag/.comp` list:

```text
assets/shaders/glsl/techniques/Forward/pbr.frag
assets/shaders/glsl/techniques/Deferred/pbr_gbuffer.frag
assets/shaders/glsl/techniques/OfflineRT/offline_pbr_direct_ray.comp
```

- [ ] Keep them copied by `SyncShaderRuntimeSources`.

### Step 3.2: Add variant compile target

- [ ] Add `CompileMaterialSourceShaderVariants`.
- [ ] The script compiles at least:

```text
Forward/pbr.frag + standard_pbr.contract.glsl
Deferred/pbr_gbuffer.frag + standard_pbr.contract.glsl
OfflineRT/offline_pbr_direct_ray.comp + standard_pbr.contract.glsl
```

- [ ] The script must emit diagnostics containing base shader path, source URI, and macro name if compilation fails.

### Step 3.3: Update tests

- [ ] `test_shader_compiler` must assert naked compile fails for a variant-only shader with the expected diagnostic.
- [ ] It must also assert variant compile succeeds with `standard-pbr`.
- [ ] Run:

```bash
cmake --build build --target CompileShaders CompileMaterialSourceShaderVariants test_shader_compiler
./build/src/test/test_shader_compiler
```

---

## Task 4: RenderPathNode Contract, Vocabulary, And Signature

**Files:**

- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/infra/resource_parsers/render_pass_node_parser.cpp`
- Modify: `src/infra/resource_parsers/render_path_graph_resource_parser.cpp`
- Add: `src/core/asset/render_path_node_signature.hpp/.cpp` or equivalent local module
- Modify: `assets/render_paths/*.render-path.yaml`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`
- Modify: `src/test/integration/test_material_source_variant_pipeline.cpp`

### Step 4.1: Add contract data model

- [ ] Add enums/structs for:

```cpp
enum class RenderPathNodeRenderingMode { Dynamic, Traditional };
enum class RenderPathGeometryVertexContract { PositionOnly, PositionNormalUvTangent };
struct RenderPathGeometryContract { RenderPathGeometryVertexContract vertex; PrimitiveTopology topology; };
struct RenderPathAttachmentContract { std::string target; ImageFormat format; u32 samples; u32 layers; bool depth; };
```

- [ ] Add `StringID getRenderPathNodeSignature(const RenderPassNode&)`.

### Step 4.2: Add resource vocabulary

- [ ] Centralize allowlisted names:

```text
geometry.vertex, geometry.index, material.bsdf, scene.camera, scene.lights,
shadow.main, hdr.color, depth.main, swapchain.color, debug.overlay,
gbuffer.albedoAlpha, gbuffer.normalRoughness, gbuffer.material,
feature.toneMapping
```

- [ ] Unknown source/target names fail parser validation.

### Step 4.3: Parse explicit `rendering`, `geometry`, and `attachments`

- [ ] Every raster pass must declare `rendering`.
- [ ] Draw passes must declare `geometry`.
- [ ] `dynamic` raster passes must declare attachment format/sample/layer through target contracts when they output attachments.
- [ ] `traditional` raster passes must declare the same attachment contract and mark it traditional.
- [ ] Compute passes can use `rendering: none` only if the model explicitly supports it; otherwise require a separate compute contract.

### Step 4.4: Update default render path YAML

- [ ] Add explicit fields to all default passes.
- [ ] Add `standard-pbr` to Forward/Deferred `filters.bsdf`.
- [ ] Keep shader URI spelling unchanged for this REQ; `073-d` owns URI migration.

### Step 4.5: Run parser tests

- [ ] Run:

```bash
cmake --build build --target test_render_resource_parsers test_material_source_variant_pipeline
./build/src/test/test_render_resource_parsers
./build/src/test/test_material_source_variant_pipeline
```

---

## Task 5: Hard-cut PipelineKey Algorithm

**Files:**

- Modify: `src/core/pipeline/pipeline_key.hpp/.cpp`
- Modify: `src/core/pipeline/pipeline_build_desc.hpp/.cpp`
- Modify: `src/core/frame_graph/render_queue.hpp/.cpp`
- Modify: `src/core/scene/object.cpp/.hpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_pipeline_identity.cpp`
- Modify: `src/test/integration/test_pipeline_build_info.cpp`
- Modify: `src/test/integration/test_material_source_variant_pipeline.cpp`

### Step 5.1: Replace PipelineKey inputs

- [ ] Replace public builders with:

```cpp
static PipelineKey build(StringID materialTypeVariant,
                         StringID renderPathNodeSignature);
```

- [ ] Remove object and target builders from positive paths.
- [ ] Keep deleted-builder fallout explicit; do not leave overloads forwarding to old semantics.

### Step 5.2: Add render path node signature to work items

- [ ] `ValidatedRenderablePassData` and `RenderWorkItem` must carry `renderPathNodeSignature`.
- [ ] `RenderWorkQueue::build` must receive/pass the RenderPathNode contract, not only a bare pass id.
- [ ] Post-process/fullscreen manual items in `vulkan_realtime_renderer.cpp` must use the same node signature construction.

### Step 5.3: Material type variant identity

- [ ] Add a `MaterialTypeVariant` string ID composed from:

```text
material type
material source URI
source reflection hash
source signature
resolved base shader identity
```

- [ ] Do not include material URI, texture handles, texture presence, or parameter values.

### Step 5.4: Geometry is validation, not key identity

- [ ] Validate object mesh topology/vertex contract against RenderPathNode geometry.
- [ ] Do not include full `VertexLayout` or object mesh signature in the key.
- [ ] `PipelineBuildDesc` may still carry filtered `VertexLayout` for Vulkan pipeline creation, but this must be checked to be compatible with the node geometry contract.

### Step 5.5: Run pipeline tests

- [ ] Run:

```bash
cmake --build build --target test_pipeline_identity test_pipeline_build_info test_material_source_variant_pipeline
./build/src/test/test_pipeline_identity
./build/src/test/test_pipeline_build_info
./build/src/test/test_material_source_variant_pipeline
```

---

## Task 6: Material Type Variant Resolver And Final Reflection

**Files:**

- Add: `src/core/asset/material_source_variant.hpp/.cpp` for core identity types, if needed.
- Add: `src/infra/resource_parsers/material_source_variant_resolver.hpp/.cpp`
- Modify: `src/infra/resource_parsers/render_resource_scene_parser_adapters.cpp`
- Modify: `src/core/scene/scene_resource_table.hpp/.cpp`
- Modify: `src/core/asset/shader.hpp`
- Modify: `src/core/scene/object.cpp`
- Modify: tests from Tasks 0, 2, and 4

### Step 6.1: Build type/source uniqueness validation

- [ ] Traverse all scene materials after resource registration.
- [ ] For each `bsdf.type`, require a single source URI, reflection hash, and source signature.
- [ ] Fail if one type maps to multiple sources.
- [ ] Fail if `.material` type differs from reflected source type.
- [ ] Diagnostic includes material URI/handle, type, source URI, reflection hash, and signature.

### Step 6.2: Resolve required variants per RenderPathNode

- [ ] For each RenderPathNode requiring `material.bsdf`, collect material types allowed by `filters.bsdf` and present in scene.
- [ ] Compile final shader variants with `LX_MATERIAL_CONTRACT_SOURCE`.
- [ ] Reflect final variants and store `ShaderStageCode`, bindings, vertex inputs, and variant identity.
- [ ] Do not compile base shader as the final fact for variant-only passes.

### Step 6.3: Attach final variant shader to renderable pass data

- [ ] Replace ad hoc `shaderProgramWithMaterialSourceVariant` in `SceneNode` with resolver-produced final shader identity.
- [ ] The renderable path consumes final shader reflection only.
- [ ] Missing resolver entry is a preparation error, not a fallback to `material->getPassShader`.

### Step 6.4: Diagnostics

- [ ] Add a structured diagnostic dump path with:

```text
material type
source URI
source signature
base shader identity
RenderPathNode id/signature
compile key
reflection key
PipelineKey
bindings summary
```

### Step 6.5: Run variant tests

- [ ] Run:

```bash
cmake --build build --target test_material_source_variant_pipeline test_render_resource_parsers
./build/src/test/test_material_source_variant_pipeline
./build/src/test/test_render_resource_parsers
```

---

## Task 7: Dynamic/Traditional Rendering Mode Plumbing

**Files:**

- Modify: `src/core/pipeline/pipeline_build_desc.hpp/.cpp`
- Modify: `src/backend/vulkan/details/pipelines/graphics_pipeline.hpp/.cpp`
- Modify: `src/backend/vulkan/details/pipelines/pipeline_cache.hpp/.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_vulkan_pipeline.cpp`
- Modify: `src/test/integration/test_material_source_variant_pipeline.cpp`

### Step 7.1: Carry rendering mode and attachment contract

- [x] `PipelineBuildDesc` must carry:

```cpp
RenderPathNodeRenderingMode renderingMode;
std::vector<RenderPathAttachmentContract> attachments;
```

- [x] Attachment formats come from RenderPathNode target contract for both dynamic and traditional Vulkan graphics pipeline creation.

### Step 7.2: Implement dynamic rendering pipeline creation

- [x] For `dynamic`, set `VkGraphicsPipelineCreateInfo::renderPass = VK_NULL_HANDLE`.
- [x] Add `VkPipelineRenderingCreateInfo` to `pNext` with color/depth/stencil formats.
- [x] Command recording for dynamic passes must use `vkCmdBeginRendering` / `vkCmdEndRendering`.
- [x] If the Vulkan device lacks Vulkan 1.3 / `VK_KHR_dynamic_rendering`, fail with a backend unsupported rendering mode diagnostic.

### Step 7.3: Preserve traditional as explicit mode

- [x] For `traditional`, keep existing render pass/framebuffer path.
- [x] Do not auto-switch between modes.
- [x] PipelineKey still uses the RenderPathNodeSignature; no separate RenderTarget key axis returns.

### Step 7.4: Run Vulkan tests

- [x] Run headless tests:

```bash
cmake --build build --target test_vulkan_pipeline test_material_source_variant_pipeline
./build/src/test/test_material_source_variant_pipeline
```

- [x] If available, run video-device tests:

```bash
xvfb-run -a ./build/src/test/test_vulkan_pipeline
```

---

## Task 8: Helmet Conversion Tool And Generated Scene

**Files:**

- Add: `src/tools/lxe_gltf_material_convert/lxe_gltf_material_convert.py`
- Modify: top-level or tool CMake/install wiring only if needed
- Add generated fixture under `assets/scenes/generated/helmet_standard_pbr.scene.yaml` or `data/scenes/helmet-standard-pbr/`
- Add: `src/test/integration/test_lxe_gltf_material_convert.py`
- Modify: `src/test/CMakeLists.txt`

### Step 8.1: Build converter

- [ ] The converter reads glTF JSON with Python stdlib.
- [ ] The converter is a required `073-c` deliverable; do not move it to later smoke/hard-cut requirements.
- [ ] It emits:

```text
<out>/materials/damaged_helmet_standard_pbr.material
<out>/helmet_standard_pbr.scene.yaml
```

- [ ] The material file must contain:

```yaml
schema: lxe.material.v2
bsdf:
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
```

- [ ] Parameters must preserve base color factor, metallic factor, roughness factor, base color texture, metallic-roughness texture, normal texture, occlusion texture, emissive factor/texture, alpha mode, and alpha cutoff.

### Step 8.2: Generated scene rules

- [ ] The generated scene can reference `assets/models/damaged_helmet/DamagedHelmet.gltf` for mesh.
- [ ] It must reference the generated `standard-pbr` material.
- [ ] It must not use `source: gltf`.
- [ ] It must not reference `assets/materials/pbr.material`.

### Step 8.3: Add converter test

- [ ] Assert output files exist.
- [ ] Assert material and scene contain the clean path fields.
- [ ] Assert no old path strings are present.

### Step 8.4: Run converter test

- [ ] Run:

```bash
python3 src/test/integration/test_lxe_gltf_material_convert.py --source-dir .
```

---

## Task 9: Helmet Realtime Smoke And Diagnostics

**Files:**

- Modify: `src/tools/lxe_realtime_render/lxe_realtime_render.py`
- Add: `src/test/integration/test_helmet_standard_pbr_realtime_smoke.py`
- Modify: `src/test/CMakeLists.txt`
- Modify diagnostics in resolver/render path/backend code from earlier tasks

### Step 9.1: Add image statistics check

- [ ] Extend `lxe_realtime_render.py` or add a helper to read the output PNG/EXR metadata and compute a simple non-black statistic.
- [ ] Require non-zero lit pixel count and average luminance above a small threshold.
- [ ] Keep the threshold low and deterministic for 192x192 Helmet.

### Step 9.2: Add smoke test

- [ ] Build editor and required shader targets:

```bash
cmake --build build --target lxe_editor CompileMaterialSourceShaderVariants
```

- [ ] Run:

```bash
xvfb-run -a python3 src/tools/lxe_realtime_render/lxe_realtime_render.py \
  --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml \
  --profile preview \
  --xvfb
```

### Step 9.3: Prove no old path was used

- [ ] Smoke metadata or logs must include:

```text
type=standard-pbr
standard_pbr.contract.glsl
RenderPathNodeSignature
PipelineKey
final shader reflection
```

- [ ] Smoke metadata/logs must not include:

```text
assets/materials/pbr.material
source: gltf
legacy material fallback
debug material fallback
empty source
MaterialUBO as positive path
```

### Step 9.4: If smoke cannot render, stop

- [ ] Do not relax tests.
- [ ] Record the exact failing preparation diagnostic in `REQ-073-c` implementation status.
- [ ] Fix the missing contract if it is in scope; otherwise split it to a named later REQ only if it is genuinely outside 073-c.

---

## Task 10: Closeout

**Files:**

- Modify: `notes/requirements/073-c-material-source-shader-variant-boundary.md`
- Modify: `docs/superpowers/plans/2026-06-14-073-c-material-source-variant-renderpath-pipeline-identity.md`
- Generated by build: `notes/superpowers/` ignored, `mkdocs.gen.yml` ignored

### Step 10.1: Run required verification

- [ ] Run focused CPU tests:

```bash
cmake --build build --target \
  test_material_source_contract \
  test_gltf_scene_asset_loader \
  test_material_source_variant_pipeline \
  test_render_resource_parsers \
  test_pipeline_identity \
  test_pipeline_build_info \
  test_shader_compiler

./build/src/test/test_material_source_contract
./build/src/test/test_gltf_scene_asset_loader
./build/src/test/test_material_source_variant_pipeline
./build/src/test/test_render_resource_parsers
./build/src/test/test_pipeline_identity
./build/src/test/test_pipeline_build_info
./build/src/test/test_shader_compiler
```

- [ ] Run shader targets:

```bash
cmake --build build --target CompileShaders CompileMaterialSourceShaderVariants
```

- [ ] Run notes build:

```bash
scripts/notes/serve_site.sh --build
```

- [ ] Run Helmet smoke when Vulkan/Xvfb is available:

```bash
xvfb-run -a python3 src/tools/lxe_realtime_render/lxe_realtime_render.py \
  --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml \
  --profile preview \
  --xvfb
```

### Step 10.2: Update requirement status

- [ ] Mark completed items in `REQ-073-c`.
- [ ] List exact verification commands and results.
- [ ] If any lower-confidence item remains, it must have a named owning follow-up REQ and a reason tied to dependency order.

### Step 10.3: Commit

- [ ] Commit implementation and docs:

```bash
git status --short
git add assets src notes docs scripts
git commit -m "Implement material source shader variant pipeline identity"
```
