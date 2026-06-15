#include "infra/resource_parsers/mesh_resource_parser.hpp"

#include "core/asset/mesh.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "infra/scene_asset/scene_mesh_loader.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace LX_infra {
namespace {

struct MemoryMeshVertex final {
  LX_core::Vec3f pos{};

  static const LX_core::VertexLayout &getLayout() {
    static const LX_core::VertexLayout layout{
        {{"inPos", 0, LX_core::DataType::Float3, sizeof(LX_core::Vec3f),
          offsetof(MemoryMeshVertex, pos)}},
        sizeof(MemoryMeshVertex)};
    return layout;
  }
};

[[nodiscard]] bool isMemoryUri(const LX_core::ResourceUri &uri) {
  return uri.string().rfind("memory://", 0) == 0;
}

[[nodiscard]] std::filesystem::path pathFromUri(const LX_core::ResourceUri &uri) {
  const std::string &text = uri.string();
  constexpr std::string_view assetsPrefix = "assets://";
  constexpr std::string_view filePrefix = "file://";
  if (text.rfind(assetsPrefix, 0) == 0) {
    return std::filesystem::path("assets") / text.substr(assetsPrefix.size());
  }
  if (text.rfind(filePrefix, 0) == 0) {
    return std::filesystem::path(text.substr(filePrefix.size()));
  }
  return std::filesystem::path(text);
}

[[nodiscard]] LX_core::MeshBufferUniquePtr makeMemoryTriangleMesh() {
  auto vertices = std::vector<MemoryMeshVertex>{
      {{0.0f, 0.0f, 0.0f}},
      {{1.0f, 0.0f, 0.0f}},
      {{0.0f, 1.0f, 0.0f}},
  };
  auto indices = std::vector<u32>{0, 1, 2};
  auto vb = LX_core::VertexBuffer<MemoryMeshVertex>::create(std::move(vertices));
  auto ib = LX_core::IndexBuffer::create(std::move(indices));
  return LX_core::MeshBuffer::create(
             vb, ib,
             LX_core::BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}})
      ->cloneUnique();
}

[[nodiscard]] ParsedSceneResource makeFailedMeshParse(
    LX_core::SceneResourceTable &table, const LX_core::ResourceUri &uri,
    const LX_core::ResourceUri &ownerUri, std::string message) {
  ParsedSceneResource parsed;
  parsed.metadata.type = LX_core::SceneResourceType::Mesh;
  parsed.metadata.uri = uri;
  parsed.metadata.state = LX_core::ResourceState::Failed;
  parsed.metadata.diagnostics.push_back(LX_core::ResourceDiagnostic{
      .ownerUri = ownerUri,
      .resourceUri = uri,
      .parserName = "MeshResourceParser",
      .message = message,
  });
  parsed.diagnostics.push_back(std::move(message));
  parsed.identity = table.internResourceMetadata(parsed.metadata);
  return parsed;
}

} // namespace

ParsedSceneResource MeshResourceParser::parse(
    LX_core::SceneResourceTable &table, const LX_core::ResourceUri &uri,
    const SceneResourceParseContext &context) const {
  const LX_core::ResourceUri canonicalUri =
      table.resolveUri(context.ownerUri, uri);

  if (const auto existing = table.findMesh(canonicalUri)) {
    ParsedSceneResource parsed;
    parsed.metadata.type = LX_core::SceneResourceType::Mesh;
    parsed.metadata.uri = canonicalUri;
    parsed.identity =
        table.loadOrGetResource(LX_core::SceneResourceType::Mesh, canonicalUri);
    if (const auto *metadata = table.findResourceMetadata(parsed.identity)) {
      parsed.metadata = *metadata;
    }
    return parsed;
  }

  LX_core::MeshBufferUniquePtr mesh;
  if (isMemoryUri(canonicalUri)) {
    mesh = makeMemoryTriangleMesh();
  } else {
    try {
      mesh = scene_asset::loadSceneMeshAsset(pathFromUri(canonicalUri))
                 ->cloneUnique();
    } catch (const std::exception &error) {
      return makeFailedMeshParse(table, canonicalUri, context.ownerUri,
                                 error.what());
    }
  }

  const LX_core::MeshHandle handle =
      table.registerMesh(canonicalUri, std::move(mesh));
  ParsedSceneResource parsed;
  parsed.metadata.type = LX_core::SceneResourceType::Mesh;
  parsed.metadata.uri = canonicalUri;
  if (!handle.isValid()) {
    return makeFailedMeshParse(table, canonicalUri, context.ownerUri,
                               "memory mesh payload registration failed");
  }
  parsed.identity =
      table.loadOrGetResource(LX_core::SceneResourceType::Mesh, canonicalUri);
  if (const auto *metadata = table.findResourceMetadata(parsed.identity)) {
    parsed.metadata = *metadata;
  }
  return parsed;
}

} // namespace LX_infra
