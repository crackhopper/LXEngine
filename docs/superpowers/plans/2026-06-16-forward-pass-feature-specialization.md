# Forward Pass Feature Specialization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move finite room backgrounds out of renderer runtime into generated scene assets, keep the default renderer as a data-driven Forward pass for skybox/surface lighting/tone mapping/gamma, and split bloom into an explicit graph blit pass.

**Architecture:** Finite room/box backgrounds are ordinary scene geometry generated offline by Python, with pre-tonemapped sRGB textures and unlit materials. Runtime Forward no longer creates finite boxes and no longer has EnvironmentBox/SkyboxBackground/PostProcess render-path variants; it uses `feature.forwardPass` only for Forward flow switches. Infinite skybox display is a Forward flow helper controlled by `render_skybox` and parameterized by `feature.skybox`; IBL/indirect lighting is parameterized only by `feature.environmentLighting`; bloom is a separate graph blit pass parameterized by `feature.bloom`.

**Tech Stack:** Python asset tool, C++20, Vulkan `VkSpecializationInfo`, RenderPathGraph YAML, RenderFeature YAML, GLSL common helpers, CTest/Ninja.

---

## Corrected Design Boundaries

- First deliverable is a Python finite-room conversion tool.
- Finite box is not a skybox mode and not a renderer runtime branch.
- Finite box becomes ordinary generated scene content:
  - mesh
  - six-face or atlas texture mapping
  - sRGB LDR texture generated from HDR/cubemap input using tone mapping plus fixed sRGB encode
  - unlit material
  - scene snippet or complete scene asset entry
- Forward pass must not special-case finite box renderables.
- Runtime C++ must not auto-create finite environment boxes.
- `feature.skybox` means directly visible infinite skybox only.
- `feature.environmentLighting` means IBL / indirect environment lighting only.
- `feature.forwardPass` means Forward flow switches only:
  - `render_skybox`
  - `enable_tonemapping`
  - `enable_gamma`
- `feature.toneMapping` owns tone mapping calculation parameters only: exposure/mode. It does not own enable/disable flow, gamma, or output encoding.
- `feature.bloom` owns bloom pass parameters. Bloom is a graph blit pass after Forward, not a Forward shader helper in this slice.
- C++ does not infer or rewrite feature values. Values come from parsed YAML. C++ only validates invalid combinations in a dedicated validation unit.
- This slice implements the Forward pass cleanup plus a named Bloom blit pass split. Deferred/GBuffer pass features are intentionally deferred.
- Deletions in this plan are hard cuts. Do not keep production compatibility branches, dormant helpers, fallback shader entry points, or runtime finite-box legacy code. Only explicitly named negative audits may mention removed tokens.

---

## File Map

- Create: `scripts/assets/generate_finite_skybox_room.py`
  - Converts a KTX2 cubemap environment plus bounds into an ordinary OBJ room model with UVs plus generated engine material/texture assets.
- Create: generated fixture assets under `assets/scenes/generated/` or `assets/generated/finite_rooms/`
  - Commit the generated finite room assets used by the helmet test scene.
- Create or reuse: unlit material asset/shader support
  - Add `assets/shaders/glsl/common/materials/unlit_texture.contract.glsl` if no unlit texture material exists.
  - Add generated `.material` using `schema: lxe.material.v2`; do not rely on Wavefront `.mtl`.
- Create: `assets/effects/forward_pass.render-feature.yaml`
  - Forward flow switches only.
- Create: `assets/effects/skybox.render-feature.yaml`
  - Infinite visible skybox only.
- Modify: `assets/effects/environment_lighting.render-feature.yaml`
  - Hard-cut finite/background display ownership from runtime flow.
- Modify: `assets/effects/tone_mapping.render-feature.yaml`
  - Keep tone mapping calculation parameters only; remove gamma/output encoding ownership.
- Create: `assets/effects/bloom.render-feature.yaml`
  - Bloom parameters only.
- Modify: `src/core/asset/render_effect.hpp`
  - Add RenderFeature shader ABI ownership metadata.
- Modify: `src/infra/resource_parsers/render_feature_resource_parser.cpp`
  - Parse feature level and shader URI metadata instead of rejecting it.
- Create: `src/core/frame_graph/render_feature_shader_validation.hpp`
- Create: `src/core/frame_graph/render_feature_shader_validation.cpp`
  - Validate shader-level feature `binding/member` declarations and pass-level feature specialization parameters against shader reflection.
- Modify if needed: shader reflection support under `src/infra/shader_compiler/`
  - Existing reflection already exposes resource bindings and UBO members.
  - Add real specialization constant reflection if it is not already available.
  - Do not emulate specialization reflection with GLSL string scanning.
- Modify: `assets/render_paths/forward_main.render-path.yaml`
- Delete: `assets/render_paths/forward_bloom.render-path.yaml`
  - Bloom is no longer a separate render-path variant selected instead of Forward.
  - Keep one default render path graph with a Forward pass followed by a Bloom blit pass.
  - Wire `feature.forwardPass`, `feature.skybox`, `feature.environmentLighting`, `feature.toneMapping`, and `feature.bloom`.
- Create: `src/core/frame_graph/render_path_feature_validation.hpp`
- Create: `src/core/frame_graph/render_path_feature_validation.cpp`
  - Central schema-combination validation.
- Modify: `src/core/pipeline/pipeline_build_desc.hpp/.cpp`
- Modify: `src/core/pipeline/pipeline_key.hpp/.cpp`
  - Pipeline specialization facts and key identity.
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
  - Generic pass-level feature specialization constants resolved from parsed feature metadata and shader reflection.
