#pragma once

#include "core/asset/texture.hpp"
#include "core/math/vec.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <memory>
#include <vector>

namespace LX_core {

struct alignas(16) EnvironmentData final : public IGpuResource {
  struct Param {
    Vec4f params{0.0f, 1.0f, 0.0f, 0.0f};
  };

  explicit EnvironmentData(float iblIntensity = 0.0f,
                           float prefilteredMipCount = 1.0f) {
    param.params = Vec4f{iblIntensity, prefilteredMipCount, 0.0f, 0.0f};
  }

  void setParams(float iblIntensity, float prefilteredMipCount) {
    param.params = Vec4f{iblIntensity, prefilteredMipCount, 0.0f, 0.0f};
    setDirty();
  }

  [[nodiscard]] float getIblIntensity() const { return param.params.x; }
  [[nodiscard]] float getPrefilteredMipCount() const { return param.params.y; }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &param; }
  u32 getByteSize() const override { return sizeof(Param); }
  StringID getBindingName() const override {
    static const StringID kName("EnvironmentUBO");
    return kName;
  }

  Param param{};
};

using EnvironmentDataUniquePtr = std::unique_ptr<EnvironmentData>;

struct IblEnvironmentResources {
  CombinedTextureSamplerSharedPtr skyboxCubemap;
  CombinedTextureSamplerSharedPtr irradianceCubemap;
  CombinedTextureSamplerSharedPtr prefilteredRadianceCubemap;
  CombinedTextureSamplerSharedPtr brdfLut;
  CombinedTextureSamplerSharedPtr equirectangularMap;
  std::unique_ptr<IGpuResource> bakedSkyboxCubemap;
  std::unique_ptr<IGpuResource> bakedIrradianceCubemap;
  std::unique_ptr<IGpuResource> bakedPrefilteredRadianceCubemap;
  std::unique_ptr<IGpuResource> bakedBrdfLut;
  EnvironmentDataUniquePtr environmentUbo;
};

inline IblEnvironmentResources
completeIblEnvironmentResources(IblEnvironmentResources resources) {
  if (resources.skyboxCubemap) {
    resources.skyboxCubemap->setBindingName(StringID("SkyboxMap"));
  }
  if (resources.irradianceCubemap) {
    resources.irradianceCubemap->setBindingName(StringID("IrradianceMap"));
  }
  if (resources.prefilteredRadianceCubemap) {
    resources.prefilteredRadianceCubemap->setBindingName(
        StringID("PrefilteredEnvMap"));
  }
  if (resources.brdfLut) {
    resources.brdfLut->setBindingName(StringID("BrdfLut"));
  }
  return resources;
}

} // namespace LX_core
