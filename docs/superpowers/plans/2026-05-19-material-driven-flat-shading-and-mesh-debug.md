# Material-Driven Flat Shading And Mesh Debug Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add material-driven flat shading and mesh debug rendering without preserving a second surface/wireframe debug path.

**Architecture:** `shadingModel` and `meshOverlay` are parsed from `.material` files and translated into ordinary material/template/shader state. Flat shading uses a shader variant first; mesh debug uses a normal material and line-list mesh data, so render queue and pipeline build stay material-driven. Debug-only flags and visibility masks control whether debug objects are rendered, not how they are shaded.

**Tech Stack:** C++20, Vulkan backend, GLSL, yaml-cpp, CMake/Ninja, existing LXEngine `MaterialTemplate` / `MaterialInstance` / `RenderQueue`.

---

## File Structure

- Modify `src/core/asset/material_pass_definition.hpp`
  - Add `ShadingModel`, `MeshOverlayState`, string helpers, and include them in material pass pipeline signatures.
- Modify `src/infra/material_loader/generic_material_loader.cpp`
  - Parse top-level `shadingModel` and `meshOverlay`.
  - Translate `Flat` into `USE_FLAT_SHADING`.
  - Seed `MeshOverlayUBO.color` when the shader exposes it.
- Modify `assets/shaders/glsl/blinnphong_0.frag`
  - Add `USE_FLAT_SHADING` path using `dFdx/dFdy(vWorldPos)`.
  - Ignore normal map when flat shading is enabled.
- Create `assets/shaders/glsl/mesh_debug.vert`
  - Minimal world/camera transform for line-list debug geometry.
- Create `assets/shaders/glsl/mesh_debug.frag`
  - Unlit fixed-color output from `MeshOverlayUBO.color`.
- Create `assets/materials/mesh_debug.material`
  - Ordinary material asset with `shadingModel: Flat` and `meshOverlay.enabled: true`.
- Modify `src/core/asset/mesh.hpp`
  - Add an explicit helper entry point for edge-line mesh derivation.
- Modify `src/demos/lxe_editor/scene_builder.cpp`
  - Add a helper that can build a line-list mesh from an existing triangle mesh for debug material assignment.
- Modify `src/test/integration/test_generic_material_loader.cpp`
  - Add loader tests for `shadingModel`, invalid values, and `meshOverlay`.
- Modify `src/test/integration/test_shader_compiler.cpp`
  - Add shader compile/reflection tests for flat and mesh debug shaders.
- Modify `src/test/integration/test_scene_runtime.cpp`
  - Add ordinary render queue / pipeline signature coverage for debug material line topology.
- Create `notes/temp/material-debug-unification-gaps.md` only when a non-unified path is found
  - Only if implementation discovers a non-unified path.

## Task 1: Material Schema And Pipeline Identity

**Files:**
- Modify: `src/core/asset/material_pass_definition.hpp`
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Test: `src/test/integration/test_generic_material_loader.cpp`

- [ ] **Step 1: Write failing tests for shadingModel parsing**

Append this helper near the other local helpers in `src/test/integration/test_generic_material_loader.cpp`:

```cpp
bool passHasEnabledVariant(const MaterialInstanceSharedPtr &mat,
                           StringID pass,
                           const std::string &macroName) {
  const auto tmpl = mat ? mat->getTemplate() : nullptr;
  if (!tmpl) {
    return false;
  }
  const auto passDef = tmpl->getPassDefinition(pass);
  if (!passDef.has_value()) {
    return false;
  }
  for (const auto &variant : passDef->get().shaderProgram.variants) {
    if (variant.macroName == macroName) {
      return variant.enabled;
    }
  }
  return false;
}
```

Add this test function:

```cpp
void test_flat_shading_model_enables_variant() {
  std::cout << "\n-- test_flat_shading_model_enables_variant --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  const auto matPath = root / "assets" / "materials" /
                       "test_flat_shading_model.material";
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n"
           "shadingModel: Flat\n"
           "parameters:\n"
           "  MaterialUBO.baseColor: [0.7, 0.7, 0.7]\n"
           "  MaterialUBO.shininess: 12.0\n"
           "  MaterialUBO.specularIntensity: 1.0\n"
           "  MaterialUBO.enableAlbedo: 0\n"
           "  MaterialUBO.enableNormal: 0\n"
           "  MaterialUBO.debugShadowMode: 0\n";
  }

  auto prev = fs::current_path();
  fs::current_path(root);
  auto mat = loadGenericMaterial(matPath);
  fs::current_path(prev);
  fs::remove(matPath);

  REQUIRE(mat != nullptr);
  REQUIRE(passHasEnabledVariant(mat, Pass_Forward, "USE_FLAT_SHADING"));

  std::cout << "  shadingModel Flat enables USE_FLAT_SHADING\n";
}
```

