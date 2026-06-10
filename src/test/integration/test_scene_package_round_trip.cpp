#include "core/package/scene_package_manifest.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
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

[[nodiscard]] ResourceMetadata makeMetadata(SceneResourceType type,
                                            const char *uri,
                                            const char *contentHash) {
  ResourceMetadata metadata;
  metadata.type = type;
  metadata.uri = ResourceUri(uri);
  metadata.contentHash = contentHash;
  return metadata;
}

[[nodiscard]] SceneResourceGraphExport makeGraph() {
  SceneResourceGraphExport graph;

  auto material = makeMetadata(SceneResourceType::Material,
                               "assets/materials/carpaint.material",
                               "material:matte:red");
  material.dependencies.push_back(ResourceUri("assets/textures/red.png"));

  auto mesh = makeMetadata(SceneResourceType::Mesh, "assets/meshes/body.obj",
                           "mesh:body:v1");
  mesh.dependencies.push_back(ResourceUri("assets/materials/carpaint.material"));

  auto texture = makeMetadata(SceneResourceType::Texture,
                              "assets/textures/red.png", "texture:red:v1");

  graph.handles = {
      ResourceIdentityHandle{7, 2},
      ResourceIdentityHandle{3, 9},
      ResourceIdentityHandle{5, 1},
  };
  graph.resources = {
      material,
      mesh,
      texture,
  };
  return graph;
}

[[nodiscard]] SceneResourceGraphExport makeGraphWithDifferentInputOrder() {
  SceneResourceGraphExport graph = makeGraph();
  std::swap(graph.handles[0], graph.handles[2]);
  std::swap(graph.resources[0], graph.resources[2]);
  return graph;
}

void testPackageRoundTripPreservesManifestResourcesAndDependencies() {
  const SceneResourceGraphExport graph = makeGraph();

  const ScenePackageManifest manifest = buildScenePackageManifest(graph);
  const std::string manifestText = writeScenePackageManifest(manifest);
  const ScenePackageManifest restored = readScenePackageManifest(manifestText);

  EXPECT(restored.schemaVersion == 1, "schema version should round trip");
  EXPECT(restored.resources.size() == 3,
         "all graph resources should round trip through package manifest");
  EXPECT(restored.rootHash == manifest.rootHash,
         "root hash should be persisted in the manifest");

  const auto materialIt =
      std::find_if(restored.resources.begin(), restored.resources.end(),
                   [](const ScenePackageResourceRecord &record) {
                     return record.metadata.uri ==
                            "assets/materials/carpaint.material";
                   });
  EXPECT(materialIt != restored.resources.end(),
         "material resource should round trip by canonical URI");
  if (materialIt == restored.resources.end()) {
    for (const auto &record : restored.resources) {
      std::cerr << "[INFO] restored uri: " << record.metadata.uri.string()
                << '\n';
    }
  }
  if (materialIt != restored.resources.end()) {
    EXPECT(materialIt->sourceHandle.index == 7 &&
               materialIt->sourceHandle.generation == 2,
           "resource identity handle should round trip");
    EXPECT(materialIt->metadata.type == SceneResourceType::Material,
           "resource type should round trip");
    EXPECT(materialIt->metadata.contentHash == "material:matte:red",
           "resource content hash metadata should round trip");
    EXPECT(materialIt->metadata.dependencies.size() == 1,
           "resource dependency count should round trip");
    EXPECT(materialIt->metadata.dependencies.front() ==
               "assets/textures/red.png",
           "resource dependency URI should round trip");
  }
}

void testPackageHashIsInputOrderIndependentAndMetadataSensitive() {
  const ScenePackageManifest first = buildScenePackageManifest(makeGraph());
  const ScenePackageManifest reordered =
      buildScenePackageManifest(makeGraphWithDifferentInputOrder());

  EXPECT(first.rootHash == reordered.rootHash,
         "same resources in a different input order should hash identically");

  SceneResourceGraphExport changedMetadata = makeGraph();
  changedMetadata.resources.front().contentHash = "material:matte:blue";
  const ScenePackageManifest metadataChanged =
      buildScenePackageManifest(changedMetadata);
  EXPECT(first.rootHash != metadataChanged.rootHash,
         "content metadata changes should change package hash");

  SceneResourceGraphExport changedUri = makeGraph();
  changedUri.resources.front().uri =
      ResourceUri("assets/materials/blue.material");
  const ScenePackageManifest uriChanged = buildScenePackageManifest(changedUri);
  EXPECT(first.rootHash != uriChanged.rootHash,
         "canonical URI changes should change package hash");

  SceneResourceGraphExport changedDependency = makeGraph();
  changedDependency.resources.front().dependencies.front() =
      ResourceUri("assets/textures/blue.png");
  const ScenePackageManifest dependencyChanged =
      buildScenePackageManifest(changedDependency);
  EXPECT(first.rootHash != dependencyChanged.rootHash,
         "dependency graph changes should change package hash");
}

void testPackageBytesRoundTripManifestText() {
  const ScenePackageManifest manifest = buildScenePackageManifest(makeGraph());
  const std::vector<std::byte> bytes = writeScenePackageBytes(manifest);
  const ScenePackageManifest restored = readScenePackageBytes(bytes);

  EXPECT(restored.rootHash == manifest.rootHash,
         "binary package bytes should preserve manifest root hash");
  EXPECT(writeScenePackageManifest(restored) ==
             writeScenePackageManifest(manifest),
         "binary package bytes should preserve deterministic manifest text");
}

} // namespace

int main() {
  testPackageRoundTripPreservesManifestResourcesAndDependencies();
  testPackageHashIsInputOrderIndependentAndMetadataSensitive();
  testPackageBytesRoundTripManifestText();

  if (g_failures != 0) {
    std::cerr << g_failures << " scene package checks failed\n";
    return 1;
  }
  return 0;
}
