# REQ-071 Material Resource Rendering Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the full REQ-071 material, resource, GPU upload, package, and validation pipeline as one continuous migration.

**Architecture:** Material v2 PBRT envelopes become the only migrated material parameter truth. `SceneResourceTable` owns CPU resources and package persisted state; `IGpuResourceTable` owns backend upload, bindless tables, pipeline cache, and indirect draw; FrameGraph is compiled from explicit material/effect technique declarations.

**Tech Stack:** C++20, CMake/Ninja, yaml-cpp, SPIRV-Cross reflection, Vulkan, GLSL, Python test helpers, LXEngine integration tests.

---

## Ground Rules

- Work milestone order: A -> B -> C -> D -> E -> F.
- Use subagents for bounded reading, implementation, and review tasks, but keep interface decisions and integration in the main session.
- Keep commits frequent and scoped. Do not include unrelated existing worktree changes.
- If a verification command cannot run, record the exact command, failure, and suspected cause in this plan and in the active 071 requirement implementation notes.
- Do not claim final 071 completion until all mandatory gates pass or the user explicitly changes scope.

## File Structure

### New Or Heavily Revised Core Files

- `src/core/asset/material_parameter_envelope.hpp/.cpp`: typed PBRT envelope values and validation helpers.
- `src/core/asset/material_surface_schema.hpp/.cpp`: PBRT BSDF template schema registry.
- `src/core/asset/material_technique_set.hpp/.cpp`: explicit technique/pass/effect-neutral material technique data.
- `src/core/asset/render_effect.hpp/.cpp`: camera pre/post effect asset contract.
- `src/core/resource/resource_uri.hpp/.cpp`: canonical URI, base URI resolution, diagnostics path formatting.
- `src/core/resource/resource_handle.hpp`: generic type/index/generation handles and typed aliases.
- `src/core/resource/resource_metadata.hpp`: resource state, dependency, hash, diagnostics metadata.
- `src/core/resource/scene_resource_table.hpp/.cpp`: migrated or wrapper home for resource table identity/dependency APIs.
- `src/core/resource/scene_resource_table_upload_view.hpp`: typed arrays exported for GPU upload.
- `src/core/frame_graph/graph_resource_registry.hpp/.cpp`: standard source/target registry and SSA-style logical target validation.
- `src/core/frame_graph/technique_validator.hpp/.cpp`: active technique validation for material/effect passes.
- `src/core/rhi/gpu_resource_table.hpp`: backend-independent GPU table interface and handles.
- `src/core/task/task_graph.hpp/.cpp`: upload/preload task graph, progress, and diagnostics.
- `src/core/package/scene_package_format.hpp`: `.lxpkg` structs, section and chunk records.
- `src/core/package/scene_resource_hash.hpp/.cpp`: deterministic persisted-state Merkle hashing.

### New Or Heavily Revised Infra Files

- `src/infra/material_loader/material_resource_parser.hpp/.cpp`: Material v2 parser.
- `src/infra/material_loader/pbrt_material_defaults.hpp/.cpp`: converter defaults configuration support.
- `src/infra/resource_parsers/mesh_resource_parser.hpp/.cpp`: mesh parser adapter.
- `src/infra/resource_parsers/texture_resource_parser.hpp/.cpp`: texture parser adapter.
- `src/infra/resource_parsers/render_effect_resource_parser.hpp/.cpp`: RenderEffect parser.
- `src/infra/resource_parsers/scene_resource_parser_registry.hpp/.cpp`: parser lookup and dispatch.
- `src/infra/scene_io/scene_package_writer.hpp/.cpp`: `.lxpkg` writer.
- `src/infra/scene_io/scene_package_loader.hpp/.cpp`: `.lxpkg` streaming loader.
- `src/infra/scene_io/scene_validation_profile.hpp/.cpp`: validation profile parse and defaults.

### Backend And Tool Files

- `src/backend/vulkan/vulkan_gpu_resource_table.hpp/.cpp`: Vulkan `IGpuResourceTable`.
- `src/backend/vulkan/vulkan_realtime_renderer.cpp/.hpp`: execute FrameGraph work through GPUResourceTable and indirect draw.
- `src/backend/vulkan/offline/*.cpp/.hpp`: offline direct path consumes the same table and validation profile.
- `src/tools/lxe_compare_exr/`: diagnostics-aware comparison extensions.
- `src/tools/lxe_realtime_render/` or `src/demos/lxe_editor/`: headless realtime validation entry.
- `src/demos/lxe_editor/scene_runtime.cpp/.hpp`: source/package loading state, validation profile, and bridge removal.

### Tests

