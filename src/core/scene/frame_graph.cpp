#include "core/scene/frame_graph.hpp"

#include "core/scene/scene.hpp"
#include <unordered_set>

namespace LX_core {

void FrameGraph::addPass(FramePass pass) {
  m_passes.push_back(std::move(pass));
}

void FrameGraph::buildFromScene(const Scene &scene) {
  // 委托给 RenderQueue::buildFromScene —— 它负责 clearItems / supportsPass 过滤
  /// / scene 级资源合并 / sort。FrameGraph 本身只是 pass 列表的 iteration 入口。
  for (auto &pass : m_passes) {
    pass.queue.buildFromScene(scene, pass.name);
  }
}

std::vector<PipelineBuildInfo>
FrameGraph::collectAllPipelineBuildInfos() const {
  std::unordered_set<PipelineKey, PipelineKey::Hash> seen;
  std::vector<PipelineBuildInfo> out;
  for (const auto &pass : m_passes) {
    for (auto info : pass.queue.collectUniquePipelineBuildInfos()) {
      if (seen.insert(info.key).second)
        out.push_back(std::move(info));
    }
  }
  return out;
}

} // namespace LX_core
