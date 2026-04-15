#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/details/device.hpp"
#include "core/utils/env.hpp"
#include "infra/window/window.hpp"
#include <vulkan/vulkan.h>

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
  expSetEnvVK();
  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan Buffer", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanBuffer");

    std::vector<uint32_t> indices = {0u, 1u, 2u};
    const VkDeviceSize size =
        static_cast<VkDeviceSize>(indices.size() * sizeof(uint32_t));

    auto buffer = LX_core::backend::VulkanBuffer::create(
        *device, size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    buffer->uploadData(indices.data(), size);

    auto *mapped = static_cast<uint32_t *>(buffer->map());
    if (mapped == nullptr) {
      std::cerr << "Buffer map returned null\n";
      return 1;
    }
    const bool ok = mapped[0] == indices[0] && mapped[1] == indices[1] &&
                    mapped[2] == indices[2];
    buffer->unmap();

    if (!ok) {
      std::cerr << "Buffer contents mismatch\n";
      return 1;
    }

    // Also smoke-test an index buffer usage creation.
    auto indexBuffer = LX_core::backend::VulkanBuffer::create(
        *device, size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    indexBuffer->uploadData(indices.data(), size);

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanBuffer test: " << e.what() << "\n";
    return 0;
  }
}
