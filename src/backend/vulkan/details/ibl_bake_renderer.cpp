#include "ibl_bake_renderer.hpp"

#include "commands/command_buffer_manager.hpp"
#include "device.hpp"
#include "device_resources/buffer.hpp"
#include "device_resources/texture.hpp"
#include "render_objects/framebuffer.hpp"
#include "render_objects/render_pass.hpp"
#include "resource_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace LX_core::backend {
namespace {

constexpr VkFormat kBakeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

void transitionSubresource(VkCommandBuffer cmd, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkAccessFlags srcAccessMask,
                           VkAccessFlags dstAccessMask,
                           VkPipelineStageFlags srcStage,
                           VkPipelineStageFlags dstStage, u32 mipLevel,
                           u32 faceLayer) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcAccessMask = srcAccessMask;
  barrier.dstAccessMask = dstAccessMask;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = mipLevel;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = faceLayer;
  barrier.subresourceRange.layerCount = 1;
  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
}

void transitionTexture2D(VkCommandBuffer cmd, VkImage image,
                         VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkAccessFlags srcAccessMask,
                         VkAccessFlags dstAccessMask,
                         VkPipelineStageFlags srcStage,
                         VkPipelineStageFlags dstStage) {
  transitionSubresource(cmd, image, oldLayout, newLayout, srcAccessMask,
                        dstAccessMask, srcStage, dstStage, 0, 0);
}

bool bufferHasNonZeroByte(const void *mapped, VkDeviceSize byteSize) {
  const auto *bytes = static_cast<const unsigned char *>(mapped);
  return std::any_of(bytes, bytes + static_cast<std::size_t>(byteSize),
                     [](unsigned char value) { return value != 0; });
}

u32 mipExtent(u32 baseSize, u32 mipLevel) {
  return std::max(baseSize >> mipLevel, 1u);
}

} // namespace

IblBakeRenderer::IblBakeRenderer(VulkanDevice &device,
                                 VulkanResourceManager &resourceManager,
                                 VulkanCommandBufferManager &cmdBufferManager)
    : m_device(device), m_resourceManager(resourceManager),
      m_cmdBufferManager(cmdBufferManager) {}

IblBakeResult
IblBakeRenderer::bakeStaticEnvironment(const IblBakeSettings &settings) {
  const VkImageUsageFlags cubeUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
      VK_IMAGE_USAGE_SAMPLED_BIT;
  m_resourceManager.createOrGetCubemapBakeAttachment(
      StringID("SkyboxMap"), VkExtent2D{settings.skyboxSize, settings.skyboxSize},
      kBakeFormat, 1, cubeUsage);
  m_resourceManager.createOrGetCubemapBakeAttachment(
      StringID("IrradianceMap"),
      VkExtent2D{settings.irradianceSize, settings.irradianceSize}, kBakeFormat,
      1, cubeUsage);
  m_resourceManager.createOrGetCubemapBakeAttachment(
      StringID("PrefilteredEnvMap"),
      VkExtent2D{settings.prefilterSize, settings.prefilterSize}, kBakeFormat,
      settings.prefilterMipCount, cubeUsage);
  m_resourceManager.createOrGetFrameGraphAttachment(
      StringID("BrdfLut"), VkExtent2D{settings.brdfLutSize, settings.brdfLutSize},
      kBakeFormat, VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT);

  clearCubemap(StringID("SkyboxMap"), settings.skyboxSize, 1, 0.25f);
  clearCubemap(StringID("IrradianceMap"), settings.irradianceSize, 1, 0.5f);
  clearCubemap(StringID("PrefilteredEnvMap"), settings.prefilterSize,
               settings.prefilterMipCount, 0.75f);
  clearBrdfLut(settings.brdfLutSize);

  IblBakeResult result{
      .skybox = std::make_shared<BakedTextureResource>(StringID("SkyboxMap"),
                                                       StringID("SkyboxMap")),
      .irradiance = std::make_shared<BakedTextureResource>(
          StringID("IrradianceMap"), StringID("IrradianceMap")),
      .prefiltered = std::make_shared<BakedTextureResource>(
          StringID("PrefilteredEnvMap"), StringID("PrefilteredEnvMap")),
      .brdfLut = std::make_shared<BakedTextureResource>(StringID("BrdfLut"),
                                                        StringID("BrdfLut")),
  };
  m_resourceManager.aliasCubemapBakeTextureResource(result.skybox,
                                                   StringID("SkyboxMap"));
  m_resourceManager.aliasCubemapBakeTextureResource(result.irradiance,
                                                   StringID("IrradianceMap"));
  m_resourceManager.aliasCubemapBakeTextureResource(
      result.prefiltered, StringID("PrefilteredEnvMap"));
  m_resourceManager.aliasFrameGraphTextureResource(result.brdfLut,
                                                  StringID("BrdfLut"));
  return result;
}