Add this test function:

```cpp
void test_default_shading_model_stays_smooth() {
  std::cout << "\n-- test_default_shading_model_stays_smooth --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto prev = fs::current_path();
  fs::current_path(root);
  auto mat = loadGenericMaterial(root / "assets" / "materials" /
                                 "blinnphong_lit.material");
  fs::current_path(prev);

  REQUIRE(mat != nullptr);
  REQUIRE(!passHasEnabledVariant(mat, Pass_Forward, "USE_FLAT_SHADING"));

  std::cout << "  missing shadingModel defaults to Smooth\n";
}
```

Add this test function:

```cpp
void test_invalid_shading_model_rejected() {
  std::cout << "\n-- test_invalid_shading_model_rejected --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  const auto matPath = root / "assets" / "materials" /
                       "test_invalid_shading_model.material";
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n"
           "shadingModel: Banana\n";
  }

  bool rejected = false;
  try {
    auto prev = fs::current_path();
    fs::current_path(root);
    (void)loadGenericMaterial(matPath);
    fs::current_path(prev);
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("unknown shadingModel") !=
               std::string::npos;
  }
  fs::remove(matPath);

  REQUIRE(rejected);
  std::cout << "  invalid shadingModel rejected\n";
}
```

Call the three tests from `main()` after `test_generic_loader_produces_valid_instance()`.

- [ ] **Step 2: Run the tests to verify they fail**

Run:

```bash
cmake --build build --target test_generic_material_loader -j2
./build/src/test/test_generic_material_loader
```

Expected: build succeeds, test binary fails because `shadingModel: Flat` does not enable `USE_FLAT_SHADING` or invalid values are accepted.

- [ ] **Step 3: Add material schema types**

In `src/core/asset/material_pass_definition.hpp`, add these enums and structs after `BlendFactor`:

```cpp
enum class ShadingModel : u8 { Smooth, Flat };

struct MeshOverlayState {
  bool enabled = false;
  Vec4f color{0.0f, 0.0f, 0.0f, 1.0f};

  bool operator==(const MeshOverlayState &rhs) const {
    return enabled == rhs.enabled && color.x == rhs.color.x &&
           color.y == rhs.color.y && color.z == rhs.color.z &&
           color.w == rhs.color.w;
  }
};
```

Add `#include "core/math/vec.hpp"` to the same header.

Add string helpers:

```cpp
inline const char *toString(ShadingModel model) {
  switch (model) {
  case ShadingModel::Smooth:
    return "Smooth";
  case ShadingModel::Flat:
    return "Flat";
  }
  return "ShadingUnknown";
}
```

Add fields to `MaterialPassDefinition`:

```cpp
ShadingModel shadingModel = ShadingModel::Smooth;
MeshOverlayState meshOverlay;
```

Update `MaterialPassDefinition::getPipelineSignature()` so the fields array includes shading model and overlay state:

```cpp
StringID fields[] = {
    shaderProgram.getPipelineSignature(),
    renderState.getPipelineSignature(),
    GlobalStringTable::get().Intern(toString(shadingModel)),
    GlobalStringTable::get().Intern(meshOverlay.enabled ? "MeshOverlay"
                                                        : "NoMeshOverlay"),
};
```

- [ ] **Step 4: Parse shadingModel and translate it into variants**

In `src/infra/material_loader/generic_material_loader.cpp`, add:

```cpp
LX_core::ShadingModel parseShadingModel(const YAML::Node &node) {
  if (!node || !node.IsDefined()) {
    return LX_core::ShadingModel::Smooth;
  }
  const auto value = node.as<std::string>();
  if (value == "Smooth") {
    return LX_core::ShadingModel::Smooth;
  }
  if (value == "Flat") {
    return LX_core::ShadingModel::Flat;
  }
  fatalLoader("unknown shadingModel '" + value + "'");
}

void upsertVariant(std::vector<LX_core::ShaderVariant> &variants,
                   const std::string &macroName, bool enabled) {
  for (auto &variant : variants) {
    if (variant.macroName == macroName) {
      variant.enabled = enabled;
      return;
    }
  }
  variants.push_back(LX_core::ShaderVariant{macroName, enabled});
}

void applyShadingModelVariants(std::vector<LX_core::ShaderVariant> &variants,
                               LX_core::ShadingModel shadingModel) {
  if (shadingModel == LX_core::ShadingModel::Flat) {
    upsertVariant(variants, "USE_FLAT_SHADING", true);
  }
}
```

