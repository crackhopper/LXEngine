#pragma once
#include "../math/vec.hpp"
#include "texture.hpp"
#include <optional>

namespace LX_core {

struct Material {
  // 基础 PBR 属性
  Vec3f baseColor = Vec3f(1.0f, 1.0f, 1.0f);
  float metallic = 0.0f;
  float roughness = 1.0f;

  // 可选贴图
  std::optional<TexturePtr> albedoMap;
  std::optional<TexturePtr> normalMap;
  std::optional<TexturePtr> metallicRoughnessMap;

  // 如果以后要扩展 shader 参数可以加更多 optional Texture 或 float
};

using MaterialPtr = std::shared_ptr<Material>; // 共享使用
} // namespace LX_core