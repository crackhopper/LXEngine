# Shared PBR Equivalence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build one shared glTF/PBR loading path used by editor, realtime profile output, and offline renderer, then verify DamagedHelmet direct-light PBR equivalence with linear EXR comparison while preserving the existing MVP offline shader path.

**Architecture:** Move the current lxe_editor-local DamagedHelmet mesh/material bridge into infra as a reusable scene asset loader that produces `MeshBuffer` and `MaterialInstance`. Realtime and offline both consume the same `SceneResourceTable`; only raster draw versus primary-ray hit differs. Add a second offline compute shader for PBR direct lighting and extract common Cook-Torrance code into `assets/shaders/glsl/common/pbr.glsl`.

**Tech Stack:** C++20, CMake/Ninja, Vulkan, GLSL, yaml-cpp, cgltf, stb_image, TinyEXR, `lxe_offline_render`, `lxe_realtime_render.py`, `lxe_compare_exr`.

---

## File Structure

- Create `src/infra/scene_asset/gltf_scene_asset_loader.hpp`: public shared loader API and result structs.
- Create `src/infra/scene_asset/gltf_scene_asset_loader.cpp`: glTF mesh conversion, tangent generation, PBR material bridge, texture loading, diagnostics.
- Modify `src/infra/CMakeLists.txt`: add the new scene asset loader source.
- Modify `src/demos/lxe_editor/scene_builder.cpp`: remove duplicated glTF mesh/material/tangent/texture helper code and call shared loader.
- Modify `src/demos/lxe_editor/scene_runtime.cpp`: make `builtin://lxe_editor/helmet` and plain glTF `mesh.uri` delegate to the shared loader.
- Modify `src/infra/offline/offline_scene_loader.cpp`: support non-builtin glTF mesh URIs through the same shared loader.
- Modify `src/core/scene/scene_gpu_records.hpp/.cpp`: extend material GPU record to include AO/emissive texture indices and flags.
- Modify `src/core/scene/scene_resource_table.hpp/.cpp` and `src/core/scene/scene_resource_table_upload_view.hpp`: carry material texture samplers or texture table entries into offline upload.
- Modify `src/core/offline/offline_scene_storage_resources.hpp/.cpp`: build offline texture descriptor resources and PBR material records from upload view.
- Modify `src/backend/vulkan/offline/offline_compute_shader.cpp`: support multiple offline compute shaders and descriptor contracts.
- Create `assets/shaders/glsl/common/pbr.glsl`: shared direct-light PBR functions.
- Modify `assets/shaders/glsl/pbr.frag`: include shared common and keep IBL/shadow behavior controlled by existing resources/config.
- Create `assets/shaders/glsl/offline_pbr_direct_ray.comp`: primary-ray direct-light PBR shader using the same common.
- Modify `src/infra/scene_io/scene_document.cpp`, `src/core/offline/offline_render_job.hpp`, and `src/core/offline/offline_render_profile.hpp/.cpp`: add config value for selecting offline shader/integrator mode without breaking `software-compute`.
- Add `assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml`: direct-light equivalence scene.
- Modify `src/tools/CMakeLists.txt`: add a new `RunRealtimeOfflineHelmetPbrCompare` custom target while keeping `RunRealtimeOfflineDiagnostic`.
- Modify tests under `src/test/integration/`: add shared loader, offline loader, GPU scene, shader, and material coverage tests.

## Task 1: Shared Loader Failing Tests

**Files:**
- Modify: `src/test/integration/CMakeLists.txt`
- Create: `src/test/integration/test_gltf_scene_asset_loader.cpp`
- Create in Task 2: `src/infra/scene_asset/gltf_scene_asset_loader.hpp`

- [x] **Step 1: Write the failing shared-loader test**

Add this test file:

```cpp
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

bool hasTexture(const LX_core::MaterialInstanceSharedPtr &material,
                const char *binding) {
  return material && material->getTexture(LX_core::StringID(binding)) != nullptr;
}

void testDamagedHelmetSharedAssetLoadsFullPbr() {
  const bool found = LX_core::cdToWhereAssetsExist(
      "models/damaged_helmet/DamagedHelmet.gltf");
  expect(found, "DamagedHelmet asset root must be discoverable");

  const auto result = LX_infra::scene_asset::loadGltfSceneAsset(
      "assets/models/damaged_helmet/DamagedHelmet.gltf");

  expect(result.mesh != nullptr, "shared loader should create mesh");
  expect(result.material != nullptr, "shared loader should create material");
  expect(result.generatedTangents,
         "DamagedHelmet should generate tangents because glTF has no TANGENT");
  expect(result.normalMapEnabled,
         "DamagedHelmet normal map should remain enabled after tangent generation");
  expect(hasTexture(result.material, "albedoMap"),
         "shared loader should bind albedoMap");
  expect(hasTexture(result.material, "normalMap"),
         "shared loader should bind normalMap");
  expect(hasTexture(result.material, "metallicRoughnessMap"),
         "shared loader should bind metallicRoughnessMap");
  expect(hasTexture(result.material, "aoMap"),
         "shared loader should bind aoMap");
  expect(hasTexture(result.material, "emissiveMap"),
         "shared loader should bind emissiveMap");
}

} // namespace

int main() {
  testDamagedHelmetSharedAssetLoadsFullPbr();
  std::cout << "test_gltf_scene_asset_loader passed\n";
  return 0;
}
```

Add it to `src/test/integration/CMakeLists.txt` using the existing test pattern:

```cmake
add_lx_integration_test(test_gltf_scene_asset_loader
  test_gltf_scene_asset_loader.cpp
)
target_link_libraries(test_gltf_scene_asset_loader
  PRIVATE
    lxe_core
    lxe_infra
)
```

- [x] **Step 2: Run the test and verify it fails**

Run:

```bash
cmake --build build --target test_gltf_scene_asset_loader -j2
```

Expected: build fails because `infra/scene_asset/gltf_scene_asset_loader.hpp` does not exist.

- [x] **Step 3: Commit the failing test**

```bash
git add src/test/integration/CMakeLists.txt src/test/integration/test_gltf_scene_asset_loader.cpp
git commit -m "test: cover shared gltf pbr asset loader"
```

## Task 2: Shared glTF Scene Asset Loader

