#include "core/asset/material_contract.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/mesh.hpp"
#include "core/resource/resource_metadata.hpp"
#include "core/asset/render_effect.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

struct TestVertex final {
  Vec3f position;

  static VertexLayout getLayout() {
    return VertexLayout(
        std::vector<VertexLayoutItem>{
            VertexLayoutItem{"position", 0, DataType::Float3,
                             sizeof(Vec3f), 0}},
        sizeof(TestVertex));
  }
};

MeshBufferUniquePtr makeTriangleMesh() {
  auto vertices = std::vector<TestVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2};
  auto vb = VertexBuffer<TestVertex>::create(std::move(vertices));
  auto ib = IndexBuffer::create(std::move(indices));
  return MeshBuffer::create(
             vb, ib,
             BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}})
      ->cloneUnique();
}

MaterialContractReflection makeMaterialContract(const char *uri,
                                                const char *type,
                                                const char *reflectionHash) {
  MaterialContractReflection contract;
  contract.sourceUri = ResourceUri(uri);
  contract.declaredType = type;
  contract.reflectionHash = reflectionHash;
  contract.storageAbiHash = "storage-v1";
  contract.accessorAbiHash = "material-surface-v1";
  return contract;
}

MaterialContractStorageField makeStorageField(const char *name,
                                              const char *parameterName) {
  MaterialContractStorageField field;
  field.name = name;
  field.type = MaterialContractStorageFieldType::Vec4;
  field.inputKind = MaterialContractStorageInputKind::ParameterValue;
  field.parameterName = parameterName;
  field.defaultValue = Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
  return field;
}

MaterialContractStorageField makeTextureSlotField(const char *name,
                                                  const char *parameterName) {
  MaterialContractStorageField field;
  field.name = name;
  field.type = MaterialContractStorageFieldType::TextureSlot;
  field.inputKind = MaterialContractStorageInputKind::ParameterTexture;
  field.parameterName = parameterName;
  field.defaultTextureSemantic = "white";
  return field;
}

MaterialInstanceUniquePtr
makeSourceMaterial(MaterialContractReflection contract) {
  auto material = MaterialInstance::createUnique(
      MaterialTemplate::create(contract.declaredType));
  material->setBsdfType(contract.declaredType);
  material->setMaterialSourceUri(contract.sourceUri);
  material->setMaterialSourceSignature(contract.sourceSignature());
  material->setMaterialSourceReflectionHash(contract.reflectionHash);
  material->setMaterialContractReflection(std::move(contract));
  return material;
}

void registerObject(SceneResourceTable &table, MeshHandle mesh,
                    MaterialHandle material) {
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  (void)table.registerObject(object);
}

void requireSceneRenderableGeometry(RenderPassNode &pass) {
  pass.input.geometry = RenderPathGeometryContract{};
}

bool buildUploadThrows(SceneResourceTable &table,
                       const std::string &expectedText) {
  try {
    (void)table.buildUploadView();
  } catch (const std::logic_error &error) {
    return std::string(error.what()).find(expectedText) != std::string::npos;
  }
  return false;
}

const SceneSourceLocalMaterialStorageView *
findSourceStorage(const SceneResourceTableUploadView &view,
                  StringID sourceSignature) {
  const auto found = std::find_if(
      view.sourceMaterialStorages.begin(), view.sourceMaterialStorages.end(),
      [sourceSignature](const SceneSourceLocalMaterialStorageView &storage) {
        return storage.sourceSignature == sourceSignature;
      });
  return found == view.sourceMaterialStorages.end() ? nullptr : &*found;
}

bool sourceRecordRangeHasContiguousLocalIndices(
    const SceneResourceTableUploadView &view,
    const SceneSourceLocalMaterialStorageView &storage) {
  if (storage.recordOffset + storage.recordCount >
      view.sourceMaterialRecords.size()) {
    return false;
  }
  for (u32 i = 0; i < storage.recordCount; ++i) {
    if (view.sourceMaterialRecords[storage.recordOffset + i]
            .sourceLocalMaterialIndex != i) {
      return false;
    }
  }
  return true;
}

std::vector<ResourceUri> shaderSourceFixture() {
  return {
      ResourceUri("memory://shaders/surface_lit.vert"),
      ResourceUri("memory://shaders/surface_lit.frag"),
  };
}

