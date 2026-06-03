#pragma once

#include "core/offline/offline_render_job.hpp"

#include <memory>

namespace LX_core::backend::offline {

/*
@source_analysis.section VulkanOfflineRenderer 是 headless compute 执行器
`VulkanOfflineRenderer` 是离线渲染实验场当前的 Vulkan 后端入口。它接收
`OfflineRenderJob`，内部初始化 headless `VulkanDevice`，创建 compute pipeline，
上传 triangle/material/BVH/camera buffer，dispatch compute shader，再把线性
float RGBA readback 回 CPU。

它故意不复用 realtime `FrameGraph`、swapchain 和 draw item，因为离线 renderer 的
目标是可复现实验、ground truth 对比和 path tracing 迭代。共享点放在更低层：
Vulkan device、buffer、command manager、shader 编译产物和 core/infra 的 scene
输入链路。
*/
class VulkanOfflineRenderer final {
public:
  VulkanOfflineRenderer();
  ~VulkanOfflineRenderer();

  VulkanOfflineRenderer(const VulkanOfflineRenderer &) = delete;
  VulkanOfflineRenderer &operator=(const VulkanOfflineRenderer &) = delete;

  [[nodiscard]] LX_core::offline::OfflineReadbackImage
  render(const LX_core::offline::OfflineRenderJob &job);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace LX_core::backend::offline
