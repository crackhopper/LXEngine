# Material Hard Cut Surface Render Graph Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete legacy material-local technique/runtime assets and restore Helmet/BMW M6 rendering through pure `lxe.material.v2` SurfaceMaterial plus explicit RenderPathGraph.

**Architecture:** `.material` files only describe PBRT BSDF envelope truth. RenderPathGraph owns shader/pass/render-state/filter truth. The implementation intentionally deletes old assets/tests/code first so remaining compile failures expose every dependency on `MaterialTemplate` pass state, `MaterialInstance` parameter buffers, `GenericMaterialLoader` legacy parsing, and GPU record fallbacks.

**Tech Stack:** C++20, CMake/Ninja, yaml-cpp, LXEngine core/infra tests, PBRT converter Python tests, Vulkan shader assets.

---

## Scope Check

This plan is deliberately broad because the approved design is a hard cut. Do not preserve compatibility for deleted runtime/demo/test assets. The only runtime scene acceptance targets are:

- Helmet: `assets/models/damaged_helmet/DamagedHelmet.gltf` and the retained Helmet scene entry.
- BMW M6: `data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml` plus generated `data/scenes/bmw-m6/materials/runtime-pbr-approx/*.material`.

## File Structure

### Delete Or Remove From Build

- Delete runtime material assets:
  - `assets/materials/blinnphong_default.material`
  - `assets/materials/blinnphong_lit.material`
  - `assets/materials/blinnphong_textured.material`
  - `assets/materials/debug_line.material`
  - `assets/materials/mesh_debug.material`
  - `assets/materials/rtr_experiment_template.material`
  - `assets/materials/rtr_shadertoy_quantum_core.material`
  - `assets/materials/test_invalid_normal_no_light.material`
  - `assets/materials/test_invalid_normal_no_uv.material`
- Keep only:
  - `assets/materials/pbr.material`
  - `assets/materials/pbr_gold.material`
  - `data/scenes/bmw-m6/materials/runtime-pbr-approx/*.material`
- Delete non-target model/demo assets unless a retained Helmet/BMW scene references them:
  - `assets/models/builtin/`
  - `assets/models/3dgs_train_sample/`
  - `assets/models/sponza/`
  - `assets/models/stanford_bunny/`
  - `assets/models/viking_room/`
  - non-Helmet `assets/scenes/*.scene.yaml`
  - non-target `assets/project_templates/`
- Delete shader assets used only by removed materials:
  - BlinnPhong
  - debug line
  - mesh debug
  - RTR/Shadertoy
  - old material-local generated SPIR-V for those shaders
- Remove tests from `src/test/CMakeLists.txt` when their only purpose is old material-local behavior.

### Modify Core Material Types

- `src/core/asset/material_template.hpp`
  - remove `MaterialPassDefinition` storage and canonical reflection bindings.
  - keep only a surface template identity/schema wrapper or replace uses with `MaterialSurfaceSchema`.
- `src/core/asset/material_instance.hpp`
- `src/core/asset/material_instance.cpp`
  - remove `ParameterBuffer`, `setParameter`, `readParameterValue`, `getParameterBuffer*`, pass enable state, pass shader/render-state/pipeline accessors.
  - keep envelope table, typed resource handles, dependencies, render class/tags, dirty/version, texture handle APIs keyed by envelope parameter name.
- `src/core/scene/scene_gpu_records.cpp`
  - delete all fallback reads from `MaterialUBO`, `SurfaceParams`, `baseColor`, `surfaceColor`, `specularIntensity`, `ambientIntensity`, `emissive`, `shininess`.
  - keep only envelope-to-record mapping.
- `src/core/scene/object.hpp`
- `src/core/scene/object.cpp`
  - remove material-owned pass validation from `SceneNode`.
  - make renderability come from graph validation and object/material handles.
- `src/core/frame_graph/render_queue.cpp`
  - stop asking `MaterialInstance` for shader/pass/render-state.
  - consume graph-produced pass data.

### Modify Infra Parsers And Loaders

- `src/infra/material_loader/generic_material_loader.hpp`
- `src/infra/material_loader/generic_material_loader.cpp`
  - become a strict v2 wrapper around `MaterialResourceParser`.
  - non-v2 `.material` fatal.
  - delete legacy YAML parsing helpers.
- `src/infra/material_loader/material_resource_parser.cpp`
  - keep as SurfaceMaterial parser.
  - create envelope-only `MaterialInstance`.
- `src/core/asset/render_path_graph.hpp`
  - define `RenderPath`, `RenderPassNode`, `RenderPathGraph`, source/target/filter structs if not already sufficient in existing `render_effect.hpp`.
- `src/infra/resource_parsers/render_effect_resource_parser.hpp`
- `src/infra/resource_parsers/render_effect_resource_parser.cpp`
  - rename semantics in code or add new parser façade so tests and callers use RenderPathGraph naming.
- `assets/render_paths/forward_main.render-path.yaml`
  - minimal graph for Helmet/BMW.

### Modify Scene Runtime

- `src/demos/lxe_editor/scene_builder.cpp`
  - keep only Helmet/BMW construction or mark old builders unavailable.
- `src/demos/lxe_editor/scene_runtime.cpp`
  - remove parameter buffer material editing commands.
  - keep material assignment only for v2 surface material URIs.
- `src/infra/offline/offline_scene_loader.cpp`
- `src/infra/scene_asset/scene_material_loader.cpp`
  - stop expecting old material-local pass state.

