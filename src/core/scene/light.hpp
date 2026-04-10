#pragma once
#include "core/gpu/render_resource.hpp"
#include "core/math/vec.hpp"

namespace LX_core {
class LightBase {};

struct alignas(16) DirectionalLightUBO : public IRenderResource {
  DirectionalLightUBO(ResourcePassFlag passFlag)
    : m_passFlag(passFlag) {}
  struct Param {
    Vec4f dir;
    Vec4f color;
  };
  Param param;
  static constexpr usize ResourceSize = sizeof(Param);

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
    return ResourceSize;
  }

  virtual PipelineSlotId getPipelineSlotId() const override {
    return PipelineSlotId::LightUBO;
  }  
private:
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};
using DirectionalLightUboPtr = std::shared_ptr<DirectionalLightUBO>;

class DirectionalLight : public LightBase {
public:
  DirectionalLight(ResourcePassFlag passFlag = ResourcePassFlag::Forward)
    : ubo(std::make_shared<DirectionalLightUBO>(passFlag)) {}
  DirectionalLightUboPtr ubo;

  DirectionalLightUboPtr getUBO() const { return ubo; }
};
using DirectionalLightPtr = std::shared_ptr<DirectionalLight>;
} // namespace LX_core