#pragma once
#include "../math/vec.hpp"
#include "texture.hpp"
#include <optional>
#include <memory>

namespace LX_core {

enum PipelineType {
  // 预设的固定管线，不可以自定义shader
  PT_BlinnPhong, // 传统光照模型: 基于 Blinn-Phong 模型；同时也支持无光照渲染
  PT_PBR_CookTorrance, // 基于物理的渲染模型：采用 Cook-Torrance 模型
  // 下面的尚未实现
  PT_Shadow,
  PT_UI,
  PT_Custom,
};

enum ShaderType {
  ST_Vertex,
  ST_Fragment,
};

std::string getPipelineId(PipelineType type, i32 subid = 0);

std::string getPredefinedShaderName(const std::string &pipelineId,
                                    ShaderType type);

class MaterialBase {
public:
  virtual std::string pipelineId() = 0; // 材质对应的 pipeline id

  virtual std::string getShaderName(ShaderType type) {
    return getPredefinedShaderName(pipelineId(), type);
  }
};

class MaterialBlinnPhong : public MaterialBase {
  static const i32 subid = 0;

public:
  // 确保std140对齐
  // | 类型              | 对齐方式         | 说明                       |
  // | --------------- | ------------ | ------------------------ |
  // | `float`/`int`   | 4 bytes      | 标量占 4 bytes              |
  // | `vec2`          | 8 bytes      | 2 * 4 bytes              |
  // | `vec3` / `vec4` | 16 bytes     | vec3 自动填充到 vec4 对齐       |
  // | `mat4`          | 16 bytes 对齐  | 每列 16 bytes，矩阵本身 16 字节对齐 |
  // | struct          | 结构体对齐到最大成员对齐 | 结构体整体也要 16 bytes 对齐      |
  struct alignas(16) UBO {
    int enableLighting = 1;  // 4 bytes
    int enableTexture = 1;   // 4 bytes
    int enableNormalMap = 1; // 4 bytes
    int padding0 = 0;        // 4 bytes padding，保证 std140 对齐

    Vec3f baseColor = Vec3f(1.0f, 1.0f, 1.0f); // 12 bytes
    float shininess = 32.0f; // 4 bytes, 和 vec3 对齐总共 16 bytes
  };

  UBO ubo;

  TexturePtr colorTex;
  TexturePtr normalTex;

  std::string pipelineId() override {
    return getPipelineId(PipelineType::PT_BlinnPhong, subid);
  }

  std::string getShaderName(ShaderType type) override {
    return getPredefinedShaderName(pipelineId(), type);
  }
};

class MaterialPBR : public MaterialBase {
  static const i32 subid = 0;

public:
  std::string pipelineId() override {
    return getPipelineId(PipelineType::PT_PBR_CookTorrance, subid);
  }

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

using MaterialPtr = std::shared_ptr<MaterialBase>; // 共享使用
} // namespace LX_core