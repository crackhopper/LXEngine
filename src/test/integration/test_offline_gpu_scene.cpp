#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/shader.hpp"
#include "core/asset/texture.hpp"
#include "core/frame_graph/render_work_build_context.hpp"
#include "core/offline/offline_render_job.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "core/offline/offline_render_work_graph.hpp"
#include "core/raytracing/software_bvh.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "core/scene/scene_gpu_records.hpp"
#include "backend/vulkan/offline/offline_compute_shader.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/offline/offline_scene_loader.hpp"
#include "infra/scene_io/scene_document.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace LX_core;

namespace {

int failures = 0;
constexpr u32 LeafNodeFlag = 0x80000000u;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

ShaderVariant materialContractSourceVariant() {
  return ShaderVariant{
      .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
      .enabled = true,
      .macroValue = "\"common/materials/uber.contract.glsl\"",
  };
}

struct TestVertex final {
  Vec3f pos{};

  static const VertexLayout &getLayout() {
    static const VertexLayout layout{
        {{"inPos", 0, DataType::Float3, sizeof(Vec3f),
          offsetof(TestVertex, pos)}},
        sizeof(TestVertex)};
    return layout;
  }
};

MeshBufferSharedPtr makeMeshBuffer() {
  auto vertices = std::vector<TestVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  return MeshBuffer::create(
      vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshBufferSharedPtr makeOffsetMeshBuffer() {
  auto vertices = std::vector<TestVertex>{
      {{-10.0f, -10.0f, 0.0f}},
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{1, 2, 3};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  auto storage = GeometryStorage::create(vb, ib);
  return MeshBuffer::create(
      storage, 1, 0, 3, 3, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshBuffer::UniquePtr uniqueMesh(const MeshBufferSharedPtr &mesh) {
  return mesh->cloneUnique();
}

MaterialInstance::UniquePtr
uniqueMaterial(const MaterialInstanceSharedPtr &material) {
  return material->cloneInstanceDataUnique();
}

[[nodiscard]] u32 packedU32(float value) { return std::bit_cast<u32>(value); }

[[nodiscard]] bool isLeaf(const SceneSoftwareBvhNode &node) {
  return (packedU32(node.boundsMaxCount.w) & LeafNodeFlag) != 0;
}

[[nodiscard]] u32 leafFirst(const SceneSoftwareBvhNode &node) {
  return packedU32(node.boundsMinLeftFirst.w);
}

[[nodiscard]] u32 leafCount(const SceneSoftwareBvhNode &node) {
  return packedU32(node.boundsMaxCount.w) & ~LeafNodeFlag;
}

[[nodiscard]] std::filesystem::path
findOfflineShaderSourcePath(const char *shaderFilename) {
  std::filesystem::path probe = std::filesystem::current_path();
  for (int i = 0; i < 8; ++i) {
    const auto candidate =
        probe / "assets" / "shaders" / "glsl" / "techniques" /
        "OfflineRT" / shaderFilename;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    const auto parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = parent;
  }
  return {};
}

[[nodiscard]] std::filesystem::path findProjectFile(const char *relativePath) {
  const auto sourceRoot = std::filesystem::path(__FILE__)
                              .parent_path()
                              .parent_path()
                              .parent_path()
                              .parent_path();
  const auto sourceCandidate = sourceRoot / relativePath;
  if (std::filesystem::exists(sourceCandidate)) {
    return sourceCandidate;
  }

  std::filesystem::path probe = std::filesystem::current_path();
  for (int i = 0; i < 8; ++i) {
    const auto candidate = probe / relativePath;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    const auto parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = parent;
  }
  return {};
}

void writeTextFile(const std::filesystem::path &path,
                   const std::string &content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  out << content;
}

[[nodiscard]] std::filesystem::path writeOfflineLoaderSmokeScene() {
  const std::filesystem::path dir =
      std::filesystem::current_path() / "tmp_offline_loader_material_v2";
  std::filesystem::create_directories(dir);
  writeTextFile(dir / "smoke.material", R"(schema: lxe.material.v2
bsdf:
  type: matte
  source: assets://shaders/glsl/common/materials/matte.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [0.8, 0.2, 0.1] }
    sigma: { kind: float, value: 0.0 }
)");
  writeTextFile(dir / "smoke.scene.yaml", R"(scene:
  name: Offline Loader Material V2 Smoke
  gameplayCameraPath: /camera
  defaultOutputProfile: smoke
  outputProfiles:
    smoke:
      camera: /camera
      width: 32
      height: 32
      outputFormat: exr-png
      outDir: artifacts/test/offline-loader
      backgroundColor: [0.0, 0.0, 0.0]
  offlineRender:
    integrator: software-compute
    samples: 1
    maxBounce: 1
    seed: 1
    profile: smoke
root:
  nodeName: scene_root
  name: ''
  transform:
    translation: [0.0, 0.0, 0.0]
    rotation: [1.0, 0.0, 0.0, 0.0]
    scale: [1.0, 1.0, 1.0]
  visibilityMask: 4294967295
  children:
    - nodeName: camera
      name: camera
      transform:
        translation: [0.0, 0.0, 3.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      camera:
        type: perspective
        fovY: 45.0
        aspect: 1.0
        nearPlane: 0.1
        farPlane: 20.0
        cullingMask: 4294967295
    - nodeName: cube
      name: cube
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      mesh:
        uri: builtin://lxe_editor/primitives/cube
      material:
        uri: smoke.material
    - nodeName: key_light
      name: key_light
      transform:
        translation: [0.0, 0.0, 0.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      light:
        kind: Directional
        direction: [-0.35, -0.85, -0.4]
        color: [1.0, 1.0, 1.0]
        intensity: 3.0
)");
  return dir / "smoke.scene.yaml";
}

[[nodiscard]] std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file) {
    return {};
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

[[nodiscard]] bool
hasStorageBuffer(const std::vector<ShaderResourceBinding> &bindings,
                 const std::string &name) {
  for (const auto &binding : bindings) {
    if (binding.name == name &&
        binding.type == ShaderPropertyType::StorageBuffer) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] const ShaderResourceBinding *
findBinding(const std::vector<ShaderResourceBinding> &bindings,
            const std::string &name) {
  for (const auto &binding : bindings) {
    if (binding.name == name) {
      return &binding;
    }
  }
  return nullptr;
}

[[nodiscard]] GpuResourceRef
findDescriptorResource(const RenderWorkItem &item, StringID bindingName) {
  for (const DescriptorResourceRef &resource : item.descriptorResources) {
    if (resource.getBindingName() == bindingName && resource.isResource()) {
      return resource.resource();
    }
  }
  return {};
}

void testOfflineShaderUsesUnifiedSceneBuffers() {
  const auto shaderPath =
      findOfflineShaderSourcePath("offline_primary_ray.comp");
  EXPECT(!shaderPath.empty(),
         "offline shader source should be discoverable for reflection test");
  if (shaderPath.empty()) {
    return;
  }

  const auto compileResult = LX_infra::ShaderCompiler::compileFile(
      shaderPath, {materialContractSourceVariant()});
  EXPECT(compileResult.success,
         "offline shader should compile before reflection");
  if (!compileResult.success) {
    std::cerr << compileResult.errorMessage << '\n';
    return;
  }

  const auto bindings =
      LX_infra::ShaderReflector::reflect(compileResult.stages);
  EXPECT(!hasStorageBuffer(bindings, "SceneVertices"),
         "offline shader should not expose legacy packed SceneVertices SSBO");
  EXPECT(hasStorageBuffer(bindings, "ScenePositions"),
         "offline shader should use bindless position-only ScenePositions "
         "SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneAttributeStreams"),
         "offline shader should use bindless SceneAttributeStreams SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneAttributeValues"),
         "offline shader should use bindless SceneAttributeValues SSBO");
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
  EXPECT(hasStorageBuffer(bindings, "SceneBvhNodes"),
         "offline shader should use unified SceneBvhNodes SSBO");
  EXPECT(hasStorageBuffer(bindings, "SceneFrameParams"),
         "offline shader should use unified SceneFrameParams SSBO");
  EXPECT(hasStorageBuffer(bindings, "OutputPixels"),
         "offline shader should use OutputPixels SSBO");
}

void testOfflinePbrDirectShaderCompiles() {
  const auto shaderPath =
      findOfflineShaderSourcePath("offline_pbr_direct_ray.comp");
  EXPECT(
      !shaderPath.empty(),
      "offline PBR shader source should be discoverable for reflection test");
  if (shaderPath.empty()) {
    return;
  }

  const auto compileResult = LX_infra::ShaderCompiler::compileFile(
      shaderPath, {materialContractSourceVariant()});
  EXPECT(compileResult.success,
         "offline PBR direct shader should compile before reflection");
  if (!compileResult.success) {
    std::cerr << compileResult.errorMessage << '\n';
    return;
  }

  const auto bindings =
      LX_infra::ShaderReflector::reflect(compileResult.stages);
  EXPECT(hasStorageBuffer(bindings, "OutputPixels"),
         "offline PBR shader should write OutputPixels");
}

void testOfflinePbrEmissiveTextureMatchesRealtimeSemantics() {
  const auto shaderPath =
      findOfflineShaderSourcePath("offline_pbr_direct_ray.comp");
  EXPECT(!shaderPath.empty(),
         "offline PBR shader source should be discoverable for semantic test");
  if (shaderPath.empty()) {
    return;
  }

  const std::string shaderSource = readTextFile(shaderPath);
  EXPECT(!shaderSource.empty(),
         "offline PBR shader source should be readable for semantic test");
  EXPECT(shaderSource.find("pbrInput.emissive = max(surface.emissive") !=
             std::string::npos,
         "offline PBR emissive should come from the material accessor surface");
  EXPECT(shaderSource.find("material.emissiveTexture") == std::string::npos,
         "offline PBR shader should not directly read legacy emissive texture "
         "fields");
}

void testOfflinePbrDirectShaderDefersTexturesToAccessor() {
  const auto shaderPath =
      findOfflineShaderSourcePath("offline_pbr_direct_ray.comp");
  EXPECT(!shaderPath.empty(),
         "offline PBR shader source should be discoverable for accessor test");
  if (shaderPath.empty()) {
    return;
  }

  const std::string shaderSource = readTextFile(shaderPath);
  EXPECT(shaderSource.find("GL_EXT_nonuniform_qualifier") != std::string::npos,
         "offline PBR shader should enable nonuniform qualifier extension");
  EXPECT(shaderSource.find("lxLoadMaterialSurface") != std::string::npos,
         "offline PBR shader should obtain texture-backed fields through the "
         "material accessor");
  EXPECT(shaderSource.find("SceneTextures[nonuniformEXT(textureIndex)]") ==
             std::string::npos,
         "offline PBR pass shader should not directly index legacy material "
         "textures");
}

void testOfflinePbrDoesNotFlipBackFaceNormals() {
  const auto shaderPath =
      findOfflineShaderSourcePath("offline_pbr_direct_ray.comp");
  EXPECT(!shaderPath.empty(),
         "offline PBR shader source should be discoverable for normal test");
  if (shaderPath.empty()) {
    return;
  }

  const std::string shaderSource = readTextFile(shaderPath);
  EXPECT(shaderSource.find("normal = -normal") == std::string::npos,
         "offline PBR shader should not turn back-face hits into front-face "
         "shading by flipping normals");
  EXPECT(shaderSource.find("dot(normal, dir) > 0.0") == std::string::npos,
         "offline PBR shader should not use ray-facing normal flip logic");
}

void testOfflinePrimaryDoesNotFlipBackFaceNormals() {
  const auto shaderPath = findOfflineShaderSourcePath("offline_primary_ray.comp");
  EXPECT(!shaderPath.empty(),
         "offline primary shader source should be discoverable for normal test");
  if (shaderPath.empty()) {
    return;
  }

  const std::string shaderSource = readTextFile(shaderPath);
  EXPECT(shaderSource.find("normal = -normal") == std::string::npos,
         "offline primary shader should not turn back-face hits into "
         "front-face shading by flipping normals");
  EXPECT(shaderSource.find("dot(normal, dir) > 0.0") == std::string::npos,
         "offline primary shader should not use ray-facing normal flip logic");
}

void testOfflineShadersCullHitsFromMaterialCullMode() {
  const std::array shaderNames{"offline_primary_ray.comp"};
  for (const auto *shaderName : shaderNames) {
    const auto shaderPath = findOfflineShaderSourcePath(shaderName);
    EXPECT(!shaderPath.empty(),
           "offline shader source should be discoverable for cullMode test");
    if (shaderPath.empty()) {
      continue;
    }

    const std::string shaderSource = readTextFile(shaderPath);
    EXPECT(shaderSource.find("shouldCullRayHit") != std::string::npos,
           "offline ray shader should evaluate material cullMode during "
           "intersection");
    EXPECT(shaderSource.find("MATERIAL_CULL_MODE_BACK") != std::string::npos,
           "offline ray shader should define back-face culling mode");
    EXPECT(shaderSource.find("MATERIAL_CULL_MODE_FRONT") != std::string::npos,
           "offline ray shader should define front-face culling mode");
    EXPECT(shaderSource.find("det > 0.0") != std::string::npos,
           "offline ray shader should derive front-facing hits from triangle "
           "winding instead of flipping normals");
  }
}

void testOfflinePbrReadsTangentSignFromUploadAbiField() {
  const auto shaderPath =
      findOfflineShaderSourcePath("offline_pbr_direct_ray.comp");
  EXPECT(
      !shaderPath.empty(),
      "offline PBR shader source should be discoverable for tangent ABI test");
  if (shaderPath.empty()) {
    return;
  }

  const std::string shaderSource = readTextFile(shaderPath);
  EXPECT(shaderSource.find("SCENE_ATTRIBUTE_TANGENT0") != std::string::npos,
         "offline PBR shader should read tangent data from the bindless "
         "tangent attribute stream");
  EXPECT(shaderSource.find("t0.w * w + t1.w * u + t2.w * v") !=
             std::string::npos,
         "offline PBR shader should read tangent sign from tangent0.w");
  EXPECT(shaderSource.find("uvTangentSign") == std::string::npos,
         "offline PBR shader should not depend on legacy packed "
         "uvTangentSign fields");
}

void testOfflinePbrDirectShaderUsesMaterialAccessorSurface() {
  const auto shaderPath =
      findOfflineShaderSourcePath("offline_pbr_direct_ray.comp");
  EXPECT(!shaderPath.empty(),
         "offline PBR shader source should be discoverable for input test");
  if (shaderPath.empty()) {
    return;
  }

  const std::string shaderSource = readTextFile(shaderPath);
  EXPECT(!shaderSource.empty(),
         "offline PBR shader source should be readable for input test");
  EXPECT(shaderSource.find("#include \"common/material_surface.glsl\"") !=
             std::string::npos,
         "offline PBR shader should include the material surface ABI");
  EXPECT(shaderSource.find("#include LX_MATERIAL_CONTRACT_SOURCE") !=
             std::string::npos,
         "offline PBR shader should expose the material contract source hook");
  EXPECT(shaderSource.find("lxLoadMaterialSurface(materialIndex") !=
             std::string::npos,
         "offline PBR shader should call the material accessor");
  EXPECT(shaderSource.find("vec3 baseColor = max(surface.baseColor") !=
             std::string::npos,
         "offline PBR shader should read base color from accessor surface");
  EXPECT(shaderSource.find("pbrInput.metallic = clamp(surface.metallic") !=
             std::string::npos,
         "offline PBR shader should read metallic from accessor surface");
  EXPECT(shaderSource.find("pbrInput.roughness = clamp(surface.roughness") !=
             std::string::npos,
         "offline PBR shader should read roughness from accessor surface");
  EXPECT(shaderSource.find("pbrInput.ao = clamp(surface.ao") !=
             std::string::npos,
         "offline PBR shader should read AO from accessor surface");
  EXPECT(shaderSource.find("pbrInput.emissive = max(surface.emissive") !=
             std::string::npos,
         "offline PBR shader should read emissive from accessor surface");
  EXPECT(shaderSource.find("vec3 N = normalize(surface.normal)") !=
             std::string::npos,
         "offline PBR shader should read shading normal from accessor "
         "surface");
  EXPECT(shaderSource.find("lxPbrLayeredClearcoatDirectLight") !=
             std::string::npos,
         "offline PBR shader should use the shared layered clearcoat direct "
         "lighting helper");
  EXPECT(shaderSource.find("material.baseColorTexture") == std::string::npos,
         "offline PBR pass shader should not directly read legacy material "
         "record texture fields");
}

[[nodiscard]] Vec4f makeGpuPosition(float x, float y, float z) {
  return Vec4f{x, y, z, 1.0f};
}

[[nodiscard]] SceneGpuObjectRecord makeIdentityObject() {
  SceneGpuObjectRecord object;
  object.objectToWorld = {
      Vec4f{1.0f, 0.0f, 0.0f, 0.0f},
      Vec4f{0.0f, 1.0f, 0.0f, 0.0f},
      Vec4f{0.0f, 0.0f, 1.0f, 0.0f},
      Vec4f{0.0f, 0.0f, 0.0f, 1.0f},
  };
  return object;
}

struct ManualUploadView final {
  std::vector<Vec4f> positions;
  std::vector<u32> indices;
  std::vector<SceneGpuPrimitiveRecord> primitives;
  std::vector<SceneGpuObjectRecord> objects;

  [[nodiscard]] SceneResourceTableUploadView view() const {
    return SceneResourceTableUploadView{
        .positions = positions,
        .indices = indices,
        .primitives = primitives,
        .objects = objects,
    };
  }
};

[[nodiscard]] ManualUploadView makeSingleTriangleUploadView() {
  ManualUploadView upload;
  upload.positions = {
      makeGpuPosition(0.0f, 0.0f, 0.0f),
      makeGpuPosition(1.0f, 0.0f, 0.0f),
      makeGpuPosition(0.0f, 1.0f, 0.0f),
  };
  upload.indices = {0, 1, 2};
  upload.objects = {makeIdentityObject()};
  upload.primitives.push_back(SceneGpuPrimitiveRecord{
      .indexOffset = 0,
      .meshIndex = 0,
      .materialIndex = 0,
      .objectIndex = 0,
  });
  return upload;
}

[[nodiscard]] ManualUploadView makeSeparatedTriangleUploadView(u32 count) {
  ManualUploadView upload;
  upload.objects = {makeIdentityObject()};
  upload.positions.reserve(static_cast<usize>(count) * 3);
  upload.indices.reserve(static_cast<usize>(count) * 3);
  upload.primitives.reserve(count);
  for (u32 i = 0; i < count; ++i) {
    const float x = static_cast<float>(i) * 10.0f;
    const u32 vertexOffset = static_cast<u32>(upload.positions.size());
    upload.positions.push_back(makeGpuPosition(x, 0.0f, 0.0f));
    upload.positions.push_back(makeGpuPosition(x + 1.0f, 0.0f, 0.0f));
    upload.positions.push_back(makeGpuPosition(x, 1.0f, 0.0f));
    upload.indices.push_back(vertexOffset);
    upload.indices.push_back(vertexOffset + 1);
    upload.indices.push_back(vertexOffset + 2);
    upload.primitives.push_back(SceneGpuPrimitiveRecord{
        .indexOffset = static_cast<u32>(upload.indices.size() - 3),
        .meshIndex = i,
        .materialIndex = 0,
        .objectIndex = 0,
    });
  }
  return upload;
}

void expectBvhBuildThrows(const SceneResourceTableUploadView &view,
                          const std::string &messageFragment) {
  bool threw = false;
  try {
    (void)SceneSoftwareBvh::build(view);
  } catch (const std::runtime_error &error) {
    threw =
        std::string(error.what()).find(messageFragment) != std::string::npos;
  }
  EXPECT(threw, "malformed software BVH upload view should fail clearly");
}

void testSoftwareBvhBuildsFromSceneResourceTable() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(MaterialInstance::create(
          MaterialTemplate::create("software_bvh_material"))));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);
  (void)objectHandle;

  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(table.buildUploadView());
  EXPECT(!bvh.nodes().empty(), "software BVH should contain nodes");
  EXPECT(bvh.primitiveCount() == 1,
         "one indexed triangle should produce one BVH primitive");
  const u32 packedRootCount = packedU32(bvh.nodes().front().boundsMaxCount.w);
  EXPECT((packedRootCount & ~LeafNodeFlag) == 1,
         "single-triangle BVH root should reference one primitive");
}

void testSoftwareBvhThrowsForEmptyPrimitiveList() {
  bool threw = false;
  try {
    (void)SceneSoftwareBvh::build(SceneResourceTable{}.buildUploadView());
  } catch (const std::runtime_error &error) {
    threw = std::string(error.what()).find("empty primitive list") !=
            std::string::npos;
  }
  EXPECT(threw, "empty software BVH build should fail clearly");
}

void testSoftwareBvhUsesBindlessPositionStreamSource() {
  const auto bvhPath = findProjectFile("src/core/raytracing/software_bvh.cpp");
  EXPECT(!bvhPath.empty(),
         "software BVH source should be discoverable for bindless audit");
  const auto validationPath =
      findProjectFile("src/core/offline/offline_render_validation.cpp");
  EXPECT(!validationPath.empty(),
         "offline validation source should be discoverable for bindless audit");
  if (bvhPath.empty() || validationPath.empty()) {
    return;
  }

  const std::string bvhSource = readTextFile(bvhPath);
  const std::string validationSource = readTextFile(validationPath);
  EXPECT(bvhSource.find("scene.positions") != std::string::npos,
         "software BVH should read bounds from bindless positions");
  EXPECT(bvhSource.find("scene.vertices") == std::string::npos,
         "software BVH should not read legacy packed vertices");
  EXPECT(bvhSource.find("SceneGpuVertexRecord") == std::string::npos,
         "software BVH should not depend on the legacy packed vertex record");
  EXPECT(validationSource.find("uploadView.positions") != std::string::npos,
         "offline validation should validate bindless positions");
  EXPECT(validationSource.find("uploadView.vertices") == std::string::npos,
         "offline validation should not validate legacy packed vertices");
}

void testSoftwareBvhUsesCompactUploadIndicesAndObjectTransform() {
  SceneResourceTable table;
  const auto baseMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto liveMesh = table.registerMesh(uniqueMesh(makeOffsetMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(MaterialInstance::create(
          MaterialTemplate::create("software_bvh_transform_material"))));
  (void)baseMesh;

  Mat4f objectToWorld = Mat4f::translate(Vec3f{2.0f, 3.0f, 4.0f});
  ObjectResource object;
  object.mesh = liveMesh;
  object.material = material;
  object.objectToWorld = objectToWorld;
  object.worldBounds = BoundingBox{{2.0f, 3.0f, 4.0f}, {3.0f, 4.0f, 4.0f}};
  const auto objectHandle = table.registerObject(object);
  (void)objectHandle;

  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(table.buildUploadView());
  EXPECT(bvh.primitiveCount() == 1,
         "one indexed offset triangle should produce one BVH primitive");
  EXPECT(bvh.primitives().front().meshIndex == 1,
         "BVH should preserve compact mesh upload index");
  EXPECT(
      bvh.nodes().front().boundsMinLeftFirst.x == 2.0f,
      "root bounds should include object-space vertices transformed to world");
  EXPECT(bvh.nodes().front().boundsMinLeftFirst.y == 3.0f,
         "root bounds should include translated y minimum");
  EXPECT(bvh.nodes().front().boundsMinLeftFirst.z == 4.0f,
         "root bounds should include translated z minimum");
}

void testSoftwareBvhRejectsMalformedUploadViewReferences() {
  {
    auto upload = makeSingleTriangleUploadView();
    upload.primitives.front().objectIndex = 1;
    expectBvhBuildThrows(upload.view(), "object index");
  }
  {
    auto upload = makeSingleTriangleUploadView();
    upload.primitives.front().indexOffset = 1;
    expectBvhBuildThrows(upload.view(), "index range");
  }
  {
    auto upload = makeSingleTriangleUploadView();
    upload.indices.front() = 3;
    expectBvhBuildThrows(upload.view(), "vertex index");
  }
}

void testSoftwareBvhLeafRangesReferenceReorderedPrimitives() {
  const auto upload = makeSeparatedTriangleUploadView(5);
  const SceneSoftwareBvh bvh = SceneSoftwareBvh::build(upload.view());

  EXPECT(bvh.nodes().size() > 1,
         "five separated triangles should create internal BVH nodes");
  const auto &root = bvh.nodes().front();
  EXPECT(!isLeaf(root),
         "root node should be internal for more than four primitives");

  const u32 leftIndex = packedU32(root.boundsMinLeftFirst.w);
  const u32 rightIndex = packedU32(root.boundsMaxCount.w);
  EXPECT(leftIndex < bvh.nodes().size(), "left child index should be valid");
  EXPECT(rightIndex < bvh.nodes().size(), "right child index should be valid");

  std::array<bool, 5> visited{};
  for (const u32 childIndex : {leftIndex, rightIndex}) {
    if (childIndex >= bvh.nodes().size()) {
      continue;
    }
    const auto &child = bvh.nodes()[childIndex];
    EXPECT(isLeaf(child), "root children should be leaves for five primitives");
    const u32 first = leafFirst(child);
    const u32 count = leafCount(child);
    EXPECT(count > 0, "leaf should reference at least one primitive");
    EXPECT(static_cast<usize>(first) + count <= bvh.primitives().size(),
           "leaf primitive range should stay inside reordered primitive array");
    for (u32 i = 0;
         i < count && static_cast<usize>(first) + i < bvh.primitives().size();
         ++i) {
      const auto primitive = bvh.primitives()[first + i];
      EXPECT(primitive.primitiveIndex < visited.size(),
             "leaf range should map through reordered primitive references");
      if (primitive.primitiveIndex < visited.size()) {
        visited[primitive.primitiveIndex] = true;
      }
    }
  }
  for (bool wasVisited : visited) {
    EXPECT(wasVisited,
           "each upload primitive should appear in one BVH leaf range");
  }
}

void testSoftwareBvhLayoutContract() {
  EXPECT(sizeof(SceneSoftwareBvhNode) == 32,
         "SceneSoftwareBvhNode std430 contract should stay stable");
  EXPECT(sizeof(SceneSoftwareBvhPrimitive) == 12,
         "SceneSoftwareBvhPrimitive should only store derived BVH references");
}

void expectInvalidOfflineJobThrows(const offline::OfflineRenderJob &job,
                                   const std::string &messageFragment) {
  bool threw = false;
  try {
    offline::validateOfflineRenderJob(job);
  } catch (const std::runtime_error &error) {
    threw =
        std::string(error.what()).find(messageFragment) != std::string::npos;
  }
  EXPECT(threw, "invalid offline render job should fail before Vulkan setup");
}

[[nodiscard]] offline::OfflineRenderJob makeRenderableJobWithoutCamera() {
  offline::OfflineRenderJob job;
  job.output.width = 1;
  job.output.height = 1;
  const auto mesh = job.scene.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto material =
      job.scene.registerMaterial(uniqueMaterial(MaterialInstance::create(
          MaterialTemplate::create("offline_validation_material"))));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = job.scene.registerObject(object);
  (void)objectHandle;
  return job;
}

[[nodiscard]] CameraResource makeValidationCameraResource() {
  const CameraPose pose =
      makeCameraPose(Vec3f{0.0f, 0.0f, 3.0f}, Vec3f{0.0f, 0.0f, -1.0f},
                     Vec3f{0.0f, 1.0f, 0.0f});
  const CameraProjection projection;
  return CameraResource{
      .pose = pose,
      .projection = projection,
      .view = makeCameraViewMatrix(pose),
      .proj = makeCameraProjectionMatrix(projection),
      .active = true,
  };
}

[[nodiscard]] offline::OfflineRenderJob makeRenderableJobWithCamera() {
  offline::OfflineRenderJob job = makeRenderableJobWithoutCamera();
  const auto camera = job.scene.registerCamera(makeValidationCameraResource());
  (void)camera;
  return job;
}

void testOfflineRenderWorkGraphBuildsRayTracePass() {
  offline::OfflineRenderJob job = makeRenderableJobWithCamera();
  job.output.width = 17;
  job.output.height = 9;

  FrameGraph graph = offline::createOfflineRenderFrameGraph(job.output);
  graph.build(LX_core::RenderWorkBuildContext::offline(job));
  EXPECT(graph.getPasses().size() == 1,
         "offline default graph should have one ray trace pass");
  if (graph.getPasses().empty()) {
    return;
  }

  const FramePass &pass = graph.getPasses().front();
  EXPECT(pass.name == Pass_OfflineRayTrace,
         "offline graph pass should use OfflineRayTrace identity");
  EXPECT(pass.queue.getItems().size() == 1,
         "offline ray trace pass should submit one compute work item");
  if (pass.queue.getItems().empty()) {
    return;
  }

  const RenderWorkItem &item = pass.queue.getItems().front();
  EXPECT(item.domain == RenderDomain::Offline,
         "offline work item should mark offline domain");
  EXPECT(item.kind == RenderWorkKind::ComputeDispatch,
         "offline ray trace pass should execute as compute dispatch");
  EXPECT(!item.shaderInfo,
         "legacy offline build without shader should not silently invent one");
  EXPECT(
      item.compute.groupCountX == 3 && item.compute.groupCountY == 2 &&
          item.compute.groupCountZ == 1,
      "offline dispatch groups should round output dimensions to 8x8 groups");
}

void testOfflineRenderWorkGraphUsesJobComputeShader() {
  offline::OfflineRenderJob job = makeRenderableJobWithCamera();
  auto compileResult = LX_infra::ShaderCompiler::compileFile(
      findOfflineShaderSourcePath("offline_primary_ray.comp"));
  const auto shader = std::make_shared<LX_infra::CompiledShader>(
      std::move(compileResult.stages),
      LX_infra::ShaderReflector::reflect(compileResult.stages));
  EXPECT(shader != nullptr, "offline shader should compile for graph build");
  if (!shader) {
    return;
  }
  job.offlineShader = shader;

  FrameGraph graph = offline::createOfflineRenderFrameGraph(job.output);
  graph.build(LX_core::RenderWorkBuildContext::offline(job));
  EXPECT(!graph.getPasses().empty(),
         "offline graph should have pass when built with job shader");
  if (graph.getPasses().empty() ||
      graph.getPasses().front().queue.getItems().empty()) {
    return;
  }
  const RenderWorkItem &item =
      graph.getPasses().front().queue.getItems().front();
  EXPECT(item.shaderInfo == shader,
         "offline work item should carry compute shader from job");
}

void testOfflineSceneLoaderMapsMaterialV2ToOfflineRayTracePass() {
  const std::filesystem::path scenePath = writeOfflineLoaderSmokeScene();
  const auto document = LX_infra::scene_io::loadSceneDocument(scenePath);
  LX_infra::offline::OfflineAssetResolver resolver(scenePath);

  LX_infra::offline::OfflineSceneLoader strictLoader(resolver);
  auto strictLoaded = strictLoader.load(document, "/camera");
  EXPECT(!strictLoaded.offlineShader,
         "loader without explicit OfflineRT provider must not invent shader");
  auto strictUpload = strictLoaded.table.buildUploadView();
  const RenderSceneSnapshot strictSnapshot = strictLoaded.table.buildSnapshot();
  EXPECT(!strictSnapshot.materialHandles.empty(),
         "strict loader should still register the material");
  EXPECT(strictUpload.materials.empty(),
         "strict source-contract material should not create a legacy material "
         "record");
  EXPECT(!strictUpload.materialRefs.empty() &&
             !strictUpload.sourceMaterialRecords.empty(),
         "strict source-contract material should create a material ref and "
         "source-local record");
  if (!strictSnapshot.materialHandles.empty()) {
    const auto material =
        strictLoaded.table.resolve(strictSnapshot.materialHandles[0]);
    EXPECT(material.has_value(),
           "strict loader material handle should resolve from the table");
    if (material.has_value()) {
      EXPECT(!material->get().isPassEnabled(Pass_OfflineRayTrace),
             "strict loader should not enable OfflineRayTrace without provider");
    }
  }

  auto expectedShader =
      backend::offline::createOfflineComputeShader(
          "techniques/OfflineRT/offline_pbr_direct_ray");
  LX_infra::offline::OfflineSceneLoader loader(
      resolver, [expectedShader] { return expectedShader; });
  auto loaded = loader.load(document, "/camera");
  EXPECT(loaded.offlineShader == expectedShader,
         "Material v2 loader should use the explicit OfflineRT shader");

  auto upload = loaded.table.buildUploadView();
  const RenderSceneSnapshot snapshot = loaded.table.buildSnapshot();
  EXPECT(!snapshot.materialHandles.empty(),
         "loader should register the Material v2 instance");
  EXPECT(upload.materials.empty(),
         "source-contract Material v2 instance should not create a legacy "
         "material record");
  EXPECT(!upload.materialRefs.empty() && !upload.sourceMaterialRecords.empty(),
         "source-contract Material v2 instance should create a material ref "
         "and source-local record");
  if (snapshot.materialHandles.empty()) {
    return;
  }
  const auto material = loaded.table.resolve(snapshot.materialHandles[0]);
  EXPECT(material.has_value(),
         "loader material handle should resolve from the table");
  if (!material.has_value()) {
    return;
  }
  EXPECT(material->get().isPassEnabled(Pass_OfflineRayTrace),
         "Material v2 loader should enable the OfflineRayTrace pass");
  EXPECT(material->get().getPassShader(Pass_OfflineRayTrace) == expectedShader,
         "OfflineRayTrace pass should point at the explicit shader provider");
}

void testOfflineWorkItemCarriesUnifiedSceneTextureArray() {
  offline::OfflineRenderJob job = makeRenderableJobWithCamera();

  FrameGraph graph = offline::createOfflineRenderFrameGraph(job.output);
  graph.build(LX_core::RenderWorkBuildContext::offline(job));

  const RenderWorkItem &item =
      graph.getPasses().front().queue.getItems().front();
  const auto resource = std::find_if(
      item.descriptorResources.begin(), item.descriptorResources.end(),
      [](const DescriptorResourceRef &candidate) {
        return candidate.getBindingName() == StringID("SceneTextures");
      });
  EXPECT(resource != item.descriptorResources.end() &&
             resource->isTextureArray(),
         "offline scene storage should carry unified SceneTextures array");
  if (resource != item.descriptorResources.end() &&
      resource->isTextureArray()) {
    EXPECT(resource->textures().size() == 256,
           "SceneTextures should provide exactly 256 descriptor slots");
  }
}

void testOfflineWorkItemDoesNotInjectImplicitLightOrEnvironment() {
  offline::OfflineRenderJob job = makeRenderableJobWithCamera();

  FrameGraph graph = offline::createOfflineRenderFrameGraph(job.output);
  graph.build(LX_core::RenderWorkBuildContext::offline(job));

  const RenderWorkItem &item =
      graph.getPasses().front().queue.getItems().front();
  const GpuResourceRef frameParamsResource =
      findDescriptorResource(item, StringID("SceneFrameParams"));
  EXPECT(frameParamsResource.isValid(),
         "offline work item should expose SceneFrameParams");
  if (!frameParamsResource.isValid()) {
    return;
  }
  const auto &params = *static_cast<const SceneGpuFrameParams *>(
      frameParamsResource.get().getRawData());
  EXPECT(params.lightDirectionIntensity.w == 0.0f,
         "offline scene without lights must not inject default light intensity");
  EXPECT(params.lightColorEnvironment.x == 0.0f &&
             params.lightColorEnvironment.y == 0.0f &&
             params.lightColorEnvironment.z == 0.0f,
         "offline scene without lights must not inject default light color");
  EXPECT(params.lightColorEnvironment.w == 0.0f,
         "offline scene must not inject default environment intensity");
}

void testOfflineRenderJobValidationRejectsZeroDimensions() {
  offline::OfflineRenderJob job = makeRenderableJobWithoutCamera();
  job.output.width = 0;
  expectInvalidOfflineJobThrows(job, "width/height must be positive");
}

void testOfflineRenderJobValidationRejectsMissingCamera() {
  const offline::OfflineRenderJob job = makeRenderableJobWithoutCamera();
  expectInvalidOfflineJobThrows(job, "active camera");
}

void testOfflineRenderJobValidationRejectsNonRenderableScene() {
  offline::OfflineRenderJob job;
  job.output.width = 1;
  job.output.height = 1;
  const auto camera = job.scene.registerCamera(makeValidationCameraResource());
  (void)camera;
  expectInvalidOfflineJobThrows(job, "no upload positions");
}

} // namespace

int main() {
  testSoftwareBvhLayoutContract();
  testOfflineShaderUsesUnifiedSceneBuffers();
  testOfflinePbrDirectShaderCompiles();
  testOfflinePbrEmissiveTextureMatchesRealtimeSemantics();
  testOfflinePbrDirectShaderDefersTexturesToAccessor();
  testOfflinePbrDoesNotFlipBackFaceNormals();
  testOfflinePrimaryDoesNotFlipBackFaceNormals();
  testOfflineShadersCullHitsFromMaterialCullMode();
  testOfflinePbrReadsTangentSignFromUploadAbiField();
  testOfflinePbrDirectShaderUsesMaterialAccessorSurface();
  testOfflineRenderJobValidationRejectsZeroDimensions();
  testOfflineRenderJobValidationRejectsMissingCamera();
  testOfflineRenderJobValidationRejectsNonRenderableScene();
  testOfflineRenderWorkGraphBuildsRayTracePass();
  testOfflineRenderWorkGraphUsesJobComputeShader();
  testOfflineSceneLoaderMapsMaterialV2ToOfflineRayTracePass();
  testOfflineWorkItemCarriesUnifiedSceneTextureArray();
  testOfflineWorkItemDoesNotInjectImplicitLightOrEnvironment();
  testSoftwareBvhThrowsForEmptyPrimitiveList();
  testSoftwareBvhUsesBindlessPositionStreamSource();
  testSoftwareBvhBuildsFromSceneResourceTable();
  testSoftwareBvhUsesCompactUploadIndicesAndObjectTransform();
  testSoftwareBvhRejectsMalformedUploadViewReferences();
  testSoftwareBvhLeafRangesReferenceReorderedPrimitives();
  if (failures != 0) {
    std::cerr << "test_offline_gpu_scene failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_gpu_scene passed\n";
  return 0;
}
