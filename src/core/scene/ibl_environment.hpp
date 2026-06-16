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
    Vec4f ambientColorIntensity{0.0f, 0.0f, 0.0f, 0.0f};
  };

  explicit EnvironmentData(float iblIntensity = 0.0f,
                           float prefilteredMipCount = 1.0f,
                           Vec3f ambientColor = Vec3f{0.0f, 0.0f, 0.0f},
                           float ambientIntensity = 0.0f) {
    param.params = Vec4f{iblIntensity, prefilteredMipCount, 0.0f, 0.0f};
    param.ambientColorIntensity =
        Vec4f{ambientColor.x, ambientColor.y, ambientColor.z,
              ambientIntensity};
  }

  void setParams(float iblIntensity, float prefilteredMipCount) {
    param.params = Vec4f{iblIntensity, prefilteredMipCount, 0.0f, 0.0f};
    setDirty();
  }

  void setAmbient(Vec3f color, float intensity) {
    param.ambientColorIntensity = Vec4f{color.x, color.y, color.z, intensity};
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

struct alignas(16) EnvironmentLightingData final : public IGpuResource {
  struct Param {
    Vec4f colorIntensity{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4f rotationBackgroundMode{0.0f, 1.0f, 0.0f, 0.0f};
  };

  enum class BackgroundMode : u32 {
    None = 0,
    Infinite = 1,
    FiniteBox = 2,
  };

  void set(Vec3f color, float intensity, float rotation,
           BackgroundMode backgroundMode) {
    param.colorIntensity = Vec4f{color.x, color.y, color.z, intensity};
    param.rotationBackgroundMode =
        Vec4f{rotation, static_cast<float>(backgroundMode), 0.0f, 0.0f};
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &param; }
  u32 getByteSize() const override { return sizeof(Param); }
  StringID getBindingName() const override {
    static const StringID kName("EnvironmentLightingUBO");
    return kName;
  }

  Param param{};
};

using EnvironmentLightingDataUniquePtr =
    std::unique_ptr<EnvironmentLightingData>;

struct alignas(16) EnvironmentLightingFiniteBoxData final
    : public IGpuResource {
  struct Param {
    Vec4f minBounds{0.0f, 0.0f, 0.0f, 0.0f};
    Vec4f maxBounds{0.0f, 0.0f, 0.0f, 0.0f};
  };

  void set(Vec3f minBounds, Vec3f maxBounds) {
    param.minBounds = Vec4f{minBounds.x, minBounds.y, minBounds.z, 0.0f};
    param.maxBounds = Vec4f{maxBounds.x, maxBounds.y, maxBounds.z, 0.0f};
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &param; }
  u32 getByteSize() const override { return sizeof(Param); }
  StringID getBindingName() const override {
    static const StringID kName("EnvironmentLightingFiniteBoxUBO");
    return kName;
  }

  Param param{};
};

using EnvironmentLightingFiniteBoxDataUniquePtr =
    std::unique_ptr<EnvironmentLightingFiniteBoxData>;

struct alignas(16) ToneMappingData final : public IGpuResource {
  struct Param {
    Vec4f params{1.0f, 1.0f, 0.0f, 2.2f};
  };

  enum class Mode : u32 {
    Aces = 0,
    Reinhard = 1,
  };

  void set(bool enabled, float exposure, Mode mode, float gamma) {
    param.params = Vec4f{enabled ? 1.0f : 0.0f, exposure,
                         static_cast<float>(mode), gamma};
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &param; }
  u32 getByteSize() const override { return sizeof(Param); }
  StringID getBindingName() const override {
    static const StringID kName("ToneMappingUBO");
    return kName;
  }

  Param param{};
};

using ToneMappingDataUniquePtr = std::unique_ptr<ToneMappingData>;

struct IblEnvironmentResources {
  bool skyboxEnabled = true;
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