- Add milestone-focused integration tests under `src/test/integration/`:
  - `test_material_v2_parser.cpp`
  - `test_material_v2_converter_defaults.cpp`
  - `test_material_v2_resource_dependencies.cpp`
  - `test_technique_pass_contract.cpp`
  - `test_render_effect_resource_parser.cpp`
  - `test_frame_graph_registry.cpp`
  - `test_scene_resource_abstraction.cpp`
  - `test_scene_resource_upload_view_v2.cpp`
  - `test_gpu_resource_table_contract.cpp`
  - `test_upload_task_graph.cpp`
  - `test_bindless_indirect_contract.cpp`
  - `test_scene_package_round_trip.cpp`
  - `test_scene_package_hash.cpp`
  - `test_render_validation_profile.cpp`
  - `test_diagnostics_compare.cpp`
  - `test_071_bridge_audit.cpp`

Existing tests such as `test_generic_material_loader.cpp`, `test_material_instance.cpp`, `test_scene_resource_table.cpp`, `test_frame_graph.cpp`, `test_pipeline_cache.cpp`, `test_vulkan_resource_manager.cpp`, and offline compare tests should be migrated rather than duplicated where they already cover the same contract.

## Task 0: Baseline And Worktree Guard

**Files:**
- Modify: none
- Record results in this plan and active 071 requirement implementation sections.

- [ ] **Step 1: Inspect current worktree**

Run:

```bash
git status --short
```

Expected: Existing unrelated user changes are visible. Do not revert them.

- [ ] **Step 2: Inspect current build state**

Run:

```bash
cmake --build build --target BuildTest
```

Expected: PASS, or record the existing failure before editing code.

- [ ] **Step 3: Inspect current non-video tests**

Run:

```bash
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
```

Expected: PASS, or record the existing failure before editing code.

- [ ] **Step 4: Commit only plan/spec state if needed**

Run:

```bash
git status --short docs/superpowers/plans docs/superpowers/specs
```

Expected: Only intended plan/spec files are staged or committed.

## Task A1: Material Envelope Types And BSDF Schema Tests

**Files:**
- Create: `src/core/asset/material_parameter_envelope.hpp`
- Create: `src/core/asset/material_parameter_envelope.cpp`
- Create: `src/core/asset/material_surface_schema.hpp`
- Create: `src/core/asset/material_surface_schema.cpp`
- Test: `src/test/integration/test_material_v2_parser.cpp`

- [ ] **Step 1: Add failing tests for all required PBRT BSDF schemas**

Create tests asserting the schema registry contains `matte`, `glass`, `uber`,
`metal`, `substrate`, `fourier`, and `mix`, with required parameter names from
`notes/requirements/071-a-material-v2-pbrt-surface-contract.md`.

Run:

```bash
cmake --build build --target test_material_v2_parser
```

Expected: FAIL because schema registry does not exist.

- [ ] **Step 2: Implement envelope model**

Define:

```cpp
enum class MaterialEnvelopeKind {
  Float,
  Rgb,
  Spectrum,
  Bool,
  String,
  Texture,
  Integer,
  MaterialRef,
  BsdfTable,
};

enum class MaterialEnvelopeValueType {
  None,
  Float,
  Rgb,
};

struct MaterialParameterEnvelope final {
  MaterialEnvelopeKind kind = MaterialEnvelopeKind::Float;
  MaterialEnvelopeValueType valueType = MaterialEnvelopeValueType::None;
  std::optional<float> floatValue;
  std::optional<i32> integerValue;
  std::optional<Vec3f> rgbValue;
  std::optional<std::string> stringValue;
  std::optional<std::string> uri;
};
```

Add validation helpers:

```cpp
[[nodiscard]] bool hasInlineValue(const MaterialParameterEnvelope &envelope);
[[nodiscard]] bool hasResourceUri(const MaterialParameterEnvelope &envelope);
[[nodiscard]] std::string validateEnvelopeShape(
    const MaterialParameterEnvelope &envelope);
```

`validateEnvelopeShape` returns an empty string on success and a concrete
diagnostic string on failure.

- [ ] **Step 3: Implement BSDF schema registry**

Define:

```cpp
struct MaterialParameterSchema final {
  std::string name;
  std::vector<MaterialEnvelopeKind> allowedKinds;
  bool required = true;
};

struct MaterialSurfaceSchema final {
  std::string bsdfType;
  std::vector<MaterialParameterSchema> parameters;
};

[[nodiscard]] const MaterialSurfaceSchema *
findMaterialSurfaceSchema(std::string_view bsdfType);
```

Populate the seven required schemas with the exact required parameters from
REQ-071-a.

- [ ] **Step 4: Run parser schema test**

Run:

```bash
cmake --build build --target test_material_v2_parser
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/asset/material_parameter_envelope.* src/core/asset/material_surface_schema.* src/test/integration/test_material_v2_parser.cpp
git commit -m "Add Material v2 envelope and surface schemas"
```

## Task A2: MaterialResourceParser And Runtime Boundary

**Files:**
- Create: `src/infra/material_loader/material_resource_parser.hpp`
- Create: `src/infra/material_loader/material_resource_parser.cpp`
- Modify: `src/infra/material_loader/generic_material_loader.hpp`
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Modify: `src/core/asset/material_template.hpp`
- Modify: `src/core/asset/material_instance.hpp`
- Modify: `src/core/asset/material_instance.cpp`
- Test: `src/test/integration/test_material_v2_parser.cpp`
- Test: `src/test/integration/test_material_v2_resource_dependencies.cpp`