**Files:**
- Create: `src/infra/scene_asset/gltf_scene_asset_loader.hpp`
- Create: `src/infra/scene_asset/gltf_scene_asset_loader.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Modify: `src/demos/lxe_editor/scene_builder.cpp`

- [x] **Step 1: Add the shared loader header**

Create `src/infra/scene_asset/gltf_scene_asset_loader.hpp`:

```cpp
#pragma once

#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace LX_infra::scene_asset {

struct GltfSceneAssetLoadResult final {
  LX_core::MeshSharedPtr mesh;
  LX_core::MaterialInstanceSharedPtr material;
  bool generatedTangents = false;
  bool normalMapEnabled = false;
  std::vector<std::string> warnings;
};

[[nodiscard]] GltfSceneAssetLoadResult
loadGltfSceneAsset(const std::filesystem::path &gltfPath);

} // namespace LX_infra::scene_asset
```

- [x] **Step 2: Move mesh/tangent/texture/material bridge code**

Create `src/infra/scene_asset/gltf_scene_asset_loader.cpp` by moving these responsibilities from `src/demos/lxe_editor/scene_builder.cpp`:

```cpp
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"

#include "core/asset/texture.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/mesh_loader/gltf_mesh_loader.hpp"
#include "infra/texture_loader/texture_loader.hpp"

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace LX_infra::scene_asset {
namespace {

using LX_core::CombinedTextureSampler;
using LX_core::CombinedTextureSamplerSharedPtr;
using LX_core::IndexBuffer;
using LX_core::MaterialInstanceSharedPtr;
using LX_core::MeshSharedPtr;
using LX_core::StringID;
using LX_core::Texture;
using LX_core::TextureDesc;
using LX_core::TextureFormat;
using LX_core::Vec2f;
using LX_core::Vec3f;
using LX_core::Vec4f;
using LX_core::Vec4i;
using LX_core::VertexBuffer;
using LX_core::VertexPosNormalUvBone;

[[nodiscard]] std::vector<Vec4f>
generateTangents(const std::vector<Vec3f> &positions,
                 const std::vector<Vec3f> &normals,
                 const std::vector<Vec2f> &uvs,
                 const std::vector<u32> &indices) {
  std::vector<Vec3f> tangentSums(positions.size(), Vec3f{0.0f, 0.0f, 0.0f});
  std::vector<Vec3f> bitangentSums(positions.size(), Vec3f{0.0f, 0.0f, 0.0f});
  for (usize i = 0; i + 2u < indices.size(); i += 3u) {
    const u32 i0 = indices[i + 0u];
    const u32 i1 = indices[i + 1u];
    const u32 i2 = indices[i + 2u];
    if (i0 >= positions.size() || i1 >= positions.size() ||
        i2 >= positions.size() || i0 >= uvs.size() || i1 >= uvs.size() ||
        i2 >= uvs.size()) {
      continue;
    }
    const Vec3f edge1 = positions[i1] - positions[i0];
    const Vec3f edge2 = positions[i2] - positions[i0];
    const Vec2f deltaUv1 = uvs[i1] - uvs[i0];
    const Vec2f deltaUv2 = uvs[i2] - uvs[i0];
    const float determinant =
        deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x;
    if (std::abs(determinant) < 1.0e-6f) {
      continue;
    }
    const float r = 1.0f / determinant;
    const Vec3f tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * r;
    const Vec3f bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * r;
    tangentSums[i0] += tangent;
    tangentSums[i1] += tangent;
    tangentSums[i2] += tangent;
    bitangentSums[i0] += bitangent;
    bitangentSums[i1] += bitangent;
    bitangentSums[i2] += bitangent;
  }

  std::vector<Vec4f> generated;
  generated.reserve(positions.size());
  const Vec3f fallbackTangent{1.0f, 0.0f, 0.0f};
  for (usize i = 0; i < positions.size(); ++i) {
    const Vec3f normal =
        i < normals.size() ? normals[i].normalized() : Vec3f{0.0f, 1.0f, 0.0f};
    Vec3f tangent = tangentSums[i] - normal * normal.dot(tangentSums[i]);
    tangent = tangent.length2() <= 1.0e-8f ? fallbackTangent : tangent.normalized();
    const float sign =
        normal.cross(tangent).dot(bitangentSums[i]) < 0.0f ? -1.0f : 1.0f;
    generated.emplace_back(tangent.x, tangent.y, tangent.z, sign);
  }
  return generated;
}

[[nodiscard]] CombinedTextureSamplerSharedPtr
loadCombinedTexture(const std::filesystem::path &path) {
  infra::TextureLoader loader;
  loader.load(path.string());
  if (loader.getWidth() <= 0 || loader.getHeight() <= 0 ||
      loader.getData() == nullptr) {
    throw std::runtime_error("failed to load texture: " + path.string());
  }
  const usize byteCount = static_cast<usize>(loader.getWidth()) *
                          static_cast<usize>(loader.getHeight()) * 4u;
  std::vector<u8> pixels(loader.getData(), loader.getData() + byteCount);
  TextureDesc desc{static_cast<u32>(loader.getWidth()),
                   static_cast<u32>(loader.getHeight()), TextureFormat::RGBA8};
  auto tex = std::make_shared<Texture>(desc, std::move(pixels));
  return std::make_shared<CombinedTextureSampler>(std::move(tex));
}

[[nodiscard]] MeshSharedPtr buildMeshFromGltf(const infra::GLTFLoader &loader,
                                              bool &generatedTangents) {
  const auto &positions = loader.getPositions();
  const auto &normals = loader.getNormals();
  const auto &uvs = loader.getTexCoords();
  const auto &authoredTangents = loader.getTangents();
  const auto &indices = loader.getIndices();
  if (positions.empty() || indices.empty()) {
    throw std::runtime_error("glTF asset has empty mesh geometry");
  }
  std::vector<Vec4f> generated;
  if (authoredTangents.empty() && !uvs.empty()) {
    generated = generateTangents(positions, normals, uvs, indices);
    generatedTangents = !generated.empty();
  }
  std::vector<VertexPosNormalUvBone> vertices;
  vertices.reserve(positions.size());
  const Vec3f fallbackNormal{0.0f, 1.0f, 0.0f};
  const Vec2f fallbackUv{0.0f, 0.0f};
  const Vec4f fallbackTangent{1.0f, 0.0f, 0.0f, 1.0f};
  const Vec4i zeroBones{0, 0, 0, 0};
  const Vec4f zeroWeights{0.0f, 0.0f, 0.0f, 0.0f};
  for (usize i = 0; i < positions.size(); ++i) {
    const Vec3f normal = i < normals.size() ? normals[i] : fallbackNormal;
    const Vec2f uv = i < uvs.size() ? uvs[i] : fallbackUv;
    const Vec4f tangent =
        i < authoredTangents.size()
            ? authoredTangents[i]
            : (i < generated.size() ? generated[i] : fallbackTangent);
    vertices.emplace_back(positions[i], normal, uv, tangent, zeroBones, zeroWeights);
  }
  auto vb = VertexBuffer<VertexPosNormalUvBone>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::vector<u32>(indices));
  return LX_core::Mesh::create(vb, ib, loader.getBounds());
}

void bindTextureIfPresent(MaterialInstanceSharedPtr &material,
                          const std::filesystem::path &gltfDir,
                          const std::string &uri,
                          const char *bindingName) {
  if (uri.empty()) {
    return;
  }
  material->setTexture(StringID(bindingName), loadCombinedTexture(gltfDir / uri));
}

[[nodiscard]] MaterialInstanceSharedPtr
buildMaterialFromGltfPbr(const infra::GLTFPbrMaterial &pbr,
                         const std::filesystem::path &gltfDir,
                         const bool normalMapEnabled) {
  auto material =
      LX_infra::loadGenericMaterial("assets/materials/pbr_gltf_helmet.material");
  if (!material) {
    throw std::runtime_error("failed to load pbr_gltf_helmet.material");
  }
  material->setParameter(StringID("MaterialUBO"), StringID("baseColorFactor"),
                         pbr.baseColorFactor);
  material->setParameter(StringID("MaterialUBO"), StringID("metallicFactor"),
                         pbr.metallicFactor);
  material->setParameter(StringID("MaterialUBO"), StringID("roughnessFactor"),
                         pbr.roughnessFactor);
  material->setParameter(StringID("MaterialUBO"), StringID("ao"), 1.0f);
  bindTextureIfPresent(material, gltfDir, pbr.baseColorTexture, "albedoMap");
  bindTextureIfPresent(material, gltfDir, pbr.metallicRoughnessTexture,
                       "metallicRoughnessMap");
  bindTextureIfPresent(material, gltfDir, pbr.occlusionTexture, "aoMap");
  bindTextureIfPresent(material, gltfDir, pbr.emissiveTexture, "emissiveMap");
  if (normalMapEnabled) {
    bindTextureIfPresent(material, gltfDir, pbr.normalTexture, "normalMap");
  }
  material->syncGpuData();
  return material;
}

} // namespace

