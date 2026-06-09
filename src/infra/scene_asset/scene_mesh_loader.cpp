#include "infra/scene_asset/scene_mesh_loader.hpp"

#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "infra/mesh_loader/obj_mesh_loader.hpp"
#include "infra/scene_asset/gltf_scene_asset_loader.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace LX_infra::scene_asset {
namespace {

[[nodiscard]] std::string lowerExtension(const std::filesystem::path &path) {
  std::string extension = path.extension().string();
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

[[nodiscard]] LX_core::MeshSharedPtr buildMeshFromObj(infra::ObjLoader &loader) {
  const auto &positions = loader.getPositions();
  const auto &normals = loader.getNormals();
  const auto &uvs = loader.getTexCoords();
  const auto &indices = loader.getIndices();
  if (positions.empty() || indices.empty()) {
    throw std::runtime_error("OBJ asset has empty mesh geometry");
  }

  std::vector<LX_core::VertexPosNormalUvBone> vertices;
  vertices.reserve(positions.size());
  const LX_core::Vec3f fallbackNormal{0.0f, 1.0f, 0.0f};
  const LX_core::Vec2f fallbackUv{0.0f, 0.0f};
  const LX_core::Vec4f fallbackTangent{1.0f, 0.0f, 0.0f, 1.0f};
  const LX_core::Vec4i zeroBones{0, 0, 0, 0};
  const LX_core::Vec4f zeroWeights{0.0f, 0.0f, 0.0f, 0.0f};

  for (usize i = 0; i < positions.size(); ++i) {
    const LX_core::Vec3f normal =
        i < normals.size() ? normals[i] : fallbackNormal;
    const LX_core::Vec2f uv = i < uvs.size() ? uvs[i] : fallbackUv;
    vertices.emplace_back(positions[i], normal, uv, fallbackTangent, zeroBones,
                          zeroWeights);
  }

  auto vertexBuffer =
      LX_core::VertexBuffer<LX_core::VertexPosNormalUvBone>::create(
          std::move(vertices));
  auto indexBuffer = LX_core::IndexBuffer::create(std::vector<u32>(indices));
  return LX_core::Mesh::create(vertexBuffer, indexBuffer, loader.getBounds());
}

} // namespace

LX_core::MeshSharedPtr loadSceneMeshAsset(const std::filesystem::path &meshPath) {
  const std::string extension = lowerExtension(meshPath);
  if (extension == ".obj") {
    infra::ObjLoader loader;
    loader.load(meshPath.string());
    return buildMeshFromObj(loader);
  }
  if (extension == ".gltf" || extension == ".glb") {
    return loadGltfMeshAsset(meshPath).mesh;
  }
  throw std::runtime_error("unsupported scene mesh asset extension: " +
                           meshPath.string());
}

} // namespace LX_infra::scene_asset
