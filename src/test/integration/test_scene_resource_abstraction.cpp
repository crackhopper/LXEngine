#include "core/resource/resource_metadata.hpp"
#include "core/resource/resource_uri.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/resource_parsers/mesh_resource_parser.hpp"
#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"
#include "infra/resource_parsers/scene_resource_parser_registry.hpp"
#include "infra/resource_parsers/texture_resource_parser.hpp"

#include <filesystem>
#include <iostream>

using namespace LX_core;
using namespace LX_infra;

#ifndef LXE_SOURCE_DIR
#define LXE_SOURCE_DIR ""
#endif

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testResourceUriCanonicalization() {
  const ResourceUri scene = ResourceUri::canonicalize(
      "assets/scenes/garage/scene.yaml", "../materials/paint.material");
  const ResourceUri material =
      ResourceUri::canonicalize("assets/scenes/materials/base.material",
                                "../materials/paint.material");

  EXPECT(scene.string() == "assets/scenes/materials/paint.material",
         "scene-relative URI should canonicalize");
  EXPECT(material.string() == "assets/scenes/materials/paint.material",
         "material-relative URI should canonicalize to same value");
  EXPECT(scene == material, "canonical URIs should compare equal");
}

void testSceneResourceTableIdentityDedup() {
  SceneResourceTable table;
  ResourceMetadata meshMeta;
  meshMeta.type = SceneResourceType::Mesh;
  meshMeta.uri = ResourceUri("assets/meshes/car.obj");
  ResourceMetadata textureMeta = meshMeta;
  textureMeta.type = SceneResourceType::Texture;

  const ResourceIdentityHandle first = table.internResourceMetadata(meshMeta);
  const ResourceIdentityHandle second = table.internResourceMetadata(meshMeta);
  const ResourceIdentityHandle third = table.internResourceMetadata(textureMeta);

  EXPECT(first.isValid(), "first identity handle should be valid");
  EXPECT(first == second, "same canonical URI plus type should deduplicate");
  EXPECT(!(first == third), "same URI with different type should not deduplicate");
}

void testParserRegistryDiagnosticsIncludeContext() {
  SceneResourceParserRegistry registry;
  registry.registerParser(
      SceneResourceType::Material, ".material", "material-test-parser",
      [](SceneResourceTable &, const ResourceUri &uri,
         const SceneResourceParseContext &) {
        ParsedSceneResource parsed;
        parsed.metadata.type = SceneResourceType::Material;
        parsed.metadata.uri = uri;
        parsed.diagnostics.push_back("parse failed");
        return parsed;
      });

  SceneResourceTable table;
  const auto parsed = registry.parse(table, SceneResourceType::Material,
                                     ResourceUri("assets/materials/bad.material"),
                                     {.ownerUri =
                                          ResourceUri("assets/scenes/a.scene")});

  EXPECT(!parsed.diagnostics.empty(), "parser failure should emit diagnostics");
  EXPECT(parsed.diagnostics.front().find("assets/scenes/a.scene") !=
             std::string::npos,
         "diagnostic should include owner URI");
  EXPECT(parsed.diagnostics.front().find("assets/materials/bad.material") !=
             std::string::npos,
         "diagnostic should include resource URI");
  EXPECT(parsed.diagnostics.front().find("material-test-parser") !=
             std::string::npos,
         "diagnostic should include parser name");
}

void testParserRegistryRoutesCompoundYamlExtensions() {
  SceneResourceParserRegistry registry;
  registry.registerParser(
      SceneResourceType::RenderPathGraph, ".render-path.yaml",
      "render-path-graph-test-parser",
      [](SceneResourceTable &, const ResourceUri &uri,
         const SceneResourceParseContext &) {
        ParsedSceneResource parsed;
        parsed.metadata.type = SceneResourceType::RenderPathGraph;
        parsed.metadata.uri = uri;
        return parsed;
      });
  registry.registerParser(
      SceneResourceType::RenderFeature, ".render-feature.yaml",
      "render-feature-test-parser",
      [](SceneResourceTable &, const ResourceUri &uri,
         const SceneResourceParseContext &) {
        ParsedSceneResource parsed;
        parsed.metadata.type = SceneResourceType::RenderFeature;
        parsed.metadata.uri = uri;
        return parsed;
      });

  SceneResourceTable table;
  const SceneResourceParseContext context{
      .ownerUri = ResourceUri("assets/scenes/a.scene")};
  const auto graph = registry.parse(
      table, SceneResourceType::RenderPathGraph,
      ResourceUri("assets/render_paths/forward_main.render-path.yaml"),
      context);
  const auto feature = registry.parse(
      table, SceneResourceType::RenderFeature,
      ResourceUri("assets/effects/tone_mapping.render-feature.yaml"), context);
  const auto mismatched = registry.parse(
      table, SceneResourceType::RenderPathGraph,
      ResourceUri("assets/effects/tone_mapping.render-feature.yaml"), context);

  EXPECT(graph.diagnostics.empty(),
         ".render-path.yaml should route to graph parser");
  EXPECT(graph.metadata.type == SceneResourceType::RenderPathGraph,
         "graph parser should own .render-path.yaml resources");
  EXPECT(feature.diagnostics.empty(),
         ".render-feature.yaml should route to feature parser");
  EXPECT(feature.metadata.type == SceneResourceType::RenderFeature,
         "feature parser should own .render-feature.yaml resources");
  EXPECT(!mismatched.diagnostics.empty(),
         "graph resource type should not accept .render-feature.yaml");
}

