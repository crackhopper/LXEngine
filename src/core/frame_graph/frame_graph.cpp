#include "core/frame_graph/frame_graph.hpp"

#include "core/scene/scene.hpp"
#include <sstream>
#include <unordered_set>
#include <utility>

namespace LX_core {

FrameGraphResourceRef FrameGraphResourceRef::colorAttachment(StringID name) {
  return FrameGraphResourceRef{name, FrameGraphAttachmentKind::Color};
}

FrameGraphResourceRef FrameGraphResourceRef::depthAttachment(StringID name) {
  return FrameGraphResourceRef{name, FrameGraphAttachmentKind::Depth};
}

FrameGraphRead FrameGraphRead::sampled(StringID resource,
                                       StringID bindingName) {
  return FrameGraphRead{resource, bindingName};
}

bool CompiledFrameGraph::isValid() const { return m_errors.empty(); }

const std::vector<std::string> &CompiledFrameGraph::getErrors() const {
  return m_errors;
}

std::string CompiledFrameGraph::errorText() const {
  std::ostringstream out;
  for (usize i = 0; i < m_errors.size(); ++i) {
    if (i != 0) {
      out << '\n';
    }
    out << m_errors[i];
  }
  return out.str();
}

const std::vector<CompiledFrameGraphPass> &
CompiledFrameGraph::getPasses() const {
  return m_passes;
}

void FrameGraph::addPass(FramePass pass) {
  m_passes.push_back(std::move(pass));
}

void FrameGraph::buildFromScene(const Scene &scene) {
  // REQ-009: delegate with pass.target so Scene::getSceneLevelResources
  // can apply per-target camera filtering. Each FramePass already carries its
  // own target; FrameGraph simply threads it through.
  for (auto &pass : m_passes) {
    pass.queue.buildFromScene(scene, pass.name, RenderTarget{pass.target});
  }
}

CompiledFrameGraph FrameGraph::compile() const {
  CompiledFrameGraph out;
  std::unordered_set<StringID, StringID::Hash> available;
  std::unordered_set<StringID, StringID::Hash> written;

  const auto debugName = [](StringID id) {
    return GlobalStringTable::get().toDebugString(id);
  };

  for (const auto &pass : m_passes) {
    for (const auto &read : pass.reads) {
      if (available.find(read.resource) == available.end()) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " reads missing resource " +
                               debugName(read.resource) +
                               " (resource was not written by an earlier pass)");
      }
    }

    for (const auto &write : pass.writes) {
      if (write.resource.name == StringID{}) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " writes unnamed resource");
        continue;
      }
      if (written.find(write.resource.name) != written.end()) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " writes duplicate resource " +
                               debugName(write.resource.name) +
                               " (resource was already written)");
        continue;
      }
      written.insert(write.resource.name);
      available.insert(write.resource.name);
    }

    out.m_passes.push_back(
        CompiledFrameGraphPass{pass.name, pass.target, pass.reads, pass.writes});
  }

  return out;
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
