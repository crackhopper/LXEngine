#pragma once
/// Temporary Blinn-Phong material + UBO for backend/tests until a real material system
/// wires through core. Not part of the core layer.

#include "core/gpu/render_resource.hpp"
#include "core/math/vec.hpp"
#include "core/resources/texture.hpp"
#include "core/resources/material.hpp"
#include <memory>
#include <vector>

namespace LX_core::backend {

/// Matches `MaterialUBO` in `shaders/glsl/blinnphong_0.frag` (std140).
struct alignas(16) MaterialBlinnPhongUBO : public IRenderResource {
  struct Params {
    Vec3f baseColor{0.8f, 0.8f, 0.8f};
    float shininess = 12.0f;
    float specularIntensity = 1.0f;
    int32_t enableAlbedo = 0;
    int32_t enableNormalMap = 0;
    int32_t padding = 0;
  } params{};

  explicit MaterialBlinnPhongUBO(ResourcePassFlag passFlag) : m_passFlag(passFlag) {}

  ResourcePassFlag getPassFlag() const override { return m_passFlag; }
  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &params; }
  static constexpr usize ResourceSize = sizeof(Params);
  u32 getByteSize() const override { return static_cast<u32>(ResourceSize); }
  PipelineSlotId getPipelineSlotId() const override {
    return PipelineSlotId::MaterialUBO;
  }

private:
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};

using MaterialBlinnPhongUboPtr = std::shared_ptr<MaterialBlinnPhongUBO>;

class MaterialBlinnPhong : public IMaterial {
public:
  using Ptr = std::shared_ptr<MaterialBlinnPhong>;

  MaterialBlinnPhongUboPtr ubo;
  CombinedTextureSamplerPtr albedoSampler;
  CombinedTextureSamplerPtr normalSampler;

  static Ptr create(ResourcePassFlag passFlag = ResourcePassFlag::Forward) {
    return Ptr(new MaterialBlinnPhong(passFlag));
  }

  std::vector<IRenderResourcePtr> getDescriptorResources() const override {
    std::vector<IRenderResourcePtr> out;
    out.push_back(std::dynamic_pointer_cast<IRenderResource>(ubo));
    if (albedoSampler) {
      out.push_back(std::dynamic_pointer_cast<IRenderResource>(albedoSampler));
    }
    if (normalSampler) {
      out.push_back(std::dynamic_pointer_cast<IRenderResource>(normalSampler));
    }
    return out;
  }

  IShaderPtr getShaderInfo() const override { return nullptr; }

  ResourcePassFlag getPassFlag() const override { return m_passFlag; }

private:
  explicit MaterialBlinnPhong(ResourcePassFlag passFlag)
      : ubo(std::make_shared<MaterialBlinnPhongUBO>(passFlag)), m_passFlag(passFlag) {
    ubo->params.enableAlbedo = 0;
    ubo->params.enableNormalMap = 0;
  }

  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};

} // namespace LX_core::backend