### Tests To Keep Or Rewrite

- Keep and update:
  - `src/test/integration/test_material_v2_parser.cpp`
  - `src/test/integration/test_material_v2_resource_dependencies.cpp`
  - `src/test/integration/test_scene_resource_table.cpp`
  - `src/test/integration/test_gltf_scene_asset_loader.cpp`
  - `src/test/integration/test_scene_runtime.cpp`
  - `src/test/integration/test_lxe_pbrt_scene_convert.py`
  - `src/test/integration/test_technique_pass_contract.cpp` renamed/rewritten as RenderPathGraph contract coverage.
- Delete or remove from CMake:
  - `src/test/integration/test_generic_material_loader.cpp` old behavior cases, or rewrite as strict-v2-only small test.
  - `src/test/integration/test_material_variant_rules.cpp`
  - `src/test/integration/test_mesh_debug_geometry.cpp`
  - old BlinnPhong/debug/RTR shader/material tests in `test_shader_compiler.cpp`, `test_assets_layout.cpp`, `test_vulkan_shader.cpp`, `test_pipeline_identity.cpp`, `test_pipeline_cache.cpp`, `test_pipeline_build_info.cpp`.

## Task 0: Baseline Inventory

**Files:**
- Modify: none

- [ ] **Step 1: Record current dirty state**

Run:

```bash
git status --short
```

Expected: dirty worktree is visible. Do not revert unrelated changes.

- [ ] **Step 2: List old material truth**

Run:

```bash
rg -n "MaterialUBO|SurfaceParams|baseColorFactor|metallicFactor|roughnessFactor|defaultTechnique|variantRules|techniques:" src assets data
```

Expected: output includes the legacy files that this plan deletes or rewrites.

- [ ] **Step 3: List retained target scene chains**

Run:

```bash
rg -n "DamagedHelmet|damaged_helmet|bmw-m6|pbrt_bmw_m6|runtime-pbr-approx|pbr.material|pbr_gold.material" assets data src/test/integration src/demos src/infra
```

Expected: identify the exact Helmet/BMW references to preserve.

## Task 1: Delete Legacy Assets And Test Registration

**Files:**
- Delete: old assets listed in File Structure.
- Modify: `src/test/CMakeLists.txt`
- Modify: `assets/shaders/CMakeLists.txt`

- [ ] **Step 1: Delete old material assets**

Run:

```bash
rm -f \
  assets/materials/blinnphong_default.material \
  assets/materials/blinnphong_lit.material \
  assets/materials/blinnphong_textured.material \
  assets/materials/debug_line.material \
  assets/materials/mesh_debug.material \
  assets/materials/rtr_experiment_template.material \
  assets/materials/rtr_shadertoy_quantum_core.material \
  assets/materials/test_invalid_normal_no_light.material \
  assets/materials/test_invalid_normal_no_uv.material
```

Expected: the files are gone; no replacement files are created.

- [ ] **Step 2: Delete non-target model and scene assets**

Run:

```bash
rm -rf \
  assets/models/3dgs_train_sample \
  assets/models/builtin \
  assets/models/sponza \
  assets/models/stanford_bunny \
  assets/models/viking_room \
  assets/project_templates \
  assets/scenes/3dgs_train_sample.scene.yaml \
  assets/scenes/ibl_metal_sphere.scene.yaml \
  assets/scenes/lxe_editor.scene.yaml \
  assets/scenes/procedural_shader_gallery.scene.yaml \
  assets/scenes/realtime_offline_compare_diagnostic.scene.yaml \
  assets/scenes/realtime_offline_compare_flat.scene.yaml \
  assets/scenes/shadow_tutorial.scene.yaml
```

Expected: only retained Helmet scene files and BMW data remain from runtime scenes.

- [ ] **Step 3: Delete old material-local shader assets**

Run:

```bash
rm -f \
  assets/shaders/glsl/techniques/Forward/blinnphong_0.vert \
  assets/shaders/glsl/techniques/Forward/blinnphong_0.frag \
  assets/shaders/glsl/techniques/Forward/blinnphong_0.vert.spv \
  assets/shaders/glsl/techniques/Forward/blinnphong_0.frag.spv \
  assets/shaders/glsl/techniques/Forward/debug_line.vert \
  assets/shaders/glsl/techniques/Forward/debug_line.frag \
  assets/shaders/glsl/techniques/Forward/debug_line.vert.spv \
  assets/shaders/glsl/techniques/Forward/debug_line.frag.spv \
  assets/shaders/glsl/techniques/Forward/mesh_debug.vert \
  assets/shaders/glsl/techniques/Forward/mesh_debug.frag \
  assets/shaders/glsl/techniques/Forward/mesh_debug.vert.spv \
  assets/shaders/glsl/techniques/Forward/mesh_debug.frag.spv \
  assets/shaders/glsl/techniques/Forward/rtr_experiment_template.vert \
  assets/shaders/glsl/techniques/Forward/rtr_experiment_template.frag \
  assets/shaders/glsl/techniques/Forward/rtr_experiment_template.vert.spv \
  assets/shaders/glsl/techniques/Forward/rtr_experiment_template.frag.spv \
  assets/shaders/glsl/techniques/Forward/rtr_shadertoy_quantum_core.vert \
  assets/shaders/glsl/techniques/Forward/rtr_shadertoy_quantum_core.frag \
  assets/shaders/glsl/techniques/Forward/rtr_shadertoy_quantum_core.vert.spv \
  assets/shaders/glsl/techniques/Forward/rtr_shadertoy_quantum_core.frag.spv
```