Extend `CompiledPass`:

```cpp
LX_core::ShadingModel shadingModel = LX_core::ShadingModel::Smooth;
LX_core::MeshOverlayState meshOverlay;
```

In the top-level root iteration, capture:

```cpp
YAML::Node shadingModelNode;
YAML::Node meshOverlayNode;
```

and:

```cpp
else if (key == "shadingModel")
  shadingModelNode = YAML::Clone(it->second);
else if (key == "meshOverlay")
  meshOverlayNode = YAML::Clone(it->second);
```

Before compiling passes:

```cpp
const auto globalShadingModel = parseShadingModel(shadingModelNode);
```

After each `mergeVariants(...)`, call:

```cpp
applyShadingModelVariants(variants, globalShadingModel);
```

After `compilePassShader(...)`, set:

```cpp
cp.shadingModel = globalShadingModel;
```

When building each `MaterialPassDefinition`, set:

```cpp
entry.shadingModel = cp.shadingModel;
entry.meshOverlay = cp.meshOverlay;
```

- [ ] **Step 5: Run loader tests**

Run:

```bash
cmake --build build --target test_generic_material_loader -j2
./build/src/test/test_generic_material_loader
```

Expected: all `test_generic_material_loader` tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/core/asset/material_pass_definition.hpp src/infra/material_loader/generic_material_loader.cpp src/test/integration/test_generic_material_loader.cpp
git commit -m "add material shading model parsing"
```

## Task 2: Flat Shading Shader Variant

**Files:**
- Modify: `assets/shaders/glsl/blinnphong_0.frag`
- Test: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add failing shader contract test**

In `src/test/integration/test_shader_compiler.cpp`, add this function near the other BlinnPhong contract tests:

```cpp
static bool testBlinnPhongFlatShadingVariant(
    const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: BlinnPhong flat shading variant\n";
  std::cout << "========================================\n";

  const auto vertPath = shaderDir / "blinnphong_0.vert";
  const auto fragPath = shaderDir / "blinnphong_0.frag";
  std::vector<ShaderVariant> variants = {
      ShaderVariant{"USE_LIGHTING", true},
      ShaderVariant{"USE_FLAT_SHADING", true},
  };
  auto compileResult = ShaderCompiler::compileProgram(vertPath, fragPath,
                                                      variants);
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto materialIt =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "MaterialUBO";
      });
  if (materialIt == bindings.end()) {
    std::cerr << "  FAIL: MaterialUBO missing in flat variant\n";
    return false;
  }

  std::cout << "  PASS: BlinnPhong flat shading variant compiles\n";
  return true;
}
```

Call it from `main()`:

```cpp
if (!testBlinnPhongFlatShadingVariant(shaderDir))
  ++failures;
```

- [ ] **Step 2: Run the test to verify current behavior**

Run:

```bash
cmake --build build --target test_shader_compiler -j2
./build/src/test/test_shader_compiler
```

Expected: the test can compile before implementation because unused macros are legal. Treat this as a weak RED. Continue to Step 3 and rely on code review plus runtime verification in subsequent tasks.

- [ ] **Step 3: Implement flat normal path**

In `assets/shaders/glsl/blinnphong_0.frag`, replace the normal setup block that currently chooses normal-map or `vWorldNormal` with this shape:

```glsl
vec3 computeSmoothNormal() {
#ifdef USE_NORMAL_MAP
    mat3 tbn = vTBN;
    tbn[0] = normalize(tbn[0]);
    tbn[1] = normalize(tbn[1]);
    tbn[2] = normalize(tbn[2]);
    if (material.enableNormal != 0) {
        vec3 normalSample = texture(normalMap, vUV).rgb * 2.0 - 1.0;
        return normalize(tbn * normalSample);
    }
#endif
    return normalize(vWorldNormal);
}

vec3 computeFlatNormal() {
    vec3 dx = dFdx(vWorldPos);
    vec3 dy = dFdy(vWorldPos);
    return normalize(cross(dx, dy));
}
```

Then in `main()`, use:

```glsl
#ifdef USE_FLAT_SHADING
    vec3 N = computeFlatNormal();
