#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace infra {

class GLTFLoader {
public:
  GLTFLoader();
  ~GLTFLoader();

  void load(const std::string& filename);

  const std::vector<glm::vec3>& getPositions() const;
  const std::vector<glm::vec3>& getNormals() const;
  const std::vector<glm::vec2>& getTexCoords() const;
  const std::vector<uint32_t>& getIndices() const;

private:
  struct Impl;
  Impl* pImpl;
};

} // namespace infra