void testBuiltinDefaultTexturesAreStableSceneResources() {
  SceneResourceTable table;
  const ResourceUri white("builtin://textures/default/white");
  const ResourceUri black("builtin://textures/default/black");
  const ResourceUri flatNormal("builtin://textures/default/flat-normal");

  const auto whiteHandle = table.findTexture(white);
  const auto blackHandle = table.findTexture(black);
  const auto flatNormalHandle = table.findTexture(flatNormal);
  EXPECT(whiteHandle.has_value(),
         "white default texture should have stable resource identity");
  EXPECT(blackHandle.has_value(),
         "black default texture should have stable resource identity");
  EXPECT(flatNormalHandle.has_value(),
         "flat normal default texture should have stable resource identity");
  EXPECT(table.textureCount() == 3,
         "new table should register exactly the three builtin default "
         "textures");

  const SceneResourceTableUploadView firstView = table.buildUploadView();
  EXPECT(firstView.textures.size() == 3,
         "default textures should enter upload texture table");

  const SceneResourceTableUploadView secondView = table.buildUploadView();
  EXPECT(secondView.textures.size() == 3,
         "rebuilding upload view should not duplicate default texture slots");
  EXPECT(table.textureCount() == 3,
         "rebuilding upload view should not duplicate default resources");
}

RenderFeature makeEnvironmentLightingFeature(ResourceUri uri) {
  RenderFeature feature;
  feature.name = "EnvironmentLighting";
  feature.feature = "environmentLighting";
  feature.parameters["environmentMap"] = RenderFeatureParameter{
      .kind = "textureCube",
      .uri = std::move(uri),
      .valueType = "linear-radiance",
      .binding = "SkyboxMap",
      .required = true,
  };
  feature.parameters["color"] = RenderFeatureParameter{
      .kind = "vec3",
      .value = "[0.08, 0.08, 0.10]",
      .binding = "EnvironmentLightingUBO",
      .member = "color",
      .required = true,
  };
  feature.parameters["intensity"] = RenderFeatureParameter{
      .kind = "float",
      .value = "1.0",
      .binding = "EnvironmentLightingUBO",
      .member = "intensity",
      .required = true,
  };
  feature.parameters["rotation"] = RenderFeatureParameter{
      .kind = "float",
      .value = "0.0",
      .binding = "EnvironmentLightingUBO",
      .member = "rotation",
      .required = true,
  };
  return feature;
}

void testEnvironmentFeatureBuiltinWhiteCubeRegistersLiveSkyboxMap() {
  SceneResourceTable table;
  const RenderFeatureHandle handle = table.registerRenderFeature(
      ResourceUri("memory://features/environment_lighting.render-feature"),
      makeEnvironmentLightingFeature(ResourceUri("builtin:env/white_cube")));
  EXPECT(handle.isValid(), "environment feature should register");

  const auto resources = table.getEnvironmentLightingResources();
  const auto hasBinding = [&](StringID bindingName) {
    return std::any_of(resources.begin(), resources.end(),
                       [&](const GpuResourceRef &resource) {
                         return resource.isValid() &&
                                resource.getBindingName() == bindingName;
                       });
  };
  EXPECT(hasBinding(StringID("SkyboxMap")),
         "builtin white cube should register live SkyboxMap");
  EXPECT(hasBinding(StringID("EnvironmentLightingUBO")),
         "environment feature should register EnvironmentLightingUBO");
  EXPECT(!hasBinding(StringID("EnvironmentLightingFiniteBoxUBO")),
         "environment feature should not register retired finite box bounds "
         "UBO");
}

void testEnvironmentFeatureMissingUriDoesNotRegisterSkyboxMap() {
  SceneResourceTable table;
  RenderFeature feature = makeEnvironmentLightingFeature(ResourceUri{});
  const RenderFeatureHandle handle = table.registerRenderFeature(
      ResourceUri("memory://features/environment_lighting_missing_uri"),
      std::move(feature));
  EXPECT(handle.isValid(), "environment feature payload should register");
  const auto resources = table.getEnvironmentLightingResources();
  EXPECT(resources.empty(),
         "missing environmentMap.uri must not create default SkyboxMap");
}