- Modify: `src/core/scene/scene_resource_table.hpp/.cpp`
  - Register/expose parsed render-feature facts without hardcoded Forward field structs.
- Modify: `src/backend/vulkan/details/pipelines/graphics_pipeline.hpp/.cpp`
  - Vulkan specialization info.
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
  - Remove runtime finite environment box injection.
- Modify scene/runtime load path that reports render graph validation failures
  - Fatal validation errors must stop scene loading and clear/rollback the in-progress scene.
- Modify GLSL:
  - `assets/shaders/glsl/common/gamma_adjust.glsl`
  - `assets/shaders/glsl/common/material_surface.glsl`
  - `assets/shaders/glsl/features/tone_mapping.glsl`
  - `assets/shaders/glsl/features/skybox.glsl`
  - `assets/shaders/glsl/features/environment_lighting.glsl`
  - `assets/shaders/glsl/features/bloom.glsl`
  - `assets/shaders/glsl/render_paths/Bloom/blit.frag`
  - `assets/shaders/glsl/common/materials/unlit_texture.contract.glsl`
  - existing supported material contracts under `assets/shaders/glsl/common/materials/*.contract.glsl`
  - `assets/shaders/glsl/render_paths/Forward/pbr.frag`
  - delete `assets/shaders/glsl/render_paths/Environment/environment_box.frag` as a production shader entry point
- Modify tests:
  - `src/test/integration/test_render_resource_parsers.cpp`
  - `src/test/integration/test_render_work_compiler.cpp`
  - `src/test/integration/test_shader_compiler.cpp`
  - add Python tool test if repo has an appropriate Python test harness; otherwise add a CTest/script smoke.

---

### Task 1: Python Finite Room Conversion Tool

**Files:**
- Create: `scripts/assets/generate_finite_skybox_room.py`
- Create: test fixture output under `assets/scenes/generated/finite_room/`
- Modify: `assets/scenes/generated/helmet_standard_pbr.scene.yaml`
- Test: add script smoke in CMake/CTest or existing Python integration location

- [x] **Step 1: Write RED tool smoke**

Add a test command that runs:

```bash
python3 scripts/assets/generate_finite_skybox_room.py \
  --input assets/env/khronos/neutral/ggx/specular.ktx2 \
  --bounds -12 12 -8 10 -12 12 \
  --output-dir build/test_generated/finite_room \
  --name test_neutral_room \
  --tone-map aces \
  --exposure 1.0
```

Expected generated files:

```text
build/test_generated/finite_room/test_neutral_room.obj
build/test_generated/finite_room/test_neutral_room_unlit.material
build/test_generated/finite_room/test_neutral_room.scene-snippet.yaml
build/test_generated/finite_room/textures/test_neutral_room_srgb.png
```

The RED test fails because the script does not exist yet.

- [x] **Step 2: Implement CLI and validation**

`generate_finite_skybox_room.py` must accept:

```text
--input <path>
--bounds xmin xmax ymin ymax zmin zmax
--output-dir <path>
--name <asset-name>
--tone-map aces|reinhard
--exposure <float>
```

Validation:

```text
xmin < xmax
ymin < ymax
zmin < zmax
input exists
KTX2 cubemap input can be decoded
```

Invalid input exits nonzero with a clear message.

- [x] **Step 3: Generate ordinary scene assets**

The script writes:

- `test_neutral_room.obj`
  - inward-facing cube triangles generated from bounds
  - explicit `v`, `vt`, and `f` records
  - UVs mapped into the generated texture atlas
  - does not rely on Wavefront `.mtl` for engine material binding
- `test_neutral_room_unlit.material`
  - engine material file using `schema: lxe.material.v2`
  - `bsdf.type: unlit-texture`
  - `bsdf.source: assets://shaders/glsl/common/materials/unlit_texture.contract.glsl`
  - texture parameter references `textures/test_neutral_room_srgb.png`
- `textures/test_neutral_room_srgb.png`
  - one generated sRGB texture atlas containing the six cubemap faces after tone mapping plus fixed sRGB encode
- `test_neutral_room.scene-snippet.yaml`
  - one ordinary scene node/renderable that imports the OBJ model and binds `test_neutral_room_unlit.material`

Important: these assets are ordinary model/scene content. They must not depend on renderer runtime finite-box code, custom mesh YAML, or Wavefront MTL material binding.

Current repo fact: `src/infra/mesh_loader/obj_mesh_loader.cpp` uses tinyobj to read geometry/UVs, but the engine material path is `.material` loaded through `MaterialResourceParser`. `.mtl` is the Wavefront material-library sidecar format; do not make it the source of truth for LXEngine materials in this plan.

- [x] **Step 4: Generate sRGB LDR textures**

Convert environment input into one sRGB PNG atlas used by the OBJ UVs. The tool must support the repository neutral KTX2 environment:

```text
assets/env/khronos/neutral/ggx/specular.ktx2
```

Do not silently generate placeholder textures and do not substitute another environment. If Python image libraries cannot decode KTX2 directly, add a small helper around the existing engine/infra texture loader or a repo-local conversion path. The required outcome is still that the command succeeds for this KTX2 asset.

Supported first-slice inputs:

```text
1. KTX2 cubemap input, including assets/env/khronos/neutral/ggx/specular.ktx2
2. optional: a six-face directory containing px/nx/py/ny/pz/nz HDR or PNG faces
3. optional: a single equirectangular HDR/EXR file if the selected Python image stack supports it
```

The output texture must be an ordinary sRGB PNG atlas.

Tone mapping:

```text
ACES or Reinhard
then fixed sRGB encode
```

This is offline asset generation, not runtime tone mapping.

