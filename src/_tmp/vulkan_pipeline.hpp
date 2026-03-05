#pragma once
#include "../pipeline_system.hpp"
#include <vulkan/vulkan.h>

namespace LX_core {
class VulkanPipeline : public Pipeline {
public:

  VkPipeline getHandle() const { return graphicsPipeline; }
private:
  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;
};
} // namespace LX_graphics
