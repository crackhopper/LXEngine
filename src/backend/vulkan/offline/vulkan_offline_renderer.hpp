#pragma once

#include "core/offline/offline_render_job.hpp"

namespace LX_core::backend::offline {

/*
@source_analysis.section VulkanOfflineRenderer 是离线积分器协调入口
`VulkanOfflineRenderer` 是离线渲染实验场当前的 Vulkan 后端入口。它接收
`OfflineRenderJob`，先做 core 层 job 校验，再根据显式 integrator 名称选择离线
积分器。具体 headless Vulkan device、compute pipeline、buffer 上传、dispatch 和
readback 生命周期由被选中的 integrator 管理。

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
};

} // namespace LX_core::backend::offline