GltfSceneAssetLoadResult
loadGltfSceneAsset(const std::filesystem::path &gltfPath) {
  const std::filesystem::path resolved =
      gltfPath.is_absolute() ? gltfPath : LX_core::resolveRuntimePath(gltfPath);
  infra::GLTFLoader loader;
  loader.load(resolved.string());
  GltfSceneAssetLoadResult result;
  result.mesh = buildMeshFromGltf(loader, result.generatedTangents);
  const bool hasTangentBasis =
      !loader.getTangents().empty() || result.generatedTangents;
  result.normalMapEnabled =
      hasTangentBasis && !loader.getMaterial().normalTexture.empty();
  if (!result.normalMapEnabled && !loader.getMaterial().normalTexture.empty()) {
    result.warnings.push_back("normal map disabled because tangent basis is unavailable");
  }
  result.material = buildMaterialFromGltfPbr(loader.getMaterial(),
                                             resolved.parent_path(),
                                             result.normalMapEnabled);
  return result;
}

} // namespace LX_infra::scene_asset
```

- [x] **Step 3: Add the source to CMake**

In `src/infra/CMakeLists.txt`, add:

```cmake
scene_asset/gltf_scene_asset_loader.cpp
```

to the `lxe_infra` source list next to other loader sources.

- [x] **Step 4: Replace editor scene_builder duplication with shared loader**

In `src/demos/lxe_editor/scene_builder.cpp`, remove local `generateTangents`, `buildMeshFromGltf`, `loadCombinedTexture`, and `makeHelmetMaterialFromPbr`. Include:

```cpp
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"
```

Change `buildHelmetNode(...)` so it uses:

```cpp
auto asset = LX_infra::scene_asset::loadGltfSceneAsset(gltfPath);
return makeRenderableNode("helmet", std::move(asset.mesh),
                          std::move(asset.material));
