#include "core/frame_graph/frame_graph.hpp"

#include "core/scene/scene.hpp"
#include <unordered_set>

namespace LX_core {

void FrameGraph::addPass(FramePass pass) {
  m_passes.push_back(std::move(pass));
}

void FrameGraph::buildFromScene(const Scene &scene) {
  // REQ-009: delegate with pass.target so Scene::getSceneLevelResources
  // can apply per-target camera filtering. Each FramePass already carries its
  // own target; FrameGraph simply threads it through.
  for (auto &pass : m_passes) {
    pass.queue.buildFromScene(scene, pass.name, pass.target);
  }
}

std::vector<PipelineBuildDesc>
FrameGraph::collectAllPipelineBuildDescs() const {
  std::unordered_set<PipelineKey, PipelineKey::Hash> seen;
  std::vector<PipelineBuildDesc> out;
  for (const auto &pass : m_passes) {
    for (auto info : pass.queue.collectUniquePipelineBuildDescs()) {
      if (seen.insert(info.key).second)
        out.push_back(std::move(info));
    }
  }
  return out;
}

} // namespace LX_core