class TestShader final : public IShader {
public:
  TestShader() {
    m_stages.push_back(
        ShaderStageCode{ShaderStage::Vertex, std::vector<u32>{0x07230203u}});
    m_bindings.push_back(ShaderResourceBinding{
        .name = "CameraUBO",
        .set = 0,
        .binding = 0,
        .type = ShaderPropertyType::UniformBuffer,
        .size = 64,
        .stageFlags = ShaderStage::Vertex,
    });
  }

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.set == set && candidate.binding == binding) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.name == name) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  usize getProgramHash() const override { return 1; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

IShaderSharedPtr shaderPayloadFixture() {
  return std::make_shared<TestShader>();
}

void testPackageReadyGraphExport() {
  SceneResourceTable table;
  ResourceMetadata material;
  material.type = SceneResourceType::Material;
  material.uri = ResourceUri("assets/materials/paint.material");
  material.contentHash = "material-hash";
  material.dependencies.push_back(ResourceUri("assets/textures/paint.png"));

  ResourceMetadata texture;
  texture.type = SceneResourceType::Texture;
  texture.uri = ResourceUri("assets/textures/paint.png");
  texture.contentHash = "texture-hash";

  const auto materialHandle = table.internResourceMetadata(material);
  const auto textureHandle = table.internResourceMetadata(texture);

  const auto graph = table.exportResourceGraph();
  EXPECT(graph.resources.size() >= 2,
         "graph should export material and texture resources");
  EXPECT(graph.handleToIndex(materialHandle) != u32_max,
         "graph should map material handle to index");
  EXPECT(graph.handleToIndex(textureHandle) != u32_max,
         "graph should map texture handle to index");
  EXPECT(!graph.resources.empty() &&
             graph.resources[graph.handleToIndex(materialHandle)]
                     .dependencies.size() == 1,
         "graph should preserve material-to-texture dependency edge");
}

void testOverrideIdentityUsesStableHash() {
  SceneResourceTable table;
  const ResourceIdentityHandle base = table.internMaterialInstanceIdentity(
      ResourceUri("assets/materials/base.material"), "");
  const ResourceIdentityHandle firstOverride = table.internMaterialInstanceIdentity(
      ResourceUri("assets/materials/base.material"), "override-a");
  const ResourceIdentityHandle sameOverride = table.internMaterialInstanceIdentity(
      ResourceUri("assets/materials/base.material"), "override-a");
  const ResourceIdentityHandle otherOverride = table.internMaterialInstanceIdentity(
      ResourceUri("assets/materials/base.material"), "override-b");

  EXPECT(base.isValid(), "base material identity should be valid");
  EXPECT(!(base == firstOverride),
         "override material identity should differ from base");
  EXPECT(firstOverride == sameOverride,
         "same override hash should reuse material identity");
  EXPECT(!(firstOverride == otherOverride),
         "different override hash should split material identity");
}

void testRenderPathGraphResourceGraphExportsFeatureAndShaderDependencies() {
  SceneResourceTable table;
  const ResourceIdentityHandle renderer = table.loadOrGetResource(
      SceneResourceType::Renderer, ResourceUri("memory://renderer/default"));
  const ResourceIdentityHandle camera = table.loadOrGetResource(
      SceneResourceType::Camera, ResourceUri("memory://camera/main"));

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"),
      shaderSourceFixture(), shaderPayloadFixture());

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle =
      table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                    std::move(graph));
  table.addDependency(renderer, graphHandle);
  table.addDependency(camera, graphHandle);

  const auto exported = table.exportResourceGraph();
  const u32 rendererIndex = exported.handleToIndex(renderer);
  const u32 cameraIndex = exported.handleToIndex(camera);
  const u32 graphIndex =
      exported.handleToIndex(table.metadataHandle(graphHandle));
  EXPECT(rendererIndex != u32_max, "renderer resource should export");
  EXPECT(cameraIndex != u32_max, "camera resource should export");
  EXPECT(graphIndex != u32_max, "render path graph resource should export");
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  const auto dependsOn = [&](u32 ownerIndex, u32 dependencyIndex) {
    if (ownerIndex == u32_max || dependencyIndex == u32_max) {
      return false;
    }
    const auto &dependencies =
        exported.resources[ownerIndex].dependencyHandles;
    return std::find(dependencies.begin(), dependencies.end(),
                     exported.handles[dependencyIndex]) != dependencies.end();
  };

  EXPECT(dependsOn(rendererIndex, graphIndex),
         "renderer should depend on RenderPathGraph");
  EXPECT(dependsOn(cameraIndex, graphIndex),
         "camera should depend on RenderPathGraph");
  EXPECT(exported.resources[graphIndex].dependencyHandles.size() == 2,
         "RenderPathGraph should depend on feature and shader metadata");
}