```

- [x] **Step 5: Run the shared-loader test**

Run:

```bash
cmake --build build --target test_gltf_scene_asset_loader -j2
./build/src/test/integration/test_gltf_scene_asset_loader
```

Expected: build succeeds and executable prints `test_gltf_scene_asset_loader passed`.

- [x] **Step 6: Commit**

```bash
git add src/infra/scene_asset src/infra/CMakeLists.txt src/demos/lxe_editor/scene_builder.cpp
git commit -m "feat: share gltf pbr asset loading"
```

## Task 3: Runtime And Offline Loader Use The Shared Loader

**Files:**
- Modify: `src/demos/lxe_editor/scene_runtime.cpp`
- Modify: `src/infra/offline/offline_scene_loader.cpp`
- Modify: `src/test/integration/test_scene_runtime.cpp`
- Modify: `src/test/integration/test_offline_scene_loader.cpp`

- [x] **Step 1: Add failing runtime/offline scene tests**

In `test_scene_runtime.cpp`, add a test that loads plain glTF URI:

```cpp
void testPlainGltfHelmetUsesSharedPbrBridge() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_runtime_plain_gltf_helmet.yaml");
  writeSceneFile(path, "scene:\n"
                       "  name: Plain GLTF Helmet\n"
                       "  gameplayCameraPath: /game_cam\n"
                       "nodes:\n"
                       "  - nodeName: game_camera\n"
                       "    name: game_cam\n"
                       "    transform:\n"
                       "      translation: [0.0, 2.0, 6.0]\n"
                       "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
                       "      scale: [1.0, 1.0, 1.0]\n"
                       "    camera:\n"
                       "      type: perspective\n"
                       "      fovY: 45.0\n"
                       "      aspect: 1.0\n"
                       "      nearPlane: 0.1\n"
                       "      farPlane: 100.0\n"
                       "  - nodeName: helmet\n"
                       "    name: helmet\n"
                       "    mesh:\n"
                       "      uri: assets/models/damaged_helmet/DamagedHelmet.gltf\n");
  demo::SceneRuntime runtime;
  runtime.loadFromDocumentPath(path);
  auto *helmet = runtime.scene()->findByPath("/helmet");
  EXPECT(helmet != nullptr, "plain glTF helmet scene should load helmet node");
  EXPECT(nodeForwardPassHasDescriptor(helmet, LX_core::StringID("normalMap")),
         "plain glTF helmet should use shared PBR bridge and normal map");
}
```

In `test_offline_scene_loader.cpp`, add a scene with the same glTF URI and assert `objectCount() > 0`.

- [x] **Step 2: Run tests and verify current failure**

Run:

```bash
cmake --build build --target test_scene_runtime test_offline_scene_loader -j2
./build/src/test/integration/test_scene_runtime
./build/src/test/integration/test_offline_scene_loader
```

Expected: runtime may fail for plain glTF or offline fails with `offline MVP only supports shared builtin primitive meshes`.

- [x] **Step 3: Update editor runtime plain glTF path**

In `buildRenderableNodeFromDocument(...)`, before builtin primitive handling, add:

```cpp
if (nodeDocument.meshUri->ends_with(".gltf") ||
    nodeDocument.meshUri->ends_with(".glb")) {
  const std::filesystem::path meshPath =
      resolveProjectAssetPath(assetRoots, *nodeDocument.meshUri)
          .value_or(resolveRuntimePath(*nodeDocument.meshUri));
  auto asset = LX_infra::scene_asset::loadGltfSceneAsset(meshPath);
  auto node = makeRenderableNode(nodeDocument.nodeName, std::move(asset.mesh),
                                 std::move(asset.material));
  return node;
}
```

Also change the `builtin://lxe_editor/helmet` branch to call `loadGltfSceneAsset(...)` instead of `buildHelmetNode(...)`.

- [x] **Step 4: Update offline loader plain glTF path**

In `src/infra/offline/offline_scene_loader.cpp`, include:

```cpp
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"
```

Replace the non-builtin rejection with:

```cpp
RegisteredMesh registerMeshUri(SceneResourceTable &table,
                               const OfflineAssetResolver &resolver,
                               const std::string &meshUri,
                               std::unordered_map<std::string, RegisteredMesh> &meshByUri) {
  if (const auto it = meshByUri.find(meshUri); it != meshByUri.end()) {
    return it->second;
  }
  LX_core::MeshBufferSharedPtr mesh;
  LX_core::MaterialInstanceSharedPtr defaultMaterial;
  if (LX_core::isBuiltinPrimitiveMeshUri(meshUri)) {
    mesh = LX_core::buildBuiltinPrimitiveMesh(meshUri);
  } else {
    auto asset = LX_infra::scene_asset::loadGltfSceneAsset(resolver.resolve(meshUri));
    mesh = std::move(asset.mesh);
    defaultMaterial = std::move(asset.material);
  }
  const auto geometryStorageHandle =
      table.registerGeometryStorage(mesh->getGeometryStorage());
  (void)geometryStorageHandle;
  RegisteredMesh registered{.mesh = table.registerMesh(mesh),
                            .meshBuffer = std::move(mesh)};
  meshByUri.emplace(meshUri, registered);
  if (defaultMaterial) {
    // Store via existing material path logic in the node branch, not here.
  }
  return registered;
}
```

Then in the node branch, if `node.materialUri` is absent and glTF asset returned a material, register that material instead of loading fallback. Implement this with a small `RegisteredMesh` extension:

```cpp
struct RegisteredMesh final {
  MeshHandle mesh;
  MeshBufferSharedPtr meshBuffer;
  MaterialInstanceSharedPtr defaultMaterial;
};
```

- [x] **Step 5: Run tests**

Run:

```bash
cmake --build build --target test_scene_runtime test_offline_scene_loader -j2
./build/src/test/integration/test_scene_runtime
./build/src/test/integration/test_offline_scene_loader
```

Expected: both pass.

- [x] **Step 6: Commit**

```bash
git add src/demos/lxe_editor/scene_runtime.cpp src/infra/offline/offline_scene_loader.cpp src/test/integration/test_scene_runtime.cpp src/test/integration/test_offline_scene_loader.cpp
git commit -m "feat: reuse gltf pbr loader in runtime and offline scenes"
```

## Task 4: Offline Material Texture Table

