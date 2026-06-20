#pragma once

#include "core/asset/texture.hpp"
#include "core/math/vec.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/scene/ibl_bake_manifest.hpp"
#include "core/scene/scene_resource_handles.hpp"

#include <cstddef>
#include <memory>
#include <string>
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
        Vec4f{ambientColor.x, ambientColor.y, ambientColor.z, ambientIntensity};
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
    Vec4f rotation{0.0f, 0.0f, 0.0f, 0.0f};
  };

  void set(Vec3f color, float intensity, float rotationRadians) {
    param.colorIntensity = Vec4f{color.x, color.y, color.z, intensity};
    param.rotation = Vec4f{rotationRadians, 0.0f, 0.0f, 0.0f};
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

struct alignas(16) SkyboxData final : public IGpuResource {
  struct Param {
    Vec4f colorIntensity{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4f rotation{0.0f, 0.0f, 0.0f, 0.0f};
  };

  void set(Vec3f color, float intensity, float rotationRadians) {
    param.colorIntensity = Vec4f{color.x, color.y, color.z, intensity};
    param.rotation = Vec4f{rotationRadians, 0.0f, 0.0f, 0.0f};
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &param; }
  u32 getByteSize() const override { return sizeof(Param); }
  StringID getBindingName() const override {
    static const StringID kName("SkyboxUBO");
    return kName;
  }

  Param param{};
};

using SkyboxDataUniquePtr = std::unique_ptr<SkyboxData>;

struct alignas(16) SurfaceLightingData final : public IGpuResource {
  struct alignas(16) Param {
    u32 enableIblLighting = 0;
    float diffuseIblIntensity = 1.0f;
    float specularIblIntensity = 1.0f;
    u32 environmentIblReady = 0;
    u32 standardPbrIblReady = 0;
    u32 _padding0 = 0;
    u32 _padding1 = 0;
    u32 _padding2 = 0;
  };

  void set(bool enableIbl, float diffuseIntensity, float specularIntensity,
           bool environmentReady, bool standardPbrReady) {
    param.enableIblLighting = enableIbl ? 1u : 0u;
    param.diffuseIblIntensity = diffuseIntensity;
    param.specularIblIntensity = specularIntensity;
    param.environmentIblReady = environmentReady ? 1u : 0u;
    param.standardPbrIblReady = standardPbrReady ? 1u : 0u;
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &param; }
  u32 getByteSize() const override { return sizeof(Param); }
  StringID getBindingName() const override {
    static const StringID kName("SurfaceLightingUBO");
    return kName;
  }

  Param param{};
};

using SurfaceLightingDataUniquePtr = std::unique_ptr<SurfaceLightingData>;

static_assert(offsetof(SurfaceLightingData::Param, enableIblLighting) == 0);
static_assert(offsetof(SurfaceLightingData::Param, diffuseIblIntensity) == 4);
static_assert(offsetof(SurfaceLightingData::Param, specularIblIntensity) == 8);
static_assert(offsetof(SurfaceLightingData::Param, environmentIblReady) == 12);
static_assert(offsetof(SurfaceLightingData::Param, standardPbrIblReady) == 16);
static_assert(sizeof(SurfaceLightingData::Param) == 32);

struct alignas(16) ToneMappingData final : public IGpuResource {
  struct Param {
    Vec4f params{1.0f, 1.0f, 0.0f, 2.2f};
  };

  enum class Mode : u32 {
    Aces = 0,
    Reinhard = 1,
  };

  void set(bool enabled, float exposure, Mode mode, float gamma) {
    param.params =
        Vec4f{enabled ? 1.0f : 0.0f, exposure, static_cast<float>(mode), gamma};
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

struct alignas(16) BloomData final : public IGpuResource {
  struct Param {
    Vec4f params{1.0f, 0.0f, 1.0f, 0.0f};
  };

  void set(float threshold, float intensity, float radius) {
    param.params = Vec4f{threshold, intensity, radius, 0.0f};
    setDirty();
  }

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &param; }
  u32 getByteSize() const override { return sizeof(Param); }
  StringID getBindingName() const override {
    static const StringID kName("BloomUBO");
    return kName;
  }

  Param param{};
};

using BloomDataUniquePtr = std::unique_ptr<BloomData>;

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

struct IblDiffuseShPayloadResource final {
  Sh9IrradiancePayload payload;
};

struct IblTexturePayloadResource final {
  CombinedTextureSamplerSharedPtr sampler;
};

struct ActiveIblEnvironmentResources final {
  u64 generation = 0;
  IblDiffuseShHandle diffuseSh;
  IblSpecularPrefilteredCubemapHandle specularPrefilteredCubemap;
  StandardPbrBrdfLutHandle standardPbrBrdfLut;
};

struct IblEnvironmentActivationPayload final {
  u64 generation = 0;
  Sh9IrradiancePayload diffuseSh;
  CombinedTextureSamplerSharedPtr specularPrefilteredCubemap;
  CombinedTextureSamplerSharedPtr standardPbrBrdfLut;
};

struct IblEnvironmentActivationResult final {
  bool ok = false;
  u64 generation = 0;
  std::vector<std::string> diagnostics;
  std::string message;

  [[nodiscard]] static IblEnvironmentActivationResult
  success(u64 activeGeneration) {
    return IblEnvironmentActivationResult{
        .ok = true, .generation = activeGeneration, .message = "activated"};
  }

  [[nodiscard]] static IblEnvironmentActivationResult
  failure(std::vector<std::string> failureDiagnostics) {
    std::string joined;
    for (usize i = 0; i < failureDiagnostics.size(); ++i) {
      if (i != 0u) {
        joined += "; ";
      }
      joined += failureDiagnostics[i];
    }
    return IblEnvironmentActivationResult{
        .diagnostics = std::move(failureDiagnostics),
        .message = std::move(joined),
    };
  }
};

inline IblEnvironmentResources
completeIblEnvironmentResources(IblEnvironmentResources resources) {
  const auto makeFallbackSampler = [](StringID bindingName,
                                      TextureDimension dimension,
                                      TextureFormat format,
                                      TextureContent content) {
    TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = format;
    desc.content = content;
    desc.dimension = dimension;
    desc.mipLevels = 1;
    desc.arrayLayers = dimension == TextureDimension::TextureCube ? 6u : 1u;

    auto sampler = std::make_shared<CombinedTextureSampler>(
        std::make_shared<Texture>(
            desc, std::vector<u8>(expectedTextureByteCount(desc), 0)));
    sampler->setBindingName(bindingName);
    sampler->setDirty();
    return sampler;
  };

  if (!resources.irradianceCubemap) {
    resources.irradianceCubemap =
        makeFallbackSampler(StringID("IrradianceMap"),
                            TextureDimension::TextureCube,
                            TextureFormat::RGBA16Float,
                            TextureContent::Environment);
  }
  if (!resources.prefilteredRadianceCubemap) {
    resources.prefilteredRadianceCubemap =
        makeFallbackSampler(StringID("PrefilteredEnvMap"),
                            TextureDimension::TextureCube,
                            TextureFormat::RGBA16Float,
                            TextureContent::Environment);
  }
  if (!resources.brdfLut) {
    resources.brdfLut =
        makeFallbackSampler(StringID("BrdfLut"), TextureDimension::Texture2D,
                            TextureFormat::RG16Float, TextureContent::Data);
  }
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