void testUploadViewExportsRenderPathGraphPassFeatureAndShaderIndices() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"),
      shaderSourceFixture(), shaderPayloadFixture());
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle =
      table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                    std::move(graph));

  const SceneResourceTableUploadView view = table.buildUploadView();
  const auto graphIt = std::find_if(
      view.renderPathGraphIndexByHandle.begin(),
      view.renderPathGraphIndexByHandle.end(),
      [&](const SceneResourceRenderPathGraphUploadIndex &entry) {
        return entry.handle == graphHandle;
      });
  EXPECT(graphIt != view.renderPathGraphIndexByHandle.end(),
         "upload view should map RenderPathGraphHandle to typed graph index");
  EXPECT(graphIt != view.renderPathGraphIndexByHandle.end() &&
             graphIt->typedIndex < view.renderPathGraphs.size(),
         "RenderPathGraphHandle should map inside graph typed span");
  if (graphIt == view.renderPathGraphIndexByHandle.end() ||
      graphIt->typedIndex >= view.renderPathGraphs.size()) {
    return;
  }

  const SceneGpuRenderPathGraphRecord &graphRecord =
      view.renderPathGraphs[graphIt->typedIndex];
  EXPECT(graphRecord.passOffset < view.renderPathGraphPasses.size(),
         "graph record should point at pass record range");
  EXPECT(graphRecord.passCount == 1,
         "graph record should export one pass record");
  EXPECT(graphRecord.featureOffset < view.renderPathGraphFeatures.size(),
         "graph record should point at feature record range");
  EXPECT(graphRecord.featureCount == 1,
         "graph record should export one feature record");

  const SceneGpuRenderPathGraphPassRecord &passRecord =
      view.renderPathGraphPasses[graphRecord.passOffset];
  EXPECT(passRecord.shaderIndex < view.renderPathGraphShaders.size(),
         "pass record should point at shader metadata index");
}

void testUploadViewGroupsSourceLocalMaterialsWithSameSignature() {
  SceneResourceTable table;
  const MeshHandle firstMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle secondMesh = table.registerMesh(makeTriangleMesh());
  const MaterialContractReflection contract = makeMaterialContract(
      "memory://materials/matte.contract.glsl", "matte", "matte-reflect-v1");
  const StringID sourceSignature = contract.sourceSignature();
  const MaterialHandle firstMaterial =
      table.registerMaterial(makeSourceMaterial(contract));
  const MaterialHandle secondMaterial =
      table.registerMaterial(makeSourceMaterial(contract));
  registerObject(table, firstMesh, firstMaterial);
  registerObject(table, secondMesh, secondMaterial);

  const SceneResourceTableUploadView view = table.buildUploadView();
  EXPECT(view.materials.empty(),
         "source-contract materials should not create legacy material records");
  EXPECT(view.materialRefs.size() == 2,
         "draws should reference source-local material refs");
  EXPECT(view.sourceMaterialStorages.size() == 1,
         "same source signature should produce one source-local storage");
  const SceneSourceLocalMaterialStorageView *storage =
      findSourceStorage(view, sourceSignature);
  EXPECT(storage != nullptr,
         "source-local storage should be keyed by source signature");
  EXPECT(storage != nullptr && storage->recordOffset == 0,
         "same source storage should start at first source-local record");
  EXPECT(storage != nullptr && storage->recordCount == 2,
         "same source storage should cover both material records");
  EXPECT(view.sourceMaterialRecords.size() == 2,
         "source-local record span should contain both material records");
  EXPECT(view.draws.size() == 2 &&
             view.draws[0].materialIndex == u32_max &&
             view.draws[1].materialIndex == u32_max,
         "source-contract draws should not point at legacy material records");
  EXPECT(view.draws.size() == 2 &&
             view.materialRefs[view.draws[0].materialRefIndex]
                     .sourceStorageIndex == 0 &&
             view.materialRefs[view.draws[0].materialRefIndex]
                     .sourceLocalMaterialIndex == 0 &&
             view.materialRefs[view.draws[1].materialRefIndex]
                     .sourceStorageIndex == 0 &&
             view.materialRefs[view.draws[1].materialRefIndex]
                     .sourceLocalMaterialIndex == 1,
         "draw material refs should resolve to source storage and local index");
  EXPECT(storage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *storage),
         "same source storage range should contain contiguous source-local "
         "indices");
}