**Files:**
- Modify: `src/core/scene/scene_gpu_records.hpp`
- Modify: `src/core/scene/scene_gpu_records.cpp`
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/core/offline/offline_scene_storage_resources.cpp`
- Modify: `src/test/integration/test_scene_resource_table.cpp`
- Modify: `src/test/integration/test_offline_gpu_scene.cpp`

- [x] **Step 1: Add failing texture-index tests**

In `test_scene_resource_table.cpp`, replace the 64-byte material assertion with the new size once chosen, and add:

```cpp
void testPbrTextureIndicesEnterUploadView() {
  const auto material = LX_infra::scene_asset::loadGltfSceneAsset(
      "assets/models/damaged_helmet/DamagedHelmet.gltf").material;
  LX_core::SceneResourceTable table;
  const auto materialHandle = table.registerMaterial(material);
  (void)materialHandle;
  const auto upload = table.buildUploadView();
  EXPECT(!upload.materials.empty(), "upload view should contain material");
  EXPECT(upload.materials[0].baseColorTexture != u32_max,
         "base color texture index should be assigned");
  EXPECT(upload.materials[0].normalTexture != u32_max,
         "normal texture index should be assigned");
  EXPECT(upload.materials[0].metallicRoughnessTexture != u32_max,
         "MR texture index should be assigned");
  EXPECT(upload.materials[0].aoTexture != u32_max,
         "AO texture index should be assigned");
  EXPECT(upload.materials[0].emissiveTexture != u32_max,
         "emissive texture index should be assigned");
}
```

- [x] **Step 2: Extend GPU records**

Change `SceneGpuMaterialRecord`:

```cpp
struct alignas(16) SceneGpuMaterialRecord final {
  Vec4f baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  Vec4f pbrParams{0.0f, 0.5f, 0.0f, 0.0f}; // x metallic, y roughness, z specular/unused, w ao
  Vec4f emissive{0.0f, 0.0f, 0.0f, 0.0f};
  u32 baseColorTexture = u32_max;
  u32 normalTexture = u32_max;
  u32 metallicRoughnessTexture = u32_max;
  u32 aoTexture = u32_max;
  u32 emissiveTexture = u32_max;
  u32 flags = 0;
  u32 reserved0 = 0;
  u32 reserved1 = 0;
};
static_assert(sizeof(SceneGpuMaterialRecord) == 80);
```

- [x] **Step 3: Add texture table to upload view**

In `scene_resource_table_upload_view.hpp`, add:

```cpp
std::span<const LX_core::CombinedTextureSamplerSharedPtr> textures;
```

Include `core/asset/texture.hpp`.

- [x] **Step 4: Build material texture indices in SceneResourceTable**

Add `mutable std::vector<CombinedTextureSamplerSharedPtr> m_gpuTextures;` and a helper:

```cpp
u32 SceneResourceTable::registerUploadTexture(
    const CombinedTextureSamplerSharedPtr &texture,
    std::vector<CombinedTextureSamplerSharedPtr> &textures) const {
  if (!texture) {
    return u32_max;
  }
  for (u32 i = 0; i < textures.size(); ++i) {
    if (textures[i] == texture) {
      return i;
    }
  }
  textures.push_back(texture);
  return static_cast<u32>(textures.size() - 1u);
}
```

After `toGpuMaterialRecord`, set indices by binding name:

```cpp
auto record = toGpuMaterialRecord(*entry.resource);
record.baseColorTexture =
    registerUploadTexture(entry.resource->getTexture(StringID("albedoMap")), m_gpuTextures);
record.normalTexture =
    registerUploadTexture(entry.resource->getTexture(StringID("normalMap")), m_gpuTextures);
record.metallicRoughnessTexture =
    registerUploadTexture(entry.resource->getTexture(StringID("metallicRoughnessMap")), m_gpuTextures);
record.aoTexture =
    registerUploadTexture(entry.resource->getTexture(StringID("aoMap")), m_gpuTextures);
record.emissiveTexture =
    registerUploadTexture(entry.resource->getTexture(StringID("emissiveMap")), m_gpuTextures);
m_gpuMaterials.push_back(record);
```

- [x] **Step 5: Run table tests**

Run:

```bash
cmake --build build --target test_scene_resource_table test_offline_gpu_scene -j2
./build/src/test/integration/test_scene_resource_table
./build/src/test/integration/test_offline_gpu_scene
```

Expected: table tests pass; offline shader descriptor tests may still only cover MVP.

- [x] **Step 6: Commit**

```bash
git add src/core/scene src/core/offline src/test/integration/test_scene_resource_table.cpp src/test/integration/test_offline_gpu_scene.cpp
git commit -m "feat: carry pbr texture indices in scene upload"
```

## Task 5: Shared PBR GLSL Common

**Files:**
- Create: `assets/shaders/glsl/common/pbr.glsl`
- Modify: `assets/shaders/glsl/pbr.frag`
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [x] **Step 1: Add failing include/compile test**

In `test_shader_compiler.cpp`, add:

```cpp
void testPbrFragmentUsesSharedCommon() {
  const auto source = readTextFile("assets/shaders/glsl/pbr.frag");
  EXPECT(source.find("#include \"common/pbr.glsl\"") != std::string::npos,
         "pbr.frag should include shared PBR common");
}
```

Run:

```bash
cmake --build build --target test_shader_compiler -j2
./build/src/test/integration/test_shader_compiler
```

Expected: fails until the include is added.

- [x] **Step 2: Create common shader**

Create `assets/shaders/glsl/common/pbr.glsl`:

```glsl
#ifndef LX_COMMON_PBR_GLSL
#define LX_COMMON_PBR_GLSL

const float LX_PBR_PI = 3.14159265359;

struct LxPbrDirectInput {
  vec3 baseColor;
  vec3 normal;
  vec3 viewDir;
  vec3 lightDir;
  vec3 lightColor;
  float metallic;
  float roughness;
  float ao;
  vec3 emissive;
};

float lxDistributionGGX(vec3 N, vec3 H, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float NdotH = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;
  float denom = NdotH2 * (a2 - 1.0) + 1.0;
  denom = LX_PBR_PI * denom * denom;
  return a2 / max(denom, 0.0001);
}

float lxGeometrySchlickGGX(float NdotV, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return NdotV / (NdotV * (1.0 - k) + k);
}

float lxGeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
  return lxGeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
         lxGeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 lxFresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 lxPbrDirectLight(LxPbrDirectInput input) {
  vec3 N = normalize(input.normal);
  vec3 V = normalize(input.viewDir);
  vec3 L = normalize(input.lightDir);
  vec3 H = normalize(V + L);
  float roughness = clamp(input.roughness, 0.04, 1.0);
  float metallic = clamp(input.metallic, 0.0, 1.0);
  vec3 F0 = mix(vec3(0.04), input.baseColor, metallic);
  float NDF = lxDistributionGGX(N, H, roughness);
  float G = lxGeometrySmith(N, V, L, roughness);
  vec3 F = lxFresnelSchlick(max(dot(H, V), 0.0), F0);
  vec3 specular = (NDF * G * F) /
                  max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.0001);
  vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
  float NdotL = max(dot(N, L), 0.0);
  vec3 direct = (kD * input.baseColor / LX_PBR_PI + specular) *
                input.lightColor * NdotL;
  vec3 ambient = vec3(0.03) * input.baseColor * clamp(input.ao, 0.0, 1.0);
  return ambient + direct + input.emissive;
}