#else
    vec3 N = computeSmoothNormal();
#endif
```

Keep the rest of the lighting path unchanged.

- [ ] **Step 4: Compile shaders**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler -j2
./build/src/test/test_shader_compiler
glslc -DUSE_LIGHTING=1 -DUSE_FLAT_SHADING=1 -o /tmp/blinnphong_flat.frag.spv assets/shaders/glsl/blinnphong_0.frag
```

Expected: compile succeeds and shader compiler test passes.

- [ ] **Step 5: Commit**

```bash
git add assets/shaders/glsl/blinnphong_0.frag src/test/integration/test_shader_compiler.cpp
git commit -m "add flat shading shader variant"
```

## Task 3: Mesh Overlay Material Parsing

**Files:**
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Modify: `src/test/integration/test_generic_material_loader.cpp`

- [ ] **Step 1: Add failing meshOverlay loader test**

Add this test to `src/test/integration/test_generic_material_loader.cpp`:

```cpp
void test_mesh_overlay_material_metadata_loads() {
  std::cout << "\n-- test_mesh_overlay_material_metadata_loads --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  const auto matPath = root / "assets" / "materials" /
                       "test_mesh_overlay_metadata.material";
  {
    std::ofstream out(matPath);
    out << "shader: blinnphong_0\n"
           "meshOverlay:\n"
           "  enabled: true\n"
           "  color: [0.1, 0.2, 0.3, 1.0]\n"
           "parameters:\n"
           "  MaterialUBO.baseColor: [0.7, 0.7, 0.7]\n"
           "  MaterialUBO.shininess: 12.0\n"
           "  MaterialUBO.specularIntensity: 1.0\n"
           "  MaterialUBO.enableAlbedo: 0\n"
           "  MaterialUBO.enableNormal: 0\n"
           "  MaterialUBO.debugShadowMode: 0\n";
  }

  auto prev = fs::current_path();
  fs::current_path(root);
  auto mat = loadGenericMaterial(matPath);
  fs::current_path(prev);
  fs::remove(matPath);

  REQUIRE(mat != nullptr);
  const auto passDef = mat->getTemplate()->getPassDefinition(Pass_Forward);
  REQUIRE(passDef.has_value());
  REQUIRE(passDef->get().meshOverlay.enabled);
  REQUIRE(passDef->get().meshOverlay.color.x == 0.1f);
  REQUIRE(passDef->get().meshOverlay.color.y == 0.2f);
  REQUIRE(passDef->get().meshOverlay.color.z == 0.3f);
  REQUIRE(passDef->get().meshOverlay.color.w == 1.0f);

  std::cout << "  meshOverlay metadata loads into material pass\n";
}
```

Call it from `main()`.

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build build --target test_generic_material_loader -j2
./build/src/test/test_generic_material_loader
```

Expected: fails because `meshOverlay` is not parsed or stored.

- [ ] **Step 3: Implement meshOverlay parsing**

In `src/infra/material_loader/generic_material_loader.cpp`, add:

```cpp
LX_core::MeshOverlayState parseMeshOverlay(const YAML::Node &node) {
  LX_core::MeshOverlayState state;
  if (!node || !node.IsDefined()) {
    return state;
  }
  if (!node.IsMap()) {
    fatalLoader("meshOverlay must be a map");
  }
  if (const auto enabled = node["enabled"]) {
    state.enabled = enabled.as<bool>();
  }
  if (const auto color = node["color"]) {
    const auto values = color.as<std::vector<float>>();
    if (values.size() != 4) {
      fatalLoader("meshOverlay.color requires 4 values");
    }
    state.color = LX_core::Vec4f{values[0], values[1], values[2], values[3]};
  }
  return state;
}
```

After parsing `globalShadingModel`, parse:

```cpp
const auto globalMeshOverlay = parseMeshOverlay(meshOverlayNode);
```

After compiling each pass, set:

```cpp
cp.meshOverlay = globalMeshOverlay;
```

Ensure `entry.meshOverlay = cp.meshOverlay;` remains in the template build code.

- [ ] **Step 4: Run loader tests**

Run:

```bash
cmake --build build --target test_generic_material_loader -j2
./build/src/test/test_generic_material_loader
```

Expected: all loader tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/infra/material_loader/generic_material_loader.cpp src/test/integration/test_generic_material_loader.cpp
git commit -m "parse mesh overlay material metadata"
```

## Task 4: Mesh Debug Shader And Material Asset