void IblBakeRenderer::clearCubemap(StringID resourceName, u32 baseSize,
                                   u32 mipLevels, float seed) {
  auto attachmentOpt = m_resourceManager.getCubemapBakeAttachment(resourceName);
  if (!attachmentOpt.has_value()) {
    throw std::runtime_error("missing cubemap bake attachment");
  }
  auto &attachment = attachmentOpt->get();
  auto renderPass = VulkanRenderPass::create(
      m_device, std::optional<VkFormat>{attachment.format}, std::nullopt,
      false);

  auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
  for (u32 mip = 0; mip < mipLevels; ++mip) {
    const u32 extentValue = mipExtent(baseSize, mip);
    const VkExtent2D extent{extentValue, extentValue};
    for (u32 face = 0; face < 6u; ++face) {
      auto &view =
          m_resourceManager.getOrCreateCubemapBakeSubresourceView(resourceName,
                                                                  mip, face);
      auto framebuffer = VulkanFrameBuffer::create(
          m_device, renderPass->getHandle(),
          std::vector<VkImageView>{view.getHandle()}, extent);

      transitionSubresource(cmd->getHandle(), attachment.texture->getHandle(),
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, mip,
                            face);
      auto clearValues = renderPass->getClearValues();
      const float faceValue = static_cast<float>(face + 1u) / 6.0f;
      const float mipValue =
          static_cast<float>(mip + 1u) / static_cast<float>(mipLevels);
      clearValues[0].color = {seed, faceValue, mipValue, 1.0f};
      cmd->beginRenderPass(renderPass->getHandle(), framebuffer->getHandle(),
                           extent, clearValues);
      cmd->endRenderPass();
    }
  }
  m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());
}

void IblBakeRenderer::clearBrdfLut(u32 size) {
  auto attachmentOpt = m_resourceManager.getFrameGraphAttachment(
      StringID("BrdfLut"));
  if (!attachmentOpt.has_value()) {
    throw std::runtime_error("missing BRDF LUT bake attachment");
  }
  auto &attachment = attachmentOpt->get();
  auto renderPass = VulkanRenderPass::create(
      m_device, std::optional<VkFormat>{attachment.format}, std::nullopt,
      false);
  auto framebuffer = VulkanFrameBuffer::create(
      m_device, renderPass->getHandle(),
      std::vector<VkImageView>{attachment.texture->getImageView()},
      VkExtent2D{size, size});

  auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
  transitionTexture2D(cmd->getHandle(), attachment.texture->getHandle(),
                      VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  auto clearValues = renderPass->getClearValues();
  clearValues[0].color = {1.0f, 0.04f, 0.0f, 1.0f};
  cmd->beginRenderPass(renderPass->getHandle(), framebuffer->getHandle(),
                       VkExtent2D{size, size}, clearValues);
  cmd->endRenderPass();
  m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());
  attachment.currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

bool IblBakeRenderer::debugReadbackCubemapFaceHasData(StringID resourceName,
                                                      u32 mipLevel,
                                                      u32 faceLayer,
                                                      u32 extent) {
  auto attachmentOpt = m_resourceManager.getCubemapBakeAttachment(resourceName);
  if (!attachmentOpt.has_value()) {
    return false;
  }
  auto &attachment = attachmentOpt->get();
  const VkDeviceSize byteSize = static_cast<VkDeviceSize>(extent) *
                                static_cast<VkDeviceSize>(extent) * 8u;
  auto readback = VulkanBuffer::create(
      m_device, byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  auto cmd = m_cmdBufferManager.beginSingleTimeCommands();
  transitionSubresource(cmd->getHandle(), attachment.texture->getHandle(),
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, mipLevel, faceLayer);

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = mipLevel;
  region.imageSubresource.baseArrayLayer = faceLayer;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {extent, extent, 1};
  vkCmdCopyImageToBuffer(cmd->getHandle(), attachment.texture->getHandle(),
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback->getHandle(), 1, &region);
  m_cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                           m_device.getGraphicsQueue());

  const void *mapped = readback->map();
  const bool hasData = bufferHasNonZeroByte(mapped, byteSize);
  readback->unmap();
  return hasData;
}

} // namespace LX_core::backend