- [x] **Step 5: Run tool smoke GREEN**

```bash
python3 scripts/assets/generate_finite_skybox_room.py --input assets/env/khronos/neutral/ggx/specular.ktx2 --bounds -12 12 -8 10 -12 12 --output-dir build/test_generated/finite_room --name test_neutral_room --tone-map aces --exposure 1.0
```

Expected: all files exist and nonempty.
Also assert:

```text
OBJ contains vt records
OBJ does not need mtllib for LXEngine material binding
test_neutral_room_unlit.material uses schema lxe.material.v2
test_neutral_room_unlit.material references textures/test_neutral_room_srgb.png
scene snippet references test_neutral_room.obj and test_neutral_room_unlit.material
```

- [x] **Step 6: Generate committed finite room assets**

Run the same tool for the committed helmet-scene assets:

```bash
python3 scripts/assets/generate_finite_skybox_room.py --input assets/env/khronos/neutral/ggx/specular.ktx2 --bounds -12 12 -8 10 -12 12 --output-dir assets/scenes/generated/finite_room --name test_neutral_room --tone-map aces --exposure 1.0
```

Expected committed files:

```text
assets/scenes/generated/finite_room/test_neutral_room.obj
assets/scenes/generated/finite_room/test_neutral_room_unlit.material
assets/scenes/generated/finite_room/test_neutral_room.scene-snippet.yaml
assets/scenes/generated/finite_room/textures/test_neutral_room_srgb.png
```

These generated assets are part of this change and should be staged/committed with the code.

- [x] **Step 7: Add generated room to helmet test scene**

Append the generated finite room as an ordinary child node in `assets/scenes/generated/helmet_standard_pbr.scene.yaml`:

```yaml
    - nodeName: finite_neutral_room
      name: finite_neutral_room
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      mesh:
        uri: assets/scenes/generated/finite_room/test_neutral_room.obj
      material:
        uri: assets/scenes/generated/finite_room/test_neutral_room_unlit.material
```

The room is just another renderable in the helmet scene. It must not be injected by C++ runtime code.

- [x] **Step 8: Add quick helmet room smoke**

Add a fast smoke that loads `assets/scenes/generated/helmet_standard_pbr.scene.yaml` and verifies the finite room OBJ/material resources resolve.

If the implementation needs temporary debug-only code to inspect the room render, keep it local to the smoke/test path and hard-cut it before final completion. No temporary smoke helpers, runtime branches, or debug render paths may remain in production code at Task 9.

---

### Task 2: Remove Runtime Finite Box Injection

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`

- [x] **Step 1: Add negative audit**

Add an audit/test proving production runtime no longer contains:

```text
ensureEnvironmentBoxRenderable
makeEnvironmentBoxMesh
EnvironmentLightingFiniteBoxUBO
finiteBoxBounds as runtime background injection
render_paths/Environment/environment_box as runtime material shader
```

The audit should allow docs/history/tests only when they explicitly describe removed legacy behavior.
Production code and default runtime/shader assets must have zero hits for these tokens after this task.

- [x] **Step 2: Hard-cut runtime finite box creation**

Remove from `vulkan_realtime_renderer.cpp`:

- `makeEnvironmentBoxMesh`
- `createEnvironmentBoxMaterial`
- `ensureEnvironmentBoxRenderable`
- calls that auto-add `__environment_finite_box`

Do not replace it with another runtime branch.

- [x] **Step 3: Hard-cut finite box scene resource path**

Delete runtime-only finite box UBO/resource registration and every production caller. Do not leave unused compatibility helpers behind.

- [x] **Step 4: Run tests**

```bash
cmake --build build --target test_render_resource_parsers test_render_work_compiler
./build/src/test/test_render_resource_parsers
./build/src/test/test_render_work_compiler
```

Expected: PASS.

---

### Task 3: Feature Schema Cleanup

**Files:**
- Create: `assets/effects/forward_pass.render-feature.yaml`
- Create: `assets/effects/skybox.render-feature.yaml`
- Create: `assets/effects/bloom.render-feature.yaml`
- Modify: `assets/effects/environment_lighting.render-feature.yaml`
- Modify: `assets/effects/tone_mapping.render-feature.yaml`
- Modify: `src/core/asset/render_effect.hpp`
- Modify: `src/infra/resource_parsers/render_feature_resource_parser.cpp`
- Create: `src/core/frame_graph/render_feature_shader_validation.hpp`
- Create: `src/core/frame_graph/render_feature_shader_validation.cpp`
- Modify: `assets/render_paths/forward_main.render-path.yaml`
- Delete: `assets/render_paths/forward_bloom.render-path.yaml`
- Modify: `src/test/integration/test_render_resource_parsers.cpp`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [x] **Step 1: Write RED parser assertions**

Default Forward graph must reference:

```text
feature.forwardPass
feature.skybox
feature.environmentLighting
feature.toneMapping
feature.bloom
```

`forwardPass` must contain only flow switches:

```text
render_skybox
enable_tonemapping
enable_gamma
```

`skybox` must not contain finite box bounds. This is a hard cut: skybox is infinite direct display only.

`environmentLighting` must not contain visible-background fields such as `backgroundMode` or `finiteBoxBounds`. This is a hard cut in production feature assets; do not keep compatibility aliases.

`toneMapping` must contain tone mapping calculation parameters only:

```text
exposure
mode
```

`enable_gamma` is owned by `feature.forwardPass`, not `feature.toneMapping`.
Tone mapping and gamma/output encoding are separate concepts:

```text
toneMapping -> exposure / tone curve calculation
forwardPass.enable_tonemapping -> whether Forward calls tone mapping
forwardPass.enable_gamma -> final manual sRGB/gamma encode for non-sRGB targets
```

Do not expose an arbitrary numeric gamma parameter.

Every created feature must also declare its level and the shader that owns its runtime ABI. Shader-level features look like:

```yaml
level: shader
shader:
  uri: features/bloom
