#pragma once
#include "../math/vec.hpp"
#include "texture.hpp"
#include <optional>

namespace LX_core {

enum VulkanPipelineType {
  // 预设的固定管线，不可以自定义shader
  Unlit,  // 无光照模型：直接输出颜色，不进行光照计算
  Normal, // 传统光照模型: 基于 Blinn-Phong 模型
  PBR,    // 基于物理的渲染模型：采用 Cook-Torrance 模型
  // 下面的尚未实现
  Shadow,
  UI,
  Custom,
};

enum ShaderType {
  VertexShader,
  FragmentShader,
};

std::string getPipelineId(VulkanPipelineType type, i32 subid = 0);

std::string getPredefinedShaderName(const std::string &pipelineId, ShaderType type);

class MaterialBase {
public:
  virtual std::string pipelineId() = 0; // 材质对应的 pipeline id

  virtual std::string getShaderName(ShaderType type){
    return getPredefinedShaderName(pipelineId(), type);
  }
};

class MaterialNormal : public MaterialBase {
  static const i32 subid = 0;
  bool enableLight = true; // 是否启用光照

public:
  std::string pipelineId() override {
    if (!enableLight) {
      return getPipelineId(VulkanPipelineType::Unlit, subid);
    } else {
      return getPipelineId(VulkanPipelineType::Normal, subid);
    }
  }
  // 漫反射贴图
  Vec3f baseColor =
      Vec3f(1.0f, 1.0f,
            1.0f); // 基础颜色；如果没有贴图，就用这个颜色；否则和贴图颜色相乘
  TexturePtr diffuseMap;

  // 高光
  Vec3f specularColor = Vec3f(1.0f, 1.0f, 1.0f);
  f32 shininess = 32.0f;
};

class MaterialPBR : public MaterialBase {
  static const i32 subid = 0;

public:
  std::string pipelineId() override {
    return getPipelineId(VulkanPipelineType::PBR, subid);
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