- [ ] **Step 1: Add failing parser tests**

Cover:

- valid minimal material for each BSDF type
- missing required parameter
- `value` and `uri` both present
- texture envelope missing `valueType`
- `mix` child material header points at another `mix`

Run:

```bash
cmake --build build --target test_material_v2_parser
```

Expected: FAIL on missing parser.

- [ ] **Step 2: Add parser interface**

Implement:

```cpp
struct MaterialParseContext final {
  ResourceUri materialUri;
  ResourceUri baseUri;
};

struct ParsedMaterialResource final {
  LX_core::MaterialInstance::UniquePtr instance;
  std::vector<ResourceHandle> dependencies;
  std::vector<std::string> diagnostics;
};

class MaterialResourceParser final {
public:
  ParsedMaterialResource parse(SceneResourceTable &table,
                               const ResourceUri &uri,
                               std::string_view yamlText) const;
};
```

- [ ] **Step 3: Split MaterialTemplate responsibilities**

Move pass/shader/render-state ownership out to `MaterialTechniqueSet`. Keep a
temporary adapter only if needed for existing tests, named with `Legacy` and
listed in the bridge audit.

- [ ] **Step 4: Store envelope truth in MaterialInstance**

Add `std::unordered_map<StringID, MaterialParameterEnvelope, StringID::Hash>`
and typed dependency handles. Remove or deprecate old top-level PBR truth for
migrated materials. Keep shader reflection buffer packing as derived upload
logic, not authoring truth.

- [ ] **Step 5: Run material tests**

Run:

```bash
cmake --build build --target test_material_v2_parser test_material_v2_resource_dependencies test_material_instance
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/asset src/infra/material_loader src/test/integration/test_material_v2_parser.cpp src/test/integration/test_material_v2_resource_dependencies.cpp
git commit -m "Parse Material v2 resources through PBRT envelopes"
```

## Task A3: Converter Defaults And Helmet Material Smoke

**Files:**
- Create: `src/infra/material_loader/pbrt_material_defaults.hpp`
- Create: `src/infra/material_loader/pbrt_material_defaults.cpp`
- Modify: PBRT converter files found by `rg -n "pbrt|PBRT|scene_convert" src scripts assets`
- Modify: helmet material assets under `assets/models/damaged_helmet/` or `assets/materials/`
- Test: `src/test/integration/test_material_v2_converter_defaults.cpp`
- Test: `src/test/integration/test_lxe_pbrt_scene_convert.py`

- [ ] **Step 1: Add converter default tests**

Test that missing source parameter plus configured default writes explicit
envelope output and reports `pbrt-default` in converter diagnostics, while
missing source and missing default fails.

- [ ] **Step 2: Implement defaults loader**

Read a YAML defaults file into `MaterialParameterEnvelope` values by BSDF type
and parameter name. Do not hardcode PBRT defaults in converter code.

- [ ] **Step 3: Convert helmet smoke material to Material v2**

Update the helmet smoke scene/material path to use `schema:
lxe.material.v2`, `bsdf.type`, envelope parameters, `defaultTechnique`, and
explicit Forward technique data.

- [ ] **Step 4: Run A gate**

Run:

```bash
cmake --build build --target test_material_v2_converter_defaults test_lxe_pbrt_scene_convert
ctest --test-dir build --output-on-failure -R "material_v2|pbrt|helmet" 
```

Expected: PASS or record environment-specific smoke failure.

- [ ] **Step 5: Commit**

```bash
git add src/infra/material_loader assets src/test/integration/test_material_v2_converter_defaults.cpp src/test/integration/test_lxe_pbrt_scene_convert.py
git commit -m "Make PBRT conversion emit explicit Material v2 defaults"
```

## Task B1: Technique Set, RenderEffect, And Pass Contract

**Files:**
- Create: `src/core/asset/material_technique_set.hpp`
- Create: `src/core/asset/material_technique_set.cpp`
- Create: `src/core/asset/render_effect.hpp`
- Create: `src/core/asset/render_effect.cpp`
- Create: `src/infra/resource_parsers/render_effect_resource_parser.hpp`
- Create: `src/infra/resource_parsers/render_effect_resource_parser.cpp`
- Test: `src/test/integration/test_technique_pass_contract.cpp`
- Test: `src/test/integration/test_render_effect_resource_parser.cpp`

- [ ] **Step 1: Add failing tests for missing explicit pass fields**

Assert missing `shader`, `stage`, `dispatch`, `sources`, `targets`, or
`renderState` fails with material/effect URI and field path.

- [ ] **Step 2: Implement technique data**

Define:

```cpp
enum class MaterialPassStage { Raster, Compute };
enum class MaterialPassDispatch { Draw, Fullscreen, Compute };

struct MaterialPassContract final {
  std::string name;
  ResourceUri shaderUri;
  MaterialPassStage stage;
  MaterialPassDispatch dispatch;
  std::vector<std::string> sources;
  std::vector<std::string> targets;
  RenderState renderState;
  std::optional<std::string> writeMode;
};

struct MaterialTechnique final {
  std::string name;
  std::vector<MaterialPassContract> passes;
};

class MaterialTechniqueSet final {
public:
  std::string defaultTechnique;
  std::unordered_map<std::string, MaterialTechnique> techniques;
};
```

- [ ] **Step 3: Implement RenderEffect parser**

Support `schema: lxe.render-effect.v1`, `phase: pre | post`, and pass fields
matching material passes. Reject any other phase.

- [ ] **Step 4: Run B parser tests**

Run:

```bash
cmake --build build --target test_technique_pass_contract test_render_effect_resource_parser
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/asset/material_technique_set.* src/core/asset/render_effect.* src/infra/resource_parsers/render_effect_resource_parser.* src/test/integration/test_technique_pass_contract.cpp src/test/integration/test_render_effect_resource_parser.cpp
git commit -m "Add explicit technique and render effect contracts"
```

## Task B2: FrameGraph Registry And Technique Validation

**Files:**
- Create: `src/core/frame_graph/graph_resource_registry.hpp`
- Create: `src/core/frame_graph/graph_resource_registry.cpp`
- Create: `src/core/frame_graph/technique_validator.hpp`
- Create: `src/core/frame_graph/technique_validator.cpp`
- Modify: `src/core/frame_graph/frame_graph.hpp`
- Modify: `src/core/frame_graph/frame_graph.cpp`
- Test: `src/test/integration/test_frame_graph_registry.cpp`
- Test: `src/test/integration/test_frame_graph.cpp`

- [ ] **Step 1: Add failing registry tests**

Cover standard target/source acceptance, unknown source/target rejection,
multiple producers for the same logical target rejection, and `writeMode`
exception handling.

- [ ] **Step 2: Implement registry**

Register sources and targets from REQ-071-b:

```text
depth.main
gbuffer.albedo
gbuffer.normal
gbuffer.material
gbuffer.emissive
hdr.color
ldr.color
swapchain.color
shadow.main
environment.radiance
geometry.vertex
geometry.index
material.bsdf
camera.ubo
scene.lights
scene.bvh
scene.environment
```

- [ ] **Step 3: Implement validator**

Validate material has active technique, pass shader URI exists, source/target
names are registered, shader reflection matches material/effect-owned fields,
and backend capability flags allow stage/dispatch.

- [ ] **Step 4: Run B FrameGraph gate**

Run:

```bash
cmake --build build --target test_frame_graph_registry test_frame_graph
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/frame_graph src/test/integration/test_frame_graph_registry.cpp src/test/integration/test_frame_graph.cpp
git commit -m "Validate technique passes through graph resource registry"
```

## Task B3: System ABI SSBO Common Contract

**Files:**
- Create: `src/core/scene/scene_system_abi.hpp`
- Create: `assets/shaders/glsl/common/scene_system_abi.glsl`
- Modify: `src/infra/shader_compiler/shader_reflector.cpp`
- Test: `src/test/integration/test_shader_compiler.cpp`
- Test: `src/test/integration/test_technique_pass_contract.cpp`

- [ ] **Step 1: Add failing ABI reflection test**

Compile a shader that declares `SceneCameraData`, `SceneLightData`,
`SceneObjectData`, and `SceneMaterialInstanceData` as storage buffers. Assert
set/binding/type/member size and offset match the C++ mirror.

- [ ] **Step 2: Implement C++ mirror and GLSL include**

Define matching structs and fixed set/binding constants in both files. Use the
same stem `scene_system_abi`.

- [ ] **Step 3: Reject reserved binding conflicts**

Update reflection validation to fail if material/effect-owned bindings use
reserved system names with incompatible layout or set/binding.

- [ ] **Step 4: Run ABI tests**

Run:

```bash
cmake --build build --target test_shader_compiler test_technique_pass_contract
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/scene/scene_system_abi.hpp assets/shaders/glsl/common/scene_system_abi.glsl src/infra/shader_compiler src/test/integration/test_shader_compiler.cpp src/test/integration/test_technique_pass_contract.cpp
git commit -m "Define reflected scene system ABI"
```

## Task C1: Generic Resource Identity, Metadata, And Parser Registry

**Files:**
- Create: `src/core/resource/resource_uri.hpp`
- Create: `src/core/resource/resource_uri.cpp`
- Create: `src/core/resource/resource_handle.hpp`
- Create: `src/core/resource/resource_metadata.hpp`
- Create: `src/infra/resource_parsers/scene_resource_parser_registry.hpp`
- Create: `src/infra/resource_parsers/scene_resource_parser_registry.cpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_scene_resource_abstraction.cpp`

- [ ] **Step 1: Add failing resource identity tests**

