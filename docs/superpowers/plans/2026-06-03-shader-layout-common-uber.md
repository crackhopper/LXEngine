# Shader Layout Common Uber Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize shader sources into pass directories, extract shared GLSL modules, converge traditional forward descriptor ABI, and add a compile-validated PBR-capable forward Uber shader with shared SSBO scene records.

**Architecture:** Shader source layout becomes directory-based and CMake compiles shaders recursively while preserving relative output paths. Traditional PBR and Blinn-Phong stay as runtime shader families but share one forward resource ABI; the new Uber/offline path shares `common/scene` SSBO records and common material/ray helpers without enabling Vulkan bindless descriptors at runtime.

**Tech Stack:** C++20, CMake/Ninja, shaderc/glslc, GLSL 450, SPIRV-Cross reflection, Vulkan descriptor reflection tests.

---

## File Structure

- Modify `assets/shaders/CMakeLists.txt`: recursively collect shader stages and emit SPIR-V under matching relative subdirectories.
- Modify `assets/shaders/README.md`: document new shader layout and ABI.
- Modify `src/core/utils/filesystem_tools.hpp` and `src/core/utils/filesystem_tools.cpp`: make shader path helpers accept logical names with optional subdirectories.
- Modify `src/infra/material_loader/generic_material_loader.cpp`: map material shader names to categorized source paths and preserve material YAML names.
- Modify `src/backend/vulkan/vulkan_shader.cpp` or the local shader binary lookup it uses: load categorized SPIR-V paths for built-in shader names.
- Modify `src/demos/minimal_resize/main.cpp` and `src/demos/minimal_resize_baseline/main.cpp`: load `debug/minimal.*.spv`.
- Modify `src/test/integration/test_assets_layout.cpp`: assert categorized shader source and binary paths.
- Modify `src/test/integration/test_shader_compiler.cpp`: compile categorized shader paths and add Uber/offline coverage.
- Modify `src/test/integration/test_generic_material_loader.cpp`, `src/test/integration/test_material_instance.cpp`, and any path-only tests found by `rg -n "blinnphong_0\\.vert|pbr\\.vert|shadow_depth_only\\.vert|minimal\\.vert\\.spv" src assets -g '!*.spv'`: use new path helpers.
- Move shader files under `assets/shaders/glsl/{forward,shadow,post,ibl,debug,experimental,offline}`.
- Create GLSL common modules under `assets/shaders/glsl/common/{abi,geometry,lighting,material,post,ray,scene}`.
- Modify `assets/shaders/glsl/forward/blinnphong_0.vert` and `.frag`: include common modules and converge set/binding ABI.
- Modify `assets/shaders/glsl/forward/pbr.vert` and `.frag`: include common modules and use converged ABI.
- Modify `assets/shaders/glsl/offline/offline_primary_ray.comp`: include shared scene/ray/material modules.
- Create `assets/shaders/glsl/forward/uber_forward.vert` and `.frag`: compile-only PBR-capable SSBO/bindless-ready Uber shader.

---

### Task 1: Recursive Shader Build and Logical Paths

**Files:**
- Modify: `assets/shaders/CMakeLists.txt`
- Modify: `src/core/utils/filesystem_tools.hpp`
- Modify: `src/core/utils/filesystem_tools.cpp`
- Modify: `src/test/integration/test_assets_layout.cpp`

- [ ] **Step 1: Write failing asset-layout expectations for categorized shaders**

In `src/test/integration/test_assets_layout.cpp`, update source and binary checks so they expect `forward/blinnphong_0`:

```cpp
EXPECT(fs::exists(getRuntimeShaderSourceDir() / "forward" /
                  "blinnphong_0.vert"),
       "runtime root should expose categorized GLSL sources");

EXPECT(getShaderPath("forward/blinnphong_0", "vert.spv") ==
           fs::absolute(tmpRoot / "assets" / "shaders" / "glsl" /
                        "forward" / "blinnphong_0.vert.spv")
               .string(),
       "shader binary path should resolve categorized shader names");
```

Also change the packaged-root setup to create and copy categorized files:

```cpp
fs::create_directories(tmpRoot / "assets" / "shaders" / "glsl" / "forward");
fs::copy_file(repoRoot / "assets" / "shaders" / "glsl" / "forward" /
                  "blinnphong_0.vert",
              tmpRoot / "assets" / "shaders" / "glsl" / "forward" /
                  "blinnphong_0.vert",
              fs::copy_options::overwrite_existing);
fs::copy_file(repoRoot / "assets" / "shaders" / "glsl" / "forward" /
                  "blinnphong_0.frag",
              tmpRoot / "assets" / "shaders" / "glsl" / "forward" /
                  "blinnphong_0.frag",
              fs::copy_options::overwrite_existing);
std::ofstream(tmpRoot / "assets" / "shaders" / "glsl" / "forward" /
              "blinnphong_0.vert.spv")
    .put('\0');
std::ofstream(tmpRoot / "assets" / "shaders" / "glsl" / "forward" /
              "blinnphong_0.frag.spv")
    .put('\0');
```

- [ ] **Step 2: Run the focused failing test**

Run:

```bash
ninja -C build test_assets_layout && ./build/src/test/test_assets_layout
```

Expected before implementation: the test fails because `forward/blinnphong_0.vert` is not present and `getShaderPath("forward/blinnphong_0", "vert.spv")` does not point at a categorized binary.

- [ ] **Step 3: Update shader path helpers**

In `src/core/utils/filesystem_tools.hpp`, keep the existing API name and document that `shaderName` may include a subdirectory:

```cpp
std::string getShaderPath(const std::string &shaderName,
                          const std::string &stageExt);
```

In `src/core/utils/filesystem_tools.cpp`, implement path composition through `std::filesystem::path(shaderName + "." + stageExt)` so names such as `forward/blinnphong_0` preserve the directory:

```cpp
std::string getShaderPath(const std::string &shaderName,
                          const std::string &stageExt) {
  const auto shaderRelativePath =
      std::filesystem::path(shaderName + "." + stageExt);
  return resolveRuntimePath(std::filesystem::path("assets") / "shaders" /
                            "glsl" / shaderRelativePath)
      .string();
}
```

If the current implementation already uses this shape, make the smallest adjustment needed to avoid flattening `shaderName`.

- [ ] **Step 4: Make CMake compile recursively and preserve relative output**

Replace the root-only glob block in `assets/shaders/CMakeLists.txt` with recursive collection:

```cmake
file(GLOB_RECURSE GLSL_STAGE_FILES
  "${GLSL_INPUT_DIR}/*.vert"
  "${GLSL_INPUT_DIR}/*.frag"
  "${GLSL_INPUT_DIR}/*.comp"
)
```

Inside the `foreach`, compute the source-relative path and output path:

```cmake
file(RELATIVE_PATH SHADER_RELATIVE_PATH "${GLSL_INPUT_DIR}" "${SHADER_FILE}")
get_filename_component(SHADER_RELATIVE_DIR "${SHADER_RELATIVE_PATH}" DIRECTORY)
get_filename_component(FILE_NAME "${SHADER_FILE}" NAME)

if(SHADER_RELATIVE_DIR STREQUAL "")
  set(OUTPUT_DIR "${SHADER_OUTPUT_DIR}")
else()
  set(OUTPUT_DIR "${SHADER_OUTPUT_DIR}/${SHADER_RELATIVE_DIR}")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

if(FILE_NAME MATCHES "^(.+)\\.vert$")
  set(OUTPUT_SPV "${OUTPUT_DIR}/${CMAKE_MATCH_1}.vert.spv")
elseif(FILE_NAME MATCHES "^(.+)\\.frag$")
  set(OUTPUT_SPV "${OUTPUT_DIR}/${CMAKE_MATCH_1}.frag.spv")
elseif(FILE_NAME MATCHES "^(.+)\\.comp$")
  set(OUTPUT_SPV "${OUTPUT_DIR}/${CMAKE_MATCH_1}.comp.spv")
else()
  message(FATAL_ERROR "Unrecognized shader filename: ${SHADER_RELATIVE_PATH}")
endif()
```