void testUploadViewSplitsSourceLocalMaterialsBySignature() {
  SceneResourceTable table;
  const MeshHandle firstMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle secondMesh = table.registerMesh(makeTriangleMesh());
  const MaterialContractReflection matte = makeMaterialContract(
      "memory://materials/matte.contract.glsl", "matte", "matte-reflect-v1");
  const MaterialContractReflection metal = makeMaterialContract(
      "memory://materials/metal.contract.glsl", "metal", "metal-reflect-v1");
  const StringID matteSignature = matte.sourceSignature();
  const StringID metalSignature = metal.sourceSignature();
  const MaterialHandle firstMaterial =
      table.registerMaterial(makeSourceMaterial(matte));
  const MaterialHandle secondMaterial =
      table.registerMaterial(makeSourceMaterial(metal));
  registerObject(table, firstMesh, firstMaterial);
  registerObject(table, secondMesh, secondMaterial);

  const SceneResourceTableUploadView view = table.buildUploadView();
  EXPECT(view.materials.empty(),
         "source-contract materials should not create legacy material records");
  EXPECT(view.materialRefs.size() == 2,
         "source-contract materials should create material refs");
  EXPECT(view.sourceMaterialStorages.size() == 2,
         "different source signatures should produce separate storages");

  const SceneSourceLocalMaterialStorageView *matteStorage =
      findSourceStorage(view, matteSignature);
  const SceneSourceLocalMaterialStorageView *metalStorage =
      findSourceStorage(view, metalSignature);
  EXPECT(matteStorage != nullptr,
         "matte source-local storage should be present");
  EXPECT(metalStorage != nullptr,
         "metal source-local storage should be present");
  EXPECT(matteStorage != nullptr && matteStorage->recordOffset == 0 &&
             matteStorage->recordCount == 1,
         "first source storage should cover the first material record");
  EXPECT(metalStorage != nullptr && metalStorage->recordOffset == 1 &&
             metalStorage->recordCount == 1,
         "second source storage should cover the second material record");
  EXPECT(view.sourceMaterialRecords.size() == 2,
         "source-local record span should contain both source records");
  EXPECT(view.draws.size() == 2 &&
             view.materialRefs[view.draws[0].materialRefIndex]
                     .sourceStorageIndex !=
                 view.materialRefs[view.draws[1].materialRefIndex]
                     .sourceStorageIndex,
         "draw material refs should split different source storages");
  EXPECT(matteStorage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *matteStorage),
         "matte storage range should contain contiguous source-local indices");
  EXPECT(metalStorage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *metalStorage),
         "metal storage range should contain contiguous source-local indices");
}

void testUploadViewSourceLocalMaterialRangesAreNotLegacyInterleaved() {
  SceneResourceTable table;
  const MeshHandle firstMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle secondMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle thirdMesh = table.registerMesh(makeTriangleMesh());
  const MaterialContractReflection matte = makeMaterialContract(
      "memory://materials/matte.contract.glsl", "matte", "matte-reflect-v1");
  const MaterialContractReflection metal = makeMaterialContract(
      "memory://materials/metal.contract.glsl", "metal", "metal-reflect-v1");
  const StringID matteSignature = matte.sourceSignature();
  const StringID metalSignature = metal.sourceSignature();
  const MaterialHandle firstMaterial =
      table.registerMaterial(makeSourceMaterial(matte));
  const MaterialHandle secondMaterial =
      table.registerMaterial(makeSourceMaterial(metal));
  const MaterialHandle thirdMaterial =
      table.registerMaterial(makeSourceMaterial(matte));
  registerObject(table, firstMesh, firstMaterial);
  registerObject(table, secondMesh, secondMaterial);
  registerObject(table, thirdMesh, thirdMaterial);

  const SceneResourceTableUploadView view = table.buildUploadView();
  EXPECT(view.materials.empty(),
         "source-contract materials should not create legacy material records");
  EXPECT(view.materialRefs.size() == 3,
         "source-contract draws should preserve one material ref per draw");
  EXPECT(view.draws.size() == 3,
         "legacy draw span should preserve all draws");
  EXPECT(view.draws.size() == 3 && view.draws[0].materialIndex == u32_max &&
             view.draws[1].materialIndex == u32_max &&
             view.draws[2].materialIndex == u32_max,
         "source-contract draw material indices should not use legacy path");
  EXPECT(view.sourceMaterialStorages.size() == 2,
         "source-local storages should still group by source signature");
  EXPECT(view.sourceMaterialRecords.size() == 3,
         "source-local record span should contain one record per uploaded "
         "material");
  EXPECT(view.draws.size() == 3 &&
             view.materialRefs[view.draws[0].materialRefIndex]
                     .sourceLocalMaterialIndex == 0 &&
             view.materialRefs[view.draws[1].materialRefIndex]
                     .sourceLocalMaterialIndex == 0 &&
             view.materialRefs[view.draws[2].materialRefIndex]
                     .sourceLocalMaterialIndex == 1,
         "source-local indices should be assigned inside each source storage");

  const SceneSourceLocalMaterialStorageView *matteStorage =
      findSourceStorage(view, matteSignature);
  const SceneSourceLocalMaterialStorageView *metalStorage =
      findSourceStorage(view, metalSignature);
  EXPECT(matteStorage != nullptr,
         "matte source-local storage should be present");
  EXPECT(metalStorage != nullptr,
         "metal source-local storage should be present");
  EXPECT(matteStorage != nullptr && matteStorage->recordOffset == 0 &&
             matteStorage->recordCount == 2,
         "matte source-local records should be contiguous despite A,B,A "
         "legacy order");
  EXPECT(metalStorage != nullptr && metalStorage->recordOffset == 2 &&
             metalStorage->recordCount == 1,
         "metal source-local record range should follow first-seen source "
         "ordering");
  EXPECT(matteStorage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *matteStorage),
         "matte source-local range should contain contiguous local indices");
  EXPECT(metalStorage != nullptr &&
             sourceRecordRangeHasContiguousLocalIndices(view, *metalStorage),
         "metal source-local range should contain contiguous local indices");
}