```

Shader-level feature shaders are not pure data. They own the feature ABI and helper code:

```text
feature shader -> declares feature UBO/resources and helper functions
owner render-path shader -> includes/calls feature helpers according to graph/pass flow
```

Reflection validation uses the feature's own shader URI. Forward shader reflection is only used for `level: pass` specialization constants and for normal Forward pipeline compilation.

Feature levels:

```text
level: pass    -> parameters control pass flow through specialization constants
level: shader  -> parameters bind shader resources / UBO members
```

Do not infer pass-level behavior from the feature name. `forwardPass` is pass-level because the YAML says `level: pass`.

- [x] **Step 2: Add render-feature shader URI model**

Extend `RenderFeature` with explicit feature level and shader ABI metadata, for example:

```cpp
enum class RenderFeatureLevel {
  Unknown,
  Shader,
  Pass,
};

struct RenderFeatureShaderContract {
  ResourceUri uri;
};

struct RenderFeature final {
  std::string name;
  std::string feature;
  RenderFeatureLevel level = RenderFeatureLevel::Unknown;
  std::optional<RenderFeatureShaderContract> shader;
  std::unordered_map<std::string, RenderFeatureParameter> parameters;
};

using RenderFeatureValue =
    std::variant<bool, int, u32, float, std::string, Vec3, ResourceUri>;

struct RenderFeatureParameter final {
  std::string kind;
  std::optional<RenderFeatureValue> value;
  ResourceUri uri;
  std::string valueType;
  std::string binding;
  std::string member;
  bool required = false;
  std::vector<std::string> allowedValues;
  std::string requiredWhenParameter;
  std::string requiredWhenEquals;
};
```

Parser behavior:

- accept top-level `level`
- allowed levels are exactly `shader` and `pass`
- missing `level` is a parse error; do not default it in C++
- accept top-level `shader.uri`
- reject malformed `shader`
- reject UBO/texture ABI parameters without a shader URI
- reject `binding/member` on `level: pass`
- reject any parameter-level `specialization` field; specialization ids/types/stages come from shader reflection
- keep rejecting render-flow fields such as `pass`, `passes`, `phase`, and `renderState`

Important: this is not a pass declaration. The shader URI is only the ABI owner used for reflection validation.

- [x] **Step 3: Add specialization constant reflection if missing**

Audit `ShaderReflector` and `ShaderResourceBinding`/shader metadata types. If specialization constants are not exposed today, add a reflected specialization metadata type, for example:

```cpp
struct ShaderSpecializationConstantInfo {
  std::string name;
  ShaderStage stage = ShaderStage::Unknown;
  u32 constantId = 0;
  ShaderSpecializationValueType type = ShaderSpecializationValueType::Bool;
};
```

Expose it through compiled shader reflection, for example:

```cpp
class IShader {
public:
  virtual const std::vector<ShaderSpecializationConstantInfo> &
  getSpecializationConstants() const = 0;
};
```

Add a shader compiler/reflection test with specialization constants in both vertex and fragment stages:

```glsl
// vertex fixture
layout(constant_id = 17) const bool test_feature_a = true;

// fragment fixture
layout(constant_id = 23) const bool test_feature_b = false;
```

Expected reflection:

```text
name=test_feature_a stage=vertex constantId=17 type=Bool
name=test_feature_b stage=fragment constantId=23 type=Bool
```

This must use SPIR-V reflection through the existing reflector path. Do not implement this by scanning GLSL source text.

- [x] **Step 4: Add feature-to-shader reflection validation**

Add a validation helper that runs after feature assets are parsed and their referenced shader is loaded/compiled:

```cpp
std::vector<RenderFeatureShaderValidationDiagnostic>
validateRenderFeatureShaderAbi(const RenderFeature &feature,
                               const IShader &shader);
```

Rules:

- `level: shader`: for each parameter with `binding`, reflected shader bindings must contain a binding with that name
- `level: shader`: texture parameters must match reflected texture shape, e.g. `textureCube -> TextureCube`
- `level: shader`: UBO parameters with `member` must find a reflected UBO binding and a top-level member with that name
- `level: pass`: ignore resource binding validation and validate parameters against reflected specialization constants
- `level: pass`: every parameter name must match a reflected specialization constant name, and parameter `kind` must match the reflected scalar type
- if specialization constants are not currently exposed by reflection, extend the existing shader reflection/test framework rather than adding string-only ad hoc checks
- `forwardPass` flow switches must validate against Forward shader specialization constants; reflection supplies `constant_id`, stage, and type

Hard rules:

- no production `render-feature.yaml` with `binding` or `member` may skip shader resource ABI validation
- no production `level: pass` feature may skip specialization constant ABI validation

- [x] **Step 5: Create Forward feature**

```yaml
schema: lxe.render-feature.v1
name: ForwardPass
feature: forwardPass
level: pass
shader:
  uri: render_paths/Forward/pbr
parameters:
  render_skybox:
    kind: bool
    value: true
    required: true
  enable_tonemapping:
    kind: bool
    value: true
    required: true
  enable_gamma:
    kind: bool
    value: false
    required: true
