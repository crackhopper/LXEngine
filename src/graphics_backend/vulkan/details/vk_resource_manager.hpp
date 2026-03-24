#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include "core/gpu/render_resource.hpp"
#include "pipelines/vkp_pipeline.hpp"

#include <vulkan/vulkan.h>

namespace LX_core::graphic_backend {

class VulkanDevice;
class VulkanCommandBufferManager;
class VulkanRenderPass;
class VulkanBuffer;
class VulkanTexture;
class VulkanShader;

using VulkanBufferPtr = std::unique_ptr<VulkanBuffer>;
using VulkanTexturePtr = std::unique_ptr<VulkanTexture>;
using VulkanShaderPtr = std::unique_ptr<VulkanShader>;

using VulkanAnyResource =
    std::variant<VulkanBufferPtr, VulkanTexturePtr, VulkanShaderPtr>;


class VulkanResourceManager;
using VulkanResourceManagerPtr = std::unique_ptr<VulkanResourceManager>;
class VulkanResourceManager {
  struct Token {};

public:
  explicit VulkanResourceManager(Token token, VulkanDevice &device);
  ~VulkanResourceManager();

  static VulkanResourceManagerPtr create(VulkanDevice &device){
    auto p = std::make_unique<VulkanResourceManager>(Token{}, device);
    return p;
  }

  // 禁止拷贝
  VulkanResourceManager(const VulkanResourceManager &) = delete;
  VulkanResourceManager &operator=(const VulkanResourceManager &) = delete;

  void syncResource(const IRenderResourcePtr &cpuRes);
  void collectGarbage();

  void initializeRenderPassAndPipeline(VkSurfaceFormatKHR surfaceFormat, VkFormat depthFormat);

  // 快捷访问接口
  VulkanBuffer* getBuffer(void *handle);
  VulkanTexture* getTexture(void *handle);
  VulkanShader* getShader(void *handle);
  VulkanRenderPass* getRenderPass();
  VulkanPipelineBase* getRenderPipeline();

  // Needed for GPU-side uploads that require transient command buffers (e.g. textures).
  void setCommandBufferManager(VulkanCommandBufferManager &mgr) {
    m_cmdBufferMgr = &mgr;
  }

private:
  // 内部创建与更新逻辑
  std::shared_ptr<VulkanAnyResource>
  createGpuResource(const IRenderResourcePtr &cpuRes);
  void updateGpuResource(std::shared_ptr<VulkanAnyResource> &gpuRes,
                         const IRenderResourcePtr &cpuRes);

  VulkanDevice &m_device;
  std::unordered_map<void *, std::shared_ptr<VulkanAnyResource>> m_gpuResources;
  std::unordered_set<void *> m_activeHandles;

  // 管理若干个render pass和pipeline
  // TODO: 暂时仅支持了1个
  VulkanRenderPass *m_renderPass = nullptr;
  VulkanPipelineBase *m_pipeline = nullptr;

  VulkanCommandBufferManager *m_cmdBufferMgr = nullptr;
};

} // namespace LX_core::graphic_backend