void testUploadViewRejectsSourceSignatureStorageLayoutConflict() {
  SceneResourceTable table;
  const MeshHandle firstMesh = table.registerMesh(makeTriangleMesh());
  const MeshHandle secondMesh = table.registerMesh(makeTriangleMesh());

  MaterialContractReflection first = makeMaterialContract(
      "memory://materials/conflict.contract.glsl", "matte",
      "shared-reflect-v1");
  first.storageFields.push_back(makeStorageField("baseColor", "Kd"));

  MaterialContractReflection second = first;
  second.storageFields.clear();
  second.storageFields.push_back(makeStorageField("albedo", "Kd"));

  const MaterialHandle firstMaterial =
      table.registerMaterial(makeSourceMaterial(first));
  const MaterialHandle secondMaterial =
      table.registerMaterial(makeSourceMaterial(second));
  registerObject(table, firstMesh, firstMaterial);
  registerObject(table, secondMesh, secondMaterial);

  EXPECT(buildUploadThrows(table, "source signature conflict"),
         "same source signature with different storage layout should fail "
         "before upload records are produced");
}

void testUploadViewRejectsMaterialSourceSignatureMismatch() {
  SceneResourceTable table;
  const MeshHandle mesh = table.registerMesh(makeTriangleMesh());
  MaterialContractReflection contract = makeMaterialContract(
      "memory://materials/signature.contract.glsl", "matte",
      "signature-reflect-v1");
  contract.storageFields.push_back(makeStorageField("baseColor", "Kd"));

  auto material = makeSourceMaterial(contract);
  material->setMaterialSourceSignature(StringID("corrupt-source-signature"));
  const MaterialHandle materialHandle =
      table.registerMaterial(std::move(material));
  registerObject(table, mesh, materialHandle);

  EXPECT(buildUploadThrows(table, "source signature mismatch"),
         "source-contract material with mismatched instance signature should "
         "fail as an invariant violation");
}

void testUploadViewRejectsUnresolvedExplicitTextureSlot() {
  SceneResourceTable table;
  const MeshHandle mesh = table.registerMesh(makeTriangleMesh());
  MaterialContractReflection contract = makeMaterialContract(
      "memory://materials/textured.contract.glsl", "matte",
      "textured-reflect-v1");
  contract.parameters.push_back(MaterialContractParameter{
      "Kd", true, {MaterialContractParameterKind::Texture}});
  contract.storageFields.push_back(makeTextureSlotField("baseColorTexture",
                                                        "Kd"));

  auto material = makeSourceMaterial(contract);
  MaterialParameterEnvelope kdTexture;
  kdTexture.kind = MaterialEnvelopeKind::Texture;
  kdTexture.valueType = MaterialEnvelopeValueType::Rgb;
  kdTexture.uri = "memory://textures/missing-kd.png";
  material->setMaterialEnvelope(StringID("Kd"), std::move(kdTexture));

  const MaterialHandle materialHandle =
      table.registerMaterial(std::move(material));
  registerObject(table, mesh, materialHandle);

  EXPECT(buildUploadThrows(table, "unresolved texture slot"),
         "explicit texture parameters must fail if no table texture slot can "
         "be resolved");
}

void testRenderPathGraphRegistrationRejectsMissingFeatureResource() {
  SceneResourceTable table;
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"),
      shaderSourceFixture(), shaderPayloadFixture());
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/missing.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  bool rejected = false;
  try {
    const RenderPathGraphHandle graphHandle =
        table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                      std::move(graph));
    (void)graphHandle;
  } catch (const std::invalid_argument &error) {
    rejected = std::string(error.what()).find("missing RenderFeature") !=
               std::string::npos;
  }
  EXPECT(rejected,
         "RenderPathGraph registration must fail when a feature URI has no "
         "registered RenderFeature payload");
  EXPECT(table.renderFeatureCount() == 0,
         "missing feature dependency must not create a placeholder feature");
}

void testRenderPathGraphRegistrationRejectsSceneInputWithoutGeometry() {
  SceneResourceTable table;
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"),
      shaderSourceFixture(), shaderPayloadFixture());
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "InvalidInputContract";
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  graph.passes.push_back(pass);

  bool rejected = false;
  try {
    [[maybe_unused]] const RenderPathGraphHandle graphHandle =
        table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                      std::move(graph));
  } catch (const std::invalid_argument &error) {
    rejected =
        std::string(error.what()).find("input.geometry") != std::string::npos;
  }
  EXPECT(rejected,
         "RenderPathGraph registration should reject scene-renderables input "
         "without geometry");
}