void testParserRegistryConnectsRealRenderParsersToSceneResourceTable() {
  SceneResourceParserRegistry registry;
  registerRenderResourceParsers(registry);

  SceneResourceTable table;
  const auto parsed = registry.parse(
      table, SceneResourceType::RenderPathGraph,
      ResourceUri("assets/render_paths/forward_main.render-path.yaml"),
      SceneResourceParseContext{});

  for (const std::string &diagnostic : parsed.diagnostics) {
    std::cerr << "[diagnostic] " << diagnostic << '\n';
  }

  EXPECT(parsed.diagnostics.empty(),
         "real RenderPathGraph parser adapter should parse default graph");
  EXPECT(parsed.identity.isValid(),
         "real RenderPathGraph parser adapter should return graph identity");
  EXPECT(table.renderPathGraphCount() == 1,
         "real RenderPathGraph parser adapter should register graph payload");
  EXPECT(table.renderFeatureCount() == 1,
         "real RenderPathGraph parser adapter should parse and register feature "
         "payload dependency");
  EXPECT(table.shaderCount() == 4,
         "real RenderPathGraph parser adapter should register one live shader "
         "dependency per graph pass");

  const auto exported = table.exportResourceGraph();
  const u32 graphIndex = exported.handleToIndex(parsed.identity);
  EXPECT(graphIndex != u32_max, "parsed graph should export in resource graph");
  EXPECT(graphIndex != u32_max &&
             exported.resources[graphIndex].dependencyHandles.size() == 5,
         "parsed graph should depend on one feature and four shader resources");
}

void testParserReturnedIdentityOutlivesParserObject() {
  SceneResourceTable table;
  ResourceIdentityHandle meshIdentity;
  ResourceIdentityHandle textureIdentity;
  {
    MeshResourceParser meshParser;
    TextureResourceParser textureParser;
    meshIdentity = meshParser.parse(table, ResourceUri("assets/meshes/car.obj"),
                                    {}).identity;
    textureIdentity =
        textureParser.parse(table, ResourceUri("assets/textures/albedo.png"), {})
            .identity;
  }

  EXPECT(meshIdentity.isValid(), "mesh parser should return identity handle");
  EXPECT(textureIdentity.isValid(),
         "texture parser should return identity handle");
  const ResourceMetadata *mesh = table.findResourceMetadata(meshIdentity);
  const ResourceMetadata *texture = table.findResourceMetadata(textureIdentity);
  EXPECT(mesh != nullptr && mesh->type == SceneResourceType::Mesh,
         "mesh identity should remain valid after parser destruction");
  EXPECT(texture != nullptr && texture->type == SceneResourceType::Texture,
         "texture identity should remain valid after parser destruction");
}

} // namespace

int main() {
  if (std::filesystem::path sourceRoot{LXE_SOURCE_DIR}; !sourceRoot.empty()) {
    std::filesystem::current_path(sourceRoot);
  }

  testResourceUriCanonicalization();
  testSceneResourceTableIdentityDedup();
  testParserRegistryDiagnosticsIncludeContext();
  testParserRegistryRoutesCompoundYamlExtensions();
  testParserRegistryConnectsRealRenderParsersToSceneResourceTable();
  testParserReturnedIdentityOutlivesParserObject();
  if (g_failures != 0) {
    std::cerr << g_failures << " scene resource abstraction checks failed\n";
    return 1;
  }
  return 0;
}
