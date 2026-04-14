#pragma once

#include "core/resources/pipeline_build_info.hpp"
#include "core/scene/scene.hpp"
#include <vector>

namespace LX_core {

/// 一个 pass 内按 PipelineKey 聚合的 RenderingItem 队列。
/// FrameGraph::buildFromScene 填充每个 FramePass 的 RenderQueue，
/// 预构建时调用 collectUniquePipelineBuildInfos 去重后交给 backend。
class RenderQueue {
public:
  void addItem(RenderingItem item);
  void clearItems();

  /// 稳定排序：相同 pipelineKey 的项相邻，降低 pipeline 切换开销。
  void sort();

  const std::vector<RenderingItem> &getItems() const { return m_items; }

  /// 按 PipelineKey 去重后返回 PipelineBuildInfo 列表。
  std::vector<PipelineBuildInfo> collectUniquePipelineBuildInfos() const;

  /// 从 scene 构建 queue 里所有 RenderingItem。流程：
  ///   1. clearItems()
  ///   2. 拉取 scene.getSceneLevelResources() 一次
  ///   3. 遍历 scene.getRenderables()，跳过 null + !supportsPass(pass)
  ///   4. 为每个匹配项构造 RenderingItem，末尾追加 scene 级资源
  ///   5. sort() 按 PipelineKey 稳定排序
  ///
  /// RenderingItem 的构造职责本来住在 Scene::buildRenderingItemForRenderable，
  /// REQ-008 把它迁到这里：Scene 退化为纯数据容器，queue 负责物料聚合。
  void buildFromScene(const Scene &scene, StringID pass);

private:
  std::vector<RenderingItem> m_items;
};

} // namespace LX_core
