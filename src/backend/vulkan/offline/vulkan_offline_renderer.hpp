#pragma once

#include "core/offline/offline_render_job.hpp"

namespace LX_core::backend::offline {

/*
@source_analysis.section VulkanOfflineRenderer 是离线积分器协调入口
`VulkanOfflineRenderer` 是离线渲染实验场当前的 Vulkan 后端入口。它接收
`OfflineRenderJob`，先做 core 层 job 校验，再根据显式 integrator 名称选择离线
积分器。具体 headless Vulkan device、compute pipeline、buffer 上传、dispatch 和
readback 生命周期由被选中的 integrator 管理。

它和 realtime 路径复用 core `FrameGraph` / `RenderWorkItem` / resource table
输入链路；差异收敛在 integrator 的执行目标和 Vulkan headless 管线。离线渲染会在
job 的 `SceneResourceTable` 内建立 render-scope storage/output 资源，所以 render
入口接收可变 job，而不是把临时资源塞进独立旁路。
*/
class VulkanOfflineRenderer final {
public:
  VulkanOfflineRenderer();
  ~VulkanOfflineRenderer();

  VulkanOfflineRenderer(const VulkanOfflineRenderer &) = delete;
  VulkanOfflineRenderer &operator=(const VulkanOfflineRenderer &) = delete;

  [[nodiscard]] LX_core::offline::OfflineReadbackImage
  render(LX_core::offline::OfflineRenderJob &job);
};

} // namespace LX_core::backend::offline
