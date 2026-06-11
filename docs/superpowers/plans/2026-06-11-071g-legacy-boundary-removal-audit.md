# REQ-071-g Legacy Boundary Removal Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete the default/runtime legacy material, graph, tag, shader, submission, and per-draw data escape routes covered by `REQ-071-g`.

**Architecture:** Treat `071-g` as a hard cut, not a compatibility migration. The implementation starts by adding an audit gate, then deletes each boundary in order: material contract, scene identity, render graph, GPU material data, and submission. Temporary build/test failures are allowed while old coupling is exposed, but each task records the boundary that is broken and the next repair step.

**Tech Stack:** C++20, CMake/Ninja, yaml-cpp, Python unittest, LXEngine core/infra/backend tests, Vulkan shader assets, PBRT converter.

---

## Scope Check

This plan intentionally covers several code areas because `REQ-071-g` is the hard boundary-removal step. Do not split it during execution unless the user explicitly changes scope. Do not preserve compatibility paths, debug toggles, or default-off legacy switches to keep old behavior alive.

The current worktree is already dirty. Before each task, inspect the touched files and preserve unrelated user changes. Do not revert existing changes.

## File Structure

### New Test And Audit Files

- Create: `src/test/integration/test_071g_legacy_boundary_removal.cpp`
  - Static production/runtime forbidden-symbol scan.
  - Runtime contract tests for bindless decision and render work shape where this can run headless.
- Modify: `src/test/CMakeLists.txt`
  - Add `test_071g_legacy_boundary_removal` to `TEST_INTEGRATION_EXE_LIST`.

### Material Contract Boundary

- Modify: `src/infra/material_loader/generic_material_loader.cpp`
  - Keep v2-only wrapper behavior.
  - Ensure no non-v2 or legacy material-local branch remains.
- Modify: `src/infra/material_loader/material_resource_parser.cpp`
  - Remove production references to forbidden old field constants from diagnostics where possible.
  - Keep schema/field-path unknown-field diagnostics.
- Modify: `src/core/asset/material_instance.hpp`
- Modify: `src/core/asset/material_instance.cpp`
  - Remove default surface-material `ParameterBuffer` truth and shader-binding parameter accessors from migrated material path.
- Modify tests that still use `MaterialUBO` or `setParameter` as default material truth:
  - `src/test/integration/test_scene_resource_table.cpp`
  - `src/test/integration/test_gltf_scene_asset_loader.cpp`
  - `src/test/integration/test_vulkan_command_buffer.cpp`
  - `src/test/integration/test_vulkan_renderer_memory_probe.cpp`
  - `src/test/integration/test_vulkan_offscreen_submit_memory_probe.cpp`

### Scene Identity Boundary

- Modify: `src/core/scene/components/material_component.hpp`
- Modify: `src/core/scene/components/material_component.cpp`
  - Remove active tag and tagged material maps.
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/scene/scene.cpp`
  - Remove `setActiveMaterialTagForRenderables`.
- Modify: `src/core/offline/offline_render_profile.hpp`
  - Remove material tag fields.
- Modify: `src/infra/scene_io/scene_document.cpp`
  - Stop reading/writing material tag fields.
- Modify: `src/infra/offline/offline_scene_loader.cpp`
  - Stop selecting tagged materials for offline output.
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
  - Remove profile/session material tag application and commands.
- Modify: `src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py`
  - Stop emitting material tags.
- Modify assets:
  - `assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml`
  - `data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml`

### Render Graph Boundary

- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
  - Remove `makeDefaultForwardRenderPathGraph`.
  - Remove hardcoded default graph/pass insertion for migrated production path.
  - Require active RenderPathGraph from asset/resource.
- Modify: `src/core/frame_graph/frame_graph_build_plan.hpp`
- Modify: `src/core/frame_graph/frame_graph_build_plan.cpp`
  - Keep graph-to-FrameGraph build plan as the only migrated graph source.
- Modify graph tests:
  - `src/test/integration/test_frame_graph.cpp`
  - `src/test/integration/test_technique_pass_contract.cpp`
  - `src/test/integration/test_render_effect_resource_parser.cpp`

### GPU Material/Data Boundary

- Modify production shaders:
  - `assets/shaders/glsl/techniques/Forward/pbr.frag`
  - `assets/shaders/glsl/techniques/Forward/pbr_clearcoat.frag`
  - `assets/shaders/glsl/techniques/Deferred/pbr_gbuffer.frag`
  - `assets/shaders/glsl/techniques/Deferred/pbr_clearcoat_gbuffer.frag`
- Modify: `src/core/scene/scene_gpu_records.cpp`
  - Remove fallback reads from `MaterialUBO`, `SurfaceParams`, and generic parameter buffers.
- Modify shader/reflection tests:
  - `src/test/integration/test_shader_compiler.cpp`
  - `src/test/integration/test_pipeline_build_info.cpp`
  - `src/test/integration/test_scene_resource_table.cpp`

### Submission Boundary

- Modify: `src/core/frame_graph/render_validation_contract.hpp`
- Modify: `src/core/frame_graph/render_validation_contract.cpp`
  - Remove `LegacyPerItem` and make non-empty incomplete migrated work rejected.
- Modify: `src/core/frame_graph/render_queue.hpp`
- Modify: `src/core/frame_graph/render_queue.cpp`
  - Stop silently treating `raster.drawData` as a normal default work shape.
- Modify: `src/core/frame_graph/render_upload_plan.hpp`
- Modify: `src/core/frame_graph/render_upload_plan.cpp`
  - Remove default push-constant collection.
- Modify: `src/core/scene/object.hpp`
- Modify: `src/core/scene/object.cpp`
  - Remove `PerDrawData` from default renderable validation data.
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.hpp`
  - Remove default command-buffer push constants from draw items.
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
  - Remove per-item fallback after incomplete batch coverage.

## Task 0: Baseline And Guardrails

**Files:**
- Modify: none

- [ ] **Step 1: Inspect dirty worktree**

Run:

```bash
git status --short
```