Keep `-I "${GLSL_INPUT_DIR}"`, `-MD`, `-MF`, and `DEPFILE` so common include changes rebuild dependent shaders.

- [ ] **Step 5: Run asset-layout test after shader files are moved in Task 2**

Run:

```bash
ninja -C build test_assets_layout
./build/src/test/test_assets_layout
```

Expected after Task 2: `[PASS] All asset layout tests passed.`

- [ ] **Step 6: Commit**

```bash
git add assets/shaders/CMakeLists.txt src/core/utils/filesystem_tools.hpp src/core/utils/filesystem_tools.cpp src/test/integration/test_assets_layout.cpp
git commit -m "Prepare shader build for categorized paths"
```

---

### Task 2: Move Shader Sources and Update Runtime Lookups

**Files:**
- Move: `assets/shaders/glsl/*.vert`, `*.frag`, `*.comp`
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Modify: `src/backend/vulkan/vulkan_shader.cpp`
- Modify: `src/demos/minimal_resize/main.cpp`
- Modify: `src/demos/minimal_resize_baseline/main.cpp`
- Modify: path-only tests found with `rg`

- [ ] **Step 1: Move shader files into categorized directories**

Run:

```bash
mkdir -p assets/shaders/glsl/forward assets/shaders/glsl/shadow assets/shaders/glsl/post assets/shaders/glsl/ibl assets/shaders/glsl/debug assets/shaders/glsl/experimental assets/shaders/glsl/offline
mv assets/shaders/glsl/blinnphong_0.* assets/shaders/glsl/forward/
mv assets/shaders/glsl/pbr.* assets/shaders/glsl/forward/
mv assets/shaders/glsl/shadow_depth_only.* assets/shaders/glsl/shadow/
mv assets/shaders/glsl/post_process.* assets/shaders/glsl/post/
mv assets/shaders/glsl/bloom_* assets/shaders/glsl/post/
mv assets/shaders/glsl/equirect_to_cubemap.* assets/shaders/glsl/ibl/
mv assets/shaders/glsl/ibl_* assets/shaders/glsl/ibl/
mv assets/shaders/glsl/skybox.* assets/shaders/glsl/ibl/
mv assets/shaders/glsl/texture_cube_probe.* assets/shaders/glsl/ibl/
mv assets/shaders/glsl/debug_line.* assets/shaders/glsl/debug/
mv assets/shaders/glsl/mesh_debug.* assets/shaders/glsl/debug/
mv assets/shaders/glsl/minimal.* assets/shaders/glsl/debug/
mv assets/shaders/glsl/rtr_experiment_template.* assets/shaders/glsl/experimental/
mv assets/shaders/glsl/rtr_shadertoy_quantum_core.* assets/shaders/glsl/experimental/
mv assets/shaders/glsl/offline_primary_ray.* assets/shaders/glsl/offline/
```

Then remove stale checked-in SPIR-V files from the old root if any remain:

```bash
find assets/shaders/glsl -maxdepth 1 -type f \( -name '*.spv' -o -name '*.vert' -o -name '*.frag' -o -name '*.comp' \) -print
```

Expected: no root-level concrete shader stage or SPIR-V files remain, except `common` include files if present.

- [ ] **Step 2: Add shader logical-name mapping in the generic material loader**

In `src/infra/material_loader/generic_material_loader.cpp`, add:

```cpp
std::string shaderSourceNameForMaterialName(const std::string &shaderName) {
  if (shaderName == "blinnphong_0" || shaderName == "pbr") {
    return "forward/" + shaderName;
  }
  if (shaderName == "shadow_depth_only") {
    return "shadow/" + shaderName;
  }
  if (shaderName == "mesh_debug") {
    return "debug/" + shaderName;
  }
  if (shaderName == "rtr_experiment_template" ||
      shaderName == "rtr_shadertoy_quantum_core") {
    return "experimental/" + shaderName;
  }
  return shaderName;
}
```

Change `compileProgramForShaderName(...)` to use the mapped name:

```cpp
const auto shaderSourceName = shaderSourceNameForMaterialName(shaderName);
const fs::path vertPath = shaderDir / (shaderSourceName + ".vert");
const fs::path fragPath = shaderDir / (shaderSourceName + ".frag");
```

Keep `MaterialPassDefinition::shaderProgram.shaderName` as the original YAML name, such as `blinnphong_0`, so material assets do not change only because files moved.

- [ ] **Step 3: Update Vulkan built-in shader binary lookup**

Find the implementation used by `src/test/integration/test_vulkan_shader.cpp`:

```bash
rg -n "getShaderPath\\(|shaderName \\+|minimal\\.vert\\.spv|blinnphong_0" src/backend src/demos src/test/test_render_triangle.cpp
```

If it passes raw names to `getShaderPath`, map built-ins in the same style:

```cpp
std::string shaderBinaryNameForBuiltinName(const std::string &shaderName) {
  if (shaderName == "blinnphong_0" || shaderName == "pbr") {
    return "forward/" + shaderName;
  }
  if (shaderName == "shadow_depth_only") {
    return "shadow/" + shaderName;
  }
  if (shaderName == "minimal" || shaderName == "debug_line" ||
      shaderName == "mesh_debug") {
    return "debug/" + shaderName;
  }
  return shaderName;
}
```

Then call:

```cpp
const auto path = getShaderPath(shaderBinaryNameForBuiltinName(shaderName),
                                stageExtension);
```

- [ ] **Step 4: Update minimal demo shader binary paths**

In `src/demos/minimal_resize/main.cpp` and `src/demos/minimal_resize_baseline/main.cpp`, replace:

```cpp
readShaderCode(shaderDir / "minimal.vert.spv");
readShaderCode(shaderDir / "minimal.frag.spv");
```

with:

```cpp
readShaderCode(shaderDir / "debug" / "minimal.vert.spv");
readShaderCode(shaderDir / "debug" / "minimal.frag.spv");
```

- [ ] **Step 5: Update path-only tests**

For each direct source path match from:

```bash
rg -n "blinnphong_0\\.vert|blinnphong_0\\.frag|pbr\\.vert|shadow_depth_only\\.vert|minimal\\.vert\\.spv" src assets -g '!*.spv'
```

replace root paths with categorized paths. Examples:

```cpp
shaderDir / "forward" / "blinnphong_0.vert"
shaderDir / "forward" / "pbr.frag"
shaderDir / "shadow" / "shadow_depth_only.vert"
shaderDir / "debug" / "minimal.vert.spv"
```

- [ ] **Step 6: Run path and build verification**

Run:

```bash
ninja -C build CompileShaders
ninja -C build test_assets_layout test_generic_material_loader test_vulkan_shader
./build/src/test/test_assets_layout
./build/src/test/test_generic_material_loader
./build/src/test/test_vulkan_shader
```

Expected: all commands pass and compiled SPIR-V files appear under categorized build directories.

- [ ] **Step 7: Commit**

```bash
git add assets/shaders src/infra/material_loader/generic_material_loader.cpp src/backend src/demos src/test assets/shaders/README.md
git commit -m "Move shaders into categorized directories"
```

---

### Task 3: Converge Traditional Forward ABI

