#pragma once
#include <vulkan/vulkan.h>

VkVertexInputBindingDescription getBindingDescription();
std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();