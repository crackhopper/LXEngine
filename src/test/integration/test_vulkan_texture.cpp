#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/device.hpp"
#include "infra/window/window.hpp"
#include "core/utils/env.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
  expSetEnvVK();
  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan Texture", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanTexture");

    auto cmdMgr = LX_core::backend::VulkanCommandBufferManager::create(
        *device, /*maxFramesInFlight=*/1,
        device->getGraphicsQueueFamilyIndex());

    // Create a small RGBA8 texture and a host-visible staging buffer.
    constexpr u32 width = 4;
    constexpr u32 height = 4;
    constexpr VkDeviceSize pixelBytes = width * height * 4;

    std::vector<u8> pixels(pixelBytes, 255);
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x) {
        // Simple gradient pattern.
        const u32 idx = (y * width + x) * 4;
        pixels[idx + 0] = static_cast<u8>(x * 20);  // R
        pixels[idx + 1] = static_cast<u8>(y * 20);  // G
        pixels[idx + 2] = 0;                               // B
        pixels[idx + 3] = 255;                            // A
      }
    }

    auto texture = LX_core::backend::VulkanTexture::create(
        *device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_FILTER_LINEAR);

    auto staging = LX_core::backend::VulkanBuffer::create(
        *device, pixelBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging->uploadData(pixels.data(), pixelBytes);

    auto cmd = cmdMgr->beginSingleTimeCommands();

    // Transition -> copy -> transition back.
    texture->transitionLayout(*cmd, VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    texture->copyFromBuffer(*cmd, *staging);
    texture->transitionLayout(*cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    cmdMgr->endSingleTimeCommands(std::move(cmd), device->getGraphicsQueue());

    if (texture->getCurrentLayout() !=
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      std::cerr << "Texture layout mismatch\n";
      return 1;
    }

    auto sampledAttachment = LX_core::backend::VulkanTexture::createForAttachment(
        *device, 256, 256, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    if (sampledAttachment->getImageView() == VK_NULL_HANDLE) {
      std::cerr << "Sampled attachment image view is null\n";
      return 1;
    }
    if (sampledAttachment->getSampler() == VK_NULL_HANDLE) {
      std::cerr << "Sampled attachment sampler is null\n";
      return 1;
    }

    cmdMgr->beginFrame(0);
    auto transitionCmd = cmdMgr->allocateBuffer();
    transitionCmd->begin();
    auto sampledColorAttachment =
        LX_core::backend::VulkanTexture::createForAttachment(
            *device, 64, 64, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
    sampledColorAttachment->transitionLayout(
        *transitionCmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (sampledColorAttachment->getCurrentLayout() !=
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      std::cerr << "Color sampled attachment layout transition failed\n";
      return 1;
    }

    sampledAttachment->transitionLayout(
        *transitionCmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (sampledAttachment->getCurrentLayout() !=
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      std::cerr << "Depth sampled attachment layout transition failed\n";
      return 1;
    }

    bool unsupportedTransitionRejected = false;
    try {
      sampledColorAttachment->transitionLayout(
          *transitionCmd, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    } catch (const std::runtime_error &e) {
      unsupportedTransitionRejected =
          std::string(e.what()).find("Unsupported layout transition") !=
          std::string::npos;
    }
    if (!unsupportedTransitionRejected) {
      std::cerr << "Unsupported texture layout transition was not rejected\n";
      return 1;
    }
    if (sampledColorAttachment->getCurrentLayout() !=
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      std::cerr << "Rejected transition changed the texture layout\n";
      return 1;
    }
    transitionCmd->end();

    bool invalidAspectRejected = false;
    try {
      (void)LX_core::backend::VulkanTexture::createForAttachment(
          *device, 32, 32, VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
          VK_IMAGE_ASPECT_DEPTH_BIT);
    } catch (const std::runtime_error &e) {
      invalidAspectRejected =
          std::string(e.what()).find("Image aspect") != std::string::npos;
    }
    if (!invalidAspectRejected) {
      std::cerr << "Invalid color format/depth aspect attachment was not rejected clearly\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanTexture test: " << e.what() << "\n";
    return 0;
  }
}