```

`forwardPass` has no UBO. Its ABI is the reflected Forward shader specialization constants. The YAML parameter names must match the specialization constant names in the shader:

```text
render_skybox
enable_tonemapping
enable_gamma
```

C++ must not hardcode these names, ids, stages, or types. The feature file plus shader reflection are the source of truth.

- [x] **Step 6: Create Skybox feature**

```yaml
schema: lxe.render-feature.v1
name: Skybox
feature: skybox
level: shader
shader:
  uri: features/skybox
parameters:
  environmentMap:
    kind: textureCube
    uri: assets/env/khronos/neutral/ggx/specular.ktx2
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  color:
    kind: vec3
    value: [1.0, 1.0, 1.0]
    binding: SkyboxUBO
    member: color
    required: true
  intensity:
    kind: float
    value: 1.0
    binding: SkyboxUBO
    member: intensity
    required: true
  rotation:
    kind: float
    value: 0.0
    binding: SkyboxUBO
    member: rotation
    required: true
```

- [x] **Step 7: Clean environmentLighting**

Keep only IBL/indirect lighting fields. Do not leave direct visible skybox/background mode fields in the new runtime path.

`environmentLighting` must also reference its ABI owner:

```yaml
level: shader
shader:
  uri: features/environment_lighting
```

Its UBO/texture fields must be validated against `features/environment_lighting` reflection. It must not validate or own directly visible skybox fields.

- [x] **Step 8: Update ToneMapping feature**

`assets/effects/tone_mapping.render-feature.yaml` must include:

```yaml
schema: lxe.render-feature.v1
name: ToneMapping
feature: toneMapping
level: shader
shader:
  uri: features/tone_mapping
parameters:
  exposure:
    kind: float
    value: 1.0
    binding: ToneMappingUBO
    member: exposure
    required: true
  mode:
    kind: enum
    value: aces
    binding: ToneMappingUBO
    member: mode
    required: true
    allowedValues: [aces, reinhard]
```

`ToneMappingUBO` must not contain `enabled`, gamma, or output-encoding parameters. Tone mapping helper functions should apply only exposure and the selected tone curve. Whether Forward calls tone mapping is controlled by `forwardPass.enable_tonemapping`; final gamma/output encoding is controlled by `forwardPass.enable_gamma`.

- [x] **Step 9: Create Bloom feature**

```yaml
schema: lxe.render-feature.v1
name: Bloom
feature: bloom
level: shader
shader:
  uri: features/bloom
parameters:
  threshold:
    kind: float
    value: 1.0
    binding: BloomUBO
    member: threshold
    required: true
  intensity:
    kind: float
    value: 0.0
    binding: BloomUBO
    member: intensity
    required: true
  radius:
    kind: float
    value: 1.0
    binding: BloomUBO
    member: radius
    required: true
```

Bloom is a separate graph blit pass that runs after Forward has produced the full image. `features/bloom` declares `BloomUBO` and helper functions used by `render_paths/Bloom/blit.frag`. This slice only verifies the pass is split and wired; it does not require visual bloom quality testing.

- [x] **Step 10: Wire the single Forward graph asset**

In `forward_main.render-path.yaml` add graph features:

```yaml
forwardPass:
  uri: effects/forward_pass.render-feature.yaml
skybox:
  uri: effects/skybox.render-feature.yaml
bloom:
  uri: effects/bloom.render-feature.yaml
```

Add sources to Forward pass:

```yaml
- feature.forwardPass
- feature.skybox
- feature.environmentLighting
- feature.toneMapping
```

Forward pass `input.material.type` must include `unlit-texture` so the generated finite room OBJ participates in the same Forward pass as the helmet.

Add a separate Bloom blit pass after Forward. It reads Forward's color output and uses:

```yaml
- feature.bloom
```

- [x] **Step 11: Hard-cut forward_bloom render path**

Delete `assets/render_paths/forward_bloom.render-path.yaml`.

Update tests so default asset audits expect one default render path graph, not a `forward_bloom` variant. Bloom is represented by a Bloom blit pass plus `feature.bloom`, not by selecting a second Forward render-path file.

- [x] **Step 12: Run parser and shader ABI validation GREEN**

```bash
cmake --build build --target test_render_resource_parsers test_shader_compiler
./build/src/test/test_render_resource_parsers
./build/src/test/test_shader_compiler
```

Expected: PASS.

---

### Task 4: Add Central Feature Validation

**Files:**
- Create: `src/core/frame_graph/render_path_feature_validation.hpp`
- Create: `src/core/frame_graph/render_path_feature_validation.cpp`
- Modify scene/runtime load path that reports render graph validation failures
- Modify tests

This task is for cross-feature and target-format rules. Shader ABI validation already belongs to Task 3 and must run before runtime graph execution.

- [ ] **Step 1: Write RED validation test**

Create an invalid graph/feature combination:

```text
Forward target format: BGRA8Srgb
forwardPass.enable_gamma: true
```

Expected diagnostic:

```text
FATAL: sRGB target must not use manual gamma
```

- [ ] **Step 2: Add validation API**

```cpp
struct RenderPathFeatureValidationDiagnostic {
  std::string message;
  bool fatal = true;
};

std::vector<RenderPathFeatureValidationDiagnostic>
validateRenderPathFeatureCombination(const RenderPathGraph &graph,
                                     const FrameGraph &frameGraph,
                                     const SceneResourceTable &resources);
