#pragma once

#include "core/asset/texture.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/utils/string_table.hpp"

#include <memory>
#include <unordered_map>

#include <vulkan/vulkan.h>

namespace LX_core::backend {

class VulkanCommandBufferManager;
class VulkanDevice;
class VulkanResourceManager;

struct IblBakeSettings {
  CombinedTextureSamplerSharedPtr equirectangularMap;
  u32 skyboxSize = 64;
  u32 irradianceSize = 32;
  u32 prefilterSize = 64;
  u32 prefilterMipCount = 5;
  u32 brdfLutSize = 128;
};

class BakedTextureResource final : public IGpuResource {
public:
  BakedTextureResource(StringID resourceName, StringID bindingName)
      : m_resourceName(resourceName), m_bindingName(bindingName) {}

  ResourceType getType() const override { return ResourceType::Special; }
  const void *getRawData() const override { return nullptr; }
  u32 getByteSize() const override { return 0; }
  StringID getBindingName() const override { return m_bindingName; }
  StringID getResourceName() const { return m_resourceName; }

private:
  StringID m_resourceName;
  StringID m_bindingName;
};

struct IblBakeResult {
  std::shared_ptr<BakedTextureResource> skybox;
  std::shared_ptr<BakedTextureResource> irradiance;
  std::shared_ptr<BakedTextureResource> prefiltered;
  std::shared_ptr<BakedTextureResource> brdfLut;
};

class IblBakeRenderer final {
public:
  IblBakeRenderer(VulkanDevice &device, VulkanResourceManager &resourceManager,
                  VulkanCommandBufferManager &cmdBufferManager);

  IblBakeResult bakeStaticEnvironment(const IblBakeSettings &settings);

  bool debugReadbackCubemapFaceHasData(StringID resourceName, u32 mipLevel,
                                       u32 faceLayer, u32 extent);
  bool debugReadbackBrdfLutHasData(u32 extent);

private:
  void clearCubemap(StringID resourceName, u32 baseSize, u32 mipLevels,
                    float seed);
  void renderEquirectToCubemap(const CombinedTextureSamplerSharedPtr &source,
                               u32 skyboxSize);
  void renderIrradianceCubemap(u32 irradianceSize);
  void renderPrefilterCubemap(u32 prefilterSize, u32 mipLevels);
  void clearBrdfLut(u32 size);
  void transitionBrdfLutToShaderRead();
  void transitionCubemapToShaderRead(StringID resourceName, u32 mipLevels);
  VkImageLayout getTrackedCubemapLayout(StringID resourceName, u32 mipLevel,
                                        u32 faceLayer) const;
  void setTrackedCubemapLayout(StringID resourceName, u32 mipLevel,
                               u32 faceLayer, VkImageLayout layout);

  VulkanDevice &m_device;
  VulkanResourceManager &m_resourceManager;
  VulkanCommandBufferManager &m_cmdBufferManager;
  std::unordered_map<usize, VkImageLayout> m_cubemapLayouts;
};

} // namespace LX_core::backend
