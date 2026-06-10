#include "core/resource/resource_metadata.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <iostream>

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

} // namespace

int main() {
  testPackageReadyGraphExport();
  testOverrideIdentityUsesStableHash();
  if (g_failures != 0) {
    std::cerr << g_failures << " resource upload view v2 checks failed\n";
    return 1;
  }
  return 0;
}
