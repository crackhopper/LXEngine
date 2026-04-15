#define TINYOBJLOADER_IMPLEMENTATION
#include "obj_mesh_loader.hpp"
#include <tinyobjloader/tiny_obj_loader.h>
#include <stdexcept>
#include <unordered_map>

namespace infra {

struct ObjLoader::Impl {
  std::vector<LX_core::Vec3f> positions;
  std::vector<LX_core::Vec3f> normals;
  std::vector<LX_core::Vec2f> texCoords;
  std::vector<uint32_t> indices;
};

ObjLoader::ObjLoader() : pImpl(new Impl) {}

ObjLoader::~ObjLoader() { delete pImpl; }

static uint64_t makeKey(int a, int b, int c) {
  return (static_cast<uint64_t>(a + 0x7FFFFFFF) << 42) |
         (static_cast<uint64_t>(b + 0x7FFFFFFF) << 21) |
         static_cast<uint64_t>(c + 0x7FFFFFFF);
}

void ObjLoader::load(const std::string &filename) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                        filename.c_str())) {
    throw std::runtime_error("Failed to load OBJ file: " + filename + "\n" +
                             err);
  }

  pImpl->positions.clear();
  pImpl->normals.clear();
  pImpl->texCoords.clear();
  pImpl->indices.clear();

  std::unordered_map<uint64_t, uint32_t> vertexMap;

  for (const auto &shape : shapes) {
    for (const auto &index : shape.mesh.indices) {
      uint64_t key =
          makeKey(index.vertex_index, index.normal_index, index.texcoord_index);

      if (vertexMap.find(key) == vertexMap.end()) {
        uint32_t newIndex = static_cast<uint32_t>(pImpl->positions.size());
        vertexMap[key] = newIndex;

        if (index.vertex_index >= 0) {
          pImpl->positions.emplace_back(
              attrib.vertices[3 * index.vertex_index + 0],
              attrib.vertices[3 * index.vertex_index + 1],
              attrib.vertices[3 * index.vertex_index + 2]);
        }

        if (index.normal_index >= 0) {
          pImpl->normals.emplace_back(
              attrib.normals[3 * index.normal_index + 0],
              attrib.normals[3 * index.normal_index + 1],
              attrib.normals[3 * index.normal_index + 2]);
        }

        if (index.texcoord_index >= 0) {
          pImpl->texCoords.emplace_back(
              attrib.texcoords[2 * index.texcoord_index + 0],
              attrib.texcoords[2 * index.texcoord_index + 1]);
        }
      }

      pImpl->indices.push_back(vertexMap[key]);
    }
  }
}

const std::vector<LX_core::Vec3f> &ObjLoader::getPositions() const {
  return pImpl->positions;
}

const std::vector<LX_core::Vec3f> &ObjLoader::getNormals() const {
  return pImpl->normals;
}

const std::vector<LX_core::Vec2f> &ObjLoader::getTexCoords() const {
  return pImpl->texCoords;
}

const std::vector<uint32_t> &ObjLoader::getIndices() const {
  return pImpl->indices;
}

} // namespace infra