```

- [ ] **Step 3: Implement first rule**

If Forward writes sRGB and `forwardPass.enable_gamma=true`, reject. Do not rewrite the value.

- [ ] **Step 4: Stop scene loading on fatal validation errors**

Run validation after graph/features are loaded and before pipeline preparation.

Behavior on fatal validation error:

```text
1. print the fatal diagnostic
2. abort the scene/render-path load
3. clear or rollback the in-progress scene load state
4. do not keep a half-loaded graph
5. do not rewrite any feature value
6. do not silently fall back to another render path or target format
```

This is a project principle: C++ may validate and fail fast, but it must not repair bad schema-derived configuration at runtime.

- [ ] **Step 5: Run validation tests**

```bash
cmake --build build --target test_render_resource_parsers test_render_work_compiler
./build/src/test/test_render_resource_parsers
./build/src/test/test_render_work_compiler
```

Expected: PASS.

---

### Task 5: Add Pipeline Specialization Facts

**Files:**
- Modify: `src/core/pipeline/pipeline_build_desc.hpp`
- Modify: `src/core/pipeline/pipeline_build_desc.cpp`
- Modify: `src/core/pipeline/pipeline_key.hpp`
- Modify: `src/core/pipeline/pipeline_key.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`

- [ ] **Step 1: Write RED pipeline key test**

Two otherwise identical pipeline descs with different reflected specialization values must have different pipeline keys. Use a generic constant id from a pass-level feature fixture, not a hardcoded Forward field name.

- [ ] **Step 2: Add specialization structs**

```cpp
enum class ShaderSpecializationValueType : u8 {
  Bool,
  Int,
  UInt,
  Float,
};

struct ShaderSpecializationConstant {
  u32 constantId = 0;
  ShaderStage stage = ShaderStage::Unknown;
  ShaderSpecializationValueType type = ShaderSpecializationValueType::Bool;
  u32 valueU32 = 0;
  bool operator==(const ShaderSpecializationConstant &rhs) const = default;
};
```

Add to `PipelineBuildDesc`:

```cpp
std::vector<ShaderSpecializationConstant> specializationConstants;
```

- [ ] **Step 3: Include specialization constants in PipelineKey**

Compose a stable specialization signature from sorted `(stage, constantId)` entries.

- [ ] **Step 4: Run key test**

```bash
cmake --build build --target test_render_work_compiler && ./build/src/test/test_render_work_compiler
```

Expected: PASS.

---

### Task 6: Resolve Pass-Level Features To Reflected Specialization Constants

**Files:**
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/frame_graph/render_work_compiler.cpp`
- Modify: `src/test/integration/test_render_work_compiler.cpp`

Do not hardcode Forward specialization constant names or IDs in C++.
The source of truth is:

```text
render-feature.yaml level: pass
  -> parameter names and values
  -> shader reflection provides specialization constant name / id / stage / type
  -> RenderWorkCompiler copies reflected constants plus YAML values into PipelineBuildDesc
```

`forwardPass` is only the first pass-level feature using this path. Later pass-level features must reuse the same generic resolver.

- [ ] **Step 1: Add RED RenderWorkCompiler test**

Register a pass-level feature with mixed true/false values and assert the prepared pipeline desc contains the specialization constants declared by that feature asset and reflected from its shader URI.

The test must prove the compiler is not using a hardcoded Forward list:

```text
1. create a pass-level test feature with two bool parameters
2. create a test shader whose reflected specialization constants have matching names and non-default ids, for example 17 and 23
3. compile a graph that references this feature and shader
4. assert PipelineBuildDesc contains reflected IDs 17 and 23 with the YAML values
5. assert no unrelated Forward hardcoded IDs are added
```

This can be a small test-only fixture shader if reusing `render_paths/Forward/pbr` would make the test too broad.

- [ ] **Step 2: Store parsed pass-level specialization facts**

```cpp
struct PassFeatureSpecializationValue {
  std::string parameterName;
  ShaderStage stage = ShaderStage::Unknown;
  u32 constantId = 0;
  ShaderSpecializationValueType type = ShaderSpecializationValueType::Bool;
  u32 valueU32 = 0;
};

struct PassFeatureData {
  std::string featureName;
  ResourceUri shaderUri;
  std::vector<PassFeatureSpecializationValue> specializationValues;
};
```

These facts come from parsed `RenderFeature` entries with `level: pass` after shader ABI validation succeeds.

- [ ] **Step 3: Do not infer values in C++**

The implementation parses booleans from YAML only. No target-format/environment-mode heuristics.
Do not create a C++ struct with named fields like `renderSkybox` or `enableIbl`. That would reintroduce hardcoded pass semantics.

- [ ] **Step 4: Fill pipeline specialization constants generically**

For every graph feature where `RenderFeature.level == Pass`:

```text
for each parameter:
  require prior shader reflection validation success
  find reflected specialization constant by parameter name
  convert YAML scalar value to ShaderSpecializationConstant.valueU32
  append (reflected stage, reflected constantId, reflected type, valueU32) to PipelineBuildDesc
```

Do not switch on `feature.forwardPass`.
Do not switch on parameter names such as `render_skybox`.
Do not manually reproduce the constant IDs in C++.

- [ ] **Step 5: Run RenderWorkCompiler GREEN**

```bash
cmake --build build --target test_render_work_compiler && ./build/src/test/test_render_work_compiler
```

Expected: PASS.

---

### Task 7: Wire Vulkan Specialization Info

**Files:**
- Modify: `src/backend/vulkan/details/pipelines/graphics_pipeline.hpp`
- Modify: `src/backend/vulkan/details/pipelines/graphics_pipeline.cpp`

- [ ] **Step 1: Store specialization constants**

Keep `PipelineBuildDesc::specializationConstants` in `VulkanGraphicsPipeline`.

- [ ] **Step 2: Attach Vertex and Fragment `VkSpecializationInfo`**

Build `VkSpecializationMapEntry` and data arrays for both Vertex and Fragment constants. Group reflected specialization constants by `ShaderStage` and attach the matching `VkSpecializationInfo` to the corresponding shader stage create info. This slice only needs Vertex and Fragment support; unsupported stages should produce a fatal diagnostic instead of being ignored.