Assert scene-relative and material-relative URIs canonicalize consistently,
same canonical URI plus type deduplicates, same URI with different type does
not deduplicate, and diagnostics include owner URI/resource URI/parser name.

- [ ] **Step 2: Implement resource URI and metadata**

Add canonical URI type, resource type enum, entry state enum, dependency list,
diagnostic list, and content hash field.

- [ ] **Step 3: Add parser registry**

Implement parser lookup by `ResourceType` and extension/schema. Registry calls
parser with `SceneResourceTable&`, canonical URI, and parse context.

- [ ] **Step 4: Run resource abstraction test**

Run:

```bash
cmake --build build --target test_scene_resource_abstraction
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/resource src/core/scene/scene_resource_table.* src/infra/resource_parsers src/test/integration/test_scene_resource_abstraction.cpp
git commit -m "Add canonical resource identity and parser registry"
```

## Task C2: Mesh, Texture, Camera, Light, Effect Parser Split

**Files:**
- Create: `src/infra/resource_parsers/mesh_resource_parser.hpp`
- Create: `src/infra/resource_parsers/mesh_resource_parser.cpp`
- Create: `src/infra/resource_parsers/texture_resource_parser.hpp`
- Create: `src/infra/resource_parsers/texture_resource_parser.cpp`
- Modify: `src/infra/scene_asset/scene_mesh_loader.cpp`
- Modify: `src/infra/scene_asset/scene_material_loader.cpp`
- Modify: `src/infra/scene_io/scene_document.cpp`
- Test: `src/test/integration/test_scene_resource_abstraction.cpp`

- [ ] **Step 1: Add failing parser ownership test**

Assert parser returns handles and resources remain valid after parser object is
destroyed.

- [ ] **Step 2: Move format-specific logic into parsers**

Mesh parser delegates OBJ/glTF decoding to existing loaders, registers geometry
and mesh resources. Texture parser registers image metadata/payload resources.
Scene document load calls parser registry rather than owning format details.

- [ ] **Step 3: Run parser split tests**

Run:

```bash
cmake --build build --target test_scene_resource_abstraction test_gltf_scene_asset_loader test_scene_document
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/infra/resource_parsers src/infra/scene_asset src/infra/scene_io src/test/integration/test_scene_resource_abstraction.cpp
git commit -m "Split scene resource parsing by resource type"
```

## Task C3: Override Instances, Dependency Graph, And Upload View V2

**Files:**
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Modify: `src/core/scene/scene_gpu_records.hpp`
- Test: `src/test/integration/test_scene_resource_upload_view_v2.cpp`
- Test: `src/test/integration/test_scene_resource_abstraction.cpp`

- [ ] **Step 1: Add failing override and upload view tests**

Assert material override creates distinct `MaterialInstance` resource, base
instance is unchanged, dependency edges include material-to-texture and
camera-to-effect, and upload view exports typed arrays with handle-to-index
mappings.

- [ ] **Step 2: Implement override identity**

Use source material URI plus stable override hash as material instance
identity. Reuse same override hash, split different override hash.

- [ ] **Step 3: Implement package-ready graph export**

Export resource metadata, dependencies, stable ids, and typed arrays without
GPU handles or dirty flags.

- [ ] **Step 4: Run C gate**

Run:

```bash
cmake --build build --target test_scene_resource_abstraction test_scene_resource_upload_view_v2
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/scene src/test/integration/test_scene_resource_abstraction.cpp src/test/integration/test_scene_resource_upload_view_v2.cpp
git commit -m "Export package-ready scene resource graph"
```

## Task D1: IGpuResourceTable Interface And Vulkan Shell

**Files:**
- Create: `src/core/rhi/gpu_resource_table.hpp`
- Create: `src/backend/vulkan/vulkan_gpu_resource_table.hpp`
- Create: `src/backend/vulkan/vulkan_gpu_resource_table.cpp`
- Modify: `src/backend/vulkan/details/resource_manager.hpp`
- Modify: `src/backend/vulkan/details/resource_manager.cpp`
- Test: `src/test/integration/test_gpu_resource_table_contract.cpp`

- [ ] **Step 1: Add failing core interface test**

Assert core header includes no Vulkan types and exposes handles for buffers,
images, samplers, descriptor tables, bindless slots, indirect draw buffers,
pipelines, cache import/export, and progress query.

- [ ] **Step 2: Implement interface**

Define pure virtual `IGpuResourceTable` with create/update buffer/image/sampler,
bindless table update, indirect buffer update, pipeline find/getOrCreate,
cache import/export, and progress query methods.

- [ ] **Step 3: Implement Vulkan shell**

Wrap existing Vulkan resource manager/pipeline cache behind the new interface
without changing default draw path yet.

- [ ] **Step 4: Run D interface tests**

Run:

