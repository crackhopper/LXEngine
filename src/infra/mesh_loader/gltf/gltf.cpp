#include "gltf.hpp"
#include <fstream>
#include <stdexcept>

namespace infra {

struct GLTFLoader::Impl {
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  std::vector<glm::vec2> texCoords;
  std::vector<uint32_t> indices;
};

GLTFLoader::GLTFLoader() : pImpl(new Impl) {}

GLTFLoader::~GLTFLoader() {
  delete pImpl;
}

void GLTFLoader::load(const std::string& filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open GLTF file: " + filename);
  }

  size_t fileSize = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);

  char header[12];
  if (!file.read(header, 12)) {
    throw std::runtime_error("Failed to read GLTF header");
  }

  uint32_t magic = *reinterpret_cast<uint32_t*>(header);
  if (magic == 0x46546C67) {
    throw std::runtime_error("Binary GLTF (.glb) not yet supported - use cgltf library");
  }

  throw std::runtime_error("ASCII GLTF (.gltf) not yet supported - use cgltf library");
}

const std::vector<glm::vec3>& GLTFLoader::getPositions() const {
  return pImpl->positions;
}

const std::vector<glm::vec3>& GLTFLoader::getNormals() const {
  return pImpl->normals;
}

const std::vector<glm::vec2>& GLTFLoader::getTexCoords() const {
  return pImpl->texCoords;
}

const std::vector<uint32_t>& GLTFLoader::getIndices() const {
  return pImpl->indices;
}

} // namespace infra
