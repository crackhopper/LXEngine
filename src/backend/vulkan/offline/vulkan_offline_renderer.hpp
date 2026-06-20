#pragma once

#include "core/frame_graph/frame_graph_executor.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/offline/offline_render_result.hpp"
#include "core/resource/resource_uri.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "core/scene/scene_render_settings.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace LX_core::backend::offline {

/*
@source_analysis.section Offline render request 是 headless executor 的边界
`VulkanOfflineRenderRequest` 不再表达一套 offline-only job graph。它把
offline CLI/profile resolver 已经决定好的事实交给 Vulkan 后端：scene resource
table、选中的 output profile、离线 runtime 参数、profile 名、输出路径和
RenderPathGraph URI。这样 backend 不需要猜 shader、pass 或 readback 目标；它只需
加载 graph、编译 FrameGraph、准备 pass work，再交给统一 `FrameGraphExecutor`。

`VulkanOfflineRenderResult` 同样保持很薄：executor 的 payload 是真实 readback 合同，
`OfflineReadbackImage` 是 writer 需要的线性 RGBA 图像视图。文件格式、PNG tone
mapping 和 metadata 写出留给 infra/offline writer，而不是混进 Vulkan 执行层。
*/
struct VulkanOfflineRenderRequest final {
  SceneResourceTable scene;
  SceneRenderSettings renderSettings;
  LX_core::offline::OutputProfile output;
  LX_core::offline::OfflineRenderSettings offline;
  std::string profileName;
  std::filesystem::path outputPath;
  ResourceUri renderPathGraphUri{
      "assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml"};
};

struct VulkanOfflineRenderResult final {
  FrameGraphExecutionPayload payload;
  LX_core::offline::OfflineReadbackImage image;
};

class VulkanOfflineRenderer final {
public:
  VulkanOfflineRenderer();
  ~VulkanOfflineRenderer();

  VulkanOfflineRenderer(const VulkanOfflineRenderer &) = delete;
  VulkanOfflineRenderer &operator=(const VulkanOfflineRenderer &) = delete;

  [[nodiscard]] VulkanOfflineRenderResult render(VulkanOfflineRenderRequest request);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace LX_core::backend::offline