```bash
cmake --build build --target test_gpu_resource_table_contract test_vulkan_resource_manager
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/rhi/gpu_resource_table.hpp src/backend/vulkan/vulkan_gpu_resource_table.* src/backend/vulkan/details/resource_manager.* src/test/integration/test_gpu_resource_table_contract.cpp
git commit -m "Introduce backend-independent GPU resource table"
```

## Task D2: Upload Task Graph And Editor Loading State

**Files:**
- Create: `src/core/task/task_graph.hpp`
- Create: `src/core/task/task_graph.cpp`
- Modify: `src/demos/lxe_editor/editor_session.hpp`
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Modify: `src/demos/lxe_editor/ui_overlay.cpp`
- Test: `src/test/integration/test_upload_task_graph.cpp`
- Test: `src/test/integration/test_lxe_editor_session.cpp`

- [ ] **Step 1: Add failing task graph tests**

Assert task graph runs dependency order, reports phase/name/progress, records
diagnostics, and stops activation after fatal task failure.

- [ ] **Step 2: Implement task graph**

Define task name, phase, progress, dependency ids, `run()` function, and
diagnostics. First implementation may be single-worker but must expose progress
events.

- [ ] **Step 3: Add editor loading state**

Scene load, package restore, and technique switch enter loading state, show
current task/progress/log, and activate scene only after success.

- [ ] **Step 4: Run task/editor tests**

Run:

```bash
cmake --build build --target test_upload_task_graph test_lxe_editor_session
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/task src/demos/lxe_editor src/test/integration/test_upload_task_graph.cpp src/test/integration/test_lxe_editor_session.cpp
git commit -m "Add upload task graph and editor loading state"
```

## Task D3: Global Bindless Tables And Material Storage By Template

**Files:**
- Modify: `src/core/rhi/gpu_resource_table.hpp`
- Modify: `src/backend/vulkan/vulkan_gpu_resource_table.*`
- Modify: `src/core/scene/scene_gpu_records.hpp`
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Test: `src/test/integration/test_bindless_indirect_contract.cpp`

- [ ] **Step 1: Add failing bindless slot mapping tests**

Assert two material instances sharing one texture URI produce one CPU texture
handle, one GPU texture slot, and two material records pointing at that slot.
Assert no per-material descriptor set is created in the default path.

- [ ] **Step 2: Implement global bindless tables**

Add texture, sampler, buffer, material storage, object, mesh/geometry, and
camera/light table update APIs. Store `ResourceHandle -> GpuSlot` mapping.

- [ ] **Step 3: Implement material storage by template**

Group material instance parameter storage by BSDF template and reflected
layout. Object/draw records reference template id and material index.

- [ ] **Step 4: Run bindless tests**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/rhi src/backend/vulkan src/core/scene src/test/integration/test_bindless_indirect_contract.cpp
git commit -m "Upload scene data through global bindless tables"
```

## Task D4: Indirect Draw Execution And Transitional Cleanup

**Files:**
- Modify: `src/core/frame_graph/render_queue.hpp`
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.*`
- Modify: `src/backend/vulkan/offline/*.cpp`
- Test: `src/test/integration/test_bindless_indirect_contract.cpp`
- Test: `src/test/integration/test_071_bridge_audit.cpp`

- [ ] **Step 1: Add failing indirect draw tests**

Assert render work build groups by technique/pass/pipeline/template, produces
CPU indirect command buffers, and Vulkan uses indirect draw for supported
raster passes.

- [ ] **Step 2: Add failing bridge audit tests**

Search normal/default code path for calls to legacy material loader, legacy
per-material descriptor update, and non-bindless draw submission. Test must
fail while any default path still calls them.

- [ ] **Step 3: Implement indirect execution**

Generate draw data records and indirect commands from resource table upload
view. Execute supported raster passes through Vulkan indirect draw.

- [ ] **Step 4: Remove or isolate transitional paths**

Delete default transitional bridge calls from A-C. If keeping a debug path,
guard it behind an explicit debug flag defaulting false and assert validation
does not enable it.