**Files:**
- Modify: `assets/shaders/glsl/forward/blinnphong_0.vert`
- Modify: `assets/shaders/glsl/forward/blinnphong_0.frag`
- Modify: `assets/shaders/glsl/forward/pbr.vert`
- Modify: `assets/shaders/glsl/forward/pbr.frag`
- Modify: backend descriptor injection files found by `rg -n "CameraUBO|LightUBO|ShadowMap0|IrradianceMap|PrefilteredEnvMap|BrdfLut|EnvironmentUBO|Bones" src/backend src/core src/demos`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Update reflection tests to assert the converged ABI**

In `src/test/integration/test_shader_compiler.cpp`, update PBR expectations:

```cpp
expectBinding(bindings, "CameraUBO", ShaderPropertyType::UniformBuffer, 0, 0);
expectBinding(bindings, "LightUBO", ShaderPropertyType::UniformBuffer, 0, 1);
expectBinding(bindings, "MaterialUBO", ShaderPropertyType::UniformBuffer, 1, 0);
expectBinding(bindings, "albedoMap", ShaderPropertyType::Texture2D, 1, 1);
expectBinding(bindings, "IrradianceMap", ShaderPropertyType::TextureCube, 2, 0);
expectBinding(bindings, "PrefilteredEnvMap", ShaderPropertyType::TextureCube, 2, 1);
expectBinding(bindings, "BrdfLut", ShaderPropertyType::Texture2D, 2, 2);
expectBinding(bindings, "EnvironmentUBO", ShaderPropertyType::UniformBuffer, 2, 3);
```

Update Blinn-Phong expectations:

```cpp
expectBinding(bindings, "CameraUBO", ShaderPropertyType::UniformBuffer, 0, 0);
expectBinding(bindings, "LightUBO", ShaderPropertyType::UniformBuffer, 0, 1);
expectBinding(bindings, "ShadowMap0", ShaderPropertyType::Texture2D, 0, 2);
expectBinding(bindings, "ShadowMap1", ShaderPropertyType::Texture2D, 0, 3);
expectBinding(bindings, "ShadowMap2", ShaderPropertyType::Texture2D, 0, 4);
expectBinding(bindings, "ShadowMap3", ShaderPropertyType::Texture2D, 0, 5);
expectBinding(bindings, "MaterialUBO", ShaderPropertyType::UniformBuffer, 1, 0);
expectBinding(bindings, "albedoMap", ShaderPropertyType::Texture2D, 1, 1);
expectBinding(bindings, "normalMap", ShaderPropertyType::Texture2D, 1, 2);
```

- [ ] **Step 2: Run the shader compiler test and confirm failure**

Run:

```bash
ninja -C build test_shader_compiler
./build/src/test/test_shader_compiler assets/shaders/glsl
```

Expected before shader/backend changes: reflection assertions fail for Blinn-Phong and for PBR IBL resources if they still use the old set 3 layout.

- [ ] **Step 3: Update shader set/binding declarations**

In `forward/blinnphong_0.vert`, change:

```glsl
layout(set = 1, binding = 0) uniform CameraUBO
layout(set = 0, binding = 0) uniform LightUBO
layout(set = 3, binding = 0) uniform Bones
```

to:

```glsl
layout(set = 0, binding = 0) uniform CameraUBO
layout(set = 0, binding = 1) uniform LightUBO
layout(set = 3, binding = 0) uniform Bones
```

In `forward/blinnphong_0.frag`, change:

```glsl
layout(set = 0, binding = 0) uniform LightUBO
layout(set = 0, binding = 1) uniform sampler2D ShadowMap0;
layout(set = 0, binding = 2) uniform sampler2D ShadowMap1;
layout(set = 0, binding = 3) uniform sampler2D ShadowMap2;
layout(set = 0, binding = 4) uniform sampler2D ShadowMap3;
layout(set = 2, binding = 0) uniform MaterialUBO
layout(set = 2, binding = 1) uniform sampler2D albedoMap;
layout(set = 2, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 0) uniform CameraUBO
```

to:

```glsl
layout(set = 0, binding = 0) uniform CameraUBO
layout(set = 0, binding = 1) uniform LightUBO
layout(set = 0, binding = 2) uniform sampler2D ShadowMap0;
layout(set = 0, binding = 3) uniform sampler2D ShadowMap1;
layout(set = 0, binding = 4) uniform sampler2D ShadowMap2;
layout(set = 0, binding = 5) uniform sampler2D ShadowMap3;
layout(set = 1, binding = 0) uniform MaterialUBO
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
```

In `forward/pbr.frag`, change IBL resources:

```glsl
layout(set = 3, binding = 0) uniform samplerCube IrradianceMap;
layout(set = 3, binding = 1) uniform samplerCube PrefilteredEnvMap;
layout(set = 3, binding = 2) uniform sampler2D BrdfLut;
layout(set = 3, binding = 3) uniform EnvironmentUBO
```

to:

```glsl
layout(set = 2, binding = 0) uniform samplerCube IrradianceMap;
layout(set = 2, binding = 1) uniform samplerCube PrefilteredEnvMap;
layout(set = 2, binding = 2) uniform sampler2D BrdfLut;
layout(set = 2, binding = 3) uniform EnvironmentUBO
```

- [ ] **Step 4: Update backend/system descriptor injection to match shader ABI**

Search:

```bash
rg -n "CameraUBO|LightUBO|ShadowMap0|ShadowMap1|ShadowMap2|ShadowMap3|IrradianceMap|PrefilteredEnvMap|BrdfLut|EnvironmentUBO|Bones" src/backend src/core src/demos
```

For each system-owned descriptor resource creation, update its reflected or explicit set/binding to the converged ABI:

```cpp
CameraUBO       -> set 0 binding 0
LightUBO        -> set 0 binding 1
ShadowMap0..3   -> set 0 binding 2..5
MaterialUBO     -> set 1 binding 0
material maps   -> set 1 binding 1..N
IrradianceMap   -> set 2 binding 0
PrefilteredEnvMap -> set 2 binding 1
BrdfLut         -> set 2 binding 2
EnvironmentUBO  -> set 2 binding 3
Bones           -> set 3 binding 0
```

Do not add shader macros for old set values.

- [ ] **Step 5: Run ABI verification**

Run:

```bash
ninja -C build CompileShaders test_shader_compiler test_generic_material_loader test_material_instance test_scene_node_validation
./build/src/test/test_shader_compiler assets/shaders/glsl
./build/src/test/test_generic_material_loader
./build/src/test/test_material_instance
./build/src/test/test_scene_node_validation
```

Expected: shader reflection tests and material descriptor validation pass under the converged ABI.

- [ ] **Step 6: Commit**

```bash
git add assets/shaders/glsl/forward src/backend src/core src/demos src/test/integration/test_shader_compiler.cpp src/test/integration/test_generic_material_loader.cpp src/test/integration/test_material_instance.cpp src/test/integration/test_scene_node_validation.cpp
git commit -m "Converge forward shader resource ABI"
```

---

### Task 4: Extract Common GLSL Helpers for Raster Shaders

**Files:**
- Create: `assets/shaders/glsl/common/geometry/fullscreen_triangle.glsl`
- Create: `assets/shaders/glsl/common/geometry/cube_capture.glsl`
- Create: `assets/shaders/glsl/common/geometry/transform.glsl`
- Create: `assets/shaders/glsl/common/geometry/normals.glsl`
- Create: `assets/shaders/glsl/common/lighting/pbr_brdf.glsl`
- Create: `assets/shaders/glsl/common/lighting/blinn_phong.glsl`
- Create: `assets/shaders/glsl/common/lighting/shadow_cascade.glsl`
- Create: `assets/shaders/glsl/common/post/bloom.glsl`
- Modify: raster shaders under `assets/shaders/glsl/{forward,post,ibl}`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Create fullscreen triangle helper**

Create `assets/shaders/glsl/common/geometry/fullscreen_triangle.glsl`:

```glsl
#ifndef LX_COMMON_GEOMETRY_FULLSCREEN_TRIANGLE_GLSL
#define LX_COMMON_GEOMETRY_FULLSCREEN_TRIANGLE_GLSL

vec2 lxFullscreenTrianglePosition(uint vertexIndex) {
  vec2 positions[3] = vec2[](
      vec2(-1.0, -1.0),
      vec2(3.0, -1.0),
      vec2(-1.0, 3.0));
  return positions[vertexIndex];
}

vec2 lxFullscreenTriangleUv(uint vertexIndex) {
  vec2 uvs[3] = vec2[](
      vec2(0.0, 0.0),
      vec2(2.0, 0.0),
      vec2(0.0, 2.0));
  return uvs[vertexIndex];
}

#endif
```

Use it in post, bloom, BRDF LUT, and skybox-style fullscreen vertex shaders:

```glsl
#include "common/geometry/fullscreen_triangle.glsl"

void main() {
  vec2 pos = lxFullscreenTrianglePosition(uint(gl_VertexIndex));
  vUV = lxFullscreenTriangleUv(uint(gl_VertexIndex));
  gl_Position = vec4(pos, 0.0, 1.0);
}
```

- [ ] **Step 2: Create cube capture helper**

Create `assets/shaders/glsl/common/geometry/cube_capture.glsl` with the existing cube vertex array renamed to `lxCubeVertices` and helper:

```glsl
#ifndef LX_COMMON_GEOMETRY_CUBE_CAPTURE_GLSL
#define LX_COMMON_GEOMETRY_CUBE_CAPTURE_GLSL

const vec3 lxCubeVertices[36] = vec3[](
    vec3(-1.0, -1.0,  1.0), vec3( 1.0, -1.0,  1.0), vec3( 1.0,  1.0,  1.0),
    vec3( 1.0,  1.0,  1.0), vec3(-1.0,  1.0,  1.0), vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0, -1.0), vec3(-1.0, -1.0, -1.0), vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0, -1.0), vec3( 1.0,  1.0, -1.0), vec3( 1.0, -1.0, -1.0),
    vec3(-1.0, -1.0, -1.0), vec3(-1.0, -1.0,  1.0), vec3(-1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0), vec3(-1.0,  1.0, -1.0), vec3(-1.0, -1.0, -1.0),
    vec3( 1.0, -1.0,  1.0), vec3( 1.0, -1.0, -1.0), vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0, -1.0), vec3( 1.0,  1.0,  1.0), vec3( 1.0, -1.0,  1.0),
    vec3(-1.0,  1.0,  1.0), vec3( 1.0,  1.0,  1.0), vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0, -1.0), vec3(-1.0,  1.0, -1.0), vec3(-1.0,  1.0,  1.0),
    vec3(-1.0, -1.0, -1.0), vec3( 1.0, -1.0, -1.0), vec3( 1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0), vec3(-1.0, -1.0,  1.0), vec3(-1.0, -1.0, -1.0));

vec3 lxCubeCapturePosition(uint vertexIndex) {
  return lxCubeVertices[vertexIndex];
}

#endif
```

Use this in `ibl/equirect_to_cubemap.vert`, `ibl/ibl_irradiance_convolve.vert`, and `ibl/ibl_prefilter_env.vert`.

- [ ] **Step 3: Create transform and normal helpers**

Create `assets/shaders/glsl/common/geometry/transform.glsl`:

```glsl
#ifndef LX_COMMON_GEOMETRY_TRANSFORM_GLSL
#define LX_COMMON_GEOMETRY_TRANSFORM_GLSL

vec3 lxTransformPoint(mat4 m, vec3 p) {
  vec4 value = m * vec4(p, 1.0);
  return value.xyz / max(value.w, 1.0e-8);
}

mat3 lxNormalMatrix(mat4 model) {
  return mat3(transpose(inverse(model)));
}

#endif
```

Create `assets/shaders/glsl/common/geometry/normals.glsl`:

```glsl
#ifndef LX_COMMON_GEOMETRY_NORMALS_GLSL
#define LX_COMMON_GEOMETRY_NORMALS_GLSL

mat3 lxBuildTbn(mat3 normalMatrix, vec3 normal, vec4 tangent) {
  vec3 n = normalize(normal);
  vec3 t = normalize(normalMatrix * tangent.xyz);
  vec3 b = normalize(cross(n, t) * tangent.w);
  return mat3(t, b, n);
}

vec3 lxDecodeTangentNormal(vec3 encodedNormal) {
  return encodedNormal * 2.0 - 1.0;
}

vec3 lxApplyTangentNormal(mat3 tbn, vec3 encodedNormal) {
  return normalize(tbn * lxDecodeTangentNormal(encodedNormal));
}

vec3 lxFlatNormal(vec3 worldPos, vec3 fallbackNormal) {
  vec3 fallback = normalize(fallbackNormal);
  vec3 flatNormal = cross(dFdx(worldPos), dFdy(worldPos));
  float len2 = dot(flatNormal, flatNormal);
  if (len2 < 1.0e-10) {
    return fallback;
  }
  flatNormal *= inversesqrt(len2);
  return dot(flatNormal, fallback) < 0.0 ? -flatNormal : flatNormal;
}

#endif
```

Use these in PBR and Blinn-Phong vertex/fragment shaders.

- [ ] **Step 4: Create PBR and Blinn-Phong lighting helpers**

Create `assets/shaders/glsl/common/lighting/pbr_brdf.glsl` by moving existing PBR functions and renaming them:

```glsl
#ifndef LX_COMMON_LIGHTING_PBR_BRDF_GLSL
#define LX_COMMON_LIGHTING_PBR_BRDF_GLSL

const float LX_PI = 3.14159265359;

float lxDistributionGGX(vec3 n, vec3 h, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float ndoth = max(dot(n, h), 0.0);
  float ndoth2 = ndoth * ndoth;
  float denom = ndoth2 * (a2 - 1.0) + 1.0;
  denom = LX_PI * denom * denom;
  return a2 / max(denom, 0.0001);
}

float lxGeometrySchlickGGX(float ndotv, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return ndotv / (ndotv * (1.0 - k) + k);
}

float lxGeometrySmith(vec3 n, vec3 v, vec3 l, float roughness) {
  float ndotv = max(dot(n, v), 0.0);
  float ndotl = max(dot(n, l), 0.0);
  return lxGeometrySchlickGGX(ndotv, roughness) *
         lxGeometrySchlickGGX(ndotl, roughness);
}

vec3 lxFresnelSchlick(float cosTheta, vec3 f0) {
  return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 lxFresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
  return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
                  pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

#endif
```

Create `assets/shaders/glsl/common/lighting/blinn_phong.glsl`:

```glsl
#ifndef LX_COMMON_LIGHTING_BLINN_PHONG_GLSL
#define LX_COMMON_LIGHTING_BLINN_PHONG_GLSL

vec3 lxBlinnPhongDirect(vec3 baseColor, vec3 lightColor, vec3 n, vec3 l,
                        vec3 v, float shininess, float specularIntensity) {
  float diff = max(dot(n, l), 0.0);
  vec3 h = normalize(l + v);
  float spec = pow(max(dot(n, h), 0.0), max(shininess, 1.0));
  return baseColor * diff * lightColor +
         spec * lightColor * max(specularIntensity, 0.0);
}

#endif
```

- [ ] **Step 5: Create bloom helper**

Create `assets/shaders/glsl/common/post/bloom.glsl`:

```glsl
#ifndef LX_COMMON_POST_BLOOM_GLSL
#define LX_COMMON_POST_BLOOM_GLSL

vec3 lxBloomBlur5Tap(sampler2D sourceTex, vec2 uv, vec2 axisTexel) {
  vec3 color = texture(sourceTex, uv).rgb * 0.227027;
  color += texture(sourceTex, uv + axisTexel * 1.384615).rgb * 0.316216;
  color += texture(sourceTex, uv - axisTexel * 1.384615).rgb * 0.316216;
  color += texture(sourceTex, uv + axisTexel * 3.230769).rgb * 0.070270;
  color += texture(sourceTex, uv - axisTexel * 3.230769).rgb * 0.070270;
  return color;
}

#endif
```

Use `axisTexel = vec2(texel.x, 0.0)` in horizontal blur and `axisTexel = vec2(0.0, texel.y)` in vertical blur.

- [ ] **Step 6: Run shader compile regression**

Run:

```bash
ninja -C build CompileShaders test_shader_compiler
./build/src/test/test_shader_compiler assets/shaders/glsl
```

Expected: all raster shader contracts still pass, proving common includes did not alter resource reflection except the intentional ABI convergence from Task 3.

- [ ] **Step 7: Commit**

```bash
git add assets/shaders/glsl/common assets/shaders/glsl/forward assets/shaders/glsl/post assets/shaders/glsl/ibl src/test/integration/test_shader_compiler.cpp
git commit -m "Extract common GLSL helpers for raster shaders"
```

---

### Task 5: Shared Scene SSBO and Offline Ray Modules

**Files:**
- Create: `assets/shaders/glsl/common/scene/records.glsl`
- Create: `assets/shaders/glsl/common/scene/buffers.glsl`
- Create: `assets/shaders/glsl/common/ray/random.glsl`
- Create: `assets/shaders/glsl/common/ray/intersection.glsl`
- Create: `assets/shaders/glsl/common/ray/bvh_traversal.glsl`
- Create: `assets/shaders/glsl/common/ray/camera_ray.glsl`
- Modify: `assets/shaders/glsl/offline/offline_primary_ray.comp`
- Modify: C++ offline record structs if needed after comparing layouts
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add compute compile test for offline shader**

In `src/test/integration/test_shader_compiler.cpp`, add a helper:

```cpp
static bool testOfflinePrimaryRayShader(const std::filesystem::path &shaderDir) {
  std::cout << "Test: offline primary ray compute shader\n";
  auto compileResult =
      ShaderCompiler::compileFile(shaderDir / "offline" /
                                      "offline_primary_ray.comp",
                                  {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }
  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto hasStorage = [&bindings](const std::string &name) {
    return std::any_of(bindings.begin(), bindings.end(), [&](const auto &b) {
      return b.name == name && b.type == ShaderPropertyType::StorageBuffer;
    });
  };
  if (!hasStorage("Vertices") || !hasStorage("Materials") ||
      !hasStorage("BvhNodes") || !hasStorage("OutputBuffer")) {
    std::cerr << "  FAIL: offline shader storage buffers missing\n";
    return false;
  }
  std::cout << "  PASS: offline compute shader compiles through common ray modules\n";
  return true;
}
```

Call it from `main` after the other shader contract tests.

- [ ] **Step 2: Run the failing offline compile test**

Run:

```bash
ninja -C build test_shader_compiler
./build/src/test/test_shader_compiler assets/shaders/glsl
```

Expected before implementing modules: the new test still passes with the old monolithic shader if path migration is complete. It becomes a regression guard for the extraction steps.

- [ ] **Step 3: Create shared scene records**

Create `assets/shaders/glsl/common/scene/records.glsl`:

```glsl
#ifndef LX_COMMON_SCENE_RECORDS_GLSL
#define LX_COMMON_SCENE_RECORDS_GLSL

struct lxVertexRecord {
  vec4 position;
  vec4 normal;
  vec4 uvTangentSign;
  vec4 tangent;
};

struct lxMeshRecord {
  uint vertexOffset;
  uint indexOffset;
  uint indexCount;
  uint geometryIndex;
};

struct lxPrimitiveRecord {
  uint indexOffset;
  uint meshIndex;
  uint materialIndex;
  uint objectIndex;
};

struct lxObjectRecord {
  mat4 objectToWorld;
  mat4 worldToObject;
  vec4 boundsMin;
  vec4 boundsMax;
  uvec4 flags;
};

struct lxMaterialRecord {
  vec4 baseColor;
  vec4 pbrParams;      // x metallic, y roughness, z ao, w specularIntensity
  vec4 emissive;       // rgb emissive, a shininess or emissive scale
  uvec4 textureIndices; // x albedo, y normal, z metallicRoughness, w ao
  uvec4 flags;          // x feature flags, y shading model, z alpha mode, w emissive index
};

struct lxBvhNode {
  vec4 boundsMinLeftFirst;
  vec4 boundsMaxTriCount;
};

struct lxHitAttributes {
  vec3 normal;
  vec2 uv;
  uint materialIndex;
};

const uint LX_BVH_LEAF_NODE_FLAG = 0x80000000u;

#endif
```

- [ ] **Step 4: Create shared scene buffers**

Create `assets/shaders/glsl/common/scene/buffers.glsl`:

```glsl
#ifndef LX_COMMON_SCENE_BUFFERS_GLSL
#define LX_COMMON_SCENE_BUFFERS_GLSL

#include "common/scene/records.glsl"

layout(std430, binding = 0) readonly buffer Vertices {
  lxVertexRecord lxVertices[];
};

layout(std430, binding = 1) readonly buffer Indices {
  uint lxIndices[];
};

layout(std430, binding = 2) readonly buffer Meshes {
  lxMeshRecord lxMeshes[];
};

layout(std430, binding = 3) readonly buffer Primitives {
  lxPrimitiveRecord lxPrimitives[];
};

layout(std430, binding = 4) readonly buffer Objects {
  lxObjectRecord lxObjects[];
};

layout(std430, binding = 5) readonly buffer Materials {
  lxMaterialRecord lxMaterials[];
};

layout(std430, binding = 6) readonly buffer BvhNodes {
  lxBvhNode lxBvhNodes[];
};

#endif
```

- [ ] **Step 5: Create ray random and intersection helpers**

Create `assets/shaders/glsl/common/ray/random.glsl`:

```glsl
#ifndef LX_COMMON_RAY_RANDOM_GLSL
#define LX_COMMON_RAY_RANDOM_GLSL

uint lxWangHash(uint value) {
  value = (value ^ 61u) ^ (value >> 16u);
  value *= 9u;
  value = value ^ (value >> 4u);
  value *= 0x27d4eb2du;
  value = value ^ (value >> 15u);
  return value;
}

float lxRandom01(uint seed) {
  return float(lxWangHash(seed) & 0x00ffffffu) / float(0x01000000u);
}

#endif
```

Create `assets/shaders/glsl/common/ray/intersection.glsl`:

```glsl
#ifndef LX_COMMON_RAY_INTERSECTION_GLSL
#define LX_COMMON_RAY_INTERSECTION_GLSL

#include "common/geometry/transform.glsl"
#include "common/scene/buffers.glsl"

bool lxIntersectAabb(vec3 origin, vec3 invDir, vec3 bmin, vec3 bmax,
                     float maxT) {
  vec3 t0 = (bmin - origin) * invDir;
  vec3 t1 = (bmax - origin) * invDir;
  vec3 tmin = min(t0, t1);
  vec3 tmax = max(t0, t1);
  float lo = max(max(tmin.x, tmin.y), max(tmin.z, 0.0));
  float hi = min(min(tmax.x, tmax.y), min(tmax.z, maxT));
  return hi >= lo;
}

bool lxIntersectPrimitive(vec3 origin, vec3 dir, lxPrimitiveRecord primitive,
                          out float t, out lxHitAttributes attributes) {
  lxMeshRecord mesh = lxMeshes[primitive.meshIndex];
  lxObjectRecord objectRecord = lxObjects[primitive.objectIndex];
  uint i0 = lxIndices[primitive.indexOffset + 0u];
  uint i1 = lxIndices[primitive.indexOffset + 1u];
  uint i2 = lxIndices[primitive.indexOffset + 2u];
  lxVertexRecord a = lxVertices[mesh.vertexOffset + i0];
  lxVertexRecord b = lxVertices[mesh.vertexOffset + i1];
  lxVertexRecord c = lxVertices[mesh.vertexOffset + i2];
  vec3 v0 = lxTransformPoint(objectRecord.objectToWorld, a.position.xyz);
  vec3 v1 = lxTransformPoint(objectRecord.objectToWorld, b.position.xyz);
  vec3 v2 = lxTransformPoint(objectRecord.objectToWorld, c.position.xyz);
  vec3 e1 = v1 - v0;
  vec3 e2 = v2 - v0;
  vec3 p = cross(dir, e2);
  float det = dot(e1, p);
  if (abs(det) < 1.0e-7) {
    return false;
  }
  float invDet = 1.0 / det;
  vec3 tv = origin - v0;
  float u = dot(tv, p) * invDet;
  if (u < 0.0 || u > 1.0) {
    return false;
  }
  vec3 q = cross(tv, e1);
  float v = dot(dir, q) * invDet;
  if (v < 0.0 || u + v > 1.0) {
    return false;
  }
  t = dot(e2, q) * invDet;
  if (t <= 0.0005) {
    return false;
  }
  vec3 localNormal =
      normalize(a.normal.xyz * (1.0 - u - v) + b.normal.xyz * u +
                c.normal.xyz * v);
  vec3 normal =
      normalize(transpose(mat3(objectRecord.worldToObject)) * localNormal);
  if (dot(normal, dir) > 0.0) {
    normal = -normal;
  }
  attributes.normal = normal;
  attributes.uv = a.uvTangentSign.xy * (1.0 - u - v) +
                  b.uvTangentSign.xy * u + c.uvTangentSign.xy * v;
  attributes.materialIndex = primitive.materialIndex;
  return true;
}

#endif
```

- [ ] **Step 6: Create BVH traversal and camera ray helpers**

Create `assets/shaders/glsl/common/ray/bvh_traversal.glsl`:

```glsl
#ifndef LX_COMMON_RAY_BVH_TRAVERSAL_GLSL
#define LX_COMMON_RAY_BVH_TRAVERSAL_GLSL

#include "common/ray/intersection.glsl"

bool lxTraceScene(vec3 origin, vec3 dir, float maxT, uint primitiveCount,
                  uint bvhNodeCount, out float hitT, out vec3 hitNormal,
                  out uint materialIndex) {
  hitT = maxT;
  hitNormal = vec3(0.0, 1.0, 0.0);
  materialIndex = 0u;
  bool hit = false;
  vec3 invDir = 1.0 / dir;
  uint stack[64];
  uint stackSize = 0u;
  stack[stackSize++] = 0u;
  while (stackSize > 0u) {
    uint nodeIndex = stack[--stackSize];
    if (nodeIndex >= bvhNodeCount) {
      continue;
    }
    lxBvhNode node = lxBvhNodes[nodeIndex];
    if (!lxIntersectAabb(origin, invDir, node.boundsMinLeftFirst.xyz,
                         node.boundsMaxTriCount.xyz, hitT)) {
      continue;
    }
    uint packedNodeData = floatBitsToUint(node.boundsMaxTriCount.w);
    uint leftFirst = floatBitsToUint(node.boundsMinLeftFirst.w);
    bool isLeaf = (packedNodeData & LX_BVH_LEAF_NODE_FLAG) != 0u;
    if (isLeaf) {
      uint triCount = packedNodeData & ~LX_BVH_LEAF_NODE_FLAG;
      for (uint i = 0u; i < triCount; ++i) {
        uint primitiveIndex = leftFirst + i;
        if (primitiveIndex >= primitiveCount) {
          continue;
        }
        float t;
        lxHitAttributes attributes;
        if (lxIntersectPrimitive(origin, dir, lxPrimitives[primitiveIndex], t,
                                 attributes) &&
            t < hitT) {
          hit = true;
          hitT = t;
          hitNormal = attributes.normal;
          materialIndex = attributes.materialIndex;
        }
      }
    } else if (stackSize + 2u < 64u) {
      stack[stackSize++] = packedNodeData;
      stack[stackSize++] = leftFirst;
    }
  }
  return hit;
}

#endif
```

Create `assets/shaders/glsl/common/ray/camera_ray.glsl`:

```glsl
#ifndef LX_COMMON_RAY_CAMERA_RAY_GLSL
#define LX_COMMON_RAY_CAMERA_RAY_GLSL

vec3 lxCameraRayDirection(vec3 cameraForward, vec3 cameraRight, vec3 cameraUp,
                          vec2 ndc) {
  return normalize(cameraForward + cameraRight * ndc.x + cameraUp * ndc.y);
}

#endif
```

- [ ] **Step 7: Refactor offline compute shader to include modules**

In `offline/offline_primary_ray.comp`, remove duplicated record structs, scene SSBO declarations, random helpers, transform point, AABB/primitive intersection, trace traversal, and camera ray direction code. Add:

```glsl
#include "common/scene/buffers.glsl"
#include "common/ray/random.glsl"
#include "common/ray/bvh_traversal.glsl"
#include "common/ray/camera_ray.glsl"
```

Update variable names:

```glsl
Material material = materials[min(matIndex, params.materialCount - 1u)];
```

becomes:

```glsl
lxMaterialRecord material =
    lxMaterials[min(matIndex, params.materialCount - 1u)];
```

and trace calls become:

```glsl
lxTraceScene(origin, dir, 1.0e30, params.primitiveCount,
             params.bvhNodeCount, hitT, normal, matIndex)
```

Use `lxRandom01` and `lxCameraRayDirection` in `main`.

- [ ] **Step 8: Update C++ offline material record if shader layout changed**

Search for the C++ producer:

```bash
rg -n "struct .*Material|baseColor|emissive|pbrParams|offline.*Material|MaterialRecord" src/core src/infra src/test
```

If the current producer still writes the old three-`vec4` material layout, update it to match `lxMaterialRecord`:

```cpp
struct GpuMaterialRecord {
  LX_core::Vec4f baseColor;
  LX_core::Vec4f pbrParams;
  LX_core::Vec4f emissive;
  LX_core::Vec4u textureIndices;
  LX_core::Vec4u flags;
};
```

Set absent texture indices to `0xffffffffu`, PBR flags to `0`, and shading model to the engine's PBR value for PBR materials.

- [ ] **Step 9: Run offline shader and offline scene tests**

Run:

```bash
ninja -C build CompileShaders test_shader_compiler test_offline_gpu_scene test_offline_scene_loader
./build/src/test/test_shader_compiler assets/shaders/glsl
./build/src/test/test_offline_gpu_scene
./build/src/test/test_offline_scene_loader
```

Expected: offline compute shader compiles through common modules and offline scene tests pass.

- [ ] **Step 10: Commit**

```bash
git add assets/shaders/glsl/common/scene assets/shaders/glsl/common/ray assets/shaders/glsl/offline src/infra src/core src/test/integration/test_shader_compiler.cpp src/test/integration/test_offline_gpu_scene.cpp src/test/integration/test_offline_scene_loader.cpp
git commit -m "Share scene SSBO records with offline ray shader"
```

---

### Task 6: PBR-Capable Forward Uber Shader

**Files:**
- Create: `assets/shaders/glsl/common/abi/bindless_scene.glsl`
- Create: `assets/shaders/glsl/common/abi/bindless_material.glsl`
- Create: `assets/shaders/glsl/common/material/uber_material.glsl`
- Create: `assets/shaders/glsl/forward/uber_forward.vert`
- Create: `assets/shaders/glsl/forward/uber_forward.frag`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add failing Uber shader compile/reflection test**

In `src/test/integration/test_shader_compiler.cpp`, add:

```cpp
static bool testForwardUberShaderContract(const std::filesystem::path &shaderDir) {
  std::cout << "Test: forward Uber shader contract\n";
  auto compileResult = ShaderCompiler::compileProgram(
      shaderDir / "forward" / "uber_forward.vert",
      shaderDir / "forward" / "uber_forward.frag", {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }
  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto hasStorage = [&bindings](const std::string &name) {
    return std::any_of(bindings.begin(), bindings.end(), [&](const auto &b) {
      return b.name == name && b.type == ShaderPropertyType::StorageBuffer;
    });
  };
  const auto hasTexture = [&bindings](const std::string &name) {
    return std::any_of(bindings.begin(), bindings.end(), [&](const auto &b) {
      return b.name == name && b.type == ShaderPropertyType::Texture2D;
    });
  };
  if (!hasStorage("Objects") || !hasStorage("Materials") ||
      !hasTexture("lxBindlessTextures")) {
    std::cerr << "  FAIL: Uber shader ABI bindings missing\n";
    return false;
  }
  std::cout << "  PASS: forward Uber shader compiles and reflects shared ABI\n";
  return true;
}
```

Call it from `main`.

- [ ] **Step 2: Run failing Uber test**

Run:

```bash
ninja -C build test_shader_compiler
./build/src/test/test_shader_compiler assets/shaders/glsl
```

Expected before creating shader files: compile fails because `forward/uber_forward.vert` and `.frag` do not exist.

- [ ] **Step 3: Create bindless ABI include**

Create `assets/shaders/glsl/common/abi/bindless_material.glsl`:

```glsl
#ifndef LX_COMMON_ABI_BINDLESS_MATERIAL_GLSL
#define LX_COMMON_ABI_BINDLESS_MATERIAL_GLSL

#extension GL_EXT_nonuniform_qualifier : require

const uint LX_INVALID_TEXTURE_INDEX = 0xffffffffu;
const uint LX_MATERIAL_FLAG_ALBEDO_TEXTURE = 1u << 0;
const uint LX_MATERIAL_FLAG_NORMAL_TEXTURE = 1u << 1;
const uint LX_MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE = 1u << 2;
const uint LX_MATERIAL_FLAG_AO_TEXTURE = 1u << 3;
const uint LX_MATERIAL_FLAG_EMISSIVE_TEXTURE = 1u << 4;
const uint LX_SHADING_MODEL_PBR = 0u;
const uint LX_SHADING_MODEL_BLINN_PHONG = 1u;

layout(set = 4, binding = 0) uniform sampler2D lxBindlessTextures[];

vec4 lxSampleMaterialTexture2D(uint textureIndex, vec2 uv, vec4 fallbackValue) {
  if (textureIndex == LX_INVALID_TEXTURE_INDEX) {
    return fallbackValue;
  }
  return texture(lxBindlessTextures[nonuniformEXT(textureIndex)], uv);
}

#endif
```

Create `assets/shaders/glsl/common/abi/bindless_scene.glsl`:

```glsl
#ifndef LX_COMMON_ABI_BINDLESS_SCENE_GLSL
#define LX_COMMON_ABI_BINDLESS_SCENE_GLSL

#include "common/scene/buffers.glsl"

layout(push_constant) uniform lxUberDrawPC {
  uint objectIndex;
  uint materialIndex;
  uint meshIndex;
  uint flags;
} lxUberDraw;

#endif
```

- [ ] **Step 4: Create Uber material evaluation helper**

Create `assets/shaders/glsl/common/material/uber_material.glsl`:

```glsl
#ifndef LX_COMMON_MATERIAL_UBER_MATERIAL_GLSL
#define LX_COMMON_MATERIAL_UBER_MATERIAL_GLSL

#include "common/abi/bindless_material.glsl"
#include "common/lighting/pbr_brdf.glsl"
#include "common/lighting/blinn_phong.glsl"
#include "common/scene/records.glsl"

struct lxUberSurface {
  vec4 baseColor;
  vec3 normal;
  float metallic;
  float roughness;
  float ao;
  vec3 emissive;
  uint shadingModel;
};

lxUberSurface lxDecodeUberSurface(lxMaterialRecord materialRecord, vec2 uv,
                                  vec3 geometricNormal, mat3 tbn) {
  lxUberSurface surface;
  uint flags = materialRecord.flags.x;
  surface.baseColor = materialRecord.baseColor;
  if ((flags & LX_MATERIAL_FLAG_ALBEDO_TEXTURE) != 0u) {
    surface.baseColor *= lxSampleMaterialTexture2D(
        materialRecord.textureIndices.x, uv, vec4(1.0));
  }
  surface.normal = normalize(geometricNormal);
  if ((flags & LX_MATERIAL_FLAG_NORMAL_TEXTURE) != 0u) {
    vec3 tangentNormal = lxSampleMaterialTexture2D(
                             materialRecord.textureIndices.y, uv,
                             vec4(0.5, 0.5, 1.0, 1.0))
                             .rgb *
                         2.0 -
                         1.0;
    surface.normal = normalize(tbn * tangentNormal);
  }
  surface.metallic = clamp(materialRecord.pbrParams.x, 0.0, 1.0);
  surface.roughness = clamp(materialRecord.pbrParams.y, 0.04, 1.0);
  surface.ao = clamp(materialRecord.pbrParams.z, 0.0, 1.0);
  if ((flags & LX_MATERIAL_FLAG_METALLIC_ROUGHNESS_TEXTURE) != 0u) {
    vec4 mr = lxSampleMaterialTexture2D(
        materialRecord.textureIndices.z, uv, vec4(1.0));
    surface.metallic *= mr.b;
    surface.roughness = clamp(surface.roughness * mr.g, 0.04, 1.0);
  }
  if ((flags & LX_MATERIAL_FLAG_AO_TEXTURE) != 0u) {
    surface.ao *= lxSampleMaterialTexture2D(
                      materialRecord.textureIndices.w, uv, vec4(1.0))
                      .r;
  }
  surface.emissive = materialRecord.emissive.rgb;
  if ((flags & LX_MATERIAL_FLAG_EMISSIVE_TEXTURE) != 0u) {
    surface.emissive *= lxSampleMaterialTexture2D(
                            materialRecord.flags.w, uv, vec4(1.0))
                            .rgb;
  }
  surface.shadingModel = materialRecord.flags.y;
  return surface;
}

#endif
```

- [ ] **Step 5: Create Uber vertex shader**

Create `assets/shaders/glsl/forward/uber_forward.vert`:

```glsl
#version 450

#include "common/abi/bindless_scene.glsl"
#include "common/abi/frame_ubo.glsl"
#include "common/geometry/normals.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out mat3 vTBN;

void main() {
  lxObjectRecord objectRecord = lxObjects[lxUberDraw.objectIndex];
  vec4 worldPos = objectRecord.objectToWorld * vec4(inPosition, 1.0);
  mat3 normalMatrix = mat3(transpose(inverse(objectRecord.objectToWorld)));
  vWorldPos = worldPos.xyz;
  vNormal = normalize(normalMatrix * inNormal);
  vUV = inUV;
  vTBN = lxBuildTbn(normalMatrix, vNormal, inTangent);
  gl_Position = camera.proj * camera.view * worldPos;
}
```

If `common/abi/frame_ubo.glsl` is not created yet, create it with:

```glsl
#ifndef LX_COMMON_ABI_FRAME_UBO_GLSL
#define LX_COMMON_ABI_FRAME_UBO_GLSL

layout(set = 0, binding = 0) uniform CameraUBO {
  mat4 view;
  mat4 proj;
  vec3 eyePos;
} camera;

#endif
```

- [ ] **Step 6: Create Uber fragment shader**

Create `assets/shaders/glsl/forward/uber_forward.frag`:

```glsl
#version 450

#include "common/abi/bindless_scene.glsl"
#include "common/abi/frame_ubo.glsl"
#include "common/abi/light_ubo.glsl"
#include "common/material/uber_material.glsl"

layout(constant_id = 0) const uint LX_UBER_SHADING_POLICY = 0u;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in mat3 vTBN;

layout(location = 0) out vec4 outColor;

void main() {
  lxMaterialRecord materialRecord = lxMaterials[lxUberDraw.materialIndex];
  lxUberSurface surface =
      lxDecodeUberSurface(materialRecord, vUV, normalize(vNormal), vTBN);

  vec3 n = normalize(surface.normal);
  vec3 v = normalize(camera.eyePos - vWorldPos);
  vec3 l = normalize(-sceneLight.dir.xyz);
  vec3 h = normalize(v + l);
  vec3 lightColor = sceneLight.color.rgb;

  vec3 color;
  if (surface.shadingModel == LX_SHADING_MODEL_BLINN_PHONG ||
      LX_UBER_SHADING_POLICY == LX_SHADING_MODEL_BLINN_PHONG) {
    color = surface.baseColor.rgb * 0.03 +
            lxBlinnPhongDirect(surface.baseColor.rgb, lightColor, n, l, v,
                               max(materialRecord.emissive.w, 1.0),
                               max(materialRecord.pbrParams.w, 0.0));
  } else {
    vec3 f0 = mix(vec3(0.04), surface.baseColor.rgb, surface.metallic);
    float ndf = lxDistributionGGX(n, h, surface.roughness);
    float g = lxGeometrySmith(n, v, l, surface.roughness);
    vec3 f = lxFresnelSchlick(max(dot(h, v), 0.0), f0);
    vec3 numerator = ndf * g * f;
    float denominator =
        4.0 * max(dot(n, v), 0.0) * max(dot(n, l), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    vec3 kd = (vec3(1.0) - f) * (1.0 - surface.metallic);
    float ndotl = max(dot(n, l), 0.0);
    vec3 direct =
        (kd * surface.baseColor.rgb / LX_PI + specular) * lightColor * ndotl;
    vec3 ambient = vec3(0.03) * surface.baseColor.rgb * surface.ao;
    color = ambient + direct;
  }

  color += surface.emissive;
  outColor = vec4(color, surface.baseColor.a);
}
```

If `common/abi/light_ubo.glsl` is not created yet, create it with the converged LightUBO declaration from `forward/blinnphong_0.frag`:

```glsl
#ifndef LX_COMMON_ABI_LIGHT_UBO_GLSL
#define LX_COMMON_ABI_LIGHT_UBO_GLSL

layout(set = 0, binding = 1) uniform LightUBO {
  vec4 dir;
  vec4 color;
  mat4 shadowViewProj;
  mat4 cascadeViewProj[4];
  vec4 cascadeSplits;
  vec4 cascadeDepthRanges;
  vec4 shadowParams;
} sceneLight;

#endif
```

- [ ] **Step 7: Run Uber shader verification**

Run:

```bash
ninja -C build CompileShaders test_shader_compiler
./build/src/test/test_shader_compiler assets/shaders/glsl
```

Expected: `forward/uber_forward.vert|frag` compile, and reflection reports `Objects`, `Materials`, and `lxBindlessTextures`.

- [ ] **Step 8: Commit**

```bash
git add assets/shaders/glsl/common/abi assets/shaders/glsl/common/material assets/shaders/glsl/forward/uber_forward.vert assets/shaders/glsl/forward/uber_forward.frag src/test/integration/test_shader_compiler.cpp
git commit -m "Add compile-validated forward Uber shader"
```

---

### Task 7: Documentation and Final Verification

**Files:**
- Modify: `assets/shaders/README.md`
- Optional modify: `notes/concepts/material/future-roadmap.md` only if implementation changed the current status wording

- [ ] **Step 1: Update shader README**

Replace old root-layout examples in `assets/shaders/README.md` with:

```markdown
# Shader Assets

GLSL sources live under `assets/shaders/glsl/` and are grouped by pass or purpose:

- `forward/`: traditional forward material shaders plus compile-validated Uber shader
- `shadow/`: depth and shadow-map shaders
- `post/`: fullscreen post-processing and bloom
- `ibl/`: environment capture, skybox, and IBL bake shaders
- `debug/`: debug and minimal shaders
- `experimental/`: realtime research shaders
- `offline/`: offline compute shaders
- `common/`: included GLSL modules; these are not compiled directly

Compiled SPIR-V preserves the source-relative directory, for example:

- source: `assets/shaders/glsl/forward/pbr.frag`
- binary: `<build>/assets/shaders/glsl/forward/pbr.frag.spv`

Traditional forward shaders use the converged ABI:

```text
set 0: frame and scene resources
set 1: material-owned traditional resources
set 2: environment and IBL resources
set 3: object and animation resources
```

`forward/uber_forward.*` is bindless-ready shader preparation. It compiles and reflects a shared SSBO scene/material ABI, but current runtime rendering still uses traditional forward shaders.
```

- [ ] **Step 2: Run full verification set**

Run:

```bash
ninja -C build CompileShaders
ninja -C build test_shader_compiler test_assets_layout test_generic_material_loader test_material_instance test_scene_node_validation test_offline_gpu_scene
./build/src/test/test_shader_compiler assets/shaders/glsl
./build/src/test/test_assets_layout
./build/src/test/test_generic_material_loader
./build/src/test/test_material_instance
./build/src/test/test_scene_node_validation
./build/src/test/test_offline_gpu_scene
```

Expected: all listed commands pass.

- [ ] **Step 3: Inspect remaining old root references**

Run:

```bash
rg -n "assets/shaders/glsl/[A-Za-z0-9_]+\\.(vert|frag|comp)|shaderDir / \"[A-Za-z0-9_]+\\.(vert|frag|comp)\"|minimal\\.vert\\.spv|blinnphong_0\\.vert" src assets docs notes -g '!*.spv'
find assets/shaders/glsl -maxdepth 1 -type f \( -name '*.vert' -o -name '*.frag' -o -name '*.comp' -o -name '*.spv' \) -print
```

Expected: no stale root shader path references remain, except text in historical design/plan docs. The `find` command prints nothing.

- [ ] **Step 4: Commit**

```bash
git add assets/shaders/README.md notes/concepts/material/future-roadmap.md
git commit -m "Document categorized shader layout"
```

- [ ] **Step 5: Final status**

Run:

```bash
git status --short
git log --oneline -5
```

Expected: worktree is clean except user-owned unrelated changes, and recent commits show the shader layout/common/Uber implementation sequence.