**Files:**
- Create: `assets/shaders/glsl/mesh_debug.vert`
- Create: `assets/shaders/glsl/mesh_debug.frag`
- Create: `assets/materials/mesh_debug.material`
- Modify: `src/test/integration/test_shader_compiler.cpp`
- Modify: `src/test/integration/test_generic_material_loader.cpp`

- [ ] **Step 1: Add failing shader compiler test**

In `src/test/integration/test_shader_compiler.cpp`, add:

```cpp
static bool testMeshDebugShaderContract(const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: Mesh debug shader contract\n";
  std::cout << "========================================\n";

  const auto vertPath = shaderDir / "mesh_debug.vert";
  const auto fragPath = shaderDir / "mesh_debug.frag";
  auto compileResult = ShaderCompiler::compileProgram(vertPath, fragPath, {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto overlayIt =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "MeshOverlayUBO";
      });
  if (overlayIt == bindings.end()) {
    std::cerr << "  FAIL: MeshOverlayUBO missing\n";
    return false;
  }
  const auto *color = findMember(*overlayIt, "color");
  if (!color || color->type != ShaderPropertyType::Vec4) {
    std::cerr << "  FAIL: MeshOverlayUBO.color Vec4 missing\n";
    return false;
  }

  std::cout << "  PASS: mesh debug shader reflects overlay color\n";
  return true;
}
```

Call it from `main()`.

- [ ] **Step 2: Add failing material loader test**

In `src/test/integration/test_generic_material_loader.cpp`, add:

```cpp
void test_mesh_debug_material_loads() {
  std::cout << "\n-- test_mesh_debug_material_loads --\n";
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }

  auto prev = fs::current_path();
  fs::current_path(root);
  auto mat = loadGenericMaterial(root / "assets" / "materials" /
                                 "mesh_debug.material");
  fs::current_path(prev);

  REQUIRE(mat != nullptr);
  REQUIRE(mat->getPassShader(Pass_Forward) != nullptr);
  REQUIRE(mat->findParameterMember(StringID("MeshOverlayUBO"),
                                   StringID("color"))
              .has_value());
  REQUIRE(passHasEnabledVariant(mat, Pass_Forward, "USE_FLAT_SHADING"));
  const auto passDef = mat->getTemplate()->getPassDefinition(Pass_Forward);
  REQUIRE(passDef.has_value());
  REQUIRE(passDef->get().meshOverlay.enabled);

  const auto color =
      mat->readParameterValue(StringID("MeshOverlayUBO"), StringID("color"));
  REQUIRE(color.has_value());
  REQUIRE(color->type == MaterialParameterValueType::Vec4);
  REQUIRE(color->vectorValue.x == 0.0f);
  REQUIRE(color->vectorValue.y == 0.0f);
  REQUIRE(color->vectorValue.z == 0.0f);
  REQUIRE(color->vectorValue.w == 1.0f);

  std::cout << "  mesh_debug.material loads through material system\n";
}
```

Call it from `main()`.

- [ ] **Step 3: Run tests to verify missing assets fail**

Run:

```bash
cmake --build build --target test_shader_compiler test_generic_material_loader -j2
./build/src/test/test_shader_compiler
./build/src/test/test_generic_material_loader
```

Expected: both tests fail because `mesh_debug` assets do not exist.

- [ ] **Step 4: Create mesh debug shader files**

Create `assets/shaders/glsl/mesh_debug.vert`:

```glsl
#version 450

layout(push_constant) uniform ObjectPC {
    mat4 model;
} object;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = camera.proj * camera.view * object.model * vec4(inPosition, 1.0);
}
```

Create `assets/shaders/glsl/mesh_debug.frag`:

```glsl
#version 450

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform MeshOverlayUBO {
    vec4 color;
} overlay;

void main() {
    outColor = overlay.color;
}
```

- [ ] **Step 5: Create mesh debug material**

Create `assets/materials/mesh_debug.material`:

```yaml
shader: mesh_debug
shadingModel: Flat
meshOverlay:
  enabled: true
  color: [0.0, 0.0, 0.0, 1.0]

passes:
  Forward:
    renderState:
      cullMode: None
      depthTest: true
      depthWrite: false

parameters:
  MeshOverlayUBO.color: [0.0, 0.0, 0.0, 1.0]
```