void testFailedShaderMetadataDoesNotSatisfyRenderPathGraphDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));

  ResourceMetadata failedShader;
  failedShader.type = SceneResourceType::Shader;
  failedShader.uri = ResourceUri("memory://shaders/missing.shader");
  failedShader.state = ResourceState::Failed;
  failedShader.diagnostics.push_back(ResourceDiagnostic{
      .ownerUri = ResourceUri("memory://graphs/forward"),
      .resourceUri = failedShader.uri,
      .parserName = "test",
      .message = "shader source resolution failed",
  });
  const ResourceIdentityHandle failedShaderIdentity =
      table.internResourceMetadata(std::move(failedShader));

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/missing.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  bool rejected = false;
  try {
    const RenderPathGraphHandle graphHandle =
        table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                      std::move(graph));
    (void)graphHandle;
  } catch (const std::invalid_argument &error) {
    rejected =
        std::string(error.what()).find("missing Shader") != std::string::npos;
  }

  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(failedShaderIdentity.isValid(),
         "failed shader metadata should be interned for diagnostics");
  EXPECT(table.shaderCount() == 0,
         "failed shader metadata must not create a typed shader descriptor");
  EXPECT(rejected,
         "failed metadata-only shader must not satisfy a graph dependency");
}

void testSourceResolvedShaderWithoutPayloadDoesNotSatisfyGraphDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/source_only.shader"),
      shaderSourceFixture(), nullptr);

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/source_only.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  bool rejected = false;
  try {
    const RenderPathGraphHandle graphHandle =
        table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                      std::move(graph));
    (void)graphHandle;
  } catch (const std::invalid_argument &error) {
    const std::string message = error.what();
    rejected = message.find("memory://graphs/forward") != std::string::npos &&
               message.find("memory://shaders/source_only.shader") !=
                   std::string::npos &&
               message.find("compiled/reflected payload") !=
                   std::string::npos;
  }

  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(),
         "source-resolved shader metadata fixture should be registered");
  EXPECT(rejected,
         "source-resolved shader descriptors without a live typed payload must "
         "not satisfy a RenderPathGraph dependency");
}

void testUploadViewRejectsSourceResolvedShaderWithoutPayload() {
  SceneResourceTable table;
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/source_only.shader"),
      shaderSourceFixture(), nullptr);

  bool rejected = false;
  try {
    (void)table.buildUploadView();
  } catch (const std::logic_error &error) {
    const std::string message = error.what();
    rejected = message.find("memory://shaders/source_only.shader") !=
                   std::string::npos &&
               message.find("compiled/reflected payload") !=
                   std::string::npos;
  }

  EXPECT(shaderHandle.isValid(),
         "source-resolved shader metadata fixture should be registered");
  EXPECT(rejected,
         "upload view must reject source-resolved shaders without a live typed "
         "payload/reflection");
}

void testUploadViewRejectsReleasedRenderPathGraphFeatureDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"),
      shaderSourceFixture(), shaderPayloadFixture());
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle =
      table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                    std::move(graph));
  table.release(featureHandle);

  bool rejected = false;
  try {
    (void)table.buildUploadView();
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("missing RenderFeature") !=
               std::string::npos;
  }
  EXPECT(graphHandle.isValid(),
         "graph fixture should be registered before release");
  EXPECT(rejected,
         "upload view must fail when RenderPathGraph feature handle mapping is "
         "missing instead of exporting u32_max");
}

void testUploadViewRejectsReleasedRenderPathGraphShaderDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"),
      shaderSourceFixture(), shaderPayloadFixture());
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle =
      table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                    std::move(graph));
  table.release(shaderHandle);

  bool rejected = false;
  try {
    (void)table.buildUploadView();
  } catch (const std::logic_error &error) {
    rejected =
        std::string(error.what()).find("missing Shader") != std::string::npos;
  }
  EXPECT(graphHandle.isValid(),
         "graph fixture should be registered before shader release");
  EXPECT(rejected,
         "upload view must fail when RenderPathGraph shader handle mapping is "
         "missing instead of exporting u32_max");
}

void testExportRejectsReleasedRenderPathGraphDependencies() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"),
      shaderSourceFixture(), shaderPayloadFixture());

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle =
      table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                    std::move(graph));
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");
  EXPECT(graphHandle.isValid(), "graph fixture should be registered");

  table.release(featureHandle);

  bool rejected = false;
  try {
    (void)table.exportResourceGraph();
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("released RenderFeature") !=
               std::string::npos;
  }
  EXPECT(rejected,
         "resource graph export must fail when a graph dependency was "
         "released instead of exporting a stale dependency handle");
}

