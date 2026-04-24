#pragma once
#include "core/platform/types.hpp"
#include "core/math/vec.hpp"
#include <memory>
#include <string>
#include <vector>

namespace infra {

// REQ-011: minimal PBR material metadata extracted from a glTF primitive.
// All texture fields hold URIs relative to the directory that contains the
// `.gltf` file (i.e. exactly what the glTF document declares in `image.uri`).
// An empty string means the corresponding texture was not declared. Texture
// resolution / loading is the caller's responsibility; this struct does not
// touch the filesystem.
struct GLTFPbrMaterial {
  LX_core::Vec4f baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  LX_core::Vec3f emissiveFactor{0.0f, 0.0f, 0.0f};

  std::string baseColorTexture;
  std::string metallicRoughnessTexture;
  std::string normalTexture;
  std::string occlusionTexture;
  std::string emissiveTexture;
};

class GLTFLoader {
public:
  GLTFLoader();
  ~GLTFLoader();

  void load(const std::string &filename);

  const std::vector<LX_core::Vec3f> &getPositions() const;
  const std::vector<LX_core::Vec3f> &getNormals() const;
  const std::vector<LX_core::Vec2f> &getTexCoords() const;
  const std::vector<u32> &getIndices() const;

  // REQ-011: empty when the glTF file does not declare a TANGENT accessor for
  // the consumed primitive — no tangent generation fallback is performed.
  const std::vector<LX_core::Vec4f> &getTangents() const;

  // REQ-011: primitive's bound PBR material (defaults applied when primitive
  // has no material).
  const GLTFPbrMaterial &getMaterial() const;

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

} // namespace infra
