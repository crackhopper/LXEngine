#pragma once

#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/scene/scene.hpp"
#include <vector>

namespace LX_core {

/// 一个 pass 内按 PipelineKey 聚合的 RenderingItem 队列。
/// FrameGraph::buildFromScene 填充每个 FramePass 的 RenderQueue，
/// 预构建时调用 collectUniquePipelineBuildDescs 去重后交给 backend。
class RenderQueue {
public:
  void addItem(RenderingItem item);
  void clearItems();

  /// 稳定排序：相同 pipelineKey 的项相邻，降低 pipeline 切换开销。
  void sort();

  const std::vector<RenderingItem> &getItems() const { return m_items; }

  /// 按 PipelineKey 去重后返回 PipelineBuildDesc 列表。
  std::vector<PipelineBuildDesc> collectUniquePipelineBuildDescs() const;

  /// REQ-009 form: forwards target to Scene::getSceneLevelResources for
  /// per-target camera filtering. Renderable filtering is unchanged (still
  /// by pass alone via IRenderable::supportsPass) — target only affects the
  /// scene-level resource merge step.
  void buildFromScene(const Scene &scene, StringID pass,
                      const RenderTarget &target);

private:
  std::vector<RenderingItem> m_items;
};

} // namespace LX_core