void testExportRejectsFailedRenderPathGraphShaderDependency() {
  SceneResourceTable table;

  RenderFeature feature;
  feature.name = "Shadow";
  feature.feature = "shadowmap";
  const RenderFeatureHandle featureHandle = table.registerRenderFeature(
      ResourceUri("memory://features/shadow.render-feature"),
      std::move(feature));
  const ShaderHandle shaderHandle = table.registerShaderResource(
      ResourceUri("memory://shaders/surface_lit.shader"),
      shaderSourceFixture(), shaderPayloadFixture());

  RenderPathGraph graph;
  graph.name = "Forward";
  graph.features.push_back(RenderPathFeatureDependency{
      .slot = "shadow",
      .uri = ResourceUri("memory://features/shadow.render-feature")});
  RenderPassNode pass;
  pass.id = "ForwardOpaque";
  pass.shaderUri = ResourceUri("memory://shaders/surface_lit.shader");
  pass.sources.push_back("SceneColor");
  pass.targets.push_back("SceneColor");
  requireSceneRenderableGeometry(pass);
  graph.passes.push_back(pass);

  const RenderPathGraphHandle graphHandle =
      table.registerRenderPathGraph(ResourceUri("memory://graphs/forward"),
                                    std::move(graph));
  EXPECT(featureHandle.isValid(), "feature fixture should be registered");
  EXPECT(shaderHandle.isValid(), "shader fixture should be registered");
  EXPECT(graphHandle.isValid(), "graph fixture should be registered");

  ResourceMetadata failedShader;
  failedShader.type = SceneResourceType::Shader;
  failedShader.uri = ResourceUri("memory://shaders/surface_lit.shader");
  failedShader.state = ResourceState::Failed;
  failedShader.diagnostics.push_back(ResourceDiagnostic{
      .ownerUri = ResourceUri("memory://graphs/forward"),
      .resourceUri = failedShader.uri,
      .parserName = "test",
      .message = "shader compile failed",
  });
  const ResourceIdentityHandle failedShaderIdentity =
      table.internResourceMetadata(std::move(failedShader));
  EXPECT(failedShaderIdentity.isValid(),
         "failed shader metadata fixture should be interned");

  bool rejected = false;
  try {
    (void)table.exportResourceGraph();
  } catch (const std::logic_error &error) {
    rejected = std::string(error.what()).find("Shader") != std::string::npos &&
               std::string(error.what()).find("non-uploadable state") !=
                   std::string::npos;
  }
  EXPECT(rejected,
         "resource graph export must fail when a graph shader dependency is "
         "failed instead of exporting stale ready metadata");
}

} // namespace

int main() {
  testPackageReadyGraphExport();
  testOverrideIdentityUsesStableHash();
  testRenderPathGraphResourceGraphExportsFeatureAndShaderDependencies();
  testUploadViewExportsRenderPathGraphPassFeatureAndShaderIndices();
  testBuiltinDefaultTexturesAreStableSceneResources();
  testEnvironmentFeatureBuiltinWhiteCubeRegistersLiveSkyboxMap();
  testEnvironmentFeatureMissingUriDoesNotRegisterSkyboxMap();
  testUploadViewGroupsSourceLocalMaterialsWithSameSignature();
  testUploadViewSplitsSourceLocalMaterialsBySignature();
  testUploadViewSourceLocalMaterialRangesAreNotLegacyInterleaved();
  testUploadViewRejectsSourceSignatureStorageLayoutConflict();
  testUploadViewRejectsMaterialSourceSignatureMismatch();
  testUploadViewRejectsUnresolvedExplicitTextureSlot();
  testRenderPathGraphRegistrationRejectsMissingFeatureResource();
  testRenderPathGraphRegistrationRejectsSceneInputWithoutGeometry();
  testFailedShaderMetadataDoesNotSatisfyRenderPathGraphDependency();
  testSourceResolvedShaderWithoutPayloadDoesNotSatisfyGraphDependency();
  testUploadViewRejectsSourceResolvedShaderWithoutPayload();
  testUploadViewRejectsReleasedRenderPathGraphFeatureDependency();
  testUploadViewRejectsReleasedRenderPathGraphShaderDependency();
  testExportRejectsReleasedRenderPathGraphDependencies();
  testExportRejectsFailedRenderPathGraphShaderDependency();
  if (g_failures != 0) {
    std::cerr << g_failures << " resource upload view v2 checks failed\n";
    return 1;
  }
  return 0;
}