If these paths do not exist because the current tree already moved or deleted them, treat that as success.

- [ ] **Step 4: Remove deleted shader entries from shader CMake**

Edit `assets/shaders/CMakeLists.txt` so the removed shader basenames no longer appear. Remove entries matching:

```cmake
blinnphong_0
debug_line
mesh_debug
rtr_experiment_template
rtr_shadertoy_quantum_core
```

Expected: shader build no longer expects deleted sources.

- [ ] **Step 5: Remove old tests from CMake registration**

Edit `src/test/CMakeLists.txt` and remove test targets whose main value is old material-local behavior:

```cmake
test_generic_material_loader
test_material_variant_rules
test_mesh_debug_geometry
```

For broader tests that also cover non-material systems, keep the target but remove old material-local cases in later tasks.

- [ ] **Step 6: Run asset grep**

Run:

```bash
rg -n "blinnphong|mesh_debug|debug_line|rtr_|test_invalid_normal|MaterialUBO|defaultTechnique|variantRules|techniques:" assets data
```

Expected: no hits in runtime asset files, except historical documentation if any remains under `README.md`.

- [ ] **Step 7: Commit asset/test-registration deletion**

Run:

```bash
git add -A assets data src/test/CMakeLists.txt
git commit -m "Remove legacy material-local assets"
```

Expected: commit succeeds with deletion-only and CMake registration changes.

## Task 2: Reduce MaterialTemplate To Surface Schema

**Files:**
- Modify: `src/core/asset/material_template.hpp`
- Modify: callers in `src/core/asset`, `src/core/scene`, `src/infra`, `src/test` surfaced by compile errors.

- [ ] **Step 1: Replace template API with surface identity**

Edit `src/core/asset/material_template.hpp` so it no longer includes pass/shader headers and contains only:

```cpp
#pragma once

#include "core/utils/string_table.hpp"

#include <memory>
#include <string>

namespace LX_core {

class MaterialTemplate final {
  struct Token {};

public:
  using SharedPtr = std::shared_ptr<MaterialTemplate>;

  MaterialTemplate(Token, std::string bsdfType)
      : m_bsdfType(std::move(bsdfType)) {}

  static SharedPtr create(std::string bsdfType) {
    return std::make_shared<MaterialTemplate>(Token{}, std::move(bsdfType));
  }

  [[nodiscard]] const std::string &getName() const { return m_bsdfType; }
  [[nodiscard]] const std::string &getBsdfType() const { return m_bsdfType; }
  [[nodiscard]] StringID getTemplateId() const { return StringID(m_bsdfType); }

private:
  std::string m_bsdfType;
};

using MaterialTemplateSharedPtr = MaterialTemplate::SharedPtr;

} // namespace LX_core
```

Expected: compile fails in code that still calls pass/shader/canonical binding APIs.

- [ ] **Step 2: Run compile to expose coupling**

Run:

```bash
ninja -C build LX_Core
```

Expected: FAIL with missing `setPassDefinition`, `getPassDefinition`, `getPipelineSignature`, `findCanonicalMaterialBinding`, or `getCanonicalMaterialBindings` call sites.

- [ ] **Step 3: Remove template pass API call sites**

For each compile error:

- if the caller is an old material-local test, delete the test case or remove the target from CMake.
- if the caller is core runtime renderability, route it to the `RenderPathGraph`
  data structs and graph-filter helpers defined in Task 5 and Task 6.
- if the caller only uses canonical binding to validate texture names, replace it with envelope parameter validation from `MaterialSurfaceSchema`.

Use this replacement pattern for texture validation:

```cpp
const MaterialSurfaceSchema *schema = findMaterialSurfaceSchema(material.getBsdfType());
const bool acceptsTextureParameter =
    schema != nullptr &&
    std::find_if(schema->parameters.begin(), schema->parameters.end(),
                 [](const MaterialParameterSchema &parameter) {
                   return parameter.name == "Kd" &&
                          std::find(parameter.allowedKinds.begin(),
                                    parameter.allowedKinds.end(),
                                    MaterialEnvelopeKind::Texture) !=
                              parameter.allowedKinds.end();
                 }) != schema->parameters.end();
```

Expected: no core production caller uses `MaterialTemplate` for pass/shader/render-state.

- [ ] **Step 4: Commit template reduction**

Run:

```bash
git add src/core/asset/material_template.hpp src/core src/infra src/test
git commit -m "Reduce MaterialTemplate to surface schema identity"
```

Expected: commit succeeds after the current task compiles far enough to move to MaterialInstance failures.

## Task 3: Remove ParameterBuffer And Pass State From MaterialInstance

**Files:**
- Modify: `src/core/asset/material_instance.hpp`
- Modify: `src/core/asset/material_instance.cpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/infra/material_loader/material_resource_parser.cpp`
- Modify: `src/infra/scene_asset/gltf_scene_asset_loader.cpp`
- Modify: remaining tests that keep new behavior.

- [ ] **Step 1: Replace MaterialInstance public API**

Edit `src/core/asset/material_instance.hpp` so the public API is envelope-only:

```cpp
class MaterialInstance {
  struct Token {};

public:
  using SharedPtr = std::shared_ptr<MaterialInstance>;
  using UniquePtr = std::unique_ptr<MaterialInstance>;

  MaterialInstance(Token, MaterialTemplateSharedPtr tmpl);

  static SharedPtr create(MaterialTemplateSharedPtr tmpl);
  static UniquePtr createUnique(MaterialTemplateSharedPtr tmpl);

  MaterialInstance(const MaterialInstance &) = delete;
  MaterialInstance &operator=(const MaterialInstance &) = delete;
  MaterialInstance(MaterialInstance &&) = delete;
  MaterialInstance &operator=(MaterialInstance &&) = delete;

  void setTexture(StringID parameterName, CombinedTextureSamplerSharedPtr tex);
  void setTextureHandle(StringID parameterName, TextureHandle handle);
  [[nodiscard]] TextureHandle getTextureHandle(StringID parameterName) const;
  [[nodiscard]] CombinedTextureSamplerSharedPtr
  getTexture(StringID parameterName) const;
  void forEachPendingTextureBinding(
      const std::function<void(
          StringID, const CombinedTextureSamplerSharedPtr &)> &callback) const;

  void syncGpuData();
  [[nodiscard]] u64 getMaterialStateVersion() const;
  [[nodiscard]] bool hasPendingMaterialStateSync() const;
  void clearPendingMaterialStateSync();

  [[nodiscard]] MaterialTemplateSharedPtr getTemplate() const;

  [[nodiscard]] SharedPtr cloneInstanceData() const;
  [[nodiscard]] UniquePtr cloneInstanceDataUnique() const;

  void setBsdfType(std::string bsdfType);
  [[nodiscard]] const std::string &getBsdfType() const;
  void setRenderClass(std::string renderClass);
  [[nodiscard]] const std::string &getRenderClass() const;
  void setMaterialTags(std::vector<std::string> tags);
  [[nodiscard]] const std::vector<std::string> &getMaterialTags() const;
  void setAuthoringMetadata(
      std::unordered_map<std::string, std::string> metadata);
  [[nodiscard]] const std::unordered_map<std::string, std::string> &
  getAuthoringMetadata() const;
  void setMaterialEnvelope(StringID parameterName,
                           MaterialParameterEnvelope envelope);
  [[nodiscard]] std::optional<
      std::reference_wrapper<const MaterialParameterEnvelope>>
  getMaterialEnvelope(StringID parameterName) const;
  [[nodiscard]] usize getMaterialEnvelopeCount() const;
  void addMaterialDependency(MaterialResourceDependency dependency);
  [[nodiscard]] const std::vector<MaterialResourceDependency> &
  getMaterialDependencies() const;

private:
  void markMaterialStateDirty();

  MaterialTemplateSharedPtr m_template;
  std::unordered_map<StringID, CombinedTextureSamplerSharedPtr, StringID::Hash>
      m_pendingTextureBindingsByName;
  std::unordered_map<StringID, TextureHandle, StringID::Hash>
      m_textureHandlesByName;
  std::string m_bsdfType;
  std::string m_renderClass;
  std::vector<std::string> m_materialTags;
  std::unordered_map<std::string, std::string> m_authoringMetadata;
  std::unordered_map<StringID, MaterialParameterEnvelope, StringID::Hash>
      m_materialEnvelopesByName;
  std::vector<MaterialResourceDependency> m_materialDependencies;
  u64 m_materialStateVersion = 0;
  bool m_materialStateDirty = false;
};
```

Expected: no `ParameterBuffer`, pass API, or shader parameter API remains in the header.

- [ ] **Step 2: Replace implementation with envelope-only logic**

Edit `src/core/asset/material_instance.cpp` to remove all `ParameterBuffer` code. Keep constructor and methods equivalent to:

```cpp
MaterialInstance::MaterialInstance(Token, MaterialTemplateSharedPtr tmpl)
    : m_template(std::move(tmpl)) {
  assert(m_template && "MaterialInstance requires a template");
}

MaterialInstance::SharedPtr
MaterialInstance::create(MaterialTemplateSharedPtr tmpl) {
  return std::make_shared<MaterialInstance>(Token{}, std::move(tmpl));
}

MaterialInstance::UniquePtr
MaterialInstance::createUnique(MaterialTemplateSharedPtr tmpl) {
  return UniquePtr(new MaterialInstance(Token{}, std::move(tmpl)));
}

void MaterialInstance::setTexture(StringID parameterName,
                                  CombinedTextureSamplerSharedPtr tex) {
  const auto envelope = getMaterialEnvelope(parameterName);
  assert(envelope.has_value() &&
         envelope->get().kind == MaterialEnvelopeKind::Texture &&
         "texture target must be a texture envelope");
  m_pendingTextureBindingsByName[parameterName] = std::move(tex);
  m_textureHandlesByName.erase(parameterName);
  markMaterialStateDirty();
}

void MaterialInstance::setTextureHandle(StringID parameterName,
                                        TextureHandle handle) {
  const auto envelope = getMaterialEnvelope(parameterName);
  assert(envelope.has_value() &&
         envelope->get().kind == MaterialEnvelopeKind::Texture &&
         "texture handle target must be a texture envelope");
  m_textureHandlesByName[parameterName] = handle;
  m_pendingTextureBindingsByName.erase(parameterName);
  markMaterialStateDirty();
}
```

Also keep clone/copy logic for envelope fields, dependencies, texture handles, dirty state and metadata.

- [ ] **Step 3: Run compile**

Run:

```bash
ninja -C build test_material_instance test_material_v2_parser test_material_v2_resource_dependencies
```

Expected: FAIL in tests and production files that still use removed APIs.

