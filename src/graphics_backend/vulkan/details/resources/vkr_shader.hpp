#pragma once
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core {
namespace graphic_backend {

class VulkanDevice;
class VulkanShader;
using VulkanShaderPtr = std::unique_ptr<VulkanShader>;

class VulkanShader {
  struct Token {};

public:
  VulkanShader(Token, VulkanDevice &device, const std::string &name,
               VkShaderStageFlagBits stage);
  ~VulkanShader();

  static VulkanShaderPtr create(VulkanDevice &device,
                                const std::string &name,
                                VkShaderStageFlagBits stage) {
    return std::make_unique<VulkanShader>(Token{}, device, name, stage);
  }

  VkPipelineShaderStageCreateInfo getStageCreateInfo() const;
  VkShaderStageFlagBits getStage() const { return m_stage; }
  VkShaderModule getHandle() const { return m_module; }

private:
  std::vector<char> loadShaderFromFile(const std::string &name) const;

  VkDevice m_device = VK_NULL_HANDLE;
  VkShaderModule m_module = VK_NULL_HANDLE;
  VkShaderStageFlagBits m_stage;
  std::string m_entryPoint = "main";
};

} // namespace graphic_backend
} // namespace LX_core