- [ ] **Step 3: Build editor**

```bash
cmake --build build --target lxe_editor
```

Expected: PASS.

---

### Task 8: Forward Shader And Helpers

**Files:**
- Modify: `assets/shaders/glsl/common/material_surface.glsl`
- Create: `assets/shaders/glsl/common/gamma_adjust.glsl`
- Create: `assets/shaders/glsl/common/materials/unlit_texture.contract.glsl`
- Modify supported contracts under `assets/shaders/glsl/common/materials/`
  - `matte.contract.glsl`
  - `uber.contract.glsl`
  - `metal.contract.glsl`
  - `substrate.contract.glsl`
  - `standard_pbr.contract.glsl`
  - every other contract whose metadata says `status: supported`
- Modify: `assets/shaders/glsl/features/tone_mapping.glsl`
- Create/Modify: `assets/shaders/glsl/features/skybox.glsl`
- Create/Modify: `assets/shaders/glsl/features/environment_lighting.glsl`
- Create/Modify: `assets/shaders/glsl/features/bloom.glsl`
- Create: `assets/shaders/glsl/render_paths/Bloom/blit.frag`
- Modify: `assets/shaders/glsl/render_paths/Forward/pbr.frag`
- Delete: `assets/shaders/glsl/render_paths/Environment/environment_box.frag`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add RED shader checks**

Generic feature reflection check:

```text
for each render feature referenced by forward_main.render-path.yaml:
  load feature.shader.uri
  if feature.level == pass:
    reflect specialization constants from feature.shader.uri
    validate feature parameters by reflected specialization constant name/type
  if feature.level == shader:
    reflect resource bindings from feature.shader.uri
    validate feature binding/member declarations
```

These checks must be driven by `shader.uri` in each feature file. C++ tests must not hardcode a feature-to-shader table.

Material ABI checks:

```text
every supported material contract compiled by Forward defines lxGetMaterialType()
existing lit contracts return LX_MATERIAL_TYPE_LIT
unlit_texture.contract.glsl returns LX_MATERIAL_TYPE_UNLIT
Forward shader contains the unlit early return before direct/IBL/tone-mapping flow
```

- [ ] **Step 2: Add unlit texture material contract**

First extend the material-library ABI in `assets/shaders/glsl/common/material_surface.glsl`:

```glsl
const uint LX_MATERIAL_TYPE_LIT = 0u;
const uint LX_MATERIAL_TYPE_UNLIT = 1u;
```

Every supported material contract must define:

```glsl
uint lxGetMaterialType() {
  return LX_MATERIAL_TYPE_LIT;
}
```

The new unlit contract returns:

```glsl
uint lxGetMaterialType() {
  return LX_MATERIAL_TYPE_UNLIT;
}
```

This is the ABI used by the main Forward shader. Do not identify unlit by object name, material URI, scene node name, or C++ hardcoded material type branches.

Audit requirement:

```bash
rg -n "status: supported|lxGetMaterialType" assets/shaders/glsl/common/materials
```

Every `status: supported` contract must have exactly one `lxGetMaterialType()` definition.

Create `assets/shaders/glsl/common/materials/unlit_texture.contract.glsl`.

Contract requirements:

```text
type: unlit-texture
status: supported
parameter: baseColorTexture required texture
storage field maps baseColorTexture to a texture slot
lxLoadMaterialSurface samples baseColorTexture with uv
```

The contract should make the sampled texture color available as the material surface color for an unlit path. It must not depend on direct lighting, IBL, or environment-box code.

Generated `.material` shape:

```yaml
schema: lxe.material.v2
renderClass: surface.opaque
bsdf:
  type: unlit-texture
  source: assets://shaders/glsl/common/materials/unlit_texture.contract.glsl
  parameters:
    baseColorTexture:
      kind: texture
      valueType: rgb
      uri: textures/test_neutral_room_srgb.png
```

- [ ] **Step 3: Make Forward accept unlit texture materials**

Update `assets/render_paths/forward_main.render-path.yaml` so the Forward surface pass accepts `unlit-texture` in `input.material.type`.

Forward shader behavior for this material:

```text
call lxLoadMaterialSurface(...)
call lxGetMaterialType()
if material type is LX_MATERIAL_TYPE_UNLIT:
  write surface.baseColor / alpha as the visible surface color
  do not run direct lighting
  do not run IBL
  do not run tone mapping
  still run the common final gamma-adjust step controlled by forwardPass.enable_gamma
```

Texture loading must mark the generated atlas as color/sRGB through the existing `.material` texture envelope (`kind: texture`, `valueType: rgb`). Vulkan sampling should therefore produce linear values for the shader. The unlit branch writes that sampled linear color into the same final gamma-adjust decision point as normal lit materials; `forwardPass.enable_gamma` decides whether shader-side gamma/sRGB encode is applied. This compatibility belongs to material/contract handling, not to environment background handling.

Define the gamma helper before using it in `assets/shaders/glsl/common/gamma_adjust.glsl`:

```glsl
vec4 lxApplyGammaAdjust(vec4 linearColor);
```

This helper performs only gamma/sRGB adjustment. It must not read pass features, decide whether gamma is enabled, apply exposure, or apply tone mapping. Forward owns the `LxForwardEnableGamma` branch and calls this helper only when that reflected pass-level specialization constant is true.

- [ ] **Step 4: Tone mapping owns tone-curve behavior only**