- [ ] **Step 4: Rewrite `test_material_instance.cpp`**

Replace old parameter-buffer/pass tests with envelope-only tests:

```cpp
void testEnvelopeInstanceClonesTruthAndHandles() {
  auto material = LX_core::MaterialInstance::create(
      LX_core::MaterialTemplate::create("uber"));
  material->setBsdfType("uber");
  LX_core::MaterialParameterEnvelope kd;
  kd.kind = LX_core::MaterialEnvelopeKind::Texture;
  kd.valueType = LX_core::MaterialEnvelopeValueType::Rgb;
  kd.uri = "memory://textures/kd.png";
  material->setMaterialEnvelope(LX_core::StringID("Kd"), kd);
  material->setTextureHandle(LX_core::StringID("Kd"),
                             LX_core::TextureHandle{0, 1});

  auto clone = material->cloneInstanceData();
  REQUIRE(clone->getBsdfType() == "uber");
  REQUIRE(clone->getMaterialEnvelope(LX_core::StringID("Kd")).has_value());
  REQUIRE(clone->getTextureHandle(LX_core::StringID("Kd")).isValid());
}
```

Remove every assertion involving `MaterialUBO`, `SurfaceParams`, `setParameter`,
`readParameterValue`, pass enable state, and parameter buffer bytes.

- [ ] **Step 5: Update production callers**

For each compile error:

- remove parameter editing command paths from `src/demos/lxe_editor/scene_runtime.cpp`.
- remove `MaterialUBO` setup in `src/demos/lxe_editor/scene_builder.cpp`.
- update `src/infra/scene_asset/gltf_scene_asset_loader.cpp` to set only `Kd` and `normalmap` envelopes.
- update `src/infra/material_loader/material_resource_parser.cpp` to create `MaterialTemplate::create(bsdfType)` and set envelopes.

- [ ] **Step 6: Commit instance hard cut**

Run:

```bash
git add src/core/asset/material_instance.* src/core/asset/material_template.hpp src/core src/infra src/demos src/test
git commit -m "Remove material parameter buffer state"
```

Expected: commit succeeds after envelope-only unit tests pass.

## Task 4: Make GenericMaterialLoader Strict V2 Only

**Files:**
- Modify: `src/infra/material_loader/generic_material_loader.hpp`
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Modify: `src/test/integration/test_generic_material_loader.cpp` or replace with a smaller strict-v2 test.

- [ ] **Step 1: Replace loader implementation**

Replace `loadGenericMaterial` body with strict v2 loading:

```cpp
LX_core::MaterialInstanceSharedPtr
loadGenericMaterial(const fs::path &materialPath,
                    const GenericMaterialLoadOptions &) {
  const fs::path resolvedMaterialPath = materialPath.is_absolute()
                                            ? materialPath
                                            : resolveRuntimePath(materialPath);
  if (!fs::exists(resolvedMaterialPath)) {
    fatalLoader("material file not found: " + materialPath.string());
  }

  YAML::Node root;
  try {
    root = YAML::LoadFile(resolvedMaterialPath.string());
  } catch (const YAML::Exception &e) {
    fatalLoader("failed to parse material file: " + std::string(e.what()));
  }
  if (!root || !root.IsMap()) {
    fatalLoader("material file root is not a YAML map: " +
                resolvedMaterialPath.string());
  }
  if (!root["schema"] || !root["schema"].IsScalar() ||
      root["schema"].as<std::string>() != "lxe.material.v2") {
    fatalLoader(resolvedMaterialPath.string() +
                ": only schema lxe.material.v2 is supported");
  }

  const LX_core::ResourceUri materialUri(
      fs::relative(resolvedMaterialPath).string());
  LX_core::SceneResourceTable table;
  MaterialResourceParser parser;
  ParsedMaterialResource parsed = parser.parse(table, materialUri,
                                               YAML::Dump(root));
  if (!parsed.diagnostics.empty() || !parsed.instance) {
    std::ostringstream message;
    message << materialUri.string() << ": invalid material v2 contract";
    for (const std::string &diagnostic : parsed.diagnostics) {
      message << "\n  " << diagnostic;
    }
    fatalLoader(message.str());
  }
  return parsed.instance->cloneInstanceData();
}
```

Delete old helper functions for variants, parameter application, resource application, shader compilation, and pass compilation from this file.

- [ ] **Step 2: Replace loader tests**

Replace `src/test/integration/test_generic_material_loader.cpp` with strict coverage:

```cpp
void testDefaultPbrLoadsAsV2Only() {
  auto root = findProjectRoot();
  ScopedCurrentPath currentPath(root);
  auto mat = LX_infra::loadGenericMaterial(root / "assets/materials/pbr.material");
  REQUIRE(mat != nullptr);
  REQUIRE(mat->getBsdfType() == "uber");
  REQUIRE(mat->getMaterialEnvelope(LX_core::StringID("Kd")).has_value());
}

void testNonV2MaterialIsRejected() {
  auto path = makeTempMaterialPath("legacy_rejected");
  ScopedTempFile temp(path);
  std::ofstream(path) << "shader: techniques/Forward/pbr\n";
  bool rejected = false;
  try {
    (void)LX_infra::loadGenericMaterial(path);
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("lxe.material.v2") !=
               std::string::npos;
  }
  REQUIRE(rejected);
}
```

If this test target was removed in Task 1, create a new target named
`test_surface_material_loader` instead.

- [ ] **Step 3: Run loader tests**