- [ ] **Step 5: Run D gate**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract test_071_bridge_audit
xvfb-run -a ctest --test-dir build --output-on-failure -R "helmet|vulkan|offline" -L requires_video_device
```

Expected: PASS or record graphics environment failure with exact output.

- [ ] **Step 6: Commit**

```bash
git add src/core/frame_graph src/backend/vulkan src/test/integration/test_bindless_indirect_contract.cpp src/test/integration/test_071_bridge_audit.cpp
git commit -m "Render default passes through bindless indirect draw"
```

## Task E1: Package Format, Writer, Loader, And Hash

**Files:**
- Create: `src/core/package/scene_package_format.hpp`
- Create: `src/core/package/scene_resource_hash.hpp`
- Create: `src/core/package/scene_resource_hash.cpp`
- Create: `src/infra/scene_io/scene_package_writer.hpp`
- Create: `src/infra/scene_io/scene_package_writer.cpp`
- Create: `src/infra/scene_io/scene_package_loader.hpp`
- Create: `src/infra/scene_io/scene_package_loader.cpp`
- Test: `src/test/integration/test_scene_package_round_trip.cpp`
- Test: `src/test/integration/test_scene_package_hash.cpp`

- [ ] **Step 1: Add failing package round trip tests**

Build a small scene package and assert resource count, dependency graph,
object-to-mesh/material, camera/effect relations, and root hash match source
parse. Delete or move source YAML and assert package still loads.

- [ ] **Step 2: Define package structs**

Implement `LXPKG` header, section table, chunk table, package index, string URI
table, resource metadata, dependency graph, typed resource sections, backend
cache metadata, and content hashes.

- [ ] **Step 3: Implement deterministic hash**

Hash only persisted canonical state. Exclude upload view order, dirty flags,
runtime generation, GPU handles, bindless slots, FrameGraph result, and thread
completion order.

- [ ] **Step 4: Implement writer and streaming loader**

Writer serializes resource table persisted state. Loader reads header/table
first, restores independent sections/chunks as they complete, and does not
reparse source YAML/material/mesh files.

- [ ] **Step 5: Run package tests**

Run:

```bash
cmake --build build --target test_scene_package_round_trip test_scene_package_hash
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/package src/infra/scene_io/scene_package_* src/test/integration/test_scene_package_round_trip.cpp src/test/integration/test_scene_package_hash.cpp
git commit -m "Add scene package round trip and resource hashing"
```

## Task E2: Package Backend Cache And Source/Package Render Equivalence

**Files:**
- Modify: `src/backend/vulkan/vulkan_gpu_resource_table.*`
- Modify: `src/infra/scene_io/scene_package_writer.cpp`
- Modify: `src/infra/scene_io/scene_package_loader.cpp`
- Test: `src/test/integration/test_scene_package_round_trip.cpp`
- Test: `src/test/integration/test_offline_gpu_scene.cpp`

- [ ] **Step 1: Add failing backend cache fallback test**

Assert compatible cache imports, incompatible cache warns and rebuilds, and CPU
package still loads without cache.

- [ ] **Step 2: Add failing source/package offline equivalence test**

Render the same test scene from source parse and package restore with identical
camera, resolution, seed, sample count, tone mapping, and active profile.
Assert Merkle root first matches, then pixel payload matches exactly.

- [ ] **Step 3: Implement cache metadata sections**

Save backend name/version, GPU/driver compatibility key, pipeline key list,
pipeline cache blob, and upload manifest.

- [ ] **Step 4: Run E gate**

Run:

```bash
cmake --build build --target test_scene_package_round_trip test_scene_package_hash test_offline_gpu_scene
ctest --test-dir build --output-on-failure -R "package|offline"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/backend/vulkan src/infra/scene_io src/test/integration/test_scene_package_round_trip.cpp src/test/integration/test_offline_gpu_scene.cpp
git commit -m "Restore packages with backend cache metadata"
```

## Task F1: Validation Profile And Headless Realtime Entry

**Files:**
- Create: `src/infra/scene_io/scene_validation_profile.hpp`
- Create: `src/infra/scene_io/scene_validation_profile.cpp`
- Create or modify: `src/tools/lxe_realtime_render/`
- Modify: `src/demos/lxe_editor/commands/render_debug_commands.cpp`
- Test: `src/test/integration/test_render_validation_profile.cpp`
- Test: `src/test/integration/test_realtime_render_profile_commands.cpp`

- [ ] **Step 1: Add failing validation profile tests**

Assert profile parses source/package mode, camera, technique, resolution,
random seed, tone mapping, debug dump flags, and disables shadows, IBL, and
transparent/glass behavior for direct validation.

- [ ] **Step 2: Implement profile parser**

Support the `renderValidation`, `realtimeRender`, and `offlineRender` fields
from REQ-071-f.

- [ ] **Step 3: Implement headless realtime command**

CLI arguments: scene/package path, sourceMode, active technique, camera,
output profile, debug dump flag, and output path. It must run normal resource
load, technique validation, FrameGraph compile, GPUResourceTable upload,
bindless indirect execution, and image write.

- [ ] **Step 4: Run profile/headless tests**

Run:

```bash
cmake --build build --target test_render_validation_profile test_realtime_render_profile_commands
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/infra/scene_io/scene_validation_profile.* src/tools/lxe_realtime_render src/demos/lxe_editor/commands/render_debug_commands.cpp src/test/integration/test_render_validation_profile.cpp src/test/integration/test_realtime_render_profile_commands.cpp
git commit -m "Add direct validation profile and headless realtime render"
```

## Task F2: Diagnostics-Aware Compare

**Files:**
- Modify or create: `src/tools/lxe_compare_exr/`
- Test: `src/test/integration/test_diagnostics_compare.cpp`
- Test: existing `src/test/integration/test_lxe_compare_exr_metrics.cpp`

- [ ] **Step 1: Add failing diagnostics compare tests**

Create synthetic 8x8 diagnostic buffers that trigger edge/coverage mismatch,
input mismatch, BRDF mismatch, and unsupported/disabled classification.

- [ ] **Step 2: Implement diagnostic channels**

Read color plus materialId, objectId/drawId, normal, depth/visibility,
directInputsHash, and template-specific debug channels from EXR or typed
binary buffers.

- [ ] **Step 3: Implement classification**

Use 3x3 or 5x5 depth/normal/materialId/visibility neighborhood changes for
edge mask. Exclude edge-mask pixels from material formula thresholds. Output
top suspicious samples with coordinates, material URI/id, object/draw id,
diff value, category, and debug channel differences.

- [ ] **Step 4: Run compare tests**

Run:

```bash
cmake --build build --target test_diagnostics_compare test_lxe_compare_exr_metrics
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/tools/lxe_compare_exr src/test/integration/test_diagnostics_compare.cpp src/test/integration/test_lxe_compare_exr_metrics.cpp
git commit -m "Classify direct-lighting render differences"
```

## Task F3: Helmet/BMW Validation Assets And Final Gates

**Files:**
- Modify: `assets/scenes/`
- Modify: `assets/materials/`
- Modify: BMW converted assets found by `rg -n "BMW|M6|helmet|DamagedHelmet" assets src`
- Modify: `assets/shaders/glsl/common/`
- Modify: `assets/shaders/glsl/techniques/`
- Test: `src/test/integration/test_071_bridge_audit.cpp`
- Test: Python or CTest validation scripts for material sphere/full model cases.

- [ ] **Step 1: Add material sphere validation tests**

Generate one 64x64 sphere case per actual helmet/BMW material instance.
Render Forward direct, Deferred direct, and OfflineRT direct. Assert coverage,
non-black pixels, diagnostic buffers, and compare reports.

- [ ] **Step 2: Add full model validation tests**

Render helmet/BMW at 128x128 from source and package paths. Assert Forward,
Deferred, and OfflineRT direct outputs exist, are non-black, and compare with
diagnostics.

- [ ] **Step 3: Add headless vs editor export test**

For the same scene, technique, camera, and profile, compare headless realtime
output with editor export output. Metadata must show same technique,
FrameGraph, pipeline key, resource table root hash, and render settings hash.

- [ ] **Step 4: Add GBuffer/FrameGraph dump test**

Deferred validation must dump albedo, normal, material, depth, and lighting
targets with logical target id/version metadata. Every consumed target must
have a producer.

- [ ] **Step 5: Update helmet/BMW assets**

Ensure all materials use Material v2, declare three techniques, use direct
test representation for unsupported transparent/glass behavior, and include
validation profile settings that disable shadows, IBL, and transparency.

- [ ] **Step 6: Run final F gates**

Run:

```bash
cmake --build build --target BuildTest
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

