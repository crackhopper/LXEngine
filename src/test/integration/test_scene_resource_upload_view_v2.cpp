#include "core/resource/resource_metadata.hpp"
#include "core/asset/render_effect.hpp"
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

std::vector<ResourceUri> shaderSourceFixture() {
  return {
      ResourceUri("memory://shaders/surface_lit.vert"),
      ResourceUri("memory://shaders/surface_lit.frag"),
  };
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
  EXPECT(graph.resources.size() == 2, "graph should export two resources");
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
  testRenderPathGraphRegistrationRejectsMissingFeatureResource();
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