Run:

```bash
ninja -C build test_surface_material_loader test_material_v2_parser
```

If the target is still `test_generic_material_loader`, run:

```bash
ninja -C build test_generic_material_loader test_material_v2_parser
```

Expected: PASS. No shader compiler is involved in material loading.

- [ ] **Step 4: Commit strict loader**

Run:

```bash
git add src/infra/material_loader src/test/integration src/test/CMakeLists.txt
git commit -m "Make material loader strict v2 only"
```

Expected: commit succeeds.

## Task 5: Add Minimal RenderPathGraph Runtime Contract

**Files:**
- Create or modify: `src/core/asset/render_path_graph.hpp`
- Modify: `src/core/asset/render_effect.hpp` if existing graph structs live there.
- Modify: `src/infra/resource_parsers/render_effect_resource_parser.*`
- Create: `assets/render_paths/forward_main.render-path.yaml`
- Modify: `src/test/integration/test_technique_pass_contract.cpp`
- Modify: `src/test/integration/test_render_effect_resource_parser.cpp`

- [ ] **Step 1: Define graph data structs**

Create or update `src/core/asset/render_path_graph.hpp`:

```cpp
#pragma once

#include "core/asset/render_state.hpp"
#include "core/resource/resource_uri.hpp"

#include <string>
#include <vector>

namespace LX_core {

enum class RenderPath { Forward };
enum class RenderStage { Raster, Compute };
enum class RenderDispatch { Draw, Fullscreen, Compute };

struct RenderPassFilter final {
  std::vector<std::string> renderClasses;
  std::vector<std::string> bsdfTypes;
};

struct RenderPassNode final {
  std::string id;
  RenderStage stage = RenderStage::Raster;
  RenderDispatch dispatch = RenderDispatch::Draw;
  ResourceUri shaderUri;
  std::vector<std::string> sources;
  std::vector<std::string> targets;
  RenderState renderState;
  RenderPassFilter filters;
};

struct RenderPathGraph final {
  std::string name;
  RenderPath renderPath = RenderPath::Forward;
  std::vector<RenderPassNode> passes;
};

} // namespace LX_core
```

- [ ] **Step 2: Add minimal forward graph asset**

Create `assets/render_paths/forward_main.render-path.yaml`:

```yaml
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: ForwardOpaque
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
    filters:
      renderClass: [surface.opaque]
      bsdf: [matte, uber, metal, substrate]
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera, scene.lights]
    targets: [hdr.color, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
      blendEnable: false
```

- [ ] **Step 3: Update parser test for complete graph**

In `test_technique_pass_contract.cpp`, assert the parser accepts this minimal YAML:

```cpp
void testRenderPathGraphContractParsesForwardOpaque() {
  LX_infra::RenderEffectResourceParser parser;
  const auto parsed = parser.parse(R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: ForwardOpaque
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
    filters:
      renderClass: [surface.opaque]
      bsdf: [uber, metal]
    sources: [geometry.vertex, geometry.index, material.bsdf]
    targets: [hdr.color, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
      blendEnable: false
)");
  EXPECT(parsed.diagnostics.empty(), "complete graph should parse");
  EXPECT(parsed.graph.has_value(), "complete graph should produce graph");
  EXPECT(parsed.graph->passes.size() == 1, "one pass expected");
  EXPECT(parsed.graph->passes.front().shaderUri.string() ==
             "techniques/Forward/pbr",
         "shader uri should come from graph");
}
```

- [ ] **Step 4: Update parser negative test**

Add negative coverage:

```cpp
void testRenderPathGraphRejectsMissingShader() {
  LX_infra::RenderEffectResourceParser parser;
  const auto parsed = parser.parse(R"(
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward
passes:
  - id: ForwardOpaque
    stage: raster
    dispatch: draw
    sources: [geometry.vertex]
    targets: [hdr.color]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      blendEnable: false
)");
  EXPECT(!parsed.diagnostics.empty(), "missing shader should fail");
}
```

- [ ] **Step 5: Run graph tests**

Run:

```bash
ninja -C build test_technique_pass_contract test_render_effect_resource_parser
./build/src/test/test_technique_pass_contract
./build/src/test/test_render_effect_resource_parser
```

Expected: PASS.

- [ ] **Step 6: Commit graph contract**

Run:

```bash
git add src/core/asset/render_path_graph.hpp src/core/asset/render_effect.hpp src/infra/resource_parsers assets/render_paths src/test/integration/test_technique_pass_contract.cpp src/test/integration/test_render_effect_resource_parser.cpp
git commit -m "Add minimal render path graph contract"
```

Expected: commit succeeds.

## Task 6: Route Scene Renderability Through RenderPathGraph

**Files:**
- Modify: `src/core/scene/object.hpp`
- Modify: `src/core/scene/object.cpp`
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/core/frame_graph/render_queue.hpp`
- Modify: `src/infra/offline/offline_scene_loader.cpp`
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: tests using `supportsPass`, `getValidatedPassData`, or `RenderWorkQueue`.

- [ ] **Step 1: Introduce graph-derived render pass data**

Add a lightweight struct near existing render queue build inputs:

```cpp
struct GraphRenderPassSelection final {
  StringID passId;
  ResourceUri shaderUri;
  RenderState renderState;
  std::vector<std::string> sources;
  std::vector<std::string> targets;
};
```

Expected: render queue can receive pass data from graph without querying material.

- [ ] **Step 2: Remove SceneNode material pass enable checks**

In `src/core/scene/object.cpp`, remove logic equivalent to:

```cpp
material->isPassEnabled(pass)
material->getPassShader(pass)
material->getPassRenderState(pass)
material->getPipelineSignature(pass)
```

Replace renderability checks with:

```cpp
const bool materialMatchesGraph =
    material != nullptr &&
    graphPassSupportsMaterial(graphPass, material->getRenderClass(),
                              material->getBsdfType());
