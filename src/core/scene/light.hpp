#pragma once
#include "../gpu/render_resource.hpp"
#include "../math/vec.hpp"
#include "components/base.hpp"

namespace LX_core {
class LightBase {};

struct alignas(16) DirectionalLightUbo : public IRenderResource {
  DirectionalLightUbo(ResourcePassFlag passFlag)
    : m_passFlag(passFlag) {}
  struct Param {
    Vec4f dir;
    Vec4f color;
  };
  Param param;

  virtual ResourcePassFlag getPassFlag() const override {
    return m_passFlag;
  }
  virtual ResourceType getType() const override {
    return ResourceType::UniformBuffer;
  }
  virtual const void *getRawData() const override {
    return &param;
  }
  virtual u32 getByteSize() const override {
    return sizeof(Param);
  }

  virtual PipelineSlotId getPipelineSlotId() const override {
    return PipelineSlotId::LightUBO;
  }  
private:
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};
using DirectionalLightUboPtr = std::shared_ptr<DirectionalLightUbo>;

class DirectionalLight : public LightBase, public IComponent {
public:
  DirectionalLight(ResourcePassFlag passFlag = ResourcePassFlag::Forward)
    : ubo(std::make_shared<DirectionalLightUbo>(passFlag)) {}
  DirectionalLightUboPtr ubo;
  virtual std::vector<IRenderResourcePtr> getRenderResources() const override {
    return {
      std::dynamic_pointer_cast<IRenderResource>(ubo)
    };
  }  
};
using DirectionalLightPtr = std::shared_ptr<DirectionalLight>;
} // namespace LX_core