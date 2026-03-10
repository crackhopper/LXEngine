#pragma once

#include "core/gpu/renderer.hpp"
#include "core/platform/window.hpp"
#include "vk_device.hpp"
#include "vk_material.hpp"

#include "details/vk_descriptors.hpp"
#include "details/vk_swapchain.hpp"
#include "details/vk_pipeline.hpp"
#include "details/vk_resources.hpp"

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
  void createDefaultMaterial();

  VulkanMaterialPtr getOrCreateMaterial(LX_core::MaterialPtr material);

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

  std::shared_ptr<VulkanDescriptorSetLayout> m_cameraSetLayout;
  std::shared_ptr<VulkanDescriptorSetLayout> m_materialSetLayout;
  std::shared_ptr<VulkanPipelineLayout> m_pipelineLayout;
  std::shared_ptr<VulkanGraphicsPipeline> m_graphicsPipeline;

  std::shared_ptr<VulkanDescriptorAllocator> m_descriptorAllocator;

  CameraUBO m_cameraData;
  struct PerFrameData {
    VulkanUniformBuffer cameraUBO;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    PerFrameData(VulkanDeviceWeakPtr device) : cameraUBO(device) {}
  };
  std::vector<PerFrameData> m_frames;

  std::unordered_map<LX_core::Mesh *, VulkanMeshPtr> m_meshMap;
  std::unordered_map<LX_core::Texture *, VulkanTexturePtr> m_textureMap;
  std::unordered_map<LX_core::Material *, VulkanMaterialPtr> m_materialMap;

  VulkanMaterialPtr m_defaultMaterial;

  std::vector<DrawCommand> m_drawCommands;
};

using VulkanRendererPtr = std::shared_ptr<VulkanRenderer>;

} // namespace LX_core::graphic_backend