#endif
```

- [x] **Step 3: Refactor `pbr.frag` to call common**

At top of `pbr.frag`, add:

```glsl
#include "common/pbr.glsl"
```

Replace local direct-light function definitions and direct-light body with:

```glsl
LxPbrDirectInput pbrInput;
pbrInput.baseColor = albedo.rgb;
pbrInput.normal = N;
pbrInput.viewDir = V;
pbrInput.lightDir = L;
pbrInput.lightColor = light.color.rgb;
pbrInput.metallic = metallic;
pbrInput.roughness = roughness;
pbrInput.ao = ao;
pbrInput.emissive = vec3(0.0);
#ifdef HAS_EMISSIVE_MAP
pbrInput.emissive = texture(emissiveMap, vUV).rgb;
#endif
vec3 color = lxPbrDirectLight(pbrInput);
```

Keep the existing IBL block controlled by `environment.params.x`; do not hardcode Helmet behavior.

- [x] **Step 4: Compile shaders**

Run:

```bash
cmake --build build --target CompileShaders test_shader_compiler -j2
./build/src/test/integration/test_shader_compiler
```

Expected: shaders compile and test passes.

- [x] **Step 5: Commit**

```bash
git add assets/shaders/glsl/common/pbr.glsl assets/shaders/glsl/pbr.frag src/test/integration/test_shader_compiler.cpp
git commit -m "feat: share pbr direct lighting shader code"
```

## Task 6: Add Offline PBR Direct Shader Without Replacing MVP

**Files:**
- Create: `assets/shaders/glsl/offline_pbr_direct_ray.comp`
- Modify: `src/backend/vulkan/offline/offline_compute_shader.cpp`
- Modify: `src/backend/vulkan/offline/offline_compute_shader.hpp`
- Modify: `src/core/offline/offline_render_job.hpp`
- Modify: `src/infra/scene_io/scene_document.cpp`
- Modify: `src/test/integration/test_offline_gpu_scene.cpp`

- [x] **Step 1: Add shader contract failing test**

In `test_offline_gpu_scene.cpp`, add:

```cpp
void testOfflinePbrDirectShaderCompiles() {
  const auto shaderPath =
      LX_core::resolveRuntimePath("assets/shaders/glsl/offline_pbr_direct_ray.comp");
  const auto compileResult = LX_infra::ShaderCompiler::compileFile(shaderPath);
  EXPECT(compileResult.success,
         "offline PBR direct shader should compile before reflection");
  const auto bindings = LX_infra::ShaderReflector::reflect(compileResult.stages);
  EXPECT(hasStorageBuffer(bindings, "SceneMaterials"),
         "offline PBR shader should read SceneMaterials");
  EXPECT(hasStorageBuffer(bindings, "OutputPixels"),
         "offline PBR shader should write OutputPixels");
}
```

Run:

```bash
cmake --build build --target test_offline_gpu_scene -j2
./build/src/test/integration/test_offline_gpu_scene
```

Expected: fails because shader file does not exist.

- [x] **Step 2: Create `offline_pbr_direct_ray.comp`**

Copy the ray generation, BVH traversal, barycentric interpolation, and output structure from `offline_primary_ray.comp`. Add:

```glsl
#include "common/pbr.glsl"
```

Extend `HitAttributes`:

```glsl
struct HitAttributes {
  vec3 normal;
  vec3 tangent;
  float tangentSign;
  vec2 uv;
  uint materialIndex;
};
```

In shade, build material inputs:

```glsl
lxSceneMaterialRecord material =
    materials[min(matIndex, params.materialCount - 1u)];
vec3 baseColor = material.baseColor.rgb;
float metallic = clamp(material.pbrParams.x, 0.0, 1.0);
float roughness = clamp(material.pbrParams.y, 0.04, 1.0);
float ao = clamp(material.pbrParams.w, 0.0, 1.0);
vec3 emissive = material.emissive.rgb;
vec3 N = normalize(normal);
vec3 V = normalize(-dir);
vec3 L = normalize(-params.lightDirectionIntensity.xyz);
LxPbrDirectInput pbrInput;
pbrInput.baseColor = baseColor;
pbrInput.normal = N;
pbrInput.viewDir = V;
pbrInput.lightDir = L;
pbrInput.lightColor =
    params.lightColorEnvironment.rgb * params.lightDirectionIntensity.w;
pbrInput.metallic = metallic;
pbrInput.roughness = roughness;
pbrInput.ao = ao;
pbrInput.emissive = emissive;
return lxPbrDirectLight(pbrInput);
```

In the next task, texture sampling replaces these scalar-only values.

- [x] **Step 3: Add shader selection enum/config**

In `offline_render_job.hpp`, add:

```cpp
enum class OfflineShaderMode {
  MvpPrimaryRay,
  PbrDirectRay,
};
```

Add `OfflineShaderMode shaderMode = OfflineShaderMode::MvpPrimaryRay;` to `OfflineRenderSettings`.

In `scene_document.cpp`, parse:

```yaml
offlineRender:
  shader: pbr-direct-ray
```

Mapping:

```cpp
if (value == "mvp-primary-ray") settings.shaderMode = OfflineShaderMode::MvpPrimaryRay;
else if (value == "pbr-direct-ray") settings.shaderMode = OfflineShaderMode::PbrDirectRay;
else throw std::runtime_error("unsupported offlineRender shader: " + value);
```

- [x] **Step 4: Load selected compute shader**

Change `createOfflinePrimaryRayShader()` into:

```cpp
LX_core::IShaderSharedPtr createOfflineComputeShader(OfflineShaderMode mode);
```

Select filename:

```cpp
const char *shaderFile =
    mode == OfflineShaderMode::PbrDirectRay
        ? "offline_pbr_direct_ray.comp.spv"
        : "offline_primary_ray.comp.spv";
```

Keep the existing descriptor validation for MVP. Add a separate expected binding table for PBR direct shader as it grows.

- [x] **Step 5: Run shader tests**

Run:

```bash
cmake --build build --target CompileShaders test_offline_gpu_scene -j2
./build/src/test/integration/test_offline_gpu_scene
```

Expected: both offline shaders compile and reflection tests pass.

- [x] **Step 6: Commit**

```bash
git add assets/shaders/glsl/offline_pbr_direct_ray.comp src/backend/vulkan/offline src/core/offline src/infra/scene_io src/test/integration/test_offline_gpu_scene.cpp
git commit -m "feat: add configurable offline pbr direct shader"
```

## Task 7: Bind Offline Texture Descriptor Array

**Files:**
- Modify: `assets/shaders/glsl/offline_pbr_direct_ray.comp`
- Modify: `src/core/offline/offline_scene_storage_resources.cpp`
- Modify: `src/core/asset/texture.hpp`
- Modify: `src/core/frame_graph/render_upload_plan.cpp`
- Modify: `src/backend/vulkan/details/commands/command_buffer.cpp`
- Modify: `src/backend/vulkan/details/descriptors/descriptor_manager.cpp`
- Modify: `src/backend/vulkan/details/descriptors/descriptor_manager.hpp`
- Modify: `src/backend/vulkan/details/resource_manager.cpp`
- Modify: `src/backend/vulkan/offline/offline_compute_shader.cpp`
- Modify: `src/test/integration/test_offline_gpu_scene.cpp`

- [x] **Step 1: Add descriptor contract test**

Extend `testOfflinePbrDirectShaderCompiles()`:

```cpp
EXPECT(hasSampledImage(bindings, "SceneTextures"),
       "offline PBR shader should expose texture descriptor array");
```

Add helper:

```cpp
bool hasSampledImage(const std::vector<LX_core::ShaderResourceBinding> &bindings,
                     const std::string &name) {
  return std::any_of(bindings.begin(), bindings.end(), [&](const auto &binding) {
    return binding.name == name &&
           binding.type == LX_core::ShaderPropertyType::Texture2D;
  });
}
```

- [x] **Step 2: Add descriptor array to shader**

In `offline_pbr_direct_ray.comp`, add:

```glsl
layout(set = 0, binding = 9) uniform sampler2D SceneTextures[64];
```

Add helper:

```glsl
bool hasTexture(uint index) {
  return index != 0xffffffffu && index < 64u;
}
vec4 sampleTexture(uint index, vec2 uv, vec4 fallback) {
  return hasTexture(index) ? texture(SceneTextures[index], uv) : fallback;
}
```

Use material indices:

```glsl
vec4 albedoSample =
    sampleTexture(material.baseColorTexture, hit.uv, vec4(1.0));
baseColor *= albedoSample.rgb;
vec4 mrSample =
    sampleTexture(material.metallicRoughnessTexture, hit.uv, vec4(1.0));
metallic *= mrSample.b;
roughness *= mrSample.g;
ao *= sampleTexture(material.aoTexture, hit.uv, vec4(1.0)).r;
emissive += sampleTexture(material.emissiveTexture, hit.uv, vec4(0.0)).rgb;
if (hasTexture(material.normalTexture)) {
  vec3 tangentNormal =
      texture(SceneTextures[material.normalTexture], hit.uv).rgb * 2.0 - 1.0;
  vec3 T = normalize(hit.tangent);
  vec3 B = normalize(cross(hit.normal, T) * hit.tangentSign);
  mat3 TBN = mat3(T, B, normalize(hit.normal));
  N = normalize(TBN * tangentNormal);
}
```

- [x] **Step 3: Add texture resources in offline storage**

In `buildOfflineSceneStorageResources`, append upload view textures:

```cpp
for (usize i = 0; i < uploadView.textures.size(); ++i) {
  if (uploadView.textures[i]) {
    resources.descriptorResources.push_back(
        std::static_pointer_cast<IGpuResource>(uploadView.textures[i]));
  }
}
```

Ensure each texture resource has `getBindingName() == StringID("SceneTextures")` or add an offline wrapper resource that exposes that binding name while retaining the sampler object. Use the wrapper if `CombinedTextureSampler` currently returns its material binding name.

- [x] **Step 4: Update descriptor validation**

In `offline_compute_shader.cpp`, for PBR mode expect 10 bindings: current 0-8 plus binding 9 `SceneTextures` with descriptor count 64 and type `Texture2D`.

- [x] **Step 5: Run tests**

Run:

```bash
cmake --build build --target CompileShaders test_offline_gpu_scene -j2
./build/src/test/test_offline_gpu_scene
```

Expected: PBR shader descriptor contract passes.

- [x] **Step 6: Commit**

```bash
git add assets/shaders/glsl/offline_pbr_direct_ray.comp src/core/offline src/backend/vulkan/offline src/test/integration/test_offline_gpu_scene.cpp
git commit -m "feat: sample pbr textures in offline direct shader"
```

## Task 8: PBR Compare Scene And Tool Target

**Files:**
- Create: `assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml`
- Modify: `src/tools/CMakeLists.txt`
- Modify: `src/tools/lxe_realtime_render/lxe_realtime_render.py` only if scene import cannot handle glTF URI.

- [x] **Step 1: Create the scene file**

Create `assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml`:

```yaml
scene:
  name: Realtime Offline Helmet PBR Compare
  gameplayCameraPath: /game_cam
  environment:
    enabled: false
    intensity: 0.0
    skyboxEnabled: false
  defaultOutputProfile: preview
  outputProfiles:
    preview:
      camera: /game_cam
      width: 192
      height: 192
      outputFormat: exr-png
      outDir: artifacts/compare/helmet_pbr
      backgroundColor: [0.0, 0.0, 0.0]
  offlineRender:
    integrator: software-compute
    shader: pbr-direct-ray
    samples: 1
    maxBounce: 1
    seed: 1
    profile: preview
    shadows: false
    compareMode: shaded
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  children:
    - nodeName: game_camera
      name: game_cam
      transform:
        translation: [0.0, 0.0, 4.2]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      camera:
        type: perspective
        fovY: 35.0
        aspect: 1.0
        nearPlane: 0.1
        farPlane: 20.0
    - nodeName: helmet
      name: helmet
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      mesh:
        uri: assets/models/damaged_helmet/DamagedHelmet.gltf
      nodeMaterialOverrides:
        MaterialUBO.ao: 1.0
    - nodeName: compare_key_light
      name: compare_key_light
      light:
        kind: Directional
        direction: [-0.35, -1.0, -0.25]
        color: [1.0, 1.0, 1.0]
        intensity: 1.0
        shadowStrength: 0.0
        shadowDistance: 80.0
        shadowCascadeCount: 1
```

- [x] **Step 2: Add CMake compare target**

In `src/tools/CMakeLists.txt`, add:

```cmake
add_custom_target(RunRealtimeOfflineHelmetPbrCompare
  COMMAND ${CMAKE_SOURCE_DIR}/src/tools/lxe_realtime_render/lxe_realtime_render.py
    --scene assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml
    --profile preview
    --xvfb
  COMMAND $<TARGET_FILE:lxe_offline_render>
    --scene assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml
    --profile preview
    --out artifacts/compare/helmet_pbr/offline/render
  COMMAND $<TARGET_FILE:lxe_compare_exr>
    --reference artifacts/compare/helmet_pbr/offline/render.exr
    --candidate "artifacts/compare/helmet_pbr/realtime/Realtime%20Offline%20Helmet%20PBR%20Compare/preview/render-linear.exr"
    --diagnostic-radius 2
  DEPENDS lxe_editor lxe_offline_render lxe_compare_exr
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)
```

- [x] **Step 3: Run the target**

Run:

```bash
cmake --build build --target RunRealtimeOfflineHelmetPbrCompare -j2
```

Expected: realtime EXR, offline EXR, PNG previews, and compare output are generated. At this stage compare may fail until exact numeric differences are tuned in Task 9.

- [x] **Step 4: Commit**

```bash
git add assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml src/tools/CMakeLists.txt
git commit -m "test: add helmet pbr realtime offline compare target"
```

## Task 9: Per-Input Material Coverage Tests

**Files:**
- Create: `src/test/integration/test_pbr_material_input_coverage.cpp`
- Modify: `src/test/integration/CMakeLists.txt`
- Modify: `src/tools/lxe_compare_exr/exr_compare_metrics.hpp`
- Modify: `src/tools/lxe_compare_exr/exr_compare_metrics.cpp`

- [x] **Step 1: Add deterministic material input tests**

Create `test_pbr_material_input_coverage.cpp`:

```cpp
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
  }
}

void testAllDamagedHelmetInputsBound() {
  LX_core::cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf");
  auto asset = LX_infra::scene_asset::loadGltfSceneAsset(
      "assets/models/damaged_helmet/DamagedHelmet.gltf");
  const auto &material = asset.material;
  expect(material->getTexture(LX_core::StringID("albedoMap")) != nullptr,
         "albedo must be bound");
  expect(material->getTexture(LX_core::StringID("metallicRoughnessMap")) != nullptr,
         "metallic/roughness must be bound");
  expect(material->getTexture(LX_core::StringID("normalMap")) != nullptr,
         "normal must be bound");
  expect(material->getTexture(LX_core::StringID("aoMap")) != nullptr,
         "AO must be bound");
  expect(material->getTexture(LX_core::StringID("emissiveMap")) != nullptr,
         "emissive must be bound");
  expect(material->readParameterValue(LX_core::StringID("MaterialUBO"),
                                      LX_core::StringID("metallicFactor")).has_value(),
         "metallic scalar must be readable");
  expect(material->readParameterValue(LX_core::StringID("MaterialUBO"),
                                      LX_core::StringID("roughnessFactor")).has_value(),
         "roughness scalar must be readable");
}

} // namespace

int main() {
  testAllDamagedHelmetInputsBound();
  std::cout << "test_pbr_material_input_coverage passed\n";
  return 0;
}
```

Add it to CMake with `lxe_core` and `lxe_infra`.

- [x] **Step 2: Run material coverage test**

Run:

```bash
cmake --build build --target test_pbr_material_input_coverage -j2
./build/src/test/integration/test_pbr_material_input_coverage
```

Expected: pass.

- [x] **Step 3: Add image-difference input toggles**

Add two extra scene files using material overrides so every scalar path has a render-level coverage hook:

```yaml
nodeMaterialOverrides:
  MaterialUBO.metallicFactor: 0.0
  MaterialUBO.roughnessFactor: 1.0
  MaterialUBO.ao: 0.25
```

Render both baseline and override with realtime/offline and compare each pipeline's baseline-to-override delta using `lxe_compare_exr`.

- [x] **Step 4: Commit**

```bash
git add src/test/integration/test_pbr_material_input_coverage.cpp src/test/integration/CMakeLists.txt
git commit -m "test: cover pbr material input bindings"
```

## Task 10: Full Verification And Regression

**Files:**
- No code changes unless failures expose a defect.

- [x] **Step 1: Run focused build**

```bash
cmake --build build --target CompileShaders lxe_editor lxe_offline_render test_gltf_scene_asset_loader test_pbr_material_input_coverage test_scene_runtime test_offline_scene_loader test_scene_resource_table test_offline_gpu_scene test_shader_compiler -j2
```

Expected: all targets build.

- [x] **Step 2: Run focused tests**

```bash
ctest --test-dir build --output-on-failure -R 'test_gltf_scene_asset_loader|test_pbr_material_input_coverage|test_scene_runtime|test_offline_scene_loader|test_scene_resource_table|test_offline_gpu_scene|test_shader_compiler'
```

Expected: all listed tests pass.

- [x] **Step 3: Run MVP regression compare**

```bash
cmake --build build --target RunRealtimeOfflineDiagnostic -j2
```

Expected: original diagnostic scene still renders and compares.

- [x] **Step 4: Run Helmet PBR compare**

```bash
cmake --build build --target RunRealtimeOfflineHelmetPbrCompare -j2
```

Expected: realtime/offline linear EXR compare passes under the configured threshold.

- [x] **Step 5: Inspect output artifacts**

Check:

```bash
ls -lh artifacts/compare/helmet_pbr/offline/render.exr
ls -lh "artifacts/compare/helmet_pbr/realtime/Realtime%20Offline%20Helmet%20PBR%20Compare/preview/render-linear.exr"
```

Expected: both files exist and are non-empty.

- [x] **Step 6: Commit final verification adjustments**

Commit the compare scene, final threshold/reporting settings, and any metadata output changes made during verification:

```bash
git add <changed-files>
git commit -m "test: stabilize helmet pbr equivalence verification"
```

## Self-Review

- Spec coverage: R1 maps to Tasks 1-3; R2-R5 map to Tasks 2 and 4; R6 maps to Task 5; R7 maps to Task 6; R8 maps to Task 8; R9 maps to Task 3 and resolver-compatible loader use; R10 maps to Tasks 1, 3, 4, 6, 9, and 10.
- Placeholder scan: no TBD/TODO markers are present. Each task has concrete files, code skeletons, commands, and expected outcomes.
- Type consistency: the plan consistently uses `GltfSceneAssetLoadResult`, `loadGltfSceneAsset`, `SceneGpuMaterialRecord`, `OfflineShaderMode::PbrDirectRay`, and shader name `offline_pbr_direct_ray.comp`.