Expected: existing dirty worktree is visible. Do not revert unrelated changes.

- [ ] **Step 2: Record current forbidden symbol inventory**

Run:

```bash
rg -n "defaultTechnique|MaterialUBO|baseColorFactor|metallicFactor|roughnessFactor|materialTag|setActiveMaterialTag|activeMaterialTag|BindlessSubmissionDecisionKind::LegacyPerItem|LegacyPerItem|raster\\.drawData|PerDrawData|makeDefaultForwardRenderPathGraph" \
  src/core src/infra src/backend src/demos/lxe_editor \
  assets/materials assets/shaders assets/scenes assets/render_paths assets/effects \
  data/scenes src/tools/lxe_pbrt_scene_convert
```

Expected: output lists current legacy roots. Save important lines in the task notes or commit message for the first cleanup commit.

- [ ] **Step 3: Build current test target if possible**

Run:

```bash
cmake --build build --target BuildTest
```

Expected: either the build succeeds, or it fails on current dirty work. If it fails before `071-g` edits, record the first failing target and continue; do not fix unrelated failures in this task.

## Task 1: Add Static Boundary Audit

**Files:**
- Create: `src/test/integration/test_071g_legacy_boundary_removal.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add the test executable to CMake**

In `src/test/CMakeLists.txt`, add `test_071g_legacy_boundary_removal` after `test_071_bridge_audit`:

```cmake
  test_bindless_validation_contract
  test_071_bridge_audit
  test_071g_legacy_boundary_removal
  test_scene_package_round_trip
```

- [ ] **Step 2: Create the static audit test**

Create `src/test/integration/test_071g_legacy_boundary_removal.cpp` with:

```cpp
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

struct ScanRoot final {
  fs::path path;
  bool required = true;
};

struct ForbiddenToken final {
  std::string text;
};

bool isSkippedDirectory(const fs::path &path) {
  const std::string generic = path.generic_string();
  return generic.find("/external/") != std::string::npos ||
         generic.find("/generated/") != std::string::npos ||
         generic.find("/third_party/") != std::string::npos;
}

bool isTextFile(const fs::path &path) {
  const std::string ext = path.extension().string();
  return ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" ||
         ext == ".frag" || ext == ".vert" || ext == ".glsl" ||
         ext == ".yaml" || ext == ".yml" || ext == ".material" ||
         ext == ".py";
}

std::string readFile(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void scanFile(const fs::path &repoRoot, const fs::path &path,
              const std::vector<ForbiddenToken> &tokens) {
  const std::string text = readFile(path);
  const std::string relative = fs::relative(path, repoRoot).generic_string();
  for (const ForbiddenToken &token : tokens) {
    if (text.find(token.text) == std::string::npos) {
      continue;
    }
    std::cerr << "[FAIL] forbidden token '" << token.text << "' in "
              << relative << '\n';
    ++g_failures;
  }
}

void scanRoot(const fs::path &repoRoot, const ScanRoot &root,
              const std::vector<ForbiddenToken> &tokens) {
  const fs::path absolute = repoRoot / root.path;
  if (!fs::exists(absolute)) {
    EXPECT(!root.required, "required scan root missing: " +
                               root.path.generic_string());
    return;
  }
  for (const fs::directory_entry &entry :
       fs::recursive_directory_iterator(absolute)) {
    if (entry.is_directory() && isSkippedDirectory(entry.path())) {
      continue;
    }
    if (!entry.is_regular_file() || !isTextFile(entry.path())) {
      continue;
    }
    scanFile(repoRoot, entry.path(), tokens);
  }
}

} // namespace

int main() {
  const fs::path repoRoot = fs::current_path();
  const std::vector<ScanRoot> roots{
      {"src/core"},
      {"src/infra"},
      {"src/backend"},
      {"src/demos/lxe_editor"},
      {"assets/materials"},
      {"assets/shaders"},
      {"assets/scenes"},
      {"assets/render_paths", false},
      {"assets/effects", false},
      {"data/scenes", false},
      {"src/tools/lxe_pbrt_scene_convert", false},
  };
  const std::vector<ForbiddenToken> tokens{
      {"defaultTechnique"},
      {"MaterialUBO"},
      {"baseColorFactor"},
      {"metallicFactor"},
      {"roughnessFactor"},
      {"materialTag"},
      {"setActiveMaterialTag"},
      {"activeMaterialTag"},
      {"BindlessSubmissionDecisionKind::LegacyPerItem"},
      {"LegacyPerItem"},
      {"raster.drawData"},
      {"PerDrawData"},
      {"makeDefaultForwardRenderPathGraph"},
  };

  for (const ScanRoot &root : roots) {
    scanRoot(repoRoot, root, tokens);
  }

  if (g_failures != 0) {
    std::cerr << g_failures
              << " REQ-071-g legacy boundary audit failures\n";
    return 1;
  }
  return 0;
}
```

- [ ] **Step 3: Verify the audit fails**

Run:

```bash
cmake --build build --target test_071g_legacy_boundary_removal
./build/src/test/test_071g_legacy_boundary_removal
```

Expected: build succeeds and the test fails, reporting current forbidden tokens. If the binary path differs, run `find build -name test_071g_legacy_boundary_removal -type f`.

- [ ] **Step 4: Commit the red audit gate**

Run:

```bash
git add src/test/CMakeLists.txt src/test/integration/test_071g_legacy_boundary_removal.cpp
git commit -m "Add 071g legacy boundary audit"
```

Expected: commit only contains the new audit and CMake registration.

## Task 2: Remove materialTag From Scene Identity

**Files:**
- Modify: `src/core/scene/components/material_component.hpp`
- Modify: `src/core/scene/components/material_component.cpp`
- Modify: `src/core/scene/scene.hpp`
- Modify: `src/core/scene/scene.cpp`
- Modify: `src/core/offline/offline_render_profile.hpp`
- Modify: `src/infra/scene_io/scene_document.cpp`
- Modify: `src/infra/offline/offline_scene_loader.cpp`
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Modify: `src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py`
- Modify: `assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml`
- Modify: `data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml`

- [ ] **Step 1: Remove tag API from `MaterialComponent` header**

In `src/core/scene/components/material_component.hpp`, replace the public material API with:

```cpp
  const MaterialInstanceSharedPtr &getPendingMaterialInstance() const {
    return m_pendingMaterial;
  }
  void setMaterialInstance(MaterialInstanceSharedPtr material);
  void clearPendingMaterials();
  [[nodiscard]] MaterialHandle getMaterialHandle() const {
    return m_materialHandle;
  }
  void setMaterialHandle(MaterialHandle handle) { m_materialHandle = handle; }
  void forEachPendingMaterial(
      const std::function<void(const std::string &,
                               const MaterialInstanceSharedPtr &)> &callback)
      const;
  void forEachMaterialHandle(
      const std::function<void(const std::string &, MaterialHandle)> &callback)
      const;
```

Remove these declarations from the same class:

```cpp
  MaterialComponent(std::string tag, MaterialInstanceSharedPtr material);
  void setTaggedMaterial(std::string tag, MaterialInstanceSharedPtr material);
  void setTaggedMaterialHandle(std::string tag, MaterialHandle handle);
  [[nodiscard]] bool setActiveMaterialTag(const std::string &tag);
  [[nodiscard]] const std::string &getActiveMaterialTag() const;
  [[nodiscard]] bool hasMaterialTag(const std::string &tag) const;
  [[nodiscard]] MaterialHandle getMaterialHandleForTag(const std::string &tag) const;
```

Remove these private members:

```cpp
  std::unordered_map<std::string, MaterialInstanceSharedPtr>
      m_pendingMaterialsByTag;
  std::unordered_map<std::string, MaterialHandle> m_materialHandlesByTag;
  std::string m_activeMaterialTag;
```

- [ ] **Step 2: Remove tag behavior from `MaterialComponent` implementation**

In `src/core/scene/components/material_component.cpp`, delete the tagged constructor and these methods:

```cpp
MaterialComponent::MaterialComponent(std::string tag,
                                     MaterialInstanceSharedPtr material);
void MaterialComponent::setTaggedMaterial(std::string tag,
                                          MaterialInstanceSharedPtr material);
void MaterialComponent::setTaggedMaterialHandle(std::string tag,
                                                MaterialHandle handle);
bool MaterialComponent::setActiveMaterialTag(const std::string &tag);
bool MaterialComponent::hasMaterialTag(const std::string &tag) const;
MaterialHandle MaterialComponent::getMaterialHandleForTag(
    const std::string &tag) const;
```

Update `setMaterialInstance`:

```cpp
void MaterialComponent::setMaterialInstance(MaterialInstanceSharedPtr material) {
  unregisterPassStateListener();
  m_pendingMaterial = std::move(material);
  m_materialHandle = {};
  registerPassStateListener();
  notifyOwnerStructuralChange();
}
```

Update `clearPendingMaterials`:

```cpp
void MaterialComponent::clearPendingMaterials() {
  m_pendingMaterial.reset();
  unregisterPassStateListener();
}
```

Update iteration helpers:

```cpp
void MaterialComponent::forEachPendingMaterial(
    const std::function<void(const std::string &,
                             const MaterialInstanceSharedPtr &)> &callback)
    const {
  if (callback) {
    callback(std::string{}, m_pendingMaterial);
  }
}

void MaterialComponent::forEachMaterialHandle(
    const std::function<void(const std::string &, MaterialHandle)> &callback)
    const {
  if (callback) {
    callback(std::string{}, m_materialHandle);
  }
}
```

- [ ] **Step 3: Remove scene-level active tag setter**

Delete `Scene::setActiveMaterialTagForRenderables` declaration from `src/core/scene/scene.hpp`.

Delete the method body from `src/core/scene/scene.cpp`:

```cpp
void Scene::setActiveMaterialTagForRenderables(const std::string &tag) {
  for (const auto &renderable : m_renderables) {
    const auto *node = dynamic_cast<SceneNode *>(renderable.get());
    if (node == nullptr) {
      continue;
    }
    auto materialComponent = node->getComponent<MaterialComponent>();
    if (!materialComponent.has_value()) {
      continue;
    }
    (void)materialComponent->get().setActiveMaterialTag(tag);
  }
}
```

- [ ] **Step 4: Remove tag fields from render profile structs**

In `src/core/offline/offline_render_profile.hpp`, delete:

```cpp
  std::string materialTag;
```

from both `OutputProfile` and `OfflineRenderSettings`.

- [ ] **Step 5: Remove tag parsing and emission from scene document**

In `src/infra/scene_io/scene_document.cpp`, remove branches that read or write keys named `materialTag`. Keep unknown field preservation for other extension fields.

The output profile writer must not contain:

```cpp
if (!profile.materialTag.empty()) {
  out << YAML::Key << "materialTag" << YAML::Value << profile.materialTag;
}
```

The offline settings writer must not contain:

```cpp
if (!document.offline.materialTag.empty()) {
  out << YAML::Key << "materialTag" << YAML::Value
      << document.offline.materialTag;
}
```

- [ ] **Step 6: Remove offline loader tag selection**

In `src/infra/offline/offline_scene_loader.cpp`, remove the `materialTag` parameter from the recursive visit helper and remove calls to `findMaterialBindingByTag`. Material selection should use the node's direct material binding/handle only.

The helper signature should become:

```cpp
void visitNode(const SceneDocumentResourceResolver &resolver,
               const SceneDocumentNode &node, const std::string &path,
               const Mat4f &parentWorld, const std::string &cameraPath,
               LoadState &state)
```

- [ ] **Step 7: Remove editor and main tag application**

In `src/demos/lxe_editor/editor_session.cpp` and `src/demos/lxe_editor/main.cpp`, delete blocks that check `resolved.output.materialTag` and call `setActiveMaterialTagForRenderables`.

Delete the editor command branch that emits JSON like:

```cpp
"{\"materialTag\":\"" + jsonEscape(args[1]) + "\"}"
```

- [ ] **Step 8: Remove PBRT converter tag output**

In `src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py`, remove dictionary entries like:

```python
"materialTag": "realtime-pbr"
```

and:

```python
"materialTag": "offline-pbrt-reference"
```

Output scene/profile data must refer directly to material assets or material handles already produced by the converter.

- [ ] **Step 9: Migrate validation scenes**

Remove all `materialTag` keys from:

```bash
assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml
data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml
```

Keep the direct material references that already identify the intended runtime material.

- [ ] **Step 10: Verify no materialTag remains in production/default paths**

Run:

```bash
rg -n "materialTag|setActiveMaterialTag|activeMaterialTag" \
  src/core src/infra src/backend src/demos/lxe_editor \
  assets/scenes data/scenes src/tools/lxe_pbrt_scene_convert
```

Expected: no output except temporary references in tests being edited in later steps. If output remains in production files, remove it in this task.

- [ ] **Step 11: Run targeted tests**

Run:

```bash
cmake --build build --target test_output_profile_resolution test_lxe_pbrt_scene_convert test_gltf_scene_asset_loader
ctest --test-dir build --output-on-failure -R "test_output_profile_resolution|test_lxe_pbrt_scene_convert|test_gltf_scene_asset_loader"
```

Expected: tests pass or fail only because they still assert tag behavior. Update those tests to assert that saved/generated content does not contain `materialTag`.

- [ ] **Step 12: Commit scene identity hard cut**

Run:

```bash
git add src/core/scene/components/material_component.hpp \
  src/core/scene/components/material_component.cpp \
  src/core/scene/scene.hpp src/core/scene/scene.cpp \
  src/core/offline/offline_render_profile.hpp \
  src/infra/scene_io/scene_document.cpp \
  src/infra/offline/offline_scene_loader.cpp \
  src/demos/lxe_editor/editor_session.cpp \
  src/demos/lxe_editor/main.cpp \
  src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py \
  assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml \
  data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml
git commit -m "Remove material tag switching from default paths"
```

Expected: commit has no unrelated files.

## Task 3: Tighten Material v2 Loader And Remove Old Material Truth

**Files:**
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Modify: `src/infra/material_loader/material_resource_parser.cpp`
- Modify: `src/core/asset/material_instance.hpp`
- Modify: `src/core/asset/material_instance.cpp`
- Modify: affected material tests under `src/test/integration/`

- [ ] **Step 1: Verify `GenericMaterialLoader` is v2-only**

Run:

```bash
rg -n "defaultTechnique|techniques|parameters|resources|variantRules|shader" \
  src/infra/material_loader/generic_material_loader.cpp
```

Expected: no output for legacy parser branches. If output shows an old parse branch, delete it. The loader should keep this shape:

```cpp
if (!isMaterialV2Contract(root)) {
  fatalLoader(materialUri.string() +
              ": only schema lxe.material.v2 material files are accepted");
}

return loadMaterialV2EnvelopeContract(root, materialUri, resourceTable);
```

- [ ] **Step 2: Remove old field constants from parser diagnostics where feasible**

In `src/infra/material_loader/material_resource_parser.cpp`, keep hard validation but change production diagnostics so they report field paths/classes instead of deleted field constants.

Replace `isLegacyRootPbrParameter` with a grouped validator that does not store the old names in reusable runtime constants:

```cpp
[[nodiscard]] bool isBlockedRootMaterialTruth(std::string_view name) {
  return name == "baseColor" || name == "baseColorFactor" ||
         name == "metallic" || name == "metallicFactor" ||
         name == "roughness" || name == "roughnessFactor" || name == "ao";
}
```

This task may still leave old strings in this parser until the static audit task is made green. Record them as Material Contract Boundary failures.

- [ ] **Step 3: Disconnect `ParameterBuffer` from default surface material truth**

In `src/core/asset/material_instance.hpp`, remove public default material APIs that expose shader-binding parameters:

```cpp
void setParameter(StringID bindingName, StringID memberName, float value);
void setParameter(StringID bindingName, StringID memberName, i32 value);
void setParameter(StringID bindingName, StringID memberName,
                  const Vec3f &value);
void setParameter(StringID bindingName, StringID memberName,
                  const Vec4f &value);
std::optional<MaterialParameterValue>
readParameterValue(StringID bindingName, StringID memberName) const;
usize getParameterBufferCount() const;
const std::vector<u8> &getParameterBufferBytes(StringID bindingName) const;
const ShaderResourceBinding &
getParameterBufferLayout(StringID bindingName) const;
```

If post-process code still needs parameter buffers in this implementation cycle, move those APIs to a non-surface helper or keep them behind names that the audit excludes from default material truth. Do not keep them as generic `MaterialInstance` surface-material APIs.

- [ ] **Step 4: Update tests to use Material v2 envelopes**

In tests that currently do:

```cpp
material->setParameter(StringID("MaterialUBO"), StringID("baseColor"),
                       Vec4f{0.8f, 0.2f, 0.1f, 1.0f});
```

replace with a Material v2 envelope instance built through the existing parser or envelope setters. Use parser text for new tests:

```cpp
const std::string materialText = R"(
schema: lxe.material.v2
name: test.matte
bsdf:
  type: matte
  parameters:
    Kd:
      kind: rgb
      value: [0.8, 0.2, 0.1]
)";
```

- [ ] **Step 5: Verify loader/parser tests**

Run:

```bash
cmake --build build --target test_material_v2_parser test_material_v2_resource_dependencies test_default_material_asset_audit
ctest --test-dir build --output-on-failure -R "test_material_v2_parser|test_material_v2_resource_dependencies|test_default_material_asset_audit"
```

Expected: tests pass or fail only on diagnostics that still spell deleted old fields. Fix those diagnostics in this task.

- [ ] **Step 6: Commit material contract hard cut**

Run:

```bash
git add src/infra/material_loader/generic_material_loader.cpp \
  src/infra/material_loader/material_resource_parser.cpp \
  src/core/asset/material_instance.hpp src/core/asset/material_instance.cpp \
  src/test/integration/test_material_v2_parser.cpp \
  src/test/integration/test_material_v2_resource_dependencies.cpp \
  src/test/integration/test_default_material_asset_audit.cpp \
  src/test/integration/test_scene_resource_table.cpp \
  src/test/integration/test_gltf_scene_asset_loader.cpp
git commit -m "Make material runtime truth v2 only"
```

Expected: commit removes default material-local parameter truth without adding compatibility branches.

## Task 4: Remove MaterialUBO Shader And GPU Record Fallbacks

**Files:**
- Modify: `assets/shaders/glsl/techniques/Forward/pbr.frag`
- Modify: `assets/shaders/glsl/techniques/Forward/pbr_clearcoat.frag`
- Modify: `assets/shaders/glsl/techniques/Deferred/pbr_gbuffer.frag`
- Modify: `assets/shaders/glsl/techniques/Deferred/pbr_clearcoat_gbuffer.frag`
- Modify: `src/core/scene/scene_gpu_records.cpp`
- Modify: `src/test/integration/test_shader_compiler.cpp`
- Modify: `src/test/integration/test_scene_resource_table.cpp`

- [ ] **Step 1: Remove CPU fallback reads**

In `src/core/scene/scene_gpu_records.cpp`, delete:

```cpp
template <usize BindingCount, usize MemberCount>
std::optional<MaterialParameterValue> readFirstMaterialParameter(
    const MaterialInstance &material,
    const std::array<const char *, BindingCount> &bindingNames,
    const std::array<const char *, MemberCount> &memberNames);
Vec4f materialValueAsColor(const MaterialParameterValue &value,
                           Vec4f fallback);
f32 materialValueAsFloat(const MaterialParameterValue &value, f32 fallback);
```

Then reduce `toGpuMaterialRecord` to:

```cpp
SceneGpuMaterialRecord toGpuMaterialRecord(const MaterialInstance &material) {
  SceneGpuMaterialRecord record;
  const auto renderState = material.getPassRenderState(Pass_OfflineRayTrace);
  record.flags = (record.flags & ~kSceneGpuMaterialCullModeMask) |
                 materialCullModeAsGpuFlag(renderState.cullMode);
  (void)applyMaterialV2EnvelopeRecord(material, record);
  return record;
}
```

If `getPassRenderState(Pass_OfflineRayTrace)` has already been removed by the material hard cut, replace the cull mode source with the new envelope/render-class cull metadata introduced by that task.

- [ ] **Step 2: Rewrite PBR shaders to read global material record**

In each production PBR fragment shader, remove the block:

```glsl
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
} material;
```

Replace direct material field reads with the fixed ABI material record already used by current global scene data. If the GLSL common include is not yet present, add a minimal local shape to keep the shader compiling during this task:

```glsl
layout(set = 0, binding = 3) readonly buffer SceneMaterials {
    vec4 baseColor[];
} sceneMaterials;
```

Then use the object/material index path available in the shader. If the index ABI is not yet wired, make this task fail fast at shader compile and record it as GPU Material/Data Boundary until Task 7 restores the minimal runtime path.

- [ ] **Step 3: Add shader audit assertions**

In `src/test/integration/test_shader_compiler.cpp`, add a text-level check for the four production shader files:

```cpp
EXPECT(shaderText.find("MaterialUBO") == std::string::npos,
       "production PBR shader must not declare MaterialUBO");
EXPECT(shaderText.find("baseColorFactor") == std::string::npos,
       "production PBR shader must not read baseColorFactor");
```

- [ ] **Step 4: Verify shader and material record tests**

Run:

```bash
cmake --build build --target test_shader_compiler test_scene_resource_table
ctest --test-dir build --output-on-failure -R "test_shader_compiler|test_scene_resource_table"
```

Expected: tests pass, or shader compile fails only because the fixed ABI include/index path is missing. Do not reintroduce `MaterialUBO` to make it pass.

- [ ] **Step 5: Commit MaterialUBO removal**

Run:

```bash
git add assets/shaders/glsl/techniques/Forward/pbr.frag \
  assets/shaders/glsl/techniques/Forward/pbr_clearcoat.frag \
  assets/shaders/glsl/techniques/Deferred/pbr_gbuffer.frag \
  assets/shaders/glsl/techniques/Deferred/pbr_clearcoat_gbuffer.frag \
  src/core/scene/scene_gpu_records.cpp \
  src/test/integration/test_shader_compiler.cpp \
  src/test/integration/test_scene_resource_table.cpp
git commit -m "Remove MaterialUBO from production material path"
```

Expected: no production shader contains `MaterialUBO`.

## Task 5: Remove Built-In Default Render Graph

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/core/frame_graph/frame_graph_build_plan.cpp`
- Modify: `src/core/frame_graph/frame_graph_build_plan.hpp`
- Modify: `src/test/integration/test_frame_graph.cpp`
- Modify: `src/test/integration/test_technique_pass_contract.cpp`

- [ ] **Step 1: Delete the built-in graph helper**

In `src/backend/vulkan/vulkan_realtime_renderer.cpp`, delete the anonymous namespace function:

```cpp
LX_core::RenderPathGraph makeDefaultForwardRenderPathGraph() {
  LX_core::RenderPathGraph graph;
  graph.name = "builtin.forward";
  graph.passes.push_back(LX_core::RenderPassNode{
      LX_core::Pass_Forward, "techniques/Forward/pbr",
      LX_core::ShaderStageKind::Raster, LX_core::DispatchKind::DrawIndexed});
  return graph;
}
```

- [ ] **Step 2: Remove fallback caller**

Delete the call:

```cpp
LX_core::buildFrameGraphFromRenderPathGraph(
    makeDefaultForwardRenderPathGraph(),
    swapchainDesc)
```

Replace it with active graph lookup from scene/resource state. The initial hard-cut replacement may be:

```cpp
if (!m_activeRenderPathGraph.has_value()) {
  throw std::logic_error(
      "active RenderPathGraph resource is required for realtime renderer");
}
m_frameGraph =
    LX_core::buildFrameGraphFromRenderPathGraph(*m_activeRenderPathGraph,
                                                swapchainDesc);
```

Use the actual renderer member names in the file. If no active graph member exists, add one at the narrowest renderer-owner scope and populate it from the loaded scene/resource path in Task 7.

- [ ] **Step 3: Remove hardcoded pass insertion for migrated default path**

Search the default scene initialization path:

```bash
rg -n "m_frameGraph\\.addPass|Pass_Forward|Pass_Deferred|Pass_PostProcess|Pass_DebugOverlay|addFullscreenMaterialItem" src/backend/vulkan/vulkan_realtime_renderer.cpp
```

Delete default `m_frameGraph.addPass` calls that construct Forward,
Deferred, PostProcess, DebugOverlay, Bloom, and fullscreen material passes for
normal rendering. Keep only explicitly non-migrated debug code if it is
unreachable from default validation and does not use forbidden static audit
tokens.

- [ ] **Step 4: Add graph-required tests**

In `src/test/integration/test_frame_graph.cpp`, add or update tests so a `RenderPathGraph` with a pass id such as `StringID("ForwardOpaque")` produces a `FramePass` with that same id and no dependency on `Pass_Forward`.

Use this assertion shape:

```cpp
const auto graph = buildFrameGraphFromRenderPathGraph(renderPathGraph, target);
EXPECT(graph.getPasses().size() == 1, "graph should build one declared pass");
EXPECT(graph.getPasses()[0].name == StringID("ForwardOpaque"),
       "FrameGraph pass identity comes from RenderPathGraph pass id");
```

- [ ] **Step 5: Verify graph tests**

Run:

```bash
cmake --build build --target test_frame_graph test_technique_pass_contract
ctest --test-dir build --output-on-failure -R "test_frame_graph|test_technique_pass_contract"
```

Expected: graph tests pass. Renderer build may still fail until active graph resource wiring is completed in Task 7; record this as Render Graph Boundary if so.

- [ ] **Step 6: Commit graph hard cut**

Run:

```bash
git add src/backend/vulkan/vulkan_realtime_renderer.cpp \
  src/core/frame_graph/frame_graph_build_plan.cpp \
  src/core/frame_graph/frame_graph_build_plan.hpp \
  src/test/integration/test_frame_graph.cpp \
  src/test/integration/test_technique_pass_contract.cpp
git commit -m "Require RenderPathGraph for default renderer passes"
```

Expected: commit removes `makeDefaultForwardRenderPathGraph`.

## Task 6: Remove LegacyPerItem And PerDrawData Submission

**Files:**
- Modify: `src/core/frame_graph/render_validation_contract.hpp`
- Modify: `src/core/frame_graph/render_validation_contract.cpp`
- Modify: `src/core/frame_graph/render_queue.hpp`
- Modify: `src/core/frame_graph/render_queue.cpp`
- Modify: `src/core/frame_graph/render_upload_plan.hpp`
- Modify: `src/core/frame_graph/render_upload_plan.cpp`
- Modify: `src/core/scene/object.hpp`
- Modify: `src/core/scene/object.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.hpp`
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/test/integration/test_071_bridge_audit.cpp`
- Modify: `src/test/integration/test_071g_legacy_boundary_removal.cpp`

- [ ] **Step 1: Remove LegacyPerItem enum**

In `src/core/frame_graph/render_validation_contract.hpp`, change:

```cpp
enum class BindlessSubmissionDecisionKind {
  Empty,
  BindlessBatch,
  LegacyPerItem,
  StrictValidationRejected,
};
```

to:

```cpp
enum class BindlessSubmissionDecisionKind {
  Empty,
  BindlessBatch,
  StrictValidationRejected,
};
```

- [ ] **Step 2: Make incomplete non-empty migrated queue reject**

In `src/core/frame_graph/render_validation_contract.cpp`, replace the end of `decideBindlessSubmission` with:

```cpp
if (decision.validation.ok &&
    decision.validation.coveredItemCount == items.size()) {
  decision.kind = BindlessSubmissionDecisionKind::BindlessBatch;
  return decision;
}

decision.kind = BindlessSubmissionDecisionKind::StrictValidationRejected;
return decision;
```

Remove any branch that returns `LegacyPerItem`.

- [ ] **Step 3: Remove per-draw data from render work structures**

In `src/core/scene/object.hpp`, remove:

```cpp
struct PerDrawData {
  using SharedPtr = std::shared_ptr<PerDrawData>;
  alignas(16) u8 data[128] = {0};
  u32 activeSize = sizeof(PerDrawLayoutBase);
  const void *rawData() const { return data; }
  u32 byteSize() const { return activeSize; }
};
using PerDrawDataSharedPtr = PerDrawData::SharedPtr;
PerDrawDataSharedPtr drawData;
virtual PerDrawDataSharedPtr getPerDrawData() const { return nullptr; }
PerDrawDataSharedPtr getPerDrawData() const override;
PerDrawDataSharedPtr m_perDrawData;
```

From `ValidatedRenderablePassData`, remove:

```cpp
PerDrawDataSharedPtr drawData;
```

- [ ] **Step 4: Remove per-draw copy into RenderWorkItem**

In `src/core/frame_graph/render_queue.cpp`, remove:

```cpp
item.raster.drawData = data.drawData;
```

and remove `item.raster.drawData` from `compileIndirectBatches` rejection checks.

- [ ] **Step 5: Remove upload-plan push constants**

In `src/core/frame_graph/render_upload_plan.hpp`, delete:

```cpp
std::vector<PerDrawDataSharedPtr> pushConstants;
```

In `src/core/frame_graph/render_upload_plan.cpp`, delete helper functions that collect unique `PerDrawData` and remove push-constant collection from plan build.

- [ ] **Step 6: Remove command-buffer default push constants**

In `src/backend/vulkan/details/commands/command_buffer.cpp`, delete the block:

```cpp
if (raster.drawData && m_pushConstants.size > 0) {
  vkCmdPushConstants(m_commandBuffer, m_pipelineLayout,
                     m_pushConstants.stageFlags, m_pushConstants.offset,
                     raster.drawData->byteSize(),
                     raster.drawData->rawData());
}
```

Do not replace it with another per-draw CPU byte path.

- [ ] **Step 7: Remove renderer per-item fallback**

In `src/backend/vulkan/vulkan_realtime_renderer.cpp`, in `drawPassQueue`, remove the fallback loop:

```cpp
for (const auto &item : queue.getItems()) {
  cmd.executeWorkItem(item);
}
```

After `decideBindlessSubmission`, handle only:

```cpp
if (decision.kind == LX_core::BindlessSubmissionDecisionKind::Empty) {
  return;
}
if (decision.kind == LX_core::BindlessSubmissionDecisionKind::StrictValidationRejected) {
  throw std::logic_error(formatBindlessDecisionDiagnostic(decision));
}
if (decision.kind == LX_core::BindlessSubmissionDecisionKind::BindlessBatch) {
  const auto batches = queue.compileIndirectBatches();
  for (const auto &batch : batches) {
    RenderWorkItem batchItem = queue.getItems()[batch.sourceItemIndices.front()];
    cmd.executeWorkItem(batchItem);
  }
}
```

Use existing local diagnostic/log style if present.

- [ ] **Step 8: Update bridge audit**

In `src/test/integration/test_071_bridge_audit.cpp`, remove construction of `PerDrawData` and replace the rejected case with an incomplete batch reason that does not rely on `raster.drawData`, such as missing index buffer:

```cpp
RenderWorkItem item = makeDefaultPathDraw(vertex, index, camera, material, 0);
item.raster.indexBuffer = {};
queue.addItem(std::move(item));
```

Expect:

```cpp
decision.kind == BindlessSubmissionDecisionKind::StrictValidationRejected
```

- [ ] **Step 9: Verify submission tests**

Run:

```bash
cmake --build build --target test_071_bridge_audit test_bindless_validation_contract test_071g_legacy_boundary_removal
ctest --test-dir build --output-on-failure -R "test_071_bridge_audit|test_bindless_validation_contract|test_071g_legacy_boundary_removal"
```

Expected: no compile references remain to `LegacyPerItem`, `PerDrawData`, or `raster.drawData` in production paths.

- [ ] **Step 10: Commit submission hard cut**

Run:

```bash
git add src/core/frame_graph/render_validation_contract.hpp \
  src/core/frame_graph/render_validation_contract.cpp \
  src/core/frame_graph/render_queue.hpp \
  src/core/frame_graph/render_queue.cpp \
  src/core/frame_graph/render_upload_plan.hpp \
  src/core/frame_graph/render_upload_plan.cpp \
  src/core/scene/object.hpp src/core/scene/object.cpp \
  src/backend/vulkan/details/commands/command_buffer.cpp \
  src/backend/vulkan/details/commands/command_buffer.hpp \
  src/backend/vulkan/vulkan_realtime_renderer.cpp \
  src/test/integration/test_071_bridge_audit.cpp \
  src/test/integration/test_071g_legacy_boundary_removal.cpp
git commit -m "Remove legacy per-item and per-draw submission"
```

Expected: commit removes `LegacyPerItem` and default per-draw data.

## Task 7: Restore Minimal Default RenderPathGraph Runtime Path

**Files:**
- Modify: `src/backend/vulkan/vulkan_realtime_renderer.cpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Modify: `src/test/integration/test_071g_legacy_boundary_removal.cpp`
- Modify or create minimal assets under `assets/render_paths/` and `assets/materials/` if missing.

- [ ] **Step 1: Confirm default graph asset exists**

Run:

```bash
test -f assets/render_paths/forward_main.render-path.yaml
sed -n '1,220p' assets/render_paths/forward_main.render-path.yaml
```

Expected: graph declares at least one pass with id, shader, source/target, renderState, and filters. If any required field is missing, add it to the asset in this task.

- [ ] **Step 2: Add or expose active graph lookup**

Add a narrow API to scene/resource state if one does not already exist:

```cpp
std::optional<std::reference_wrapper<const RenderPathGraph>>
SceneResourceTable::findActiveRenderPathGraph() const;
```

Implementation should return the graph selected by current scene/camera/profile resource state. If no selection exists yet, wire the default validation scene to explicitly load `assets/render_paths/forward_main.render-path.yaml` and make missing active graph fatal.

- [ ] **Step 3: Wire renderer to active graph only**

In `src/backend/vulkan/vulkan_realtime_renderer.cpp`, initialize the FrameGraph through the active graph:

```cpp
const auto graph = m_sceneResourceTable.findActiveRenderPathGraph();
if (!graph.has_value()) {
  throw std::logic_error("default rendering requires active RenderPathGraph");
}
m_frameGraph = LX_core::buildFrameGraphFromRenderPathGraph(
    graph->get(), swapchainDesc);
```

Use actual renderer member names. Do not add a code-built default graph.

- [ ] **Step 4: Add runtime audit checks**

Extend `src/test/integration/test_071g_legacy_boundary_removal.cpp` with headless checks that do not require a video device:

```cpp
void testBindlessDecisionRejectsIncompleteMigratedQueue() {
  RenderWorkQueue queue;
  RenderWorkItem item;
  item.kind = RenderWorkKind::RasterDraw;
  item.pass = StringID("ForwardOpaque");
  queue.addItem(std::move(item));
  const auto decision =
      decideBindlessSubmission(queue, StringID("ForwardOpaque"), true, true);
  EXPECT(decision.kind ==
             BindlessSubmissionDecisionKind::StrictValidationRejected,
         "incomplete migrated queue must fail instead of fallback submit");
}
```

Call it from `main()`.

- [ ] **Step 5: Verify runtime path build**

Run:

```bash
cmake --build build --target lxe_editor test_071g_legacy_boundary_removal
./build/src/test/test_071g_legacy_boundary_removal
```

Expected: `test_071g_legacy_boundary_removal` may still fail static forbidden-token scan until Task 8, but the runtime incomplete queue check must pass.

- [ ] **Step 6: Commit minimal graph runtime path**

Run:

```bash
git add src/backend/vulkan/vulkan_realtime_renderer.cpp \
  src/core/scene/scene_resource_table.hpp \
  src/core/scene/scene_resource_table.cpp \
  src/core/scene/scene_resource_table_upload_view.hpp \
  src/test/integration/test_071g_legacy_boundary_removal.cpp \
  assets/render_paths/forward_main.render-path.yaml
git commit -m "Restore default runtime through RenderPathGraph"
```

Expected: commit contains no built-in default graph.

## Task 8: Make Static Audit Green

**Files:**
- Modify: any production/runtime files still reported by `test_071g_legacy_boundary_removal`
- Modify: `src/test/integration/test_071g_legacy_boundary_removal.cpp` only if an allowlist is needed for a true negative fixture under `src/test/integration/`

- [ ] **Step 1: Run the audit**

Run:

```bash
cmake --build build --target test_071g_legacy_boundary_removal
./build/src/test/test_071g_legacy_boundary_removal
```

Expected: failure list names remaining forbidden tokens.

- [ ] **Step 2: Remove remaining production forbidden tokens**

For each reported production/runtime file, remove the token by deleting the old path or renaming the concept to the new contract. Do not add allowlist entries for production code.

Examples:

```bash
rg -n "MaterialUBO|materialTag|LegacyPerItem|PerDrawData|raster\\.drawData|makeDefaultForwardRenderPathGraph" \
  src/core src/infra src/backend src/demos/lxe_editor assets data src/tools/lxe_pbrt_scene_convert
```

Expected after edits: no output in production/default paths.

- [ ] **Step 3: Keep negative fixtures explicit**

If a test fixture must contain an old field to prove rejection, keep it under `src/test/integration/` and ensure the static audit does not scan that file. Do not put old field strings in production error text.

- [ ] **Step 4: Verify audit passes**

Run:

```bash
./build/src/test/test_071g_legacy_boundary_removal
```

Expected: exit code 0.

- [ ] **Step 5: Commit audit cleanup**

Run:

```bash
git add src/core src/infra src/backend src/demos/lxe_editor \
  assets/materials assets/shaders assets/scenes assets/render_paths assets/effects \
  data/scenes src/tools/lxe_pbrt_scene_convert \
  src/test/integration/test_071g_legacy_boundary_removal.cpp
git status --short
```

If the `git add` command above stages unrelated files, unstage them with:

```bash
git restore --staged <unrelated-path>
```

Then commit:

```bash
git commit -m "Satisfy 071g static boundary audit"
```

Expected: commit only removes remaining forbidden production/default tokens.

## Task 9: Final Verification And Documentation Status

**Files:**
- Modify: `notes/requirements/071-g-legacy-boundary-removal-audit.md`
- Modify: `docs/superpowers/plans/2026-06-11-071g-legacy-boundary-removal-audit.md` only to check off completed steps if execution happens in-place.

- [ ] **Step 1: Run full headless build/test gate**

Run:

```bash
cmake --build build --target BuildTest
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
```

Expected: headless tests pass. If failures remain, classify each first failure under Material Contract Boundary, Scene Identity Boundary, Render Graph Boundary, GPU Material/Data Boundary, or Submission Boundary and fix it before continuing.

- [ ] **Step 2: Run video-device tests if environment supports it**

Run:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

Expected: video tests pass. If the environment lacks Vulkan/SDL support, record the exact failure in the requirement implementation status.

- [ ] **Step 3: Run final forbidden-symbol check**

Run:

```bash
rg -n "defaultTechnique|MaterialUBO|baseColorFactor|metallicFactor|roughnessFactor|materialTag|setActiveMaterialTag|activeMaterialTag|BindlessSubmissionDecisionKind::LegacyPerItem|LegacyPerItem|raster\\.drawData|PerDrawData|makeDefaultForwardRenderPathGraph" \
  src/core src/infra src/backend src/demos/lxe_editor \
  assets/materials assets/shaders assets/scenes assets/render_paths assets/effects \
  data/scenes src/tools/lxe_pbrt_scene_convert
```

Expected: no output in production/default paths.

- [ ] **Step 4: Update requirement implementation status**

In `notes/requirements/071-g-legacy-boundary-removal-audit.md`, replace:

```markdown
未实施。
```

with an implementation summary that includes:

```markdown
已实施。默认/runtime 路径已删除 legacy material-local technique、materialTag、
MaterialUBO、非 bindless per-item fallback 和 PerDrawData 默认提交路径。
`test_071g_legacy_boundary_removal` 覆盖静态边界审计和最小运行时提交决策审计。

验证：
- `cmake --build build --target BuildTest`
- `ctest --test-dir build --output-on-failure -L auto -LE requires_video_device`
- `xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device`
```

If a verification command could not run for environment reasons, include the exact command and failure instead of claiming it passed.

- [ ] **Step 5: Commit final status**

Run:

```bash
git add notes/requirements/071-g-legacy-boundary-removal-audit.md
git commit -m "Mark 071g legacy boundary removal implemented"
```

Expected: commit only contains the requirement status update.

## Self-Review Checklist

- Spec coverage:
  - Material Contract Boundary: Task 3.
  - Scene Identity Boundary: Task 2.
  - Render Graph Boundary: Task 5 and Task 7.
  - GPU Material/Data Boundary: Task 4.
  - Submission Boundary: Task 6.
  - Static audit: Task 1 and Task 8.
  - Runtime audit: Task 7.
  - Final verification/status: Task 9.
- Placeholder scan: this plan contains no placeholder markers or deferred-work steps.
- Type consistency:
  - `BindlessSubmissionDecisionKind::StrictValidationRejected` exists before and after removal.
  - `RenderWorkQueue`, `RenderWorkItem`, and `RenderPathGraph` names match existing code.
  - Any active graph accessor added in Task 7 must use actual `SceneResourceTable` names if equivalent APIs already exist.
