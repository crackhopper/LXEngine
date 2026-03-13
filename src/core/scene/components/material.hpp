#pragma once
#include "../../resources/shader.hpp"
#include "../../resources/texture.hpp"
#include "../gpu/render_resource.hpp"
#include "../math/vec.hpp"
#include "base.hpp"
#include <memory>
#include <optional>

// UBO注意：确保std140对齐
// | 类型              | 对齐方式         | 说明                       |
// | --------------- | ------------ | ------------------------ |
// | `float`/`int`   | 4 bytes      | 标量占 4 bytes              |
// | `vec2`          | 8 bytes      | 2 * 4 bytes              |
// | `vec3` / `vec4` | 16 bytes     | vec3 自动填充到 vec4 对齐       |
// | `mat4`          | 16 bytes 对齐  | 每列 16 bytes，矩阵本身 16 字节对齐 |
// | struct          | 结构体对齐到最大成员对齐 | 结构体整体也要 16 bytes 对齐 |

namespace LX_core {

class MaterialBase : public IComponent {
public:
  virtual ~MaterialBase() = default;

  virtual ShaderPtr getVertexShader() const = 0;
  virtual ShaderPtr getFragmentShader() const = 0;
  virtual ResourcePassFlag getPassFlag() const = 0;
  virtual std::vector<IRenderResourcePtr> getDescriptorResources()const = 0;
};

using MaterialPtr = std::shared_ptr<MaterialBase>; // 共享使用

struct alignas(16) MaterialBlinnPhongUbo : public IRenderResource {
  MaterialBlinnPhongUbo(ResourcePassFlag passFlag = ResourcePassFlag::Forward)
      : m_passFlag(passFlag) {}

  struct Param {
    Vec3f baseColor = Vec3f(1.0f, 1.0f, 1.0f);
    float shininess = 32.0f;
    float specularIntensity = 0.0f;
    int enableAlbedo = 1;
    int enableNormalMap = 1; // 4 bytes
    int padding0 = 0;        // 4 bytes padding，保证 std140 对齐
  };
  Param params;

  virtual ResourcePassFlag getPassFlag() const override { return m_passFlag; }
  virtual ResourceType getType() const override {
    return ResourceType::UniformBuffer;
  }
  virtual const void *getRawData() const override { return &params; }
  virtual u32 getByteSize() const override { return sizeof(Param); }
  virtual PipelineSlotId getPipelineSlotId() const override {
    return PipelineSlotId::MaterialUBO;
  }

private:
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};
using MaterialBlinnPhongUboPtr = std::shared_ptr<MaterialBlinnPhongUbo>;

class MaterialBlinnPhong : public MaterialBase {
public:
  MaterialBlinnPhong(ResourcePassFlag passFlag = ResourcePassFlag::Forward)
      : m_passFlag(passFlag) {

    params = std::make_shared<MaterialBlinnPhongUbo>(passFlag);

    albedoMap = std::make_shared<CombinedTextureSampler>(
        createWhiteTexture(), PipelineSlotId::AlbedoTexture, passFlag);
    normalMap = std::make_shared<CombinedTextureSampler>(
        createWhiteTexture(), PipelineSlotId::NormalTexture, passFlag);

    vertexShader = std::make_shared<VertexShader>("blinn_phong_0", passFlag);
    fragmentShader =
        std::make_shared<FragmentShader>("blinn_phong_0", passFlag);
  }

  virtual std::vector<IRenderResourcePtr> getRenderResources() const override {
    return {std::dynamic_pointer_cast<IRenderResource>(params),
            std::dynamic_pointer_cast<IRenderResource>(albedoMap),
            std::dynamic_pointer_cast<IRenderResource>(normalMap),
            std::dynamic_pointer_cast<IRenderResource>(vertexShader),
            std::dynamic_pointer_cast<IRenderResource>(fragmentShader),
          };
  }
  virtual std::vector<IRenderResourcePtr> getDescriptorResources() const override {
    return {std::dynamic_pointer_cast<IRenderResource>(params),
            std::dynamic_pointer_cast<IRenderResource>(albedoMap),
            std::dynamic_pointer_cast<IRenderResource>(normalMap),
          };
  }

  virtual ShaderPtr getVertexShader() const override { return vertexShader; }
  virtual ShaderPtr getFragmentShader() const override { return fragmentShader; }
  virtual ResourcePassFlag getPassFlag() const override { return m_passFlag; }

  MaterialBlinnPhongUboPtr params;
  CombinedTextureSamplerPtr albedoMap;
  CombinedTextureSamplerPtr normalMap;
  ShaderPtr vertexShader;
  ShaderPtr fragmentShader;

protected:
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};

} // namespace LX_core