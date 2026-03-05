#pragma once
#include "../resources.hpp"
#include <vulkan/vulkan.h>
namespace LX_core {
class VulkanIndexBuffer : public IndexBuffer {
public:

private:
  VkBuffer indexBuffer;
  VkDeviceMemory indexBufferMemory;
};
} // namespace LX_graphics