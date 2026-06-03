# Unify Scene GPU Data Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `SceneResourceTable` the single authoritative scene GPU data contract for realtime rendering, offline software tracing, bindless preparation, and future hardware RT preparation.

**Architecture:** Extend `SceneResourceTable` with GPU-facing records and dirty generations, move the software BVH into a neutral acceleration layer that derives from the table, and make `VulkanOfflineRenderer` select an explicit offline integrator. Remove `OfflineSceneIR` and `OfflineRayScene` as long-lived scene data models instead of wrapping them behind compatibility classes.

**Tech Stack:** C++20, Vulkan compute, GLSL std430 SSBOs, CMake/Ninja, existing integration tests under `src/test/integration`.

---

## File Structure

- Modify `src/core/scene/scene_resource_table.hpp` and `src/core/scene/scene_resource_table.cpp`: add GPU-facing record structs, iteration views, and dirty generation tracking. This is the only authoritative scene GPU data owner.
- Create `src/core/scene/scene_gpu_records.hpp`: CPU std430 record definitions shared by realtime, offline software tracing, bindless shaders, and future hardware RT adapters.
- Create `src/core/scene/scene_gpu_records.cpp`: small conversion helpers from existing math/material/resource types into GPU records.
- Create `src/core/scene/scene_resource_table_upload_view.hpp`: read-only views over table records for backend upload and acceleration builders.
- Create `src/core/raytracing/software_bvh.hpp` and `src/core/raytracing/software_bvh.cpp`: software BVH nodes and primitive references built from `SceneResourceTableUploadView`.
- Delete `src/core/offline/offline_ray_scene.hpp` and `src/core/offline/offline_ray_scene.cpp`: old duplicate packed scene path.
- Delete `src/core/offline/offline_scene.hpp` and `src/core/offline/offline_scene.cpp`: old offline-only IR path, after settings/image/job types move.
- Create `src/core/offline/offline_render_job.hpp`: `OfflineRenderJob`, `OfflineReadbackImage`, and settings/profile includes that do not carry `OfflineSceneIR`.
- Modify `src/core/offline/offline_render_profile.hpp` and `src/core/offline/offline_render_profile.cpp`: change default integrator from `primary-ray` to `software-compute` and validate names.
- Replace `src/infra/offline/offline_scene_compiler.hpp` and `src/infra/offline/offline_scene_compiler.cpp` with `OfflineSceneLoader` that populates `SceneResourceTable`.
- Modify `src/infra/CMakeLists.txt`: replace `offline/offline_scene_compiler.cpp` with `offline/offline_scene_loader.cpp`.
- Modify `src/tools/lxe_offline_render/main.cpp`: load `.scene.yaml` into `SceneResourceTable` and pass the table directly into `OfflineRenderJob`.
- Modify `src/backend/vulkan/offline/vulkan_offline_renderer.hpp` and `src/backend/vulkan/offline/vulkan_offline_renderer.cpp`: make the renderer a coordinator and move compute dispatch into `SoftwareComputeOfflineIntegrator`.
- Create `src/backend/vulkan/offline/offline_integrator.hpp`: integrator interface and explicit selection function.
- Create `src/backend/vulkan/offline/software_compute_offline_integrator.hpp` and `src/backend/vulkan/offline/software_compute_offline_integrator.cpp`: current compute path updated to unified table records plus `SoftwareBvh`.
- Modify `assets/shaders/glsl/offline_primary_ray.comp`: consume unified `lx`-prefixed scene records and software BVH buffers. If the shader-layout plan is already merged before execution, place shared definitions under the new shader common layout from that plan.
- Modify `src/test/integration/test_scene_resource_table.cpp`: add table GPU record, generation, and invalid-resource tests.
- Replace `src/test/integration/test_offline_gpu_scene.cpp`: test unified table records and software BVH, not `OfflineRayScene`.
- Rename `src/test/integration/test_offline_scene_compiler.cpp` to `src/test/integration/test_offline_scene_loader.cpp`: assert scene YAML populates the table.
- Modify `src/test/CMakeLists.txt`: replace `test_offline_scene_compiler` with `test_offline_scene_loader`.
- Create `src/test/integration/test_offline_integrator_selection.cpp`: explicit integrator selection and unsupported-name tests.
- Create `notes/requirements/2026-06-03-unified-scene-gpu-data-breaks.md`: accepted break changes and deferred capabilities for removed offline-only APIs.

## Task 1: Add GPU Record Contract to SceneResourceTable

**Files:**
- Create: `src/core/scene/scene_gpu_records.hpp`
- Create: `src/core/scene/scene_gpu_records.cpp`
- Create: `src/core/scene/scene_resource_table_upload_view.hpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/test/integration/test_scene_resource_table.cpp`