`features/tone_mapping.glsl` declares `ToneMappingUBO`, reads tone mapping params `exposure` and `mode`, and provides helper functions for exposure/tone-curve calculation only. It must not read enable flags, read gamma, apply gamma, or perform final output encoding. Forward decides whether to call it through `LxForwardEnableTonemapping`.

- [ ] **Step 5: Skybox helper owns infinite visible background behavior**

`features/skybox.glsl` declares skybox resources/UBO and helper functions for directly visible infinite skybox. Forward only calls the helper when the reflected pass-level `render_skybox` specialization constant is true.

- [ ] **Step 6: Bloom pass owns bloom math and UBO ABI**

`features/bloom.glsl` declares/uses `BloomUBO` and helper functions called from `render_paths/Bloom/blit.frag`. Bloom is not called from Forward. The default graph runs the Bloom blit pass after Forward; this slice verifies the split/wiring and does not require a visual bloom-quality assertion.

- [ ] **Step 7: Hard-cut environment box shader path**

Delete `render_paths/Environment/environment_box.frag` and all production references to it. Do not replace it with a new runtime environment-box helper. Finite rooms are generated ordinary mesh/material assets and render through the normal renderable/material path.

- [ ] **Step 8: Forward shader uses flow constants**

Forward has two draw categories inside the same pass/render context:

```text
1. skybox/filter draw, emitted by the render graph/renderer before ordinary surfaces when render_skybox is true
2. surface renderable draws, including PBR objects and generated finite-room unlit meshes
```

Do not introduce a new shader ABI such as `drawKind`, `LxForwardDrawKind`, or a C++ hardcoded material-name branch for this. The skybox/filter draw is graph/renderer scheduling inside the Forward pass. The surface shader branch below only describes material-backed surface rendering.

Surface shader flow:

```glsl
// Surface draw path. Finite room boxes are normal unlit surface renderables,
// so they reach this path exactly like any other material-backed object.
LxMaterialSurface surface = lxLoadMaterialSurface(...);
vec4 finalColor;
if (lxGetMaterialType() == LX_MATERIAL_TYPE_UNLIT) {
  // Unlit is an early return from lighting/tone mapping, not a skybox path.
  finalColor = vec4(surface.baseColor, surface.alpha);
} else {
  vec3 color = surface.baseColor;
  // Existing Forward PBR shading code stays here. Do not replace it with a new
  // lighting helper or new direct/IBL pass-flow switches in this slice.
  if (LxForwardEnableTonemapping) { /* tone mapping helper */ }
  finalColor = vec4(color, surface.alpha);
}
if (LxForwardEnableGamma) {
  finalColor = lxApplyGammaAdjust(finalColor);
}
outColor = finalColor;
```

Do not special-case finite room renderables.
Both unlit and lit/material paths must use the same final gamma-adjust branch shape. The difference is only that unlit skips lighting and tone mapping. Bloom is not part of this shader; it is a later graph blit pass.

- [ ] **Step 9: Compile shader tests**

```bash
cmake --build build --target CompileShaders test_shader_compiler && ./build/src/test/test_shader_compiler
```

Expected: PASS.

---

### Task 9: End-to-End Verification And Deploy

**Files:**
- No additional files unless tests reveal a scoped gap.

- [ ] **Step 1: Run core tests**

```bash
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler|test_shader_compiler)"
```

- [ ] **Step 2: Run realtime smoke**

```bash
ctest --test-dir build --output-on-failure -R "test_helmet_standard_pbr_realtime_smoke"
```

- [ ] **Step 3: Run direct realtime render**

```bash
python3 src/tools/lxe_realtime_render/lxe_realtime_render.py --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml --profile preview --xvfb --require-nonblack --require-pipeline-metadata --project-name codex_forward_pass_feature --editor build/src/editor/lxe_editor
```

Expected:

- no fallback
- nonblack image
- no double-gamma brightness regression
- finite room background visible in the helmet scene
- helmet remains visible in front of the room where geometry overlaps in depth
- no runtime finite-box injection path is used

- [ ] **Step 4: Hard-cut temporary smoke/debug helpers**

Before commit, remove any temporary debug code added only to inspect the room render. The final tree may keep normal automated smoke tests, but must not keep temporary runtime branches, debug render paths, or alternate scene-load paths.

Audit:

```bash
rg -n "temporary finite room|debug finite room|room smoke helper|finite room debug|__environment_finite_box|ensureEnvironmentBoxRenderable|environment_box" src assets
```

Expected: no production hits for temporary finite-room/debug helpers and no resurrected runtime finite-box path.

- [ ] **Step 5: Commit, push, and deploy editor**

Stage only files touched by this plan. Leave unrelated docs/requirements changes unstaged.

```bash
git commit -m "add data driven forward pass feature switches"
git push
```

Then MCP:

```text
ops_repo_pull
ops_build_target target=lxe_editor
ops_editor_start scene=assets/scenes/generated/helmet_standard_pbr.scene.yaml
editor_get_build_info
editor_get_summary
```

---

## Explicit Non-Goals

- Do not implement Deferred/GBuffer pass features in this slice.
- Do not add `feature.surfaceLighting`.
- Do not convert material variants to specialization constants.
- Do not add an Environment pass feature.
- Do not let environment-box logic read/own pass-level specialization constants.
- Do not let C++ infer or rewrite flow-control parameters from target format or environment mode.
- Do not reintroduce separate SkyboxBackground/PostProcess render-path variants.
- Do not keep `forward_bloom.render-path.yaml`; bloom is a Bloom blit pass inside the default graph plus `feature.bloom`.
- Do not special-case finite room renderables in Forward flow.
- Do not keep deleted finite-box runtime/shader paths as dormant production code.
