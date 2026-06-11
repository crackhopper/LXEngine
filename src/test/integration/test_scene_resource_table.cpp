#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/scene_gpu_records.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/material_resource_parser.hpp"
#include "infra/resource_parsers/mesh_resource_parser.hpp"
#include "infra/resource_parsers/texture_resource_parser.hpp"
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <type_traits>

using namespace LX_core;

namespace {

template <typename Table, typename Arg>
concept CanRegisterMesh =
    requires(Table &table, Arg arg) { table.registerMesh(arg); };

template <typename Table, typename Arg>
concept CanRegisterTexture =
    requires(Table &table, Arg arg) { table.registerTexture(arg); };

static_assert(
    !std::is_invocable_r_v<MaterialHandle,
                           decltype(&SceneResourceTable::registerMaterial),
                           SceneResourceTable *, MaterialInstanceSharedPtr>,
    "SceneResourceTable must not accept shared_ptr material "
    "registration; the table is the unique owner.");
static_assert(!CanRegisterMesh<SceneResourceTable, MeshBufferSharedPtr>,
              "SceneResourceTable must not accept shared_ptr mesh "
              "registration; the table is the unique owner.");
static_assert(
    !CanRegisterTexture<SceneResourceTable, CombinedTextureSamplerSharedPtr>,
    "SceneResourceTable must not accept shared_ptr texture "
    "registration; the table is the unique owner.");
static_assert(!std::is_invocable_r_v<
                  LightHandle, decltype(&SceneResourceTable::registerLight),
                  SceneResourceTable *, LightBaseSharedPtr>,
              "SceneResourceTable must not accept shared_ptr light "
              "registration; the table is the unique owner.");
static_assert(
    !std::is_invocable_r_v<SkeletonHandle,
                           decltype(&SceneResourceTable::registerSkeleton),
                           SceneResourceTable *, SkeletonSharedPtr>,
    "SceneResourceTable must not accept shared_ptr skeleton "
    "registration; the table is the unique owner.");

int s_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__      \
                << ")\n";                                                      \
      ++s_failures;                                                            \
    }                                                                          \
  } while (0)

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

