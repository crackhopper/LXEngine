#pragma once

#include "core/gpu/renderer.hpp"
#include "core/platform/window.hpp"
#include "vk_descriptor_allocator.hpp"
#include "vk_device.hpp"
#include "vk_framebuffer.hpp"
#include "vk_pipeline.hpp"
#include "vk_resources.hpp"
#include "vk_swapchain.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core::graphic_backend {

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct CameraUBO {
  LX_core::Mat4f view;
  LX_core::Mat4f proj;
};

struct DrawCommand {
  LX_core::MeshPtr mesh;
  LX_core::MaterialPtr material;
};

class VulkanRenderer : public LX_core::gpu::Renderer {
public:
  explicit VulkanRenderer(LX_core::WindowPtr window);
  ~VulkanRenderer() override;

  void initialize() override;
  void shutdown() override;

  void uploadMesh(LX_core::MeshPtr mesh) override;
  void uploadTexture(LX_core::TexturePtr texture) override;

  void setCamera(const LX_core::Mat4f &view,
                 const LX_core::Mat4f &proj) override;
  void drawMesh(LX_core::MeshPtr mesh,
                LX_core::MaterialPtr material) override;

  void flush() override;

private:
  void createSurface();
  void createRenderPass();
  void createFramebuffers();
  void createCommandBuffers();
  void createSyncObjects();
  void createDefaultPipeline();
  void createCameraResources();

  uint32_t findMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties);
  void createVkBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer &buffer,
                      VkDeviceMemory &memory);
  void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
  VkCommandBuffer beginSingleTimeCommands();
  void endSingleTimeCommands(VkCommandBuffer cmd);
  void createVkImage(uint32_t w, uint32_t h, VkFormat format,
                     VkImageUsageFlags usage, VkImage &image,
                     VkDeviceMemory &memory);
  void transitionImageLayout(VkImage image, VkImageLayout oldLayout,
                             VkImageLayout newLayout);
  void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w,
                         uint32_t h);

  void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);

private:
  LX_core::WindowPtr p_window;

  VulkanDevicePtr m_device;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VulkanSwapchain m_swapchain;

  VkRenderPass m_renderPass = VK_NULL_HANDLE;
  std::vector<VulkanFramebuffer> m_framebuffers;
  std::vector<VkCommandBuffer> m_commandBuffers;

  std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_imageAvailableSems{};
  std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_renderFinishedSems{};
  std::array<VkFence, MAX_FRAMES_IN_FLIGHT> m_inFlightFences{};
  uint32_t m_currentFrame = 0;

  std::shared_ptr<VulkanDescriptorSetLayout> m_descriptorSetLayout;
  std::shared_ptr<VulkanPipelineLayout> m_pipelineLayout;
  std::shared_ptr<VulkanGraphicsPipeline> m_graphicsPipeline;

  std::shared_ptr<VulkanDescriptorAllocator> m_descriptorAllocator;

  CameraUBO m_cameraData;
  struct PerFrameData {
    VkBuffer cameraBuffer = VK_NULL_HANDLE;
    VkDeviceMemory cameraMemory = VK_NULL_HANDLE;
    void *cameraMapped = nullptr;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  };
  std::array<PerFrameData, MAX_FRAMES_IN_FLIGHT> m_frames;

  std::unordered_map<LX_core::Mesh *, std::shared_ptr<VulkanMesh>> m_meshMap;
  std::unordered_map<LX_core::Texture *, std::shared_ptr<VulkanTexture>>
      m_textureMap;

  std::vector<DrawCommand> m_drawCommands;
};

using VulkanRendererPtr = std::shared_ptr<VulkanRenderer>;

} // namespace LX_core::graphic_backend