- [ ] **Step 1: Write failing record-layout tests**

Append these tests to `src/test/integration/test_scene_resource_table.cpp`, call them from `main()`, and include `core/scene/scene_gpu_records.hpp`.

```cpp
void testSceneGpuRecordLayoutContract() {
  EXPECT(sizeof(SceneGpuVertexRecord) == 64,
         "SceneGpuVertexRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuMeshRecord) == 16,
         "SceneGpuMeshRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuPrimitiveRecord) == 16,
         "SceneGpuPrimitiveRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuObjectRecord) == 176,
         "SceneGpuObjectRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuMaterialRecord) == 64,
         "SceneGpuMaterialRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuFrameParams) == 176,
         "SceneGpuFrameParams std430 contract should stay stable");
}

void testSceneResourceTableUploadViewTracksGeneration() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(makeMeshBuffer());
  const auto material =
      table.registerMaterial(MaterialInstance::create(
          MaterialTemplate::create("scene_gpu_records")));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto firstView = table.buildUploadView();
  EXPECT(firstView.generation != 0, "upload view should expose generation");
  EXPECT(firstView.meshes.size() == 1, "upload view should expose one mesh");
  EXPECT(firstView.objects.size() == 1, "upload view should expose one object");
  EXPECT(firstView.objects.front().meshIndex == mesh.index,
         "object GPU record should reference mesh index");
  EXPECT(firstView.objects.front().materialIndex == material.index,
         "object GPU record should reference material index");

  object.visible = false;
  table.updateObject(objectHandle, object);
  const auto secondView = table.buildUploadView();
  EXPECT(secondView.generation > firstView.generation,
         "object update should advance upload generation");
  EXPECT(secondView.objects.front().visible == 0,
         "object visibility should reach GPU record");
}
```

- [ ] **Step 2: Run the focused test to verify it fails**

Run: `ninja -C build test_scene_resource_table && ./build/src/test/test_scene_resource_table`

Expected: compile failure for missing `SceneGpu*` types and `SceneResourceTable::buildUploadView()`.

- [ ] **Step 3: Add record structs**

Create `src/core/scene/scene_gpu_records.hpp` with these public records. Use the exact type names in later tasks.

```cpp
#pragma once

#include "core/math/vec.hpp"
#include "core/platform/types.hpp"

#include <array>

namespace LX_core {

struct alignas(16) SceneGpuVertexRecord final {
  Vec4f position{};
  Vec4f normal{};
  Vec4f uvTangentSign{};
  Vec4f tangent{};
};

struct alignas(16) SceneGpuMeshRecord final {
  u32 vertexOffset = 0;
  u32 indexOffset = 0;
  u32 indexCount = 0;
  u32 geometryIndex = 0;
};

struct alignas(16) SceneGpuPrimitiveRecord final {
  u32 indexOffset = 0;
  u32 meshIndex = 0;
  u32 materialIndex = 0;
  u32 objectIndex = 0;
};

struct alignas(16) SceneGpuObjectRecord final {
  std::array<Vec4f, 4> objectToWorld{};
  std::array<Vec4f, 4> worldToObject{};
  Vec4f boundsMin{};
  Vec4f boundsMax{};
  u32 visible = 1;
  u32 flags = 0;
  u32 visibilityMask = 0xffffffffu;
  u32 debugId = 0;
};

struct alignas(16) SceneGpuMaterialRecord final {
  Vec4f baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  Vec4f pbrParams{0.0f, 0.5f, 0.0f, 0.0f};
  Vec4f emissive{0.0f, 0.0f, 0.0f, 0.0f};
  u32 baseColorTexture = 0xffffffffu;
  u32 normalTexture = 0xffffffffu;
  u32 metallicRoughnessTexture = 0xffffffffu;
  u32 flags = 0;
};

struct alignas(16) SceneGpuFrameParams final {
  Vec4f eye{};
  Vec4f cameraRight{};
  Vec4f cameraUp{};
  Vec4f cameraForward{};
  Vec4f lightDirectionIntensity{};
  Vec4f lightColorEnvironment{};
  Vec4f backgroundColor{};
  u32 width = 0;
  u32 height = 0;
  u32 samples = 1;
  u32 seed = 1;
  u32 primitiveCount = 0;
  u32 bvhNodeCount = 0;
  u32 materialCount = 0;
  u32 maxBounce = 1;
  u32 shadowsEnabled = 1;
  u32 compareMode = 0;
  u32 integrator = 0;
  u32 frameIndex = 0;
};

static_assert(sizeof(SceneGpuVertexRecord) == 64);
static_assert(sizeof(SceneGpuMeshRecord) == 16);
static_assert(sizeof(SceneGpuPrimitiveRecord) == 16);
static_assert(sizeof(SceneGpuObjectRecord) == 176);
static_assert(sizeof(SceneGpuMaterialRecord) == 64);
static_assert(sizeof(SceneGpuFrameParams) == 176);

} // namespace LX_core
```

- [ ] **Step 4: Add upload view API**

Create `src/core/scene/scene_resource_table_upload_view.hpp`.

```cpp
#pragma once

#include "core/scene/scene_gpu_records.hpp"

#include <span>

namespace LX_core {

struct SceneResourceTableUploadView final {
  u64 generation = 0;
  std::span<const SceneGpuVertexRecord> vertices;
  std::span<const u32> indices;
  std::span<const SceneGpuMeshRecord> meshes;
  std::span<const SceneGpuPrimitiveRecord> primitives;
  std::span<const SceneGpuObjectRecord> objects;
  std::span<const SceneGpuMaterialRecord> materials;
};

} // namespace LX_core
```

Modify `SceneResourceTable` to include the new header, add `buildUploadView() const`, cached record vectors, and `m_generation`. Increment `m_generation` in every register, update, and release path.

- [ ] **Step 5: Implement minimal packing**

In `src/core/scene/scene_gpu_records.cpp`, add `toGpuRows(const Mat4f&)` and bounds helpers. In `SceneResourceTable::buildUploadView()`, fill object and material records from existing table entries, and fill mesh records from `MeshBuffer` offsets/counts. For the first pass, vertex and index vectors may be populated from `GeometryStorage`/`MeshBuffer` using the same accessors used by existing offline packing code.

- [ ] **Step 6: Run the focused test**

Run: `ninja -C build test_scene_resource_table && ./build/src/test/test_scene_resource_table`

Expected: `test_scene_resource_table passed`.

- [ ] **Step 7: Commit**

```bash
git add src/core/scene/scene_gpu_records.hpp src/core/scene/scene_gpu_records.cpp src/core/scene/scene_resource_table_upload_view.hpp src/core/scene/scene_resource_table.hpp src/core/scene/scene_resource_table.cpp src/test/integration/test_scene_resource_table.cpp
git commit -m "feat: add unified scene GPU record contract"
```

## Task 2: Move Software BVH to a Derived Acceleration Layer

**Files:**
- Create: `src/core/raytracing/software_bvh.hpp`
- Create: `src/core/raytracing/software_bvh.cpp`
- Modify: `src/test/integration/test_offline_gpu_scene.cpp`

- [ ] **Step 1: Replace old offline scene tests with BVH-from-table tests**

Rewrite `src/test/integration/test_offline_gpu_scene.cpp` to include `core/scene/scene_resource_table.hpp` and `core/raytracing/software_bvh.hpp`. Add this test body.

```cpp
void testSoftwareBvhBuildsFromSceneResourceTable() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(makeMeshBuffer());
  const auto material =
      table.registerMaterial(MaterialInstance::create(
          MaterialTemplate::create("software_bvh_material")));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 0.0f}};
  table.registerObject(object);

  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(table.buildUploadView());
  EXPECT(!bvh.nodes().empty(), "software BVH should contain nodes");
  EXPECT(bvh.primitiveCount() == 1,
         "one indexed triangle should produce one BVH primitive");
  EXPECT(static_cast<u32>(bvh.nodes().front().boundsMaxCount.w) == 1,
         "single-triangle BVH root should reference one primitive");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `ninja -C build test_offline_gpu_scene && ./build/src/test/test_offline_gpu_scene`

Expected: compile failure for missing `core/raytracing/software_bvh.hpp`.

- [ ] **Step 3: Add software BVH public API**

Create `src/core/raytracing/software_bvh.hpp`.

```cpp
#pragma once

#include "core/math/vec.hpp"
#include "core/platform/types.hpp"
#include "core/scene/scene_resource_table_upload_view.hpp"

#include <span>
#include <vector>

namespace LX_core {

struct alignas(16) SceneSoftwareBvhNode final {
  Vec4f boundsMinLeftFirst{};
  Vec4f boundsMaxCount{};
};

struct SceneSoftwareBvhPrimitive final {
  u32 primitiveIndex = 0;
  u32 objectIndex = 0;
  u32 meshIndex = 0;
};

class SceneSoftwareBvh final {
public:
  [[nodiscard]] static SceneSoftwareBvh
  build(const SceneResourceTableUploadView &scene);

  [[nodiscard]] std::span<const SceneSoftwareBvhNode> nodes() const;
  [[nodiscard]] std::span<const SceneSoftwareBvhPrimitive> primitives() const;
  [[nodiscard]] usize primitiveCount() const;

private:
  std::vector<SceneSoftwareBvhNode> m_nodes;
  std::vector<SceneSoftwareBvhPrimitive> m_primitives;
};

} // namespace LX_core
```

- [ ] **Step 4: Move the current BVH algorithm**

Create `src/core/raytracing/software_bvh.cpp` by moving the algorithmic parts of `OfflineBvhBuilder` from `src/core/offline/offline_ray_scene.cpp`. The new builder reads only `SceneResourceTableUploadView::vertices`, `indices`, `primitives`, and `objects`. It stores only `SceneSoftwareBvhNode` and `SceneSoftwareBvhPrimitive`.

- [ ] **Step 5: Run focused tests**

Run:

```bash
ninja -C build test_offline_gpu_scene test_scene_resource_table
./build/src/test/test_offline_gpu_scene
./build/src/test/test_scene_resource_table
```

Expected: both tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/core/raytracing/software_bvh.hpp src/core/raytracing/software_bvh.cpp src/test/integration/test_offline_gpu_scene.cpp
git commit -m "feat: derive software BVH from scene resource table"
```

## Task 3: Replace Offline Scene Compiler with SceneResourceTable Loader

**Files:**
- Delete: `src/infra/offline/offline_scene_compiler.hpp`
- Delete: `src/infra/offline/offline_scene_compiler.cpp`
- Create: `src/infra/offline/offline_scene_loader.hpp`
- Create: `src/infra/offline/offline_scene_loader.cpp`
- Rename: `src/test/integration/test_offline_scene_compiler.cpp` to `src/test/integration/test_offline_scene_loader.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Rename the compiler test target**

Run: `mv src/test/integration/test_offline_scene_compiler.cpp src/test/integration/test_offline_scene_loader.cpp`

Modify `src/test/CMakeLists.txt`: replace `test_offline_scene_compiler` with `test_offline_scene_loader`.

- [ ] **Step 2: Write failing loader assertions**

In `test_offline_scene_loader.cpp`, include `infra/offline/offline_scene_loader.hpp` and assert that `OfflineSceneLoader::loadFile()` returns a table with meshes, objects, materials, a camera, and warnings.

```cpp
void testSceneYamlLoadsIntoSceneResourceTable() {
  const std::filesystem::path scenePath =
      std::filesystem::current_path() / "assets" / "scenes" /
      "realtime_offline_compare_diagnostic.scene.yaml";
  LX_infra::offline::OfflineSceneLoader loader{
      LX_infra::offline::OfflineAssetResolver(scenePath)};
  const auto loaded = loader.loadFile(scenePath, "/game_cam");

  EXPECT(loaded.table.meshCount() >= 1,
         "scene loader should register meshes in SceneResourceTable");
  EXPECT(loaded.table.materialCount() >= 1,
         "scene loader should register materials in SceneResourceTable");
  EXPECT(loaded.table.objectCount() >= 1,
         "scene loader should register objects in SceneResourceTable");
  EXPECT(loaded.table.cameraCount() == 1,
         "scene loader should register the selected camera");
  EXPECT(!loaded.table.buildUploadView().primitives.empty(),
         "loaded table should expose GPU primitives");
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `ninja -C build test_offline_scene_loader && ./build/src/test/test_offline_scene_loader`

Expected: compile failure for missing `OfflineSceneLoader`.

- [ ] **Step 4: Add loader result and API**

Create `src/infra/offline/offline_scene_loader.hpp`.

```cpp
#pragma once

#include "core/scene/scene_resource_table.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace LX_infra::offline {

struct OfflineLoadedScene final {
  LX_core::SceneResourceTable table;
  std::vector<std::string> warnings;
};

class OfflineSceneLoader final {
public:
  explicit OfflineSceneLoader(OfflineAssetResolver resolver);

  [[nodiscard]] OfflineLoadedScene
  load(const LX_infra::scene_io::SceneDocument &document,
       const std::string &cameraPath) const;

  [[nodiscard]] OfflineLoadedScene
  loadFile(const std::filesystem::path &path,
           const std::string &cameraPath) const;

private:
  OfflineAssetResolver m_resolver;
};

} // namespace LX_infra::offline
```

- [ ] **Step 5: Port compiler logic into table registration**

Create `offline_scene_loader.cpp` by moving parsing and asset resolution logic from `offline_scene_compiler.cpp`, but register meshes/materials/objects/camera directly in `SceneResourceTable`. Do not return `OfflineSceneIR`.

- [ ] **Step 6: Update infra build source list**

In `src/infra/CMakeLists.txt`, replace:

```cmake
    offline/offline_scene_compiler.cpp
```

with:

```cmake
    offline/offline_scene_loader.cpp
```

- [ ] **Step 7: Run focused loader test**

Run: `ninja -C build test_offline_scene_loader && ./build/src/test/test_offline_scene_loader`

Expected: `test_offline_scene_loader passed`.

- [ ] **Step 8: Commit**

```bash
git add src/infra/offline/offline_scene_loader.hpp src/infra/offline/offline_scene_loader.cpp src/infra/CMakeLists.txt src/test/CMakeLists.txt src/test/integration/test_offline_scene_loader.cpp
git rm src/infra/offline/offline_scene_compiler.hpp src/infra/offline/offline_scene_compiler.cpp
git commit -m "feat: load offline scenes into scene resource table"
```

## Task 4: Remove OfflineSceneIR and OfflineRayScene

**Files:**
- Create: `src/core/offline/offline_render_job.hpp`
- Modify: `src/core/offline/offline_render_profile.hpp`
- Modify: `src/core/offline/offline_render_profile.cpp`
- Delete: `src/core/offline/offline_scene.hpp`
- Delete: `src/core/offline/offline_scene.cpp`
- Delete: `src/core/offline/offline_ray_scene.hpp`
- Delete: `src/core/offline/offline_ray_scene.cpp`
- Modify: `src/tools/lxe_offline_render/main.cpp`
- Modify: include sites found by `rg -n "offline_scene|offline_ray_scene|OfflineSceneIR|OfflineRayScene" src`

- [x] **Step 1: Write compile-removal check**

Run: `rg -n "OfflineSceneIR|OfflineRayScene|OfflineRaySceneBuilder|OfflineBvhBuilder" src`

Expected before edits: references in core, backend, tests, infra, and tools. Keep the output for cleanup accounting.

- [x] **Step 2: Add new job type**

Create `src/core/offline/offline_render_job.hpp`.

```cpp
#pragma once

#include "core/offline/offline_render_profile.hpp"
#include "core/platform/types.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace LX_core::offline {

struct OfflineRenderJob final {
  SceneResourceTable scene;
  OutputProfile output;
  OfflineRenderSettings offline;
  std::string profileName;
  std::filesystem::path outputPath;
};

struct OfflineReadbackImage final {
  u32 width = 0;
  u32 height = 0;
  std::vector<float> rgba;
};

} // namespace LX_core::offline
```

- [x] **Step 3: Change includes to the new job header**

Replace includes of `core/offline/offline_scene.hpp` used only for `OfflineRenderJob` or `OfflineReadbackImage` with `core/offline/offline_render_job.hpp`.

- [x] **Step 4: Update CLI job construction**

In `src/tools/lxe_offline_render/main.cpp`, replace compiler usage with loader usage.

```cpp
LX_infra::offline::OfflineAssetResolver resolver(args.scenePath);
LX_infra::offline::OfflineSceneLoader loader(resolver);
auto loaded = loader.load(document, resolved.output.cameraPath);
for (const auto &warning : loaded.warnings) {
  std::cerr << "[offline warning] " << warning << '\n';
}

LX_core::offline::OfflineRenderJob job;
job.scene = std::move(loaded.table);
job.output = resolved.output;
job.offline = resolved.offline;
job.profileName = resolved.profileName;
job.outputPath = resolved.outputPath.value_or("");
```

- [x] **Step 5: Delete old core files**

Run:

```bash
git rm src/core/offline/offline_scene.hpp src/core/offline/offline_scene.cpp
git rm src/core/offline/offline_ray_scene.hpp src/core/offline/offline_ray_scene.cpp
```

- [x] **Step 6: Run removal search**

Run: `rg -n "OfflineSceneIR|OfflineRayScene|OfflineRaySceneBuilder|OfflineBvhBuilder|offline_scene_compiler|offline_ray_scene" src`

Expected: no matches.

- [x] **Step 7: Build focused targets**

Run:

```bash
ninja -C build lxe_offline_render test_offline_scene_loader test_offline_gpu_scene
```

Expected: all targets build.

- [x] **Step 8: Commit**

```bash
git add src/core/offline/offline_render_job.hpp src/core/offline/offline_render_profile.hpp src/core/offline/offline_render_profile.cpp src/tools/lxe_offline_render/main.cpp
git rm src/core/offline/offline_scene.hpp src/core/offline/offline_scene.cpp src/core/offline/offline_ray_scene.hpp src/core/offline/offline_ray_scene.cpp
git commit -m "refactor: remove offline-only scene data models"
```

## Task 5: Add Explicit Offline Integrator Boundary

**Files:**
- Create: `src/backend/vulkan/offline/offline_integrator.hpp`
- Create: `src/backend/vulkan/offline/software_compute_offline_integrator.hpp`
- Create: `src/backend/vulkan/offline/software_compute_offline_integrator.cpp`
- Modify: `src/backend/vulkan/offline/vulkan_offline_renderer.hpp`
- Modify: `src/backend/vulkan/offline/vulkan_offline_renderer.cpp`
- Modify: `src/core/offline/offline_render_profile.hpp`
- Modify: `src/core/offline/offline_render_profile.cpp`
- Create: `src/test/integration/test_offline_integrator_selection.cpp`
- Modify: `src/test/CMakeLists.txt`

- [x] **Step 1: Add failing integrator selection test**

Create `src/test/integration/test_offline_integrator_selection.cpp`.

```cpp
#include "backend/vulkan/offline/offline_integrator.hpp"
#include "core/offline/offline_render_profile.hpp"

#include <iostream>
#include <stdexcept>

using namespace LX_core;

namespace {
int failures = 0;
#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL: " << msg << '\n';                                    \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testSoftwareComputeNameIsSupported() {
  EXPECT(backend::offline::isOfflineIntegratorSupported("software-compute"),
         "software-compute integrator should be supported");
}

void testPrimaryRayNameIsRejected() {
  EXPECT(!backend::offline::isOfflineIntegratorSupported("primary-ray"),
         "primary-ray should not remain as the public integrator name");
}

void testDefaultIntegratorName() {
  const auto settings = offline::makeDefaultOfflineRenderSettings();
  EXPECT(settings.integrator == "software-compute",
         "default offline integrator should be software-compute");
}
} // namespace

int main() {
  testSoftwareComputeNameIsSupported();
  testPrimaryRayNameIsRejected();
  testDefaultIntegratorName();
  if (failures != 0) {
    return 1;
  }
  std::cout << "test_offline_integrator_selection passed\n";
  return 0;
}
```

Add `test_offline_integrator_selection` to `TEST_INTEGRATION_EXE_LIST` in `src/test/CMakeLists.txt`.

- [x] **Step 2: Run the test to verify it fails**

Run: `ninja -C build test_offline_integrator_selection`

Expected: compile failure for missing `backend/vulkan/offline/offline_integrator.hpp`.

- [x] **Step 3: Add integrator interface**

Create `src/backend/vulkan/offline/offline_integrator.hpp`.

```cpp
#pragma once

#include "core/offline/offline_render_job.hpp"

#include <memory>
#include <string>

namespace LX_core::backend::offline {

class OfflineIntegrator {
public:
  virtual ~OfflineIntegrator() = default;
  [[nodiscard]] virtual LX_core::offline::OfflineReadbackImage
  render(const LX_core::offline::OfflineRenderJob &job) = 0;
};

[[nodiscard]] bool isOfflineIntegratorSupported(const std::string &name);
[[nodiscard]] std::unique_ptr<OfflineIntegrator>
createOfflineIntegrator(const std::string &name);

} // namespace LX_core::backend::offline
```

- [x] **Step 4: Rename default integrator**

In `makeDefaultOfflineRenderSettings()`, set `integrator = "software-compute"`. In profile resolution, reject an empty integrator and preserve explicit unsupported names for renderer validation.

- [x] **Step 5: Move compute dispatch into SoftwareComputeOfflineIntegrator**

Create `software_compute_offline_integrator.hpp/.cpp`. Move the existing compute pipeline setup, descriptor validation, upload, dispatch, and readback logic from `VulkanOfflineRenderer::Impl` into this integrator. Change the input packing to use `job.scene.buildUploadView()` and `SceneSoftwareBvh::build(view)`.

- [x] **Step 6: Make VulkanOfflineRenderer a coordinator**

In `vulkan_offline_renderer.cpp`, keep device lifetime and render entry point, but select the integrator explicitly:

```cpp
if (!isOfflineIntegratorSupported(job.offline.integrator)) {
  throw std::runtime_error("unsupported offline integrator: " +
                           job.offline.integrator);
}
auto integrator = createOfflineIntegrator(job.offline.integrator);
return integrator->render(job);
```

- [x] **Step 7: Run focused tests**

Run:

```bash
ninja -C build test_offline_integrator_selection test_offline_render_cli lxe_offline_render
./build/src/test/test_offline_integrator_selection
./build/src/test/test_offline_render_cli
```

Expected: tests pass; unsupported integrator path returns a clear error in CLI tests.

- [x] **Step 8: Commit**

```bash
git add src/backend/vulkan/offline/offline_integrator.hpp src/backend/vulkan/offline/software_compute_offline_integrator.hpp src/backend/vulkan/offline/software_compute_offline_integrator.cpp src/backend/vulkan/offline/vulkan_offline_renderer.hpp src/backend/vulkan/offline/vulkan_offline_renderer.cpp src/core/offline/offline_render_profile.hpp src/core/offline/offline_render_profile.cpp src/test/CMakeLists.txt src/test/integration/test_offline_integrator_selection.cpp
git commit -m "feat: add explicit offline integrator boundary"
```

## Task 6: Update Offline Shader to Unified Scene Records

**Files:**
- Modify: `assets/shaders/glsl/offline_primary_ray.comp`
- Modify: `src/backend/vulkan/offline/software_compute_offline_integrator.cpp`
- Modify: shader reflection expectations in the integrator
- Modify: `src/test/integration/test_offline_gpu_scene.cpp`

- [x] **Step 1: Add shader descriptor contract test**

In `test_offline_gpu_scene.cpp`, include `core/asset/shader.hpp`, `infra/shader_compiler/shader_compiler.hpp`, and `infra/shader_compiler/shader_reflector.hpp`. Add helpers that compile `assets/shaders/glsl/offline_primary_ray.comp` through `ShaderCompiler::compileFile()` and assert unified binding names:

```cpp
bool hasStorageBuffer(const std::vector<ShaderResourceBinding> &bindings,
                      const std::string &name) {
  for (const auto &binding : bindings) {
    if (binding.name == name &&
        binding.type == ShaderPropertyType::StorageBuffer) {
      return true;
    }
  }
  return false;
}

void testOfflineShaderUsesUnifiedSceneBuffers() {
  const auto shaderPath = std::filesystem::current_path() / "assets" /
                          "shaders" / "glsl" / "offline_primary_ray.comp";
  const auto compileResult = LX_infra::ShaderCompiler::compileFile(shaderPath);
  EXPECT(compileResult.success,
         "offline shader should compile before reflection");
  const auto bindings = LX_infra::ShaderReflector::reflect(compileResult.stages);
  EXPECT(hasStorageBuffer(bindings, "SceneVertices"),
         "offline shader should use unified SceneVertices SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneIndices"),
         "offline shader should use unified SceneIndices SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneMeshes"),
         "offline shader should use unified SceneMeshes SSBO");
  EXPECT(hasStorageBuffer(bindings, "ScenePrimitives"),
         "offline shader should use unified ScenePrimitives SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneObjects"),
         "offline shader should use unified SceneObjects SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneMaterials"),
         "offline shader should use unified SceneMaterials SSBO");
}
```

- [x] **Step 2: Run shader compile target to verify failure**

Run: `ninja -C build CompileShaders test_offline_gpu_scene`

Expected: test failure because the current shader still exposes old offline buffer names.

- [x] **Step 3: Update GLSL structs and bindings**

In `offline_primary_ray.comp`, replace old `Offline*` structs with `lx`-prefixed unified records:

```glsl
struct lxSceneVertexRecord {
  vec4 position;
  vec4 normal;
  vec4 uvTangentSign;
  vec4 tangent;
};

struct lxSceneMaterialRecord {
  vec4 baseColor;
  vec4 pbrParams;
  vec4 emissive;
  uvec4 textureFlags;
};
```

Keep descriptor set numbers compatible with realtime/bindless design. Do not use macros to paper over incompatible set numbers; update C++ descriptor layout code to match the unified binding contract.

- [x] **Step 4: Update descriptor reflection validation**

In `software_compute_offline_integrator.cpp`, change validation names from `Vertices`, `Meshes`, `Objects`, and `Materials` to the unified names from Step 1. Upload buffers from `SceneResourceTableUploadView`, and upload `SceneSoftwareBvh::nodes()` as the acceleration buffer.

- [x] **Step 5: Run shader and focused offline tests**

Run:

```bash
ninja -C build CompileShaders test_offline_gpu_scene lxe_offline_render
./build/src/test/test_offline_gpu_scene
```

Expected: shader compiles and the GPU-scene test passes.

- [x] **Step 6: Commit**

```bash
git add assets/shaders/glsl/offline_primary_ray.comp src/backend/vulkan/offline/software_compute_offline_integrator.cpp src/test/integration/test_offline_gpu_scene.cpp
git commit -m "refactor: use unified scene records in offline shader"
```

## Task 7: Preserve Offline MVP and Realtime Comparison

**Files:**
- Modify: `src/test/integration/test_offline_render_cli.cpp`
- Modify: `src/test/integration/test_realtime_offline_compare_flat.py`
- Modify: scene fixtures only if their YAML references removed fields

- [x] **Step 1: Add CLI regression for software-compute**

In `test_offline_render_cli.cpp`, add or update a case that parses a profile with:

```yaml
render:
  offline:
    integrator: software-compute
```

Assert the resolved job uses `software-compute`, renders a non-empty image, and fails clearly for `integrator: hardware-ray-tracing`.

- [x] **Step 2: Run CLI regression**

Run: `ninja -C build test_offline_render_cli && ./build/src/test/test_offline_render_cli`

Expected: CLI tests pass.

- [x] **Step 3: Run offline render smoke**

Run:

```bash
ninja -C build lxe_offline_render lxe_compare_exr
./build/src/tools/lxe_offline_render/lxe_offline_render --scene assets/scenes/realtime_offline_compare_diagnostic.scene.yaml --profile preview --width 64 --height 64 --samples 1 --out artifacts/unified_scene_gpu_data_smoke.exr
```

Expected: command completes, prints finite center pixel values, and writes EXR/PNG outputs.

- [x] **Step 4: Run realtime/offline comparison**

Run: `xvfb-run -a ctest --test-dir build --output-on-failure -R test_realtime_offline_compare_flat`

Expected: comparison passes within the existing threshold.

- [x] **Step 5: Commit**

```bash
git add src/test/integration/test_offline_render_cli.cpp src/test/integration/test_realtime_offline_compare_flat.py
git commit -m "test: preserve realtime offline comparison"
```

## Task 8: Document Break Changes and Deferred Capabilities

**Files:**
- Create: `notes/requirements/2026-06-03-unified-scene-gpu-data-breaks.md`
- Modify: `notes/subsystems/scene.md`
- Modify: `notes/subsystems/vulkan-backend.md`
- Modify: `notes/concepts/material/shader.md` if shader binding docs mention the old offline path

- [x] **Step 1: Write break-change note**

Create `notes/requirements/2026-06-03-unified-scene-gpu-data-breaks.md`:

```markdown
# Unified Scene GPU Data Break Changes

This note records accepted break changes from the cleanup that made `SceneResourceTable` the only authoritative scene GPU data contract.

## Removed APIs

- `LX_core::offline::OfflineSceneIR`
- `LX_core::offline::OfflineRayScene`
- `LX_core::offline::OfflineRaySceneBuilder`
- `LX_core::offline::OfflineBvhBuilder`
- `LX_infra::offline::OfflineSceneCompiler`

## Deferred Capabilities

- Hardware ray tracing pipeline creation remains an explicit future integrator. Recovery rule: add it by deriving BLAS/TLAS/SBT data from `SceneResourceTable`, not by adding another scene IR.
- Offline-only YAML fields that do not map to `SceneResourceTable` are unsupported. Recovery rule: add the missing record field or resource handle to `SceneResourceTable`.

## Preserved Capabilities

- Software BVH remains available through `SceneSoftwareBvh`.
- The offline software-compute renderer remains the MVP offline renderer.
- Realtime/offline comparison remains the output regression.
```

- [x] **Step 2: Update subsystem notes**

Update the scene and Vulkan backend notes to say that `SceneResourceTable` is the scene GPU data contract, `SceneSoftwareBvh` is a derived acceleration structure, and `VulkanOfflineRenderer` coordinates explicit integrators.

- [x] **Step 3: Run documentation consistency search**

Run: `rg -n "OfflineSceneIR|OfflineRayScene|OfflineSceneCompiler|primary-ray" notes docs src`

Expected: matches only in this plan, the design spec, and the break-change note. No active source or subsystem note should describe the removed path as current behavior.

- [x] **Step 4: Commit**

```bash
git add notes/requirements/2026-06-03-unified-scene-gpu-data-breaks.md notes/subsystems/scene.md notes/subsystems/vulkan-backend.md notes/concepts/material/shader.md
git commit -m "docs: record unified scene gpu data break changes"
```

## Task 9: Final Verification and Cleanup

**Files:**
- All files touched by previous tasks.

- [ ] **Step 1: Verify no removed APIs remain in source**

Run:

```bash
rg -n "OfflineSceneIR|OfflineRayScene|OfflineRaySceneBuilder|OfflineBvhBuilder|OfflineSceneCompiler|primary-ray" src assets/shaders/glsl
```

Expected: no matches.

- [ ] **Step 2: Build primary targets**

Run:

```bash
ninja -C build CompileShaders
ninja -C build lxe_editor lxe_offline_render BuildTest
```

Expected: all targets build.

- [ ] **Step 3: Run headless tests**

Run: `ctest --test-dir build --output-on-failure -L auto -LE requires_video_device`

Expected: all headless tests pass.

- [ ] **Step 4: Run video-device comparison tests**

Run: `xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device`

Expected: all video-device tests pass, including `test_realtime_offline_compare_flat`.

- [ ] **Step 5: Inspect worktree**

Run:

```bash
git status --short
git log --oneline -n 10
```

Expected: worktree clean after the final commit, and recent commits match the task commits above.

- [ ] **Step 6: Final commit for verification drift**

If verification required small fixes, commit them:

```bash
git add <changed-files>
git commit -m "fix: stabilize unified scene gpu data verification"
```

Skip this commit when `git status --short` is clean.