[[nodiscard]] std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file) {
    return {};
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
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

struct TangentVertex final {
  Vec3f pos{};
  Vec3f normal{};
  Vec2f uv{};
  Vec4f tangent{};

  static const VertexLayout &getLayout() {
    static const VertexLayout layout{
        {{"inPos", 0, DataType::Float3, sizeof(TangentVertex),
          offsetof(TangentVertex, pos)},
         {"inNormal", 1, DataType::Float3, sizeof(TangentVertex),
          offsetof(TangentVertex, normal)},
         {"inUV", 2, DataType::Float2, sizeof(TangentVertex),
          offsetof(TangentVertex, uv)},
         {"inTangent", 3, DataType::Float4, sizeof(TangentVertex),
          offsetof(TangentVertex, tangent)}},
        sizeof(TangentVertex)};
    return layout;
  }
};

class FakeShader final : public IShader {
public:
  explicit FakeShader(std::vector<ShaderResourceBinding> bindings)
      : m_bindings(std::move(bindings)) {}

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &item : m_bindings) {
      if (item.set == set && item.binding == binding) {
        return std::cref(item);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &item : m_bindings) {
      if (item.name == name) {
        return std::cref(item);
      }
    }
    return std::nullopt;
  }

  usize getProgramHash() const override { return 0; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
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

MeshBufferSharedPtr makeTwoTriangleMeshBuffer() {
  auto vertices = std::vector<TestVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
      {{1.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2, 2, 1, 3};
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

MeshBufferSharedPtr makeInvalidIndexRangeMeshBuffer() {
  auto vertices = std::vector<TestVertex>{
      {{-10.0f, -10.0f, 0.0f}},
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 2, 3};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  auto storage = GeometryStorage::create(vb, ib);
  return MeshBuffer::create(
      storage, 1, 0, 3, 3, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshBufferSharedPtr makeLineListMeshBufferWithTriangleSizedIndexCount() {
  auto vertices = std::vector<TestVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
      {{1.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 1, 2, 2, 3};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib =
      IndexBuffer::create(std::move(indices), PrimitiveTopology::LineList);
  return MeshBuffer::create(
      vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MeshBufferSharedPtr makeTangentSignMeshBuffer() {
  auto vertices = std::vector<TangentVertex>{
      {{0.0f, 0.0f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {0.25f, 0.75f},
       {1.0f, 0.0f, 0.0f, -1.0f}},
      {{1.0f, 0.0f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {0.5f, 0.75f},
       {1.0f, 0.0f, 0.0f, -1.0f}},
      {{0.0f, 1.0f, 0.0f},
       {0.0f, 0.0f, 1.0f},
       {0.25f, 1.0f},
       {1.0f, 0.0f, 0.0f, -1.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2};
  auto vb = VertexBuffer<TangentVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  return MeshBuffer::create(
      vb, ib, BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});
}

MaterialInstanceSharedPtr
makeGpuRecordMaterial(const Vec4f &baseColor = Vec4f{0.25f, 0.5f, 0.75f, 0.9f},
                      const CullMode cullMode = CullMode::Back) {
  auto shader =
      std::make_shared<FakeShader>(std::vector<ShaderResourceBinding>{});
  auto materialTemplate = MaterialTemplate::create("scene_gpu_records");
  ShaderProgramSet shaderSet;
  shaderSet.shaderName = "scene_gpu_records";
  shaderSet.shader = shader;
  MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = shaderSet;
  passDefinition.renderState = RenderState{};
  passDefinition.renderState.cullMode = cullMode;
  materialTemplate->setPassDefinition(Pass_Forward, std::move(passDefinition));
  MaterialPassDefinition offlinePassDefinition;
  offlinePassDefinition.shaderProgram = shaderSet;
  offlinePassDefinition.renderState = RenderState{};
  offlinePassDefinition.renderState.cullMode = cullMode;
  materialTemplate->setPassDefinition(Pass_OfflineRayTrace,
                                      std::move(offlinePassDefinition));
  materialTemplate->rebuildMaterialInterface();

  auto material = MaterialInstance::create(materialTemplate);
  material->setBsdfType("matte");
  MaterialParameterEnvelope kd;
  kd.kind = MaterialEnvelopeKind::Rgb;
  kd.rgbValue = Vec3f{baseColor.x, baseColor.y, baseColor.z};
  material->setMaterialEnvelope(StringID("Kd"), std::move(kd));
  return material;
}

MaterialInstanceSharedPtr makeSceneMaterialsShaderMaterial(
    const Vec4f &baseColor = Vec4f{0.25f, 0.5f, 0.75f, 0.9f}) {
  ShaderResourceBinding sceneMaterials;
  sceneMaterials.name = "SceneMaterials";
  sceneMaterials.set = 0;
  sceneMaterials.binding = 7;
  sceneMaterials.type = ShaderPropertyType::StorageBuffer;
  sceneMaterials.descriptorCount = 1;
  sceneMaterials.size = sizeof(SceneGpuMaterialRecord);
  sceneMaterials.stageFlags = ShaderStage::Fragment;

  auto shader = std::make_shared<FakeShader>(
      std::vector<ShaderResourceBinding>{sceneMaterials});
  auto materialTemplate = MaterialTemplate::create("scene_materials_shader");
  ShaderProgramSet shaderSet;
  shaderSet.shaderName = "scene_materials_shader";
  shaderSet.shader = shader;
  MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = shaderSet;
  passDefinition.renderState = RenderState{};
  materialTemplate->setPassDefinition(Pass_Forward, std::move(passDefinition));
  materialTemplate->rebuildMaterialInterface();

  auto material = MaterialInstance::create(materialTemplate);
  material->setBsdfType("matte");
  MaterialParameterEnvelope kd;
  kd.kind = MaterialEnvelopeKind::Rgb;
  kd.rgbValue = Vec3f{baseColor.x, baseColor.y, baseColor.z};
  material->setMaterialEnvelope(StringID("Kd"), std::move(kd));
  return material;
}

MaterialInstanceSharedPtr makeLegacyShaderBindingOnlyMaterial() {
  ShaderResourceBinding binding;
  binding.name = "MaterialUBO";
  binding.set = 2;
  binding.binding = 0;
  binding.type = ShaderPropertyType::UniformBuffer;
  binding.size = 48;
  binding.members = {
      {"baseColor", ShaderPropertyType::Vec4, 0, 16},
      {"specularIntensity", ShaderPropertyType::Float, 16, 4},
      {"ambientIntensity", ShaderPropertyType::Float, 20, 4},
      {"clearcoatFactor", ShaderPropertyType::Float, 24, 4},
      {"clearcoatRoughness", ShaderPropertyType::Float, 28, 4},
      {"emissiveFactor", ShaderPropertyType::Vec4, 32, 16},
  };

  auto shader =
      std::make_shared<FakeShader>(std::vector<ShaderResourceBinding>{binding});
  auto materialTemplate = MaterialTemplate::create("legacy_shader_binding");
  ShaderProgramSet shaderSet;
  shaderSet.shaderName = "legacy_shader_binding";
  shaderSet.shader = shader;
  MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = shaderSet;
  passDefinition.renderState = RenderState{};
  materialTemplate->setPassDefinition(Pass_OfflineRayTrace,
                                      std::move(passDefinition));
  materialTemplate->rebuildMaterialInterface();

  auto material = MaterialInstance::create(materialTemplate);
  material->writeShaderBindingParameter(StringID("MaterialUBO"),
                                        StringID("baseColor"),
                                        Vec4f{0.9f, 0.1f, 0.2f, 0.3f});
  material->writeShaderBindingParameter(StringID("MaterialUBO"),
                                        StringID("specularIntensity"), 0.7f);
  material->writeShaderBindingParameter(StringID("MaterialUBO"),
                                        StringID("ambientIntensity"), 0.8f);
  material->writeShaderBindingParameter(StringID("MaterialUBO"),
                                        StringID("clearcoatFactor"), 0.9f);
  material->writeShaderBindingParameter(StringID("MaterialUBO"),
                                        StringID("clearcoatRoughness"), 0.6f);
  material->writeShaderBindingParameter(StringID("MaterialUBO"),
                                        StringID("emissiveFactor"),
                                        Vec4f{0.3f, 0.4f, 0.5f, 0.6f});
  material->syncGpuData();
  return material;
}

MaterialInstanceSharedPtr
makeLegacyTextureBindingOnlyMaterial(TextureHandle albedo, TextureHandle normal,
                                     TextureHandle metallic, TextureHandle ao,
                                     TextureHandle emissive) {
  std::vector<ShaderResourceBinding> bindings;
  auto addTextureBinding = [&bindings](const char *name, u32 bindingIndex) {
    ShaderResourceBinding binding;
    binding.name = name;
    binding.set = 2;
    binding.binding = bindingIndex;
    binding.type = ShaderPropertyType::Texture2D;
    binding.descriptorCount = 1;
    binding.stageFlags = ShaderStage::Fragment;
    bindings.push_back(binding);
  };
  addTextureBinding("albedoMap", 0);
  addTextureBinding("normalMap", 1);
  addTextureBinding("metallicRoughnessMap", 2);
  addTextureBinding("aoMap", 3);
  addTextureBinding("emissiveMap", 4);

  auto shader = std::make_shared<FakeShader>(std::move(bindings));
  auto materialTemplate = MaterialTemplate::create("legacy_texture_binding");
  ShaderProgramSet shaderSet;
  shaderSet.shaderName = "legacy_texture_binding";
  shaderSet.shader = shader;
  MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = shaderSet;
  passDefinition.renderState = RenderState{};
  materialTemplate->setPassDefinition(Pass_Forward, std::move(passDefinition));
  materialTemplate->rebuildMaterialInterface();

  auto material = MaterialInstance::create(materialTemplate);
  material->setTextureHandle(StringID("albedoMap"), albedo);
  material->setTextureHandle(StringID("normalMap"), normal);
  material->setTextureHandle(StringID("metallicRoughnessMap"), metallic);
  material->setTextureHandle(StringID("aoMap"), ao);
  material->setTextureHandle(StringID("emissiveMap"), emissive);
  material->syncGpuData();
  return material;
}

MeshBuffer::UniquePtr uniqueMesh(const MeshBufferSharedPtr &mesh) {
  return mesh->cloneUnique();
}

MaterialInstance::UniquePtr
uniqueMaterial(const MaterialInstanceSharedPtr &material) {
  return material->cloneInstanceDataUnique();
}

CombinedTextureSamplerUniquePtr uniqueWhiteSampler(u32 width = 1,
                                                   u32 height = 1) {
  return std::make_unique<CombinedTextureSampler>(
      createWhiteTexture(width, height));
}

MaterialInstanceUniquePtr uniqueBlankMaterial(const char *name) {
  return MaterialInstance::createUnique(MaterialTemplate::create(name));
}

u64 graphVersionForUri(const SceneResourceTable &table,
                       const ResourceUri &uri) {
  const auto graph = table.exportResourceGraph();
  for (const auto &metadata : graph.resources) {
    if (metadata.uri == uri) {
      return metadata.version;
    }
  }
  return u64_max;
}

bool graphHasReadyMetadataForUri(const SceneResourceTable &table,
                                 const ResourceUri &uri) {
  const auto graph = table.exportResourceGraph();
  for (const auto &metadata : graph.resources) {
    if (metadata.uri == uri && metadata.state == ResourceState::Ready) {
      return true;
    }
  }
  return false;
}

void testSceneResourceTableOwnsTypedPayloads() {
  SceneResourceTable table;
  TextureHandle textureHandle;
  {
    auto texture = createWhiteTexture(1, 1);
    auto sampler = std::make_unique<CombinedTextureSampler>(texture);
    textureHandle = table.registerTexture(
        ResourceUri("memory://texture/shared-white"), std::move(sampler));
  }

  MeshHandle meshHandle;
  {
    auto mesh = uniqueMesh(makeMeshBuffer());
    meshHandle = table.registerMesh(ResourceUri("memory://mesh/triangle"),
                                    std::move(mesh));
  }

  EXPECT(textureHandle.isValid(), "texture handle should be valid");
  EXPECT(meshHandle.isValid(), "mesh handle should be valid");
  EXPECT(table.hasTexture(textureHandle),
         "table should report owned texture payload");
  EXPECT(table.hasMesh(meshHandle), "table should report owned mesh payload");
  EXPECT(table.texture(textureHandle).texture()->desc().width == 1,
         "table should own texture payload");
  EXPECT(table.mesh(meshHandle).getIndexCount() == 3,
         "table should own mesh payload");
}

void testSceneResourceTableDeduplicatesCanonicalUriRegistrations() {
  SceneResourceTable table;
  const ResourceUri textureUri("memory://texture/dedup-white");
  const TextureHandle firstTexture =
      table.registerTexture(textureUri, uniqueWhiteSampler(1, 1));
  const TextureHandle secondTexture =
      table.registerTexture(textureUri, uniqueWhiteSampler(2, 2));
  EXPECT(firstTexture == secondTexture,
         "duplicate texture URI should return the existing texture handle");
  EXPECT(table.textureCount() == 1,
         "duplicate texture URI should not allocate a second texture entry");
  EXPECT(table.texture(secondTexture).texture()->desc().width == 1,
         "duplicate texture registration should preserve the existing payload");

  const ResourceUri meshUri("memory://mesh/dedup-triangle");
  const MeshHandle firstMesh =
      table.registerMesh(meshUri, uniqueMesh(makeMeshBuffer()));
  const MeshHandle secondMesh =
      table.registerMesh(meshUri, uniqueMesh(makeTwoTriangleMeshBuffer()));
  EXPECT(firstMesh == secondMesh,
         "duplicate mesh URI should return the existing mesh handle");
  EXPECT(table.meshCount() == 1,
         "duplicate mesh URI should not allocate a second mesh entry");
  EXPECT(table.mesh(secondMesh).getIndexCount() == 3,
         "duplicate mesh registration should preserve the existing payload");

  const ResourceUri materialUri("memory://material/dedup-base");
  const MaterialHandle firstMaterial = table.registerMaterialInstance(
      materialUri, uniqueBlankMaterial("dedup_base"));
  const MaterialHandle secondMaterial = table.registerMaterialInstance(
      materialUri, uniqueBlankMaterial("dedup_replacement"));
  EXPECT(firstMaterial == secondMaterial,
         "duplicate material URI should return the existing material handle");
  EXPECT(table.materialCount() == 1,
         "duplicate material URI should not allocate a second material entry");
}

void testMeshAndTextureParsersReturnTableOwnedHandles() {
  SceneResourceTable table;
  LX_infra::MeshResourceParser meshParser;
  LX_infra::TextureResourceParser textureParser;

  const ResourceUri meshUri("memory://mesh/parser-triangle");
  const auto parsedMesh = meshParser.parse(table, meshUri, {});
  const auto meshMetadata = table.findResourceMetadata(parsedMesh.identity);
  const auto meshHandle = table.findMesh(meshUri);

  EXPECT(parsedMesh.identity.isValid(),
         "mesh parser should return a resource identity");
  EXPECT(meshMetadata != nullptr &&
             meshMetadata->type == SceneResourceType::Mesh,
         "mesh parser identity should have mesh metadata");
  EXPECT(meshMetadata != nullptr && meshMetadata->state == ResourceState::Ready,
         "memory mesh parser identity should be ready");
  EXPECT(meshHandle.has_value() && table.hasMesh(*meshHandle),
         "mesh parser should populate a table-owned mesh payload");
  EXPECT(meshHandle.has_value() && table.mesh(*meshHandle).getIndexCount() == 3,
         "memory mesh parser should create a triangle payload");

  const ResourceUri textureUri("memory://texture/parser-white");
  const auto parsedTexture = textureParser.parse(table, textureUri, {});
  const auto textureMetadata =
      table.findResourceMetadata(parsedTexture.identity);
  const auto textureHandle = table.findTexture(textureUri);

  EXPECT(parsedTexture.identity.isValid(),
         "texture parser should return a resource identity");
  EXPECT(textureMetadata != nullptr &&
             textureMetadata->type == SceneResourceType::Texture,
         "texture parser identity should have texture metadata");
  EXPECT(textureMetadata != nullptr &&
             textureMetadata->state == ResourceState::Ready,
         "memory texture parser identity should be ready");
  EXPECT(textureHandle.has_value() && table.hasTexture(*textureHandle),
         "texture parser should populate a table-owned texture payload");
  EXPECT(textureHandle.has_value() &&
             table.texture(*textureHandle).texture()->desc().width == 1,
         "memory texture parser should create a 1x1 texture payload");
}

void testMeshAndTextureParsersReuseCanonicalTableOwnedResources() {
  SceneResourceTable table;
  LX_infra::MeshResourceParser meshParser;
  LX_infra::TextureResourceParser textureParser;

  const ResourceUri meshUri("memory://mesh/parser-reuse");
  const auto firstMesh = meshParser.parse(table, meshUri, {});
  const auto firstMeshHandle = table.findMesh(meshUri);
  const auto secondMesh = meshParser.parse(table, meshUri, {});
  const auto secondMeshHandle = table.findMesh(meshUri);

  EXPECT(firstMesh.identity == secondMesh.identity,
         "same canonical mesh URI should reuse the same resource identity");
  EXPECT(firstMeshHandle.has_value() && secondMeshHandle.has_value() &&
             *firstMeshHandle == *secondMeshHandle,
         "same canonical mesh URI should reuse the table-owned mesh handle");
  EXPECT(table.meshCount() == 1,
         "same canonical mesh URI should not allocate a second mesh payload");

  const ResourceUri textureUri("memory://texture/parser-reuse");
  const auto firstTexture = textureParser.parse(table, textureUri, {});
  const auto firstTextureHandle = table.findTexture(textureUri);
  const auto secondTexture = textureParser.parse(table, textureUri, {});
  const auto secondTextureHandle = table.findTexture(textureUri);

  EXPECT(firstTexture.identity == secondTexture.identity,
         "same canonical texture URI should reuse the same resource identity");
  EXPECT(
      firstTextureHandle.has_value() && secondTextureHandle.has_value() &&
          *firstTextureHandle == *secondTextureHandle,
      "same canonical texture URI should reuse the table-owned texture handle");
  EXPECT(table.textureCount() == 1, "same canonical texture URI should not "
                                    "allocate a second texture payload");
}

void testMeshAndTextureParsersLoadAssetsIntoTableStorageAndFailMissingAssets() {
  SceneResourceTable table;
  LX_infra::MeshResourceParser meshParser;
  LX_infra::TextureResourceParser textureParser;

  const bool found =
      cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf");
  EXPECT(found, "DamagedHelmet asset root should be discoverable");
  if (!found) {
    return;
  }

  const ResourceUri meshUri(
      "assets://models/damaged_helmet/DamagedHelmet.gltf");
  const auto parsedMesh = meshParser.parse(table, meshUri, {});
  const auto meshMetadata = table.findResourceMetadata(parsedMesh.identity);
  EXPECT(parsedMesh.identity.isValid(),
         "asset mesh URI should return a resource identity");
  EXPECT(meshMetadata != nullptr && meshMetadata->state == ResourceState::Ready,
         "asset mesh URI should be marked ready");
  const auto meshHandle = table.findMesh(meshUri);
  EXPECT(meshHandle.has_value() && table.hasMesh(*meshHandle),
         "mesh parser should populate a table-owned asset mesh payload");
  EXPECT(meshHandle.has_value() && table.mesh(*meshHandle).getVertexCount() > 0,
         "asset mesh payload should contain vertices");

  const auto missingMesh =
      meshParser.parse(table, ResourceUri("assets://meshes/missing.obj"), {});
  const auto missingMeshMetadata =
      table.findResourceMetadata(missingMesh.identity);
  EXPECT(missingMesh.identity.isValid(),
         "missing mesh URI should still return diagnostic metadata");
  EXPECT(missingMeshMetadata != nullptr &&
             missingMeshMetadata->state == ResourceState::Failed,
         "missing mesh URI should be marked failed");
  EXPECT(!missingMesh.diagnostics.empty(),
         "missing mesh URI should include an explicit diagnostic");

  const auto parsedTexture = textureParser.parse(
      table, ResourceUri("assets://textures/parser.png"), {});
  const auto textureMetadata =
      table.findResourceMetadata(parsedTexture.identity);
  EXPECT(parsedTexture.identity.isValid(),
         "missing texture URI should still return diagnostic metadata");
  EXPECT(textureMetadata != nullptr &&
             textureMetadata->state == ResourceState::Failed,
         "missing texture URI should be marked failed");
  EXPECT(!parsedTexture.diagnostics.empty(),
         "missing texture URI should include an explicit diagnostic");
}

void testParserFailureUpdatesExistingMetadataIdentity() {
  SceneResourceTable table;
  LX_infra::MeshResourceParser meshParser;
  LX_infra::TextureResourceParser textureParser;

  const ResourceUri meshUri("assets://meshes/parser-fail-after-identity.obj");
  const ResourceIdentityHandle meshIdentity =
      table.loadOrGetResource(SceneResourceType::Mesh, meshUri);
  EXPECT(table.metadata(meshIdentity).state == ResourceState::Ready,
         "test setup should start with metadata-only mesh identity");

  const auto parsedMesh = meshParser.parse(table, meshUri, {});
  const auto *meshMetadata = table.findResourceMetadata(parsedMesh.identity);
  EXPECT(parsedMesh.identity == meshIdentity,
         "failed mesh parse should reuse existing metadata identity");
  EXPECT(meshMetadata != nullptr &&
             meshMetadata->state == ResourceState::Failed,
         "failed mesh parse should update existing metadata state");
  EXPECT(meshMetadata != nullptr && !meshMetadata->diagnostics.empty(),
         "failed mesh parse should merge diagnostics into existing metadata");

  const ResourceUri textureUri(
      "assets://textures/parser-missing-after-identity.png");
  const ResourceIdentityHandle textureIdentity =
      table.loadOrGetResource(SceneResourceType::Texture, textureUri);
  EXPECT(table.metadata(textureIdentity).state == ResourceState::Ready,
         "test setup should start with metadata-only texture identity");

  const auto parsedTexture = textureParser.parse(table, textureUri, {});
  const auto *textureMetadata =
      table.findResourceMetadata(parsedTexture.identity);
  EXPECT(parsedTexture.identity == textureIdentity,
         "failed texture parse should reuse existing metadata identity");
  EXPECT(textureMetadata != nullptr &&
             textureMetadata->state == ResourceState::Failed,
         "failed texture parse should update existing metadata state");
  EXPECT(
      textureMetadata != nullptr && !textureMetadata->diagnostics.empty(),
      "failed texture parse should merge diagnostics into existing metadata");
}

void testFailedUriRegistrationDoesNotLeaveReadyMetadata() {
  SceneResourceTable table;
  const ResourceUri meshUri("memory://mesh/null-registration");
  const ResourceUri materialUri("memory://material/null-registration");
  const ResourceUri textureUri("memory://texture/null-registration");

  const MeshHandle mesh = table.registerMesh(meshUri, MeshBufferUniquePtr{});
  const MaterialHandle material =
      table.registerMaterialInstance(materialUri, MaterialInstanceUniquePtr{});
  const TextureHandle texture =
      table.registerTexture(textureUri, CombinedTextureSamplerUniquePtr{});

  EXPECT(!mesh.isValid(), "null mesh URI registration should fail");
  EXPECT(!material.isValid(), "null material URI registration should fail");
  EXPECT(!texture.isValid(), "null texture URI registration should fail");
  EXPECT(!graphHasReadyMetadataForUri(table, meshUri),
         "failed mesh URI registration should not export Ready metadata");
  EXPECT(!graphHasReadyMetadataForUri(table, materialUri),
         "failed material URI registration should not export Ready metadata");
  EXPECT(!graphHasReadyMetadataForUri(table, textureUri),
         "failed texture URI registration should not export Ready metadata");
}

void testReleasedSlotReuseDoesNotRetainOldMetadataIdentity() {
  SceneResourceTable table;
  const ResourceUri oldUri("memory://texture/released");
  const TextureHandle oldTexture =
      table.registerTexture(oldUri, uniqueWhiteSampler(1, 1));
  EXPECT(oldTexture.isValid(), "test setup should register old texture");
  table.release(oldTexture);

  const TextureHandle replacement =
      table.registerTexture(uniqueWhiteSampler(1, 1));
  EXPECT(replacement.isValid(),
         "test setup should register replacement texture");
  EXPECT(replacement.index == oldTexture.index,
         "test setup should reuse the released texture slot");

  table.markDirty(replacement, "replacement changed");
  EXPECT(graphVersionForUri(table, oldUri) == 0,
         "non-URI slot reuse should not dirty the released URI metadata");
}

void testInvalidAndStaleDirtyHandlesAreHarmless() {
  SceneResourceTable table;
  table.markDirty(TextureHandle{}, "invalid texture handle");

  const TextureHandle texture = table.registerTexture(
      ResourceUri("memory://texture/stale-dirty"), uniqueWhiteSampler(1, 1));
  table.release(texture);
  table.markDirty(texture, "stale texture handle");

  EXPECT(graphVersionForUri(table,
                            ResourceUri("memory://texture/stale-dirty")) == 0,
         "stale dirty handle should not mutate released resource metadata");
}

void testResourceStateVersionAndDirtyPropagation() {
  SceneResourceTable table;
  const TextureHandle texture = table.registerTexture(
      ResourceUri("memory://texture/albedo"), uniqueWhiteSampler(1, 1));
  const MaterialHandle material =
      table.registerMaterialInstance(ResourceUri("memory://material/base"),
                                     uniqueBlankMaterial("dirty_propagation"));
  table.addDependency(material, texture);

  const auto before = table.metadata(material).version;
  table.markDirty(texture, "texture reloaded");

  EXPECT(table.metadata(texture).state == ResourceState::Ready,
         "dirty ready resource should remain ready");
  EXPECT(table.metadata(texture).version > 0,
         "dirty resource should bump version");
  EXPECT(table.metadata(material).version > before,
         "dependent material should receive dirty version bump");
  EXPECT(table.metadata(material).state == ResourceState::Dirty,
         "dependent material should be marked dirty");
  EXPECT(!table.metadata(material).diagnostics.empty() &&
             table.metadata(material).diagnostics.back().message.find(
                 "texture reloaded") != std::string::npos,
         "dirty propagation should keep reason");
}

void testDirtyPropagationHandlesDependencyCyclesOnce() {
  SceneResourceTable table;
  const ResourceIdentityHandle a = table.loadOrGetResource(
      SceneResourceType::Material, ResourceUri("memory://cycle/a"));
  const ResourceIdentityHandle b = table.loadOrGetResource(
      SceneResourceType::Texture, ResourceUri("memory://cycle/b"));
  table.addDependency(a, b);
  table.addDependency(b, a);

  table.markDirty(b, "cycle dirty");

  EXPECT(table.metadata(a).version == 1,
         "cycle dirty propagation should bump first resource once");
  EXPECT(table.metadata(b).version == 1,
         "cycle dirty propagation should bump second resource once");
  EXPECT(table.metadata(a).state == ResourceState::Dirty,
         "cycle dependent should become dirty");
  EXPECT(table.metadata(b).state == ResourceState::Ready,
         "dirty root may remain ready");
}

SkeletonSharedPtr makeSkeleton() {
  return Skeleton::create({
      Bone{"root", -1, Vec3f{0.0f, 0.0f, 0.0f}, Quatf{}},
      Bone{"tip", 0, Vec3f{0.0f, 1.0f, 0.0f}, Quatf{}},
  });
}

void testGeometryStorageAndMeshBufferContract() {
  auto mesh = makeMeshBuffer();
  EXPECT(mesh->getGeometryStorage() != nullptr,
         "MeshBuffer should reference GeometryStorage");
  EXPECT(mesh->getVertexBuffer().getVertexCount() == 3,
         "MeshBuffer should expose storage vertex count");
  EXPECT(mesh->getIndexBuffer().indexCount() == 3,
         "MeshBuffer should expose storage index count");
  EXPECT(mesh->getVertexOffset() == 0, "default vertex offset should be zero");
  EXPECT(mesh->getIndexOffset() == 0, "default index offset should be zero");
  EXPECT(mesh->getBounds().isValid(), "MeshBuffer should carry bounds");
}

void testHandleGenerationInvalidatesStaleMeshHandle() {
  SceneResourceTable table;
  auto first = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  EXPECT(table.isAlive(first), "registered mesh handle should be alive");
  EXPECT(table.resolve(first).has_value(),
         "registered mesh handle should resolve");
  if (const auto mesh = table.resolve(first)) {
    EXPECT(!mesh->get().getGeometryStorage(),
           "table-owned mesh should not retain pending shared geometry");
    EXPECT(mesh->get().getGeometryStorageHandle().isValid(),
           "table-owned mesh should reference table-owned geometry by handle");
    EXPECT(table.resolve(mesh->get().getGeometryStorageHandle()).has_value(),
           "table-owned mesh geometry handle should resolve");
  }

  table.release(first);
  EXPECT(!table.isAlive(first), "released mesh handle should not be alive");
  EXPECT(!table.resolve(first).has_value(),
         "released mesh handle should not resolve");

  auto second = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  EXPECT(second.index == first.index, "table may reuse released slot");
  EXPECT(second.generation != first.generation,
         "reused slot should get a new generation");
  EXPECT(table.isAlive(second), "new mesh handle should be alive");
  EXPECT(!table.isAlive(first), "stale mesh handle should remain invalid");
}

void testSceneRegistersRenderableComponentResources() {
  auto mesh = makeMeshBuffer();
  auto material = MaterialInstance::create(
      MaterialTemplate::create("scene_resource_table"));
  auto node = SceneNode::create("resource_table_node");
  node->addComponent<MeshComponent>(mesh);
  node->addComponent<MaterialComponent>(material);
  node->addComponent<SkeletonComponent>(makeSkeleton());

  auto scene = Scene::create("resource_table_scene", node);

  const auto meshComponent = node->getComponent<MeshComponent>();
  const auto materialComponent = node->getComponent<MaterialComponent>();
  EXPECT(meshComponent.has_value(),
         "node should keep the registered mesh component");
  EXPECT(materialComponent.has_value(),
         "node should keep the registered material component");
  const auto skeletonComponent = node->getComponent<SkeletonComponent>();
  EXPECT(skeletonComponent.has_value(),
         "node should keep the registered skeleton component");
  EXPECT(dynamic_cast<IRenderableComponent *>(&meshComponent->get()) != nullptr,
         "mesh component should expose renderable component capability");

  const MeshHandle meshHandle = meshComponent->get().getMeshHandle();
  const ObjectHandle objectHandle = meshComponent->get().getObjectHandle();
  const GeometryStorageHandle geometryHandle =
      meshComponent->get().getGeometryStorageHandle();
  const MaterialHandle materialHandle =
      materialComponent->get().getMaterialHandle();
  const SkeletonHandle skeletonHandle =
      skeletonComponent ? skeletonComponent->get().getSkeletonHandle()
                        : SkeletonHandle{};
  EXPECT(!meshComponent->get().getPendingMesh(),
         "registered mesh component should clear pending shared mesh");
  EXPECT(!materialComponent->get().getPendingMaterialInstance(),
         "registered material component should clear pending shared material");
  EXPECT(skeletonComponent && !skeletonComponent->get().getPendingSkeleton(),
         "registered skeleton component should clear pending shared skeleton");
  EXPECT(geometryHandle.isValid(),
         "mesh component should receive a geometry storage handle");
  EXPECT(meshHandle.isValid(), "mesh component should receive a mesh handle");
  EXPECT(objectHandle.isValid(),
         "mesh component should receive an object handle");
  EXPECT(materialHandle.isValid(),
         "material component should receive a material handle");
  EXPECT(skeletonHandle.isValid(),
         "skeleton component should receive a skeleton handle");
  EXPECT(scene->resources().geometryStorageCount() == 1,
         "scene resource table should own one geometry storage entry");
  EXPECT(scene->resources().meshCount() == 1,
         "scene resource table should own one mesh entry");
  EXPECT(scene->resources().materialCount() == 1,
         "scene resource table should own one material entry");
  EXPECT(scene->resources().objectCount() == 1,
         "scene resource table should own one object entry");
  EXPECT(scene->resources().skeletonCount() == 1,
         "scene resource table should own one skeleton entry");
  EXPECT(scene->resources().resolve(meshHandle).has_value(),
         "mesh handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(objectHandle).has_value(),
         "object handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(geometryHandle).has_value(),
         "geometry storage handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(materialHandle).has_value(),
         "material handle should resolve through scene resource table");
  EXPECT(scene->resources().resolve(skeletonHandle).has_value(),
         "skeleton handle should resolve through scene resource table");
  if (const auto storage = scene->resources().resolve(geometryHandle)) {
    const auto vertexRef = node->getVertexBuffer();
    const auto indexRef = node->getIndexBuffer();
    EXPECT(vertexRef.isValid() &&
               &vertexRef.get() == &storage->get().getVertexBuffer(),
           "node vertex resource should reference table-owned geometry");
    EXPECT(indexRef.isValid() &&
               &indexRef.get() == &storage->get().getIndexBuffer(),
           "node index resource should reference table-owned geometry");
  }

  const auto snapshot = scene->resources().buildSnapshot();
  EXPECT(snapshot.geometryStorageHandles.size() == 1,
         "snapshot should include geometry storage handles");
  EXPECT(snapshot.meshHandles.size() == 1,
         "snapshot should include mesh handles");
  EXPECT(snapshot.materialHandles.size() == 1,
         "snapshot should include material handles");
  EXPECT(snapshot.skeletonHandles.size() == 1,
         "snapshot should include skeleton handles");
  EXPECT(snapshot.objects.size() == 1,
         "snapshot should include one object view");
  if (!snapshot.objects.empty()) {
    const auto &objectView = snapshot.objects.front();
    EXPECT(objectView.meshIndex == meshHandle.index,
           "object view should reference mesh handle index");
    EXPECT(objectView.materialIndex == materialHandle.index,
           "object view should reference material handle index");
    EXPECT(objectView.visible, "object view should be visible by default");
    EXPECT(objectView.worldBounds.isValid(),
           "object view should include world bounds");
  }

  node->setTranslation({2.0f, 0.0f, 0.0f});
  const auto syncedSnapshot = scene->resources().buildSnapshot();
  EXPECT(syncedSnapshot.objects.size() == 1,
         "scene snapshot should keep the object after transform sync");
  if (!syncedSnapshot.objects.empty()) {
    EXPECT(syncedSnapshot.objects.front().worldBounds.getCenter().x > 1.5f,
           "scene snapshot should refresh object world bounds");
  }
  const auto uploadView = scene->resources().buildUploadView();
  EXPECT(uploadView.objects.size() == 1,
         "scene upload view should keep the object after transform sync");
  if (!uploadView.objects.empty()) {
    EXPECT(uploadView.objects.front().objectToWorld[3].x == 2.0f &&
               uploadView.objects.front().objectToWorld[3].y == 0.0f &&
               uploadView.objects.front().objectToWorld[3].z == 0.0f &&
               uploadView.objects.front().objectToWorld[3].w == 1.0f,
           "scene upload objectToWorld should place translation in column 3");
    EXPECT(uploadView.objects.front().worldToObject[3].x == -2.0f &&
               uploadView.objects.front().worldToObject[3].y == 0.0f &&
               uploadView.objects.front().worldToObject[3].z == 0.0f &&
               uploadView.objects.front().worldToObject[3].w == 1.0f,
           "scene upload worldToObject should carry inverse translation");
  }

  scene->removeRenderable(node);
  EXPECT(!meshComponent->get().getGeometryStorageHandle().isValid(),
         "removed node should clear geometry storage handle");
  EXPECT(!meshComponent->get().getMeshHandle().isValid(),
         "removed node should clear mesh handle");
  EXPECT(!meshComponent->get().getObjectHandle().isValid(),
         "removed node should clear object handle");
  EXPECT(!materialComponent->get().getMaterialHandle().isValid(),
         "removed node should clear material handle");
  if (skeletonComponent) {
    EXPECT(!skeletonComponent->get().getSkeletonHandle().isValid(),
           "removed node should clear skeleton handle");
  }
  EXPECT(scene->resources().geometryStorageCount() == 0,
         "removed node should release geometry storage entry");
  EXPECT(scene->resources().meshCount() == 0,
         "removed node should release mesh entry");
  EXPECT(scene->resources().materialCount() == 0,
         "removed node should release material entry");
  EXPECT(scene->resources().objectCount() == 0,
         "removed node should release object entry");
  EXPECT(scene->resources().skeletonCount() == 0,
         "removed node should release skeleton entry");
}

void testSceneRegistersCameraAndLightResources() {
  auto scene = Scene::create("camera_light_resource_table");
  auto cameraNode = SceneNode::create("resource_camera");
  cameraNode->addComponent<CameraComponent>();
  auto cameraBeforeRegister = cameraNode->getComponent<CameraComponent>();
  EXPECT(cameraBeforeRegister.has_value(),
         "camera node should expose camera component before registration");
  if (cameraBeforeRegister.has_value()) {
    cameraBeforeRegister->get().applyProjectionState(CameraType::Orthographic,
                                                     50.0f, 1.25f, 0.2f, 120.0f,
                                                     -5.0f, 5.0f, -4.0f, 4.0f);
  }

  scene->addCamera(cameraNode);

  const auto cameraComponent = cameraNode->getComponent<CameraComponent>();
  EXPECT(cameraComponent.has_value(),
         "camera node should keep camera component");
  const CameraHandle cameraHandle = cameraComponent->get().getCameraHandle();
  EXPECT(cameraHandle.isValid(),
         "camera component should receive a camera handle");
  const auto cameraHandles = scene->getCameraHandles();
  EXPECT(cameraHandles.size() == 1,
         "scene should expose camera handles instead of owning camera data");
  EXPECT(cameraHandles.front() == cameraHandle,
         "scene camera handle list should preserve registered camera handle");
  EXPECT(scene->resources().cameraCount() == 1,
         "scene resource table should own one camera entry");
  EXPECT(scene->resources().resolve(cameraHandle).has_value(),
         "camera handle should resolve through scene resource table");
  if (const auto cameraResource = scene->resources().resolve(cameraHandle);
      cameraResource.has_value()) {
    EXPECT(cameraResource->get().projection.type == CameraType::Orthographic,
           "camera resource should preserve projection type");
    EXPECT(cameraResource->get().projection.left == -5.0f &&
               cameraResource->get().projection.right == 5.0f &&
               cameraResource->get().projection.bottom == -4.0f &&
               cameraResource->get().projection.top == 4.0f,
           "camera resource should preserve orthographic bounds");
    const Vec3f expectedForward{0.0f, 0.0f, -1.0f};
    EXPECT(cameraResource->get().pose.forward == expectedForward,
           "camera resource should preserve camera pose");
  }

  auto light = std::make_shared<DirectionalLight>();
  scene->addLight(light);
  EXPECT(scene->resources().lightCount() == 1,
         "scene resource table should own one light entry");
  const auto lightHandles = scene->getLightHandles();
  EXPECT(lightHandles.size() == 1,
         "scene should expose light handles instead of owning light pointers");
  EXPECT(scene->resources().resolve(lightHandles.front()).has_value(),
         "scene light handle should resolve through scene resource table");

  const auto snapshot = scene->resources().buildSnapshot();
  EXPECT(snapshot.cameraHandles.size() == 1,
         "snapshot should include camera handles");
  EXPECT(snapshot.lightHandles.size() == 1,
         "snapshot should include light handles");

  scene->removeCamera(cameraNode);
  EXPECT(!cameraComponent->get().getCameraHandle().isValid(),
         "removed camera should clear camera handle");
  EXPECT(scene->resources().cameraCount() == 0,
         "removed camera should release camera entry");

  const auto registeredLights = scene->getLights();
  EXPECT(registeredLights.size() == 1,
         "scene should expose table-owned light for removal");
  if (!registeredLights.empty()) {
    scene->removeLight(registeredLights.front());
  }
  EXPECT(scene->resources().lightCount() == 0,
         "removed light should release light entry");
}

void testRealtimeSceneLevelResourcesExposeGpuMaterialTables() {
  auto node = SceneNode::create("realtime_gpu_material_node");
  node->addComponent<MeshComponent>(makeMeshBuffer());
  node->addComponent<MaterialComponent>(makeGpuRecordMaterial());
  auto scene = Scene::create("realtime_gpu_material_resources");
  scene->addRenderable(node);
  scene->resources().beginRenderResourceScope();

  RenderTargetDesc targetDesc;
  targetDesc.role = RenderTargetRole::Swapchain;
  const DescriptorResourceList resources =
      scene->getSceneLevelResources(Pass_Forward, RenderTarget{targetDesc});

  const auto sceneMaterials = std::find_if(
      resources.begin(), resources.end(), [](const DescriptorResourceRef &ref) {
        return ref.getBindingName() == StringID("SceneMaterials");
      });
  EXPECT(sceneMaterials != resources.end() && sceneMaterials->isResource(),
         "realtime scene resources should include SceneMaterials");
  if (sceneMaterials != resources.end() && sceneMaterials->isResource()) {
    EXPECT(sceneMaterials->resource().get().getType() ==
               ResourceType::StorageBuffer,
           "SceneMaterials should be a storage buffer resource");
    EXPECT(sceneMaterials->resource().get().getByteSize() ==
               sizeof(SceneGpuMaterialRecord),
           "SceneMaterials should upload compact material records");
  }

  const auto sceneTextures = std::find_if(
      resources.begin(), resources.end(), [](const DescriptorResourceRef &ref) {
        return ref.getBindingName() == StringID("SceneTextures");
      });
  EXPECT(sceneTextures != resources.end() && sceneTextures->isTextureArray(),
         "realtime scene resources should include SceneTextures");
  if (sceneTextures != resources.end() && sceneTextures->isTextureArray()) {
    EXPECT(sceneTextures->textures().size() == 256,
           "SceneTextures should reserve the fixed 256-slot ABI array");
    EXPECT(std::all_of(sceneTextures->textures().begin(),
                       sceneTextures->textures().end(),
                       [](const TextureSamplerRef &texture) {
                         return texture.isValid();
                       }),
           "SceneTextures should not contain empty descriptor slots");
  }
}

void testRealtimeRenderQueueWritesTypedGpuMaterialIndex() {
  auto first = SceneNode::create("first_material_node");
  first->addComponent<MeshComponent>(makeMeshBuffer());
  first->addComponent<MaterialComponent>(
      makeSceneMaterialsShaderMaterial(Vec4f{0.1f, 0.2f, 0.3f, 1.0f}));

  auto second = SceneNode::create("second_material_node");
  second->addComponent<MeshComponent>(makeMeshBuffer());
  second->addComponent<MaterialComponent>(
      makeSceneMaterialsShaderMaterial(Vec4f{0.8f, 0.7f, 0.6f, 1.0f}));

  auto scene = Scene::create("realtime_material_index_scene", first);
  scene->addRenderable(second);
  scene->resources().beginRenderResourceScope();

  RenderTargetDesc targetDesc;
  targetDesc.role = RenderTargetRole::Swapchain;
  RenderWorkQueue queue;
  queue.build(
      RenderWorkBuildContext::realtime(*scene,
                                       RenderWorkBuildContext::RealtimeOptions{
                                           .visibleMask = VisibilityMask_All,
                                       }),
      Pass_Forward, RenderTarget{targetDesc});

  EXPECT(queue.getItems().size() == 2,
         "queue should contain both material-index test draws");
  std::vector<u32> materialIndices;
  for (const RenderWorkItem &item : queue.getItems()) {
    EXPECT(item.raster.materialIndex != u32_max,
           "queued draw should carry a typed SceneMaterials index");
    materialIndices.push_back(item.raster.materialIndex);
  }
  std::sort(materialIndices.begin(), materialIndices.end());
  EXPECT(materialIndices.size() == 2 && materialIndices[0] == 0 &&
             materialIndices[1] == 1,
         "queued draws should write compact SceneMaterials indices per draw");
}

void testSceneGpuRecordLayoutContract() {
  EXPECT(sizeof(SceneGpuMeshRecord) == 32,
         "SceneGpuMeshRecord std430 contract should expose bindless "
         "attribute stream ranges");
  EXPECT(sizeof(SceneGpuPrimitiveRecord) == 16,
         "SceneGpuPrimitiveRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuObjectRecord) == 176,
         "SceneGpuObjectRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuMaterialRecord) == 96,
         "SceneGpuMaterialRecord std430 contract should stay stable");
  EXPECT(sizeof(SceneGpuFrameParams) == 176,
         "SceneGpuFrameParams std430 contract should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, objectToWorld) == 0,
         "SceneGpuObjectRecord objectToWorld offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, worldToObject) == 64,
         "SceneGpuObjectRecord worldToObject offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, boundsMin) == 128,
         "SceneGpuObjectRecord boundsMin offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, boundsMax) == 144,
         "SceneGpuObjectRecord boundsMax offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, visible) == 160,
         "SceneGpuObjectRecord visible offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, flags) == 164,
         "SceneGpuObjectRecord flags offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, visibilityMask) == 168,
         "SceneGpuObjectRecord visibilityMask offset should stay stable");
  EXPECT(offsetof(SceneGpuObjectRecord, debugId) == 172,
         "SceneGpuObjectRecord debugId offset should stay stable");
}

void testSceneResourceTableDoesNotExportPackedVertexUploadStream() {
  const auto uploadViewPath =
      findProjectFile("src/core/scene/scene_resource_table_upload_view.hpp");
  const auto tableHeaderPath =
      findProjectFile("src/core/scene/scene_resource_table.hpp");
  const auto tableSourcePath =
      findProjectFile("src/core/scene/scene_resource_table.cpp");
  const auto gpuRecordsPath =
      findProjectFile("src/core/scene/scene_gpu_records.hpp");
  EXPECT(!uploadViewPath.empty() && !tableHeaderPath.empty() &&
             !tableSourcePath.empty() && !gpuRecordsPath.empty(),
         "scene resource table source files should be discoverable");
  if (uploadViewPath.empty() || tableHeaderPath.empty() ||
      tableSourcePath.empty() || gpuRecordsPath.empty()) {
    return;
  }

  const std::string uploadViewSource = readTextFile(uploadViewPath);
  const std::string tableHeaderSource = readTextFile(tableHeaderPath);
  const std::string tableSource = readTextFile(tableSourcePath);
  const std::string gpuRecordsSource = readTextFile(gpuRecordsPath);
  EXPECT(uploadViewSource.find("SceneGpuVertexRecord") == std::string::npos,
         "upload view should not expose the legacy packed vertex record");
  EXPECT(uploadViewSource.find("vertices") == std::string::npos,
         "upload view should not expose a legacy packed vertices span");
  EXPECT(tableHeaderSource.find("m_gpuVertices") == std::string::npos,
         "scene resource table should not cache legacy packed vertices");
  EXPECT(tableSource.find("makeGpuVertexRecord") == std::string::npos,
         "scene resource table should not build legacy packed vertex records");
  EXPECT(tableSource.find("m_gpuVertices") == std::string::npos,
         "scene resource table should not populate legacy packed vertices");
  EXPECT(gpuRecordsSource.find("SceneGpuVertexRecord") == std::string::npos,
         "GPU records should not define the legacy packed vertex record");
  EXPECT(gpuRecordsSource.find("uvTangentSign") == std::string::npos,
         "GPU records should not preserve legacy packed uv/tangent fields");
}

void testSceneGpuMaterialRecordCarriesOfflineCullMode() {
  const auto noneMaterial = toGpuMaterialRecord(
      *makeGpuRecordMaterial(Vec4f{1.0f, 1.0f, 1.0f, 1.0f}, CullMode::None));
  EXPECT((noneMaterial.flags & kSceneGpuMaterialCullModeMask) ==
             kSceneGpuMaterialCullModeNone,
         "offline GPU material record should preserve CullMode::None");

  const auto frontMaterial = toGpuMaterialRecord(
      *makeGpuRecordMaterial(Vec4f{1.0f, 1.0f, 1.0f, 1.0f}, CullMode::Front));
  EXPECT((frontMaterial.flags & kSceneGpuMaterialCullModeMask) ==
             kSceneGpuMaterialCullModeFront,
         "offline GPU material record should preserve CullMode::Front");

  const auto backMaterial = toGpuMaterialRecord(
      *makeGpuRecordMaterial(Vec4f{1.0f, 1.0f, 1.0f, 1.0f}, CullMode::Back));
  EXPECT((backMaterial.flags & kSceneGpuMaterialCullModeMask) ==
             kSceneGpuMaterialCullModeBack,
         "offline GPU material record should preserve CullMode::Back");
}

void testSceneGpuMaterialRecordIgnoresLegacyShaderBindingBuffers() {
  const auto record =
      toGpuMaterialRecord(*makeLegacyShaderBindingOnlyMaterial());

  EXPECT(record.baseColor.x == 1.0f && record.baseColor.y == 1.0f &&
             record.baseColor.z == 1.0f && record.baseColor.w == 1.0f,
         "GPU material record should ignore legacy shader-binding baseColor");
  EXPECT(record.pbrParams.z == 0.0f && record.pbrParams.w == 0.0f,
         "GPU material record should ignore legacy shader-binding PBR scalars");
  EXPECT(record.clearcoatParams.x == 0.0f && record.clearcoatParams.y == 0.04f,
         "GPU material record should ignore legacy shader-binding clearcoat "
         "scalars");
  EXPECT(record.emissive.x == 0.0f && record.emissive.y == 0.0f &&
             record.emissive.z == 0.0f && record.emissive.w == 0.0f,
         "GPU material record should ignore legacy shader-binding emissive "
         "values");
}

void testSceneResourceTableUploadViewExportsBindlessGeometryStreams() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeTangentSignMeshBuffer()));
  (void)mesh;

  const auto upload = table.buildUploadView();
  EXPECT(upload.positions.size() == 3,
         "bindless upload view should export a position-only stream");
  EXPECT(upload.attributeStreams.size() == 3,
         "bindless upload view should export normal, uv, and tangent streams "
         "outside the position stream");
  EXPECT(upload.attributeValues.size() == 9,
         "bindless upload view should pack three vec4 values per authored "
         "attribute stream");
  if (upload.positions.empty() || upload.attributeStreams.size() < 3 ||
      upload.attributeValues.size() < 9) {
    return;
  }

  EXPECT(upload.positions[0].x == 0.0f && upload.positions[0].y == 0.0f &&
             upload.positions[0].z == 0.0f && upload.positions[0].w == 1.0f,
         "position stream should carry only position data");
  EXPECT(upload.attributeStreams[0].semantic ==
             kSceneGpuAttributeSemanticNormal0,
         "first attribute stream should describe normal0");
  EXPECT(upload.attributeStreams[0].valueOffset == 0 &&
             upload.attributeStreams[0].valueCount == 3,
         "normal0 stream should cover the three authored vertices");
  EXPECT(upload.attributeStreams[1].semantic == kSceneGpuAttributeSemanticUv0,
         "second attribute stream should describe uv0");
  EXPECT(upload.attributeStreams[1].valueOffset == 3 &&
             upload.attributeStreams[1].valueCount == 3,
         "uv0 stream should cover the three authored vertices");
  EXPECT(upload.attributeStreams[2].semantic ==
             kSceneGpuAttributeSemanticTangent0,
         "third attribute stream should describe tangent0");
  EXPECT(upload.attributeStreams[2].valueOffset == 6 &&
             upload.attributeStreams[2].valueCount == 3,
         "tangent0 stream should cover the three authored vertices");
  EXPECT(upload.meshes.size() == 1,
         "bindless upload view should export one mesh descriptor");
  if (upload.meshes.empty()) {
    return;
  }
  EXPECT(upload.meshes[0].attributeStreamOffset == 0,
         "mesh descriptor should point at its first bindless attribute "
         "stream");
  EXPECT(upload.meshes[0].attributeStreamCount == 3,
         "mesh descriptor should describe how many bindless attribute "
         "streams belong to the mesh");
  EXPECT(upload.attributeValues[3].x == 0.25f &&
             upload.attributeValues[3].y == 0.75f,
         "uv0 values should be stored in bindless attribute data");
  EXPECT(upload.attributeValues[6].w == -1.0f,
         "tangent0 values should preserve the authored handedness");
}

void testDefaultPbrEnvelopeDrivesUploadView() {
  const bool found =
      cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf");
  EXPECT(found, "DamagedHelmet asset root should be discoverable");
  if (!found) {
    return;
  }

  const auto asset = LX_infra::scene_asset::loadGltfSceneAsset(
      "assets/models/damaged_helmet/DamagedHelmet.gltf",
      "assets/materials/pbr.material");
  const auto material = asset.material;
  SceneResourceTable table;
  const auto meshHandle = table.registerMesh(uniqueMesh(asset.mesh));
  const auto materialHandle = table.registerMaterial(uniqueMaterial(material));
  ObjectResource object;
  object.mesh = meshHandle;
  object.material = materialHandle;
  object.worldBounds = BoundingBox{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
  (void)table.registerObject(object);
  const auto registeredMaterial = table.resolve(materialHandle);
  EXPECT(registeredMaterial.has_value(),
         "registered material should resolve from the table");
  if (!registeredMaterial.has_value()) {
    return;
  }
  EXPECT(registeredMaterial->get().getBsdfType() == "uber",
         "default PBR material should register as an uber BSDF");
  EXPECT(registeredMaterial->get().getMaterialEnvelopeCount() >= 5,
         "default PBR material should keep PBRT envelope parameters");
  EXPECT(
      registeredMaterial->get().getMaterialEnvelope(StringID("Kd")).has_value(),
      "default PBR material should expose Kd as envelope truth");
  const auto kdEnvelope =
      registeredMaterial->get().getMaterialEnvelope(StringID("Kd"));
  EXPECT(kdEnvelope.has_value() &&
             kdEnvelope->get().kind == MaterialEnvelopeKind::Texture,
         "DamagedHelmet base color should load as a Kd texture envelope");
  const auto normalEnvelope =
      registeredMaterial->get().getMaterialEnvelope(StringID("normalmap"));
  EXPECT(normalEnvelope.has_value() &&
             normalEnvelope->get().kind == MaterialEnvelopeKind::Texture,
         "DamagedHelmet normal map should load as a normalmap texture "
         "envelope");
  EXPECT(registeredMaterial->get().getShaderBindingBufferCount() == 0,
         "default PBR material should not allocate legacy MaterialUBO storage");
  EXPECT(!registeredMaterial->get()
              .readShaderBindingParameterValue(StringID("MaterialUBO"),
                                               StringID("baseColorFactor"))
              .has_value(),
         "default PBR material should not expose legacy baseColorFactor");
  EXPECT(!registeredMaterial->get()
              .readShaderBindingParameterValue(StringID("MaterialUBO"),
                                               StringID("metallicFactor"))
              .has_value(),
         "default PBR material should not expose legacy metallicFactor");
  EXPECT(!registeredMaterial->get()
              .readShaderBindingParameterValue(StringID("MaterialUBO"),
                                               StringID("roughnessFactor"))
              .has_value(),
         "default PBR material should not expose legacy roughnessFactor");
  EXPECT(!registeredMaterial->get()
              .readShaderBindingParameterValue(StringID("MaterialUBO"),
                                               StringID("ao"))
              .has_value(),
         "default PBR material should not expose legacy AO");
  EXPECT(!registeredMaterial->get()
              .getTextureHandle(StringID("albedoMap"))
              .isValid(),
         "default PBR material should not bind legacy albedoMap");
  EXPECT(!registeredMaterial->get()
              .getTextureHandle(StringID("normalMap"))
              .isValid(),
         "default PBR material should not bind legacy normalMap");
  EXPECT(!registeredMaterial->get()
              .getTextureHandle(StringID("metallicRoughnessMap"))
              .isValid(),
         "default PBR material should not bind legacy metallicRoughnessMap");
  EXPECT(
      !registeredMaterial->get().getTextureHandle(StringID("aoMap")).isValid(),
      "default PBR material should not bind legacy aoMap");
  EXPECT(!registeredMaterial->get()
              .getTextureHandle(StringID("emissiveMap"))
              .isValid(),
         "default PBR material should not bind legacy emissiveMap");

  const auto upload = table.buildUploadView();
  EXPECT(!upload.materials.empty(), "upload view should contain material");
  EXPECT(upload.textures.size() == 2,
         "DamagedHelmet material v2 upload should register Kd and normalmap "
         "texture envelopes");
  if (upload.materials.empty()) {
    return;
  }

  EXPECT(upload.materials[0].baseColorTexture != u32_max,
         "Kd texture envelope should assign base color texture index");
  EXPECT(upload.materials[0].normalTexture != u32_max,
         "normalmap texture envelope should assign normal texture index");
  EXPECT(upload.materials[0].metallicRoughnessTexture == u32_max,
         "material v2 should not synthesize legacy metallicRoughnessMap");
  EXPECT(upload.materials[0].aoTexture == u32_max,
         "material v2 should not synthesize legacy aoMap");
  EXPECT(upload.materials[0].emissiveTexture == u32_max,
         "material v2 should not synthesize legacy emissiveMap");
  EXPECT(upload.materials[0].baseColor.x == 1.0f &&
             upload.materials[0].baseColor.y == 1.0f &&
             upload.materials[0].baseColor.z == 1.0f &&
             upload.materials[0].baseColor.w == 1.0f,
         "DamagedHelmet material v2 Kd envelope should enter the GPU material "
         "record instead of legacy baseColorFactor");
  EXPECT(upload.materials[0].pbrParams.x == 0.0f,
         "DamagedHelmet material v2 uber envelope should not synthesize "
         "legacy metallicFactor");
  EXPECT(upload.materials[0].pbrParams.y == 0.5f,
         "DamagedHelmet material v2 upload should keep the current default "
         "roughness instead of legacy roughnessFactor");
  EXPECT(upload.materials[0].pbrParams.w == 0.0f,
         "DamagedHelmet material v2 upload should not read legacy AO scalar");

  const auto rebuiltUpload = table.buildUploadView();
  EXPECT(rebuiltUpload.textures.size() == 2,
         "rebuilt material v2 upload view should not accumulate stale texture "
         "entries");
}

void testSceneWithoutIblDoesNotCreateDefaultEnvironmentResources() {
  Scene scene("no_ibl_scene");
  const auto resources = scene.resources().getIblEnvironmentResources();
  EXPECT(resources.empty(),
         "scene without configured IBL should not synthesize default "
         "environment descriptor resources");
}

void testSceneResourceTableUploadViewTracksTableGeneration() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  object.visibilityMask = 0x12345678u;
  object.debugOnly = true;
  object.debugId = StringID("scene_gpu_record_object");
  const auto objectHandle = table.registerObject(object);

  const auto firstView = table.buildUploadView();
  EXPECT(firstView.tableGeneration != 0,
         "upload view should expose table mutation generation");
  EXPECT(firstView.meshes.size() == 1, "upload view should expose one mesh");
  EXPECT(firstView.objects.size() == 1, "upload view should expose one object");
  EXPECT(firstView.primitives.size() == 1,
         "upload view should expose one primitive");
  EXPECT(firstView.primitives.front().meshIndex == mesh.index,
         "primitive GPU record should reference compact mesh index");
  EXPECT(firstView.primitives.front().materialIndex == material.index,
         "primitive GPU record should reference compact material index");
  EXPECT(firstView.objects.front().visibilityMask == 0x12345678u,
         "object GPU record should preserve visibility mask");
  EXPECT(firstView.objects.front().flags == 1,
         "object GPU record should preserve flags separately from mask");
  EXPECT(firstView.objects.front().debugId == object.debugId.id,
         "object GPU record should preserve debug id separately from mask");
  EXPECT(firstView.materials.size() == 1,
         "upload view should expose one material");
  EXPECT(firstView.materials.front().baseColor.x == 0.25f &&
             firstView.materials.front().baseColor.y == 0.5f &&
             firstView.materials.front().baseColor.z == 0.75f &&
             firstView.materials.front().baseColor.w == 1.0f,
         "material v2 Kd envelope should reach GPU record");
  EXPECT(firstView.materials.front().pbrParams.z == 0.0f,
         "material v2 record should not read shader-binding specular state");
  EXPECT(firstView.textures.empty(),
         "material without sampler bindings should not upload textures");
  EXPECT(
      firstView.materials.front().baseColorTexture == u32_max &&
          firstView.materials.front().normalTexture == u32_max &&
          firstView.materials.front().metallicRoughnessTexture == u32_max &&
          firstView.materials.front().aoTexture == u32_max &&
          firstView.materials.front().emissiveTexture == u32_max,
      "material without sampler bindings should keep sentinel texture indices");

  object.visible = false;
  table.updateObject(objectHandle, object);
  const auto secondView = table.buildUploadView();
  EXPECT(secondView.tableGeneration > firstView.tableGeneration,
         "object update should advance table mutation generation");
  EXPECT(secondView.objects.front().visible == 0,
         "object visibility should reach GPU record");
}

void testMaterialV2EnvelopeFeedsGpuMaterialRecord() {
  SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, "memory://matte-upload.material", R"(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: rgb, value: [0.2, 0.4, 0.6] }
    sigma: { kind: float, value: 0.15 }
)");
  EXPECT(parsed.instance != nullptr,
         "material v2 parser should produce an instance");
  EXPECT(parsed.diagnostics.empty(),
         "material v2 parser should accept matte envelope input");
  if (!parsed.instance) {
    return;
  }
  const auto material = table.registerMaterial(std::move(parsed.instance));
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  (void)table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(view.materials.size() == 1,
         "material v2 upload view should emit one material record");
  if (view.materials.empty()) {
    return;
  }

  const auto &record = view.materials.front();
  EXPECT(record.baseColor.x == 0.2f && record.baseColor.y == 0.4f &&
             record.baseColor.z == 0.6f && record.baseColor.w == 1.0f,
         "material v2 Kd envelope should feed GPU base color without "
         "MaterialUBO fallback");
  EXPECT(record.pbrParams.x == 0.0f,
         "matte material should not synthesize metallic from legacy PBR state");
  EXPECT(record.pbrParams.y == 0.5f,
         "matte material without roughness envelope should keep the GPU "
         "roughness default");
}

void testMaterialV2TextureEnvelopeFeedsUploadTextureSlots() {
  SceneResourceTable table;
  const auto kdTexture = table.registerTexture(
      ResourceUri("memory://textures/kd.png"), uniqueWhiteSampler(2, 2));
  const auto normalTexture = table.registerTexture(
      ResourceUri("memory://textures/normal.png"), uniqueWhiteSampler(1, 1));

  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, "memory://textured-v2.material", R"(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: texture, valueType: rgb, uri: textures/kd.png }
    normalmap: { kind: texture, valueType: rgb, uri: textures/normal.png }
    sigma: { kind: float, value: 0.0 }
)");
  EXPECT(parsed.instance != nullptr,
         "material v2 parser should produce a textured instance");
  EXPECT(parsed.diagnostics.empty(),
         "material v2 parser should accept Kd and normalmap texture "
         "envelopes");
  if (!parsed.instance) {
    return;
  }

  const auto material = table.registerMaterial(std::move(parsed.instance));
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  (void)table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(view.materials.size() == 1,
         "textured material v2 upload should emit one material record");
  EXPECT(view.textures.size() == 2,
         "textured material v2 upload should export Kd and normalmap "
         "textures");
  if (view.materials.empty()) {
    return;
  }

  const auto &record = view.materials.front();
  EXPECT(record.baseColorTexture != u32_max,
         "Kd texture envelope should feed the baseColorTexture slot");
  EXPECT(record.normalTexture != u32_max,
         "normalmap texture envelope should feed the normalTexture slot");
  EXPECT(record.metallicRoughnessTexture == u32_max,
         "material v2 should not synthesize legacy metallicRoughnessMap");

  const auto hasUploadIndex = [&view](TextureHandle handle, u32 typedIndex) {
    return std::find_if(view.textureIndexByHandle.begin(),
                        view.textureIndexByHandle.end(),
                        [&](const SceneResourceTextureUploadIndex &entry) {
                          return entry.handle == handle &&
                                 entry.typedIndex == typedIndex;
                        }) != view.textureIndexByHandle.end();
  };
  EXPECT(hasUploadIndex(kdTexture, record.baseColorTexture),
         "Kd texture envelope should keep TextureHandle to compact texture "
         "index mapping");
  EXPECT(hasUploadIndex(normalTexture, record.normalTexture),
         "normalmap texture envelope should keep TextureHandle to compact "
         "texture index mapping");
}

void testSceneResourceTableUploadViewIgnoresLegacyTextureBindings() {
  SceneResourceTable table;
  const auto albedo = table.registerTexture(
      ResourceUri("memory://legacy/a.png"), uniqueWhiteSampler(1, 1));
  const auto normal = table.registerTexture(
      ResourceUri("memory://legacy/n.png"), uniqueWhiteSampler(1, 1));
  const auto metallic = table.registerTexture(
      ResourceUri("memory://legacy/mr.png"), uniqueWhiteSampler(1, 1));
  const auto ao = table.registerTexture(ResourceUri("memory://legacy/ao.png"),
                                        uniqueWhiteSampler(1, 1));
  const auto emissive = table.registerTexture(
      ResourceUri("memory://legacy/e.png"), uniqueWhiteSampler(1, 1));

  const auto material = table.registerMaterial(
      uniqueMaterial(makeLegacyTextureBindingOnlyMaterial(
          albedo, normal, metallic, ao, emissive)));
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  (void)table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(view.materials.size() == 1,
         "legacy texture binding material should still emit material record");
  if (view.materials.empty()) {
    return;
  }

  const auto &record = view.materials.front();
  EXPECT(record.baseColorTexture == u32_max,
         "legacy albedoMap binding should not feed baseColorTexture");
  EXPECT(record.normalTexture == u32_max,
         "legacy normalMap binding should not feed normalTexture");
  EXPECT(record.metallicRoughnessTexture == u32_max,
         "legacy metallicRoughnessMap binding should not feed upload record");
  EXPECT(record.aoTexture == u32_max,
         "legacy aoMap binding should not feed upload record");
  EXPECT(record.emissiveTexture == u32_max,
         "legacy emissiveMap binding should not feed upload record");
}

void testSceneResourceTableUploadViewReflectsMaterialMutationAfterBuild() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto materialInstance = makeGpuRecordMaterial();
  const auto material =
      table.registerMaterial(uniqueMaterial(materialInstance));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto firstView = table.buildUploadView();
  auto tableMaterial = table.resolve(material);
  EXPECT(tableMaterial.has_value(),
         "table-owned material should resolve for mutation");
  if (!tableMaterial.has_value()) {
    return;
  }
  MaterialParameterEnvelope kd;
  kd.kind = MaterialEnvelopeKind::Rgb;
  kd.rgbValue = Vec3f{0.9f, 0.8f, 0.7f};
  tableMaterial->get().setMaterialEnvelope(StringID("Kd"), std::move(kd));
  const auto secondView = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep material mutation object alive");
  EXPECT(secondView.tableGeneration == firstView.tableGeneration,
         "table-owned material mutation should not advance table mutation "
         "generation");
  EXPECT(secondView.materials.size() == 1,
         "upload view should keep one material after mutation");
  EXPECT(secondView.materials.front().baseColor.x == 0.9f &&
             secondView.materials.front().baseColor.y == 0.8f &&
             secondView.materials.front().baseColor.z == 0.7f &&
             secondView.materials.front().baseColor.w == 1.0f,
         "upload view should reflect external material envelope mutation");
}

void testSceneResourceTableUploadViewPacksMatrixColumns() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.objectToWorld = Mat4f::translate({2.0f, 3.0f, 4.0f});
  object.worldToObject = Mat4f::translate({-2.0f, -3.0f, -4.0f});
  object.worldBounds = BoundingBox{{2.0f, 3.0f, 4.0f}, {3.0f, 4.0f, 4.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep translated object alive");
  EXPECT(view.objects.size() == 1,
         "upload view should expose translated object");
  if (!view.objects.empty()) {
    EXPECT(view.objects.front().objectToWorld[0].x == 1.0f &&
               view.objects.front().objectToWorld[0].y == 0.0f &&
               view.objects.front().objectToWorld[0].z == 0.0f &&
               view.objects.front().objectToWorld[0].w == 0.0f,
           "objectToWorld first GPU column should contain x basis");
    EXPECT(view.objects.front().objectToWorld[3].x == 2.0f &&
               view.objects.front().objectToWorld[3].y == 3.0f &&
               view.objects.front().objectToWorld[3].z == 4.0f &&
               view.objects.front().objectToWorld[3].w == 1.0f,
           "objectToWorld fourth GPU column should contain translation");
    EXPECT(
        view.objects.front().worldToObject[3].x == -2.0f &&
            view.objects.front().worldToObject[3].y == -3.0f &&
            view.objects.front().worldToObject[3].z == -4.0f &&
            view.objects.front().worldToObject[3].w == 1.0f,
        "worldToObject fourth GPU column should contain inverse translation");
  }
}

void testSceneResourceTableUploadViewUsesCompactRecordIndices() {
  SceneResourceTable table;
  const auto releasedMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto liveMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto releasedMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));
  const auto liveMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource releasedObject;
  releasedObject.mesh = releasedMesh;
  releasedObject.material = releasedMaterial;
  releasedObject.worldBounds =
      BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto releasedObjectHandle = table.registerObject(releasedObject);

  ObjectResource liveObject;
  liveObject.mesh = liveMesh;
  liveObject.material = liveMaterial;
  liveObject.worldBounds = BoundingBox{{2.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.0f}};
  const auto liveObjectHandle = table.registerObject(liveObject);

  table.release(releasedObjectHandle);
  table.release(releasedMesh);
  table.release(releasedMaterial);

  const auto view = table.buildUploadView();
  EXPECT(liveMesh.index == 1, "test setup should leave live mesh in slot 1");
  EXPECT(liveMaterial.index == 1,
         "test setup should leave live material in slot 1");
  EXPECT(liveObjectHandle.index == 1,
         "test setup should leave live object in slot 1");
  EXPECT(view.meshes.size() == 1, "upload view should compact live meshes");
  EXPECT(view.materials.size() == 1,
         "upload view should compact live materials");
  EXPECT(view.objects.size() == 1, "upload view should compact live objects");
  EXPECT(view.primitives.size() == 1,
         "upload view should emit one live primitive");

  EXPECT(view.primitives.front().meshIndex < view.meshes.size(),
         "primitive mesh index should point inside compact mesh span");
  EXPECT(view.primitives.front().meshIndex == 0,
         "primitive mesh index should use compact mesh record position");
  EXPECT(view.primitives.front().materialIndex < view.materials.size(),
         "primitive material index should point inside compact material span");
  EXPECT(
      view.primitives.front().materialIndex == 0,
      "primitive material index should use compact material record position");
  EXPECT(view.primitives.front().objectIndex < view.objects.size(),
         "primitive object index should point inside compact object span");
  EXPECT(view.primitives.front().objectIndex == 0,
         "primitive object index should use compact object record position");
}

void testSceneResourceTableUploadViewExportsHandleToTypedIndexMappings() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));
  const auto camera = table.registerCamera(CameraResource{});
  const auto light = table.registerLight(std::make_unique<DirectionalLight>());
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(!view.meshIndexByHandle.empty(),
         "upload view should expose mesh handle to typed index mapping");
  EXPECT(!view.materialIndexByHandle.empty(),
         "upload view should expose material handle to typed index mapping");
  EXPECT(!view.objectIndexByHandle.empty(),
         "upload view should expose object handle to typed index mapping");
  EXPECT(!view.cameraIndexByHandle.empty(),
         "upload view should expose camera handle to typed index mapping");
  EXPECT(!view.lightIndexByHandle.empty(),
         "upload view should expose light handle to typed index mapping");
  EXPECT(view.cameras.size() == 1,
         "upload view should expose camera typed storage");
  EXPECT(view.lights.size() == 1,
         "upload view should expose light typed storage");

  const auto meshIt =
      std::find_if(view.meshIndexByHandle.begin(), view.meshIndexByHandle.end(),
                   [&](const SceneResourceMeshUploadIndex &entry) {
                     return entry.handle == mesh;
                   });
  EXPECT(meshIt != view.meshIndexByHandle.end() &&
             meshIt->typedIndex < view.meshes.size(),
         "mesh handle should map to a compact mesh record index");

  const auto materialIt = std::find_if(
      view.materialIndexByHandle.begin(), view.materialIndexByHandle.end(),
      [&](const SceneResourceMaterialUploadIndex &entry) {
        return entry.handle == material;
      });
  EXPECT(materialIt != view.materialIndexByHandle.end() &&
             materialIt->typedIndex < view.materials.size(),
         "material handle should map to a compact material record index");

  const auto objectIt = std::find_if(
      view.objectIndexByHandle.begin(), view.objectIndexByHandle.end(),
      [&](const SceneResourceObjectUploadIndex &entry) {
        return entry.handle == objectHandle;
      });
  EXPECT(objectIt != view.objectIndexByHandle.end() &&
             objectIt->typedIndex < view.objects.size(),
         "object handle should map to a compact object record index");

  const auto cameraIt = std::find_if(
      view.cameraIndexByHandle.begin(), view.cameraIndexByHandle.end(),
      [&](const SceneResourceCameraUploadIndex &entry) {
        return entry.handle == camera;
      });
  EXPECT(cameraIt != view.cameraIndexByHandle.end() &&
             cameraIt->typedIndex < view.cameras.size(),
         "camera handle should map to a compact camera typed index");

  const auto lightIt = std::find_if(
      view.lightIndexByHandle.begin(), view.lightIndexByHandle.end(),
      [&](const SceneResourceLightUploadIndex &entry) {
        return entry.handle == light;
      });
  EXPECT(lightIt != view.lightIndexByHandle.end() &&
             lightIt->typedIndex < view.lights.size(),
         "light handle should map to a compact light typed index");
}

void testSceneResourceTableUploadViewSkipsObjectsWithReleasedDependencies() {
  SceneResourceTable table;
  const auto releasedMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto liveMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto releasedMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));
  const auto liveMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource missingMeshObject;
  missingMeshObject.mesh = releasedMesh;
  missingMeshObject.material = liveMaterial;
  missingMeshObject.worldBounds =
      BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto missingMeshObjectHandle = table.registerObject(missingMeshObject);

  ObjectResource missingMaterialObject;
  missingMaterialObject.mesh = liveMesh;
  missingMaterialObject.material = releasedMaterial;
  missingMaterialObject.worldBounds =
      BoundingBox{{2.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.0f}};
  const auto missingMaterialObjectHandle =
      table.registerObject(missingMaterialObject);

  table.release(releasedMesh);
  table.release(releasedMaterial);

  const auto view = table.buildUploadView();
  EXPECT(view.meshes.size() == 1,
         "upload view should still expose independent live mesh records");
  EXPECT(view.materials.empty(),
         "upload view should not expose material records from objects with "
         "released mesh dependencies");
  EXPECT(table.isAlive(missingMeshObjectHandle),
         "test setup should keep object with released mesh alive");
  EXPECT(table.isAlive(missingMaterialObjectHandle),
         "test setup should keep object with released material alive");
  EXPECT(view.objects.empty(),
         "upload view should skip objects whose mesh or material was released");
  EXPECT(
      view.primitives.empty(),
      "upload view should skip primitives whose mesh or material was released");
}