Expected: PASS. Any failure blocks final 071 completion unless the user
explicitly changes scope.

- [ ] **Step 7: Commit**

```bash
git add assets src/test src/tools src/demos src/backend src/core src/infra
git commit -m "Validate REQ-071 direct-lighting pipeline"
```

## Task Z: Requirement Closure And Notes

**Files:**
- Modify: `notes/requirements/071-a-material-v2-pbrt-surface-contract.md`
- Modify: `notes/requirements/071-b-technique-pass-effect-framegraph-contract.md`
- Modify: `notes/requirements/071-c-scene-resource-parser-and-resource-abstraction.md`
- Modify: `notes/requirements/071-d-gpu-resource-table-pipeline-cache-and-upload-tasks.md`
- Modify: `notes/requirements/071-e-scene-package-fast-load-and-template-grouping.md`
- Modify: `notes/requirements/071-f-rendering-equivalence-helmet-bmw-validation.md`
- Modify: affected notes under `notes/concepts/` and `notes/subsystems/`

- [ ] **Step 1: Update implementation status**

For each 071 file, record completed behavior, final verification commands, and
bridge audit outcome.

- [ ] **Step 2: Run notes update workflow**

Use repository notes skills/commands if requested by active workflow. At
minimum run:

```bash
scripts/notes/serve_site.sh --build
```

Expected: PASS.

- [ ] **Step 3: Commit closure docs**

```bash
git add notes docs/superpowers/plans/2026-06-10-071-material-resource-rendering-pipeline.md
git commit -m "Document REQ-071 completion"
```

## Self-Review

- Spec coverage: A-F milestones in the master design each have concrete task
  groups and final gates in this plan.
- Placeholder scan: this plan avoids placeholder markers and gives concrete
  files, tests, commands, and expected outcomes.
- Type consistency: shared names are stable across tasks:
  `MaterialParameterEnvelope`, `MaterialSurfaceSchema`,
  `MaterialTechniqueSet`, `RenderEffect`, `ResourceUri`,
  `SceneResourceTableUploadView`, `IGpuResourceTable`, and `.lxpkg`.
- Scope note: this is intentionally a master plan. During execution, each task
  group may be split into smaller subagent assignments, but the milestone gates
  remain the integration contract.
