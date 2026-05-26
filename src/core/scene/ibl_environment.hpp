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

using EnvironmentDataSharedPtr = std::shared_ptr<EnvironmentData>;

struct IblEnvironmentResources {
  CombinedTextureSamplerSharedPtr skyboxCubemap;
  CombinedTextureSamplerSharedPtr irradianceCubemap;
  CombinedTextureSamplerSharedPtr prefilteredRadianceCubemap;
  CombinedTextureSamplerSharedPtr brdfLut;
  EnvironmentDataSharedPtr environmentUbo;
};

inline TextureSharedPtr createBlackCubeTexture(TextureFormat format =
                                                   TextureFormat::RGBA16Float) {
  TextureDesc desc;
  desc.width = 1;
  desc.height = 1;
  desc.format = format;
  desc.dimension = TextureDimension::TextureCube;
  desc.arrayLayers = 6;
  return std::make_shared<Texture>(
      desc, std::vector<u8>(expectedTextureByteCount(desc), 0));
}

inline TextureSharedPtr createNeutralBrdfLutTexture() {
  TextureDesc desc;
  desc.width = 1;
  desc.height = 1;
  desc.format = TextureFormat::RGBA16Float;
  return std::make_shared<Texture>(
      desc, std::vector<u8>(expectedTextureByteCount(desc), 0));
}

inline IblEnvironmentResources createDefaultIblEnvironmentResources() {
  IblEnvironmentResources resources;
  resources.skyboxCubemap =
      std::make_shared<CombinedTextureSampler>(createBlackCubeTexture());
  resources.skyboxCubemap->setBindingName(StringID("SkyboxMap"));
  resources.irradianceCubemap =
      std::make_shared<CombinedTextureSampler>(createBlackCubeTexture());
  resources.irradianceCubemap->setBindingName(StringID("IrradianceMap"));
  resources.prefilteredRadianceCubemap =
      std::make_shared<CombinedTextureSampler>(createBlackCubeTexture());
  resources.prefilteredRadianceCubemap->setBindingName(
      StringID("PrefilteredEnvMap"));
  resources.brdfLut =
      std::make_shared<CombinedTextureSampler>(createNeutralBrdfLutTexture());
  resources.brdfLut->setBindingName(StringID("BrdfLut"));
  resources.environmentUbo = std::make_shared<EnvironmentData>(0.0f, 1.0f);
  return resources;
}

inline IblEnvironmentResources
completeIblEnvironmentResources(IblEnvironmentResources resources) {
  auto defaults = createDefaultIblEnvironmentResources();
  if (!resources.skyboxCubemap) {
    resources.skyboxCubemap = defaults.skyboxCubemap;
  } else {
    resources.skyboxCubemap->setBindingName(StringID("SkyboxMap"));
  }
  if (!resources.irradianceCubemap) {
    resources.irradianceCubemap = defaults.irradianceCubemap;
  } else {
    resources.irradianceCubemap->setBindingName(StringID("IrradianceMap"));
  }
  if (!resources.prefilteredRadianceCubemap) {
    resources.prefilteredRadianceCubemap =
        defaults.prefilteredRadianceCubemap;
  } else {
    resources.prefilteredRadianceCubemap->setBindingName(
        StringID("PrefilteredEnvMap"));
  }
  if (!resources.brdfLut) {
    resources.brdfLut = defaults.brdfLut;
  } else {
    resources.brdfLut->setBindingName(StringID("BrdfLut"));
  }
  if (!resources.environmentUbo) {
    resources.environmentUbo = defaults.environmentUbo;
  }
  return resources;
}

} // namespace LX_core