void testSceneResourceTableUploadViewSkipsObjectsWithStaleDependencies() {
  SceneResourceTable table;
  const auto staleMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto staleMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource staleObject;
  staleObject.mesh = staleMesh;
  staleObject.material = staleMaterial;
  staleObject.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto staleObjectHandle = table.registerObject(staleObject);

  table.release(staleMesh);
  table.release(staleMaterial);
  const auto replacementMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto replacementMaterial =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(staleObjectHandle),
         "test setup should keep object with stale dependencies alive");
  EXPECT(replacementMesh.index == staleMesh.index &&
             replacementMesh.generation != staleMesh.generation,
         "test setup should reuse mesh slot with a new generation");
  EXPECT(replacementMaterial.index == staleMaterial.index &&
             replacementMaterial.generation != staleMaterial.generation,
         "test setup should reuse material slot with a new generation");
  EXPECT(view.meshes.size() == 1,
         "upload view should expose replacement live mesh record");
  EXPECT(view.materials.empty(),
         "upload view should not expose material records that no live object "
         "references");
  EXPECT(view.objects.empty(),
         "upload view should skip object with stale mesh/material handles");
  EXPECT(view.primitives.empty(),
         "upload view should skip primitive with stale mesh/material handles");
}

void testSceneResourceTableUploadViewEmitsPrimitivePerTriangle() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(uniqueMesh(makeTwoTriangleMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep two-triangle object alive");
  EXPECT(view.indices.size() == 6,
         "two-triangle mesh should upload six indices");
  EXPECT(view.primitives.size() == 2,
         "two-triangle mesh should emit two primitive records");
  if (view.primitives.size() == 2) {
    EXPECT(view.primitives[0].indexOffset == 0,
           "first primitive should start at first uploaded triangle");
    EXPECT(view.primitives[1].indexOffset == 3,
           "second primitive should start at second uploaded triangle");
    EXPECT(view.primitives[0].meshIndex == 0 &&
               view.primitives[1].meshIndex == 0,
           "primitive mesh indices should use compact mesh record position");
    EXPECT(view.primitives[0].materialIndex == 0 &&
               view.primitives[1].materialIndex == 0,
           "primitive material indices should use compact material record "
           "position");
    EXPECT(
        view.primitives[0].objectIndex == 0 &&
            view.primitives[1].objectIndex == 0,
        "primitive object indices should use compact object record position");
  }
}

void testSceneResourceTableUploadViewPacksGlobalCompactVertexIndices() {
  SceneResourceTable table;
  const auto baseMesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  const auto mesh = table.registerMesh(uniqueMesh(makeOffsetMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));
  (void)baseMesh;

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep offset mesh object alive");
  EXPECT(view.positions.size() == 6,
         "upload view should keep positions from both compact mesh slices");
  EXPECT(view.indices.size() == 6,
         "upload view should keep indices from both compact mesh slices");
  if (view.indices.size() == 6) {
    EXPECT(
        view.indices[3] == 3 && view.indices[4] == 4 && view.indices[5] == 5,
        "offset mesh indices should point directly into compact vertex span");
  }
  if (!view.primitives.empty()) {
    EXPECT(view.primitives[0].indexOffset == 3,
           "offset mesh primitive should point at its global index slice");
  }
}

void testSceneResourceTableUploadViewSkipsInvalidMeshIndexRanges() {
  SceneResourceTable table;
  const auto mesh =
      table.registerMesh(uniqueMesh(makeInvalidIndexRangeMeshBuffer()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep object with invalid mesh slice alive");
  EXPECT(view.meshes.empty(),
         "invalid mesh index range should not emit a mesh record");
  EXPECT(view.positions.empty(),
         "invalid mesh index range should not emit positions");
  EXPECT(view.indices.empty(),
         "invalid mesh index range should not emit indices");
  EXPECT(view.objects.empty(),
         "invalid mesh index range should not emit dependent object records");
  EXPECT(
      view.primitives.empty(),
      "invalid mesh index range should not emit dependent primitive records");
  EXPECT(view.materials.empty(),
         "invalid mesh index range should not emit dependent material records");
}

void testSceneResourceTableUploadViewSkipsUnsupportedMeshTopology() {
  SceneResourceTable table;
  const auto mesh = table.registerMesh(
      uniqueMesh(makeLineListMeshBufferWithTriangleSizedIndexCount()));
  const auto material =
      table.registerMaterial(uniqueMaterial(makeGpuRecordMaterial()));

  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  const auto objectHandle = table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(table.isAlive(objectHandle),
         "test setup should keep object with unsupported topology alive");
  EXPECT(view.meshes.empty(),
         "non-triangle-list mesh should not emit a mesh record");
  EXPECT(view.positions.empty(),
         "non-triangle-list mesh should not emit positions");
  EXPECT(view.indices.empty(),
         "non-triangle-list mesh should not emit indices");
  EXPECT(view.objects.empty(),
         "non-triangle-list mesh should not emit dependent object records");
  EXPECT(view.primitives.empty(),
         "non-triangle-list mesh should not emit primitive records");
  EXPECT(
      view.materials.empty(),
      "unsupported mesh topology should not emit dependent material records");
}

} // namespace

int main() {
  testGeometryStorageAndMeshBufferContract();
  testSceneResourceTableOwnsTypedPayloads();
  testSceneResourceTableDeduplicatesCanonicalUriRegistrations();
  testMeshAndTextureParsersReturnTableOwnedHandles();
  testMeshAndTextureParsersReuseCanonicalTableOwnedResources();
  testMeshAndTextureParsersLoadAssetsIntoTableStorageAndFailMissingAssets();
  testParserFailureUpdatesExistingMetadataIdentity();
  testFailedUriRegistrationDoesNotLeaveReadyMetadata();
  testReleasedSlotReuseDoesNotRetainOldMetadataIdentity();
  testInvalidAndStaleDirtyHandlesAreHarmless();
  testResourceStateVersionAndDirtyPropagation();
  testDirtyPropagationHandlesDependencyCyclesOnce();
  testHandleGenerationInvalidatesStaleMeshHandle();
  testSceneRegistersRenderableComponentResources();
  testSceneRegistersCameraAndLightResources();
  testRealtimeSceneLevelResourcesExposeGpuMaterialTables();
  testRealtimeRenderQueueWritesTypedGpuMaterialIndex();
  testSceneGpuRecordLayoutContract();
  testSceneResourceTableDoesNotExportPackedVertexUploadStream();
  testSceneGpuMaterialRecordCarriesOfflineCullMode();
  testSceneGpuMaterialRecordIgnoresLegacyShaderBindingBuffers();
  testSceneResourceTableUploadViewExportsBindlessGeometryStreams();
  testDefaultPbrEnvelopeDrivesUploadView();
  testSceneWithoutIblDoesNotCreateDefaultEnvironmentResources();
  testSceneResourceTableUploadViewTracksTableGeneration();
  testMaterialV2EnvelopeFeedsGpuMaterialRecord();
  testMaterialV2TextureEnvelopeFeedsUploadTextureSlots();
  testSceneResourceTableUploadViewIgnoresLegacyTextureBindings();
  testSceneResourceTableUploadViewReflectsMaterialMutationAfterBuild();
  testSceneResourceTableUploadViewPacksMatrixColumns();
  testSceneResourceTableUploadViewUsesCompactRecordIndices();
  testSceneResourceTableUploadViewExportsHandleToTypedIndexMappings();
  testSceneResourceTableUploadViewSkipsObjectsWithReleasedDependencies();
  testSceneResourceTableUploadViewSkipsObjectsWithStaleDependencies();
  testSceneResourceTableUploadViewEmitsPrimitivePerTriangle();
  testSceneResourceTableUploadViewPacksGlobalCompactVertexIndices();
  testSceneResourceTableUploadViewSkipsInvalidMeshIndexRanges();
  testSceneResourceTableUploadViewSkipsUnsupportedMeshTopology();

  if (s_failures != 0) {
    std::cerr << "test_scene_resource_table failed: " << s_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_scene_resource_table passed\n";
  return 0;
}