```

The helper must match by render class and BSDF lists declared in the graph pass.

- [ ] **Step 3: Update render queue builder**

In `src/core/frame_graph/render_queue.cpp`, replace material-derived shader info with graph-derived shader info:

```cpp
item.shaderUri = graphPass.shaderUri;
item.renderState = graphPass.renderState;
item.passId = graphPass.passId;
```

If the current `RenderWorkItem` only has shader pointer fields, add a transitional `ResourceUri shaderUri` field and delay shader compilation to backend/pipeline code.

- [ ] **Step 4: Rewrite scene node validation tests**

Delete old `setPassEnabled` mutation tests. Add a graph-filter test:

```cpp
void testGraphFilterControlsRenderableMaterial() {
  auto material = makeV2Material("uber", "surface.opaque");
  LX_core::RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.filters.renderClasses = {"surface.opaque"};
  pass.filters.bsdfTypes = {"uber"};
  EXPECT(graphPassSupportsMaterial(pass, material->getRenderClass(),
                                   material->getBsdfType()),
         "uber opaque material should match graph pass");
}
```

- [ ] **Step 5: Run renderability tests**

Run:

```bash
ninja -C build test_scene_node_validation test_scene_runtime
./build/src/test/test_scene_node_validation
./build/src/test/test_scene_runtime
```

Expected: PASS after old pass enable tests are removed or rewritten.

- [ ] **Step 6: Commit graph renderability routing**

Run:

```bash
git add src/core/scene src/core/frame_graph src/infra/offline src/demos src/test/integration
git commit -m "Route scene renderability through render path graph"
```

Expected: commit succeeds.

## Task 7: Remove GPU Record Parameter-Buffer Fallback

**Files:**
- Modify: `src/core/scene/scene_gpu_records.cpp`
- Modify: `src/test/integration/test_scene_resource_table.cpp`
- Modify: `src/test/integration/test_offline_gpu_scene.cpp`

- [ ] **Step 1: Delete fallback helpers**

Remove from `scene_gpu_records.cpp`:

```cpp
readFirstMaterialParameter
materialValueAsColor
materialValueAsFloat
```

Delete all reads from:

```cpp
MaterialUBO
SurfaceParams
baseColor
surfaceColor
specularIntensity
ambientIntensity
emissive
emissiveFactor
shininess
```

- [ ] **Step 2: Make `toGpuMaterialRecord` envelope-only**

Use this shape:

```cpp
SceneGpuMaterialRecord toGpuMaterialRecord(const MaterialInstance &material) {
  SceneGpuMaterialRecord record;
  (void)applyMaterialV2EnvelopeRecord(material, record);
  return record;
}
```

Cull mode must come from graph/render queue state in Task 6, not from material.
If the current record still stores cull flags, keep the default until graph upload
plumbs explicit render-state flags.

- [ ] **Step 3: Update tests**

In `test_scene_resource_table.cpp`, remove tests that mutate `MaterialUBO` after upload. Keep envelope mutation tests:

```cpp
auto envelope = material->getMaterialEnvelope(StringID("Kd"))->get();
envelope.kind = MaterialEnvelopeKind::Rgb;
envelope.rgbValue = Vec3f{0.2f, 0.4f, 0.6f};
material->setMaterialEnvelope(StringID("Kd"), envelope);
const auto view = table.buildUploadView();
EXPECT(view.materials.front().baseColor.x == 0.2f,
       "GPU record should reflect envelope Kd");
```

- [ ] **Step 4: Run upload tests**

Run:

```bash
ninja -C build test_scene_resource_table test_offline_gpu_scene
./build/src/test/test_scene_resource_table
./build/src/test/test_offline_gpu_scene
```

Expected: PASS. No GPU record test mentions old parameter buffers.

- [ ] **Step 5: Commit GPU fallback removal**

Run:

```bash
git add src/core/scene/scene_gpu_records.cpp src/test/integration/test_scene_resource_table.cpp src/test/integration/test_offline_gpu_scene.cpp
git commit -m "Remove GPU material parameter fallback"
```

Expected: commit succeeds.

## Task 8: Restore Helmet And BMW Acceptance Only

**Files:**
- Modify: retained Helmet scene YAML.
- Modify: `data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml`
- Modify: `src/test/integration/test_scene_runtime.cpp`
- Modify: `src/test/integration/test_gltf_scene_asset_loader.cpp`
- Modify: `src/test/integration/test_lxe_pbrt_scene_convert.py`

- [ ] **Step 1: Ensure Helmet scene references graph**

Retained Helmet scene YAML must include active graph data in the scene/camera block:

```yaml
camera:
  renderPath: Forward
  renderPathGraph:
    uri: assets/render_paths/forward_main.render-path.yaml
```

If the retained scene format uses a different camera block, add equivalent fields
there and update `SceneDocument` parser tests accordingly.

- [ ] **Step 2: Ensure BMW scene references graph**

Edit `data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml` to include:

```yaml
camera:
  renderPath: Forward
  renderPathGraph:
    uri: assets/render_paths/forward_main.render-path.yaml
```

Preserve existing object/mesh/material references.

- [ ] **Step 3: Rewrite scene runtime acceptance tests**

In `test_scene_runtime.cpp`, remove non-target scene tests and keep:

```cpp
void testHelmetSceneLoadsV2SurfaceMaterialsOnly();
void testPbrtBmwM6SceneLoadsMaterialV2RuntimeAssetsOnly();
```

Each test must assert:

```cpp
EXPECT(material->getMaterialEnvelopeCount() > 0,
       "runtime material should be surface envelope");
EXPECT(material->getBsdfType() == "uber" || material->getBsdfType() == "metal" ||
           material->getBsdfType() == "glass" || material->getBsdfType() == "matte" ||
           material->getBsdfType() == "substrate" || material->getBsdfType() == "mix",
       "runtime material should keep PBRT BSDF type");
```

Do not assert `Pass_Forward`, `MaterialUBO`, old material candidates, or old editor presets.

- [ ] **Step 4: Run scene acceptance**

Run:

```bash
ninja -C build test_gltf_scene_asset_loader test_scene_runtime
./build/src/test/test_gltf_scene_asset_loader
./build/src/test/test_scene_runtime
```

Expected: PASS. Only Helmet/BMW runtime scene coverage remains.

- [ ] **Step 5: Commit scene acceptance**

Run:

```bash
git add assets data src/test/integration/test_scene_runtime.cpp src/test/integration/test_gltf_scene_asset_loader.cpp src/test/integration/test_lxe_pbrt_scene_convert.py
git commit -m "Limit runtime acceptance to Helmet and BMW"
```

Expected: commit succeeds.

## Task 9: Final Legacy Audit And Documentation

**Files:**
- Modify: `notes/requirements/071-a-material-v2-pbrt-surface-contract.md`
- Modify: `notes/requirements/071-b-technique-pass-effect-framegraph-contract.md`
- Modify: docs if build/test lists changed.

- [ ] **Step 1: Run source and asset legacy audit**

Run:

```bash
rg -n "MaterialUBO|SurfaceParams|baseColorFactor|metallicFactor|roughnessFactor|defaultTechnique|variantRules|techniques:" src assets data
```

Expected:

- no runtime source fallback hits.
- no runtime asset hits.
- remaining hits, if any, are docs or negative tests that explicitly assert rejection.

- [ ] **Step 2: Run retained test set**

Run:

```bash
ninja -C build \
  test_material_instance \
  test_material_v2_parser \
  test_material_v2_resource_dependencies \
  test_scene_resource_table \
  test_gltf_scene_asset_loader \
  test_scene_runtime \
  test_technique_pass_contract \
  test_render_effect_resource_parser
ctest --test-dir build --output-on-failure -R 'test_(material_instance|material_v2_parser|material_v2_resource_dependencies|scene_resource_table|gltf_scene_asset_loader|scene_runtime|technique_pass_contract|render_effect_resource_parser)'
python3 src/test/integration/test_lxe_pbrt_scene_convert.py --source-dir /home/lixiang/proj/LXEngine
```

Expected: all pass.

- [ ] **Step 3: Run whitespace check**

Run:

```bash
git diff --check -- src assets data notes docs
```

Expected: no output.

- [ ] **Step 4: Update requirement status**

Append to `notes/requirements/071-a-material-v2-pbrt-surface-contract.md`:

```markdown
### 2026-06-11: legacy material-local runtime removed

- Runtime `.material` files are pure `lxe.material.v2` SurfaceMaterial assets.
- Old BlinnPhong/debug/RTR/test material assets and their tests were deleted.
- `MaterialTemplate` no longer stores pass/shader/render-state data.
- `MaterialInstance` no longer stores `ParameterBuffer` or pass state.
- `GenericMaterialLoader` is strict v2 only.
- GPU material records are generated only from PBRT envelopes and typed handles.
- Helmet and BMW M6 are the only runtime scene acceptance targets.
```

Append to `notes/requirements/071-b-technique-pass-effect-framegraph-contract.md`:

```markdown
### 2026-06-11: minimal RenderPathGraph runtime source of truth

- Shader/pass/render-state truth moved out of `.material` assets and into an explicit Forward RenderPathGraph.
- Helmet and BMW M6 scene acceptance uses the graph path.
- Old material-local `defaultTechnique` / `techniques` behavior is no longer supported.
```

- [ ] **Step 5: Commit final audit docs**

Run:

```bash
git add notes/requirements/071-a-material-v2-pbrt-surface-contract.md notes/requirements/071-b-technique-pass-effect-framegraph-contract.md
git commit -m "Document material hard cut completion"
```

Expected: commit succeeds.

## Final Verification

Run:

```bash
git status --short
ctest --test-dir build --output-on-failure -R 'test_(material_instance|material_v2_parser|material_v2_resource_dependencies|scene_resource_table|gltf_scene_asset_loader|scene_runtime|technique_pass_contract|render_effect_resource_parser)'
python3 src/test/integration/test_lxe_pbrt_scene_convert.py --source-dir /home/lixiang/proj/LXEngine
rg -n "MaterialUBO|SurfaceParams|baseColorFactor|metallicFactor|roughnessFactor|defaultTechnique|variantRules|techniques:" src assets data
```

Expected:

- tests pass.
- grep has no runtime source/asset hits except explicit negative tests or docs.
- worktree only contains intentional changes or is clean after commits.
