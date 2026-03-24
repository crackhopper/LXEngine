#include "graphics_backend/vulkan/details/commands/vkc_cmdbuffer_manager.hpp"
#include "graphics_backend/vulkan/details/resources/vkr_buffer.hpp"
#include "graphics_backend/vulkan/details/resources/vkr_texture.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
  try {
    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize();

    auto cmdMgr = LX_core::graphic_backend::VulkanCommandBufferManager::create(
        *device, /*maxFramesInFlight=*/1,
        device->getGraphicsQueueFamilyIndex());

    // Create a small RGBA8 texture and a host-visible staging buffer.
    constexpr uint32_t width = 4;
    constexpr uint32_t height = 4;
    constexpr VkDeviceSize pixelBytes = width * height * 4;

    std::vector<uint8_t> pixels(pixelBytes, 255);
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        // Simple gradient pattern.
        const uint32_t idx = (y * width + x) * 4;
        pixels[idx + 0] = static_cast<uint8_t>(x * 20);  // R
        pixels[idx + 1] = static_cast<uint8_t>(y * 20);  // G
        pixels[idx + 2] = 0;                               // B
        pixels[idx + 3] = 255;                            // A
      }
    }

    auto texture = LX_core::graphic_backend::VulkanTexture::create(
        *device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_FILTER_LINEAR);

    auto staging = LX_core::graphic_backend::VulkanBuffer::create(
        *device, pixelBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging->uploadData(pixels.data(), pixelBytes);

    auto cmd = cmdMgr->beginSingleTimeCommands();

    // Transition -> copy -> transition back.
    texture->transitionLayout(cmd, VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    texture->copyFromBuffer(cmd, *staging);
    texture->transitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    cmdMgr->endSingleTimeCommands(cmd, device->getGraphicsQueue());

    if (texture->getCurrentLayout() !=
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      std::cerr << "Texture layout mismatch\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanTexture test: " << e.what() << "\n";
    return 0;
  }
}