- [ ] **Step 6: Compile and run shader/material tests**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler test_generic_material_loader -j2
./build/src/test/test_shader_compiler
./build/src/test/test_generic_material_loader
glslc -o assets/shaders/glsl/mesh_debug.vert.spv assets/shaders/glsl/mesh_debug.vert
glslc -o assets/shaders/glsl/mesh_debug.frag.spv assets/shaders/glsl/mesh_debug.frag
```

Expected: tests pass and source-tree SPIR-V files are produced for the new shaders.

- [ ] **Step 7: Commit**

```bash
git add assets/shaders/glsl/mesh_debug.vert assets/shaders/glsl/mesh_debug.frag assets/shaders/glsl/mesh_debug.vert.spv assets/shaders/glsl/mesh_debug.frag.spv assets/materials/mesh_debug.material src/test/integration/test_shader_compiler.cpp src/test/integration/test_generic_material_loader.cpp
git commit -m "add mesh debug material"
```

## Task 5: Material-Driven Mesh Line Geometry

**Files:**
- Modify: `src/core/asset/mesh.hpp`
- Modify: `src/core/asset/mesh.cpp`
- Test: `src/test/integration/test_material_instance.cpp` or create `src/test/integration/test_mesh_debug_geometry.cpp`
- Modify: `src/test/CMakeLists.txt` if a new test binary is created

- [ ] **Step 1: Add failing edge derivation test**

Create `src/test/integration/test_mesh_debug_geometry.cpp`:

```cpp
#include "core/asset/mesh.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"

#include <iostream>
#include <vector>

using namespace LX_core;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " "        \
                << msg << " (" #cond ")\n";                                   \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testTriangleEdgesBecomeLineList() {
  auto edgeIndices =
      makeUniqueTriangleEdgeLineIndices(std::vector<u32>{0, 1, 2});
  EXPECT(edgeIndices == std::vector<u32>({0, 1, 1, 2, 0, 2}),
         "single triangle should produce three sorted edges");
}

void testSharedEdgeDeduplicates() {
  auto edgeIndices =
      makeUniqueTriangleEdgeLineIndices(std::vector<u32>{0, 1, 2, 2, 1, 3});
  EXPECT(edgeIndices == std::vector<u32>({0, 1, 1, 2, 0, 2, 1, 3, 2, 3}),
         "two triangles should share one edge once");
}

void testNonTriangleInputRejected() {
  bool rejected = false;
  try {
    (void)makeUniqueTriangleEdgeLineIndices(std::vector<u32>{0, 1});
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("multiple of 3") !=
               std::string::npos;
  }
  EXPECT(rejected, "non-triangle index count should be rejected");
}

} // namespace

int main() {
  testTriangleEdgesBecomeLineList();
  testSharedEdgeDeduplicates();
  testNonTriangleInputRejected();

  if (failures != 0) {
    std::cerr << "test_mesh_debug_geometry failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_mesh_debug_geometry passed\n";
  return 0;
}
```

Add `test_mesh_debug_geometry` to `TEST_INTEGRATION_EXE_LIST` in `src/test/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails to compile**

Run:

```bash
cmake --build build --target test_mesh_debug_geometry -j2
```

Expected: compile fails because `makeUniqueTriangleEdgeLineIndices` does not exist.

- [ ] **Step 3: Add edge derivation API**

In `src/core/asset/mesh.hpp`, declare:

```cpp
std::vector<u32>
makeUniqueTriangleEdgeLineIndices(const std::vector<u32> &triangleIndices);
```

In `src/core/asset/mesh.cpp`, implement:

```cpp
#include "core/asset/mesh.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace LX_core {
namespace {

struct EdgeKey {
  u32 a = 0;
  u32 b = 0;

  bool operator==(const EdgeKey &rhs) const {
    return a == rhs.a && b == rhs.b;
  }
};

struct EdgeKeyHash {
  usize operator()(const EdgeKey &edge) const {
    return (static_cast<usize>(edge.a) << 32U) ^ static_cast<usize>(edge.b);
  }
};

EdgeKey makeEdge(u32 lhs, u32 rhs) {
  return lhs < rhs ? EdgeKey{lhs, rhs} : EdgeKey{rhs, lhs};
}

} // namespace

std::vector<u32>
makeUniqueTriangleEdgeLineIndices(const std::vector<u32> &triangleIndices) {
  if ((triangleIndices.size() % 3U) != 0U) {
    throw std::logic_error(
        "makeUniqueTriangleEdgeLineIndices requires a multiple of 3 indices");
  }

  std::vector<EdgeKey> orderedEdges;
  std::unordered_set<EdgeKey, EdgeKeyHash> seen;
  orderedEdges.reserve(triangleIndices.size());

  const auto addEdge = [&](u32 a, u32 b) {
    const EdgeKey edge = makeEdge(a, b);
    if (seen.insert(edge).second) {
      orderedEdges.push_back(edge);
    }
  };

  for (usize i = 0; i < triangleIndices.size(); i += 3U) {
    const u32 i0 = triangleIndices[i + 0U];
    const u32 i1 = triangleIndices[i + 1U];
    const u32 i2 = triangleIndices[i + 2U];
    addEdge(i0, i1);
    addEdge(i1, i2);
    addEdge(i2, i0);
  }

  std::sort(orderedEdges.begin(), orderedEdges.end(),
            [](const EdgeKey &lhs, const EdgeKey &rhs) {
              if (lhs.a != rhs.a) {
                return lhs.a < rhs.a;
              }
              return lhs.b < rhs.b;
            });

  std::vector<u32> out;
  out.reserve(orderedEdges.size() * 2U);
  for (const auto &edge : orderedEdges) {
    out.push_back(edge.a);
    out.push_back(edge.b);
  }
  return out;
}

} // namespace LX_core
```

- [ ] **Step 4: Run geometry test**

Run:

```bash
cmake --build build --target test_mesh_debug_geometry -j2
./build/src/test/test_mesh_debug_geometry
```

Expected: test passes.

- [ ] **Step 5: Commit**

```bash
git add src/core/asset/mesh.hpp src/core/asset/mesh.cpp src/test/CMakeLists.txt src/test/integration/test_mesh_debug_geometry.cpp
git commit -m "derive mesh debug line indices"
```

## Task 6: Scene-Level Debug Material Assignment Path

**Files:**
- Modify: `src/demos/lxe_editor/scene_runtime.hpp`
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Test: `src/test/integration/test_scene_runtime.cpp`

- [ ] **Step 1: Add failing runtime test for debug material assignment**

In `src/test/integration/test_scene_runtime.cpp`, add:

```cpp
void testRuntimeCanAssignMeshDebugMaterial() {
  const std::filesystem::path inputPath =
      makeTempPath("lx_scene_runtime_mesh_debug.scene.yaml");
  writeSceneFile(
      inputPath,
      "scene:\n"
      "  name: mesh_debug\n"
      "  gameplayCameraPath: /game_cam\n"
      "nodes:\n"
      "  - nodeName: game_camera\n"
      "    name: game_cam\n"
      "    transform:\n"
      "      translation: [0.0, 2.0, 6.0]\n"
      "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
      "      scale: [1.0, 1.0, 1.0]\n"
      "    camera:\n"
      "      eye: [0.0, 2.0, 6.0]\n"
      "      target: [0.0, 0.0, 0.0]\n"
      "      up: [0.0, 1.0, 0.0]\n"
      "  - nodeName: cube_node\n"
      "    name: cube\n"
      "    mesh:\n"
      "      uri: builtin://lxe_editor/primitives/cube\n"
      "    material:\n"
      "      uri: assets/materials/blinnphong_lit.material\n");

  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(inputPath);

  const auto result = runtime.setNodeMaterialUri(
      "/cube", "assets/materials/mesh_debug.material");
  EXPECT(result.ok, "mesh_debug material should be assignable");

  const auto materialUri = runtime.nodeMaterialUri("/cube");
  EXPECT(materialUri.has_value() &&
             *materialUri == "assets/materials/mesh_debug.material",
         "debug material assignment should update node material uri");
}
```

Call it from `main()`.

- [ ] **Step 2: Run runtime test to verify it fails before material preset allowance**

Run:

```bash
cmake --build build --target test_scene_runtime -j2
./build/src/test/test_scene_runtime
```

Expected: fails if `mesh_debug.material` is not accepted by material preset discovery or runtime assignment.

- [ ] **Step 3: Ensure mesh_debug material is allowed through existing preset discovery**

In `src/demos/lxe_editor/scene_runtime.cpp`, update `isAllowedMaterialPreset` only if needed. The preferred implementation is no special case because `mesh_debug.material` should be discoverable as an asset material. If the existing discovery only accepts `rtr_*.material`, add a general material asset discovery function:

```cpp
std::vector<std::string> discoverMaterialAssetUris() {
  std::vector<std::string> out;
  const auto materialDir = resolveRuntimePath("assets/materials");
  if (!std::filesystem::exists(materialDir)) {
    return out;
  }
  for (const auto &entry : std::filesystem::directory_iterator(materialDir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".material") {
      continue;
    }
    out.push_back("assets/materials/" + entry.path().filename().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}
```

Then in `isAllowedMaterialPreset`, check `discoverMaterialAssetUris()` instead of adding a `mesh_debug` name special case:

```cpp
const auto materialUris = discoverMaterialAssetUris();
return std::find(materialUris.begin(), materialUris.end(), uri) !=
       materialUris.end();
```

- [ ] **Step 4: Run runtime test**

Run:

```bash
cmake --build build --target test_scene_runtime -j2
./build/src/test/test_scene_runtime
```

Expected: test passes.

- [ ] **Step 5: Commit**

```bash
git add src/demos/lxe_editor/scene_runtime.cpp src/demos/lxe_editor/scene_runtime.hpp src/test/integration/test_scene_runtime.cpp
git commit -m "allow assigning mesh debug material"
```

## Task 7: Unified Path Code Review And Gap Document

**Files:**
- Create if needed: `notes/temp/material-debug-unification-gaps.md`
- Inspect: `src/core/debug_draw/`
- Inspect: `src/demos/lxe_editor/`
- Inspect: `src/backend/vulkan/`
- Inspect: `src/core/frame_graph/`
- Inspect: `assets/shaders/glsl/`
- Inspect: `assets/materials/`

- [ ] **Step 1: Run code searches for forbidden patterns**

Run:

```bash
rg -n "debug material|mesh_debug|wireframe|polygonMode|VK_POLYGON_MODE_LINE|if .*debug|isDebugOnlyRenderable|debug_draw" src assets notes docs/superpowers/specs/2026-05-19-material-driven-flat-shading-and-mesh-debug-design.md
```

Expected:

- `mesh_debug` appears in material/shader/test assets and material discovery.
- `polygonMode` remains `VK_POLYGON_MODE_FILL` unless a separate optional backend feature is explicitly implemented.
- No backend code branches on `mesh_debug` material name.
- `isDebugOnlyRenderable()` appears only in filtering/visibility logic.

- [ ] **Step 2: Write gap document only if a non-unified path remains**

If Step 1 finds a material-bypassing surface or mesh-line path that cannot be removed in this implementation, create `notes/temp/material-debug-unification-gaps.md` using concrete file/function names from the search output. The document must use this structure and must not contain generic labels:

```markdown
# Material Debug Unification Gaps

## Gap 1: Legacy debug line queue remains outside material path

- Logic: `src/core/debug_draw/debug_draw.cpp::pushLine` queues debug line vertices through the existing debug draw singleton instead of a material-authored scene renderable.
- Reason: The current implementation is frame-transient and does not own a scene node or material instance.
- Risk: Long-lived mesh debug visuals could split between material-driven renderables and the frame-transient debug line path.
- Follow-up: Introduce a scene-owned debug line renderable that uses `mesh_debug.material`, then keep `debug_draw` only for temporary gizmo/selection lines.
```

If no gap exists, do not create the file.

- [ ] **Step 3: Run full scoped verification**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler test_generic_material_loader test_mesh_debug_geometry test_scene_runtime lxe_editor -j2
./build/src/test/test_shader_compiler
./build/src/test/test_generic_material_loader
./build/src/test/test_mesh_debug_geometry
./build/src/test/test_scene_runtime
```

Expected: all listed tests pass.

- [ ] **Step 4: Commit review artifacts**

If `notes/temp/material-debug-unification-gaps.md` exists:

```bash
git add notes/temp/material-debug-unification-gaps.md
git commit -m "document material debug unification gaps"
```

If no gap document exists, commit any final review-only test/doc adjustment that remains. If there are no file changes, skip the commit and record the code review result in the final implementation response.

## Plan Self-Review

- Spec coverage: Tasks 1-4 cover `shadingModel`, flat variant, `meshOverlay`, and debug material asset. Task 5 covers material-driven line-list geometry. Task 6 covers assigning debug material to real models through the scene runtime. Task 7 covers the required no-second-path code review and optional gap document.
- Red-flag scan: No incomplete markers are used. All code-creation steps include concrete snippets and commands.
- Type consistency: `ShadingModel`, `MeshOverlayState`, `USE_FLAT_SHADING`, `MeshOverlayUBO.color`, and `makeUniqueTriangleEdgeLineIndices` are introduced before subsequent tasks reference them.
