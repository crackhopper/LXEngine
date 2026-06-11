#include "core/frame_graph/frame_graph.hpp"

#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/scene/scene.hpp"
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

void FrameGraph::build(const RenderWorkBuildContext &context) {
  // REQ-009: delegate with pass.target so Scene::getSceneLevelResources
  // can apply per-target camera filtering. Each FramePass already carries its
  // own target; FrameGraph simply threads it through.
  for (auto &pass : m_passes) {
    pass.queue.build(context, pass.name, RenderTarget{pass.target});
  }
}

CompiledFrameGraph
FrameGraph::compile(const GraphResourceRegistry &registry) const {
  CompiledFrameGraph out;
  struct ProducerInfo {
    usize passIndex = 0;
    std::optional<std::string> writeMode;
  };
  std::unordered_map<StringID, std::vector<ProducerInfo>, StringID::Hash>
      producersByTarget;
  std::vector<std::unordered_set<usize>> edges(m_passes.size());
  std::vector<usize> indegree(m_passes.size(), 0);

  const auto debugName = [](StringID id) {
    return GlobalStringTable::get().toDebugString(id);
  };
  const auto addEdge = [&](usize from, usize to) {
    if (from == to) {
      return;
    }
    if (edges[from].insert(to).second) {
      ++indegree[to];
    }
  };
  const auto sameAllowedWriteMode = [&](const FrameGraphWrite &previous,
                                        const FrameGraphWrite &current) {
    if (!previous.writeMode.has_value() || !current.writeMode.has_value()) {
      return false;
    }
    if (*previous.writeMode != *current.writeMode) {
      return false;
    }
    return registry.allowsWriteMode(debugName(current.resource.name),
                                    *current.writeMode);
  };
  const auto passWritesResource = [](const FramePass &pass, StringID resource) {
    return std::any_of(pass.writes.begin(), pass.writes.end(),
                       [&](const FrameGraphWrite &write) {
                         return write.resource.name == resource;
                       });
  };

  for (usize passIndex = 0; passIndex < m_passes.size(); ++passIndex) {
    const auto &pass = m_passes[passIndex];
    std::unordered_set<StringID, StringID::Hash> localWrites;
    for (const auto &write : pass.writes) {
      if (write.resource.name == StringID{}) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " writes unnamed resource");
        continue;
      }

      const std::string targetName = debugName(write.resource.name);
      if (!localWrites.insert(write.resource.name).second) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " writes duplicate target " + targetName +
                               " within the same pass");
        continue;
      }
      if (!registry.contains(targetName)) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " writes unknown target " + targetName);
        continue;
      }
      if (registry.isImported(targetName)) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " writes imported target " + targetName +
                               " (imported resources are source-only)");
        continue;
      }
      if (write.writeMode.has_value() &&
          !registry.allowsWriteMode(targetName, *write.writeMode)) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " writes target " + targetName +
                               " with illegal write mode " + *write.writeMode);
        continue;
      }

      auto &producers = producersByTarget[write.resource.name];
      if (!producers.empty()) {
        const auto &previousProducer = producers.back();
        const auto &previousPass = m_passes[previousProducer.passIndex];
        const auto previousWriteIt =
            std::find_if(previousPass.writes.begin(), previousPass.writes.end(),
                         [&](const FrameGraphWrite &candidate) {
                           return candidate.resource.name ==
                                  write.resource.name;
                         });
        if (previousWriteIt == previousPass.writes.end() ||
            !sameAllowedWriteMode(*previousWriteIt, write)) {
          out.m_errors.push_back(
              "pass " + debugName(pass.name) + " writes duplicate resource " +
              targetName + " (resource was already written by pass " +
              debugName(previousPass.name) + ")");
          continue;
        }
      }

      producers.push_back(ProducerInfo{passIndex, write.writeMode});
    }
  }

  for (usize passIndex = 0; passIndex < m_passes.size(); ++passIndex) {
    const auto &pass = m_passes[passIndex];
    for (const auto &read : pass.reads) {
      const std::string sourceName = debugName(read.resource);
      if (!registry.contains(sourceName)) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " reads unknown source " + sourceName);
        continue;
      }
      if (registry.isImported(sourceName)) {
        continue;
      }
      const auto producerIt = producersByTarget.find(read.resource);
      if (producerIt == producersByTarget.end() || producerIt->second.empty()) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " reads missing resource " + sourceName +
                               " (source has no producer)");
        continue;
      }

      // This resolves DAG producers from declarations, not an execution-time
      // "previous pass". Out-of-order declarations may depend on future
      // writers, while read/write feedback in one pass still needs another
      // contributing writer to avoid treating the pass as its own producer.
      const bool readsOwnWrite = passWritesResource(pass, read.resource);
      bool hasExternalProducer = false;
      for (const auto &producer : producerIt->second) {
        if (producer.passIndex == passIndex) {
          continue;
        }
        hasExternalProducer = true;
        addEdge(producer.passIndex, passIndex);
      }
      if (!hasExternalProducer && readsOwnWrite) {
        out.m_errors.push_back("pass " + debugName(pass.name) +
                               " reads missing resource " + sourceName +
                               " (source has no producer)");
      }
    }
  }

  if (!out.m_errors.empty()) {
    return out;
  }

  const auto phaseRank = [](FrameGraphPhase phase) {
    switch (phase) {
    case FrameGraphPhase::PreEffect:
      return 0u;
    case FrameGraphPhase::Material:
      return 1u;
    case FrameGraphPhase::PostEffect:
      return 2u;
    case FrameGraphPhase::Debug:
      return 3u;
    }
    return 1u;
  };

  for (usize from = 0; from < m_passes.size(); ++from) {
    for (usize to = 0; to < m_passes.size(); ++to) {
      if (phaseRank(m_passes[from].phase) < phaseRank(m_passes[to].phase)) {
        addEdge(from, to);
      }
    }
  }

  std::vector<usize> ready;
  ready.reserve(m_passes.size());
  for (usize i = 0; i < indegree.size(); ++i) {
    if (indegree[i] == 0) {
      ready.push_back(i);
    }
  }

  std::vector<usize> sorted;
  sorted.reserve(m_passes.size());
  const auto isBefore = [&](usize lhs, usize rhs) {
    const auto &left = m_passes[lhs];
    const auto &right = m_passes[rhs];
    const auto leftPhase = phaseRank(left.phase);
    const auto rightPhase = phaseRank(right.phase);
    if (leftPhase != rightPhase) {
      return leftPhase < rightPhase;
    }
    if (left.stableOrder != right.stableOrder) {
      return left.stableOrder < right.stableOrder;
    }
    return lhs < rhs;
  };

  while (!ready.empty()) {
    const auto bestIt = std::min_element(ready.begin(), ready.end(),
                                         [&](usize lhs, usize rhs) {
                                           return isBefore(lhs, rhs);
                                         });
    const usize passIndex = *bestIt;
    ready.erase(bestIt);
    sorted.push_back(passIndex);

    for (const usize consumer : edges[passIndex]) {
      --indegree[consumer];
      if (indegree[consumer] == 0) {
        ready.push_back(consumer);
      }
    }
  }

  if (sorted.size() != m_passes.size()) {
    std::ostringstream cycle;
    cycle << "frame graph cycle detected among passes:";
    for (usize i = 0; i < m_passes.size(); ++i) {
      if (indegree[i] > 0) {
        cycle << ' ' << debugName(m_passes[i].name);
      }
    }
    out.m_errors.push_back(cycle.str());
    return out;
  }

  for (const usize passIndex : sorted) {
    const auto &pass = m_passes[passIndex];
    out.m_passes.push_back(CompiledFrameGraphPass{
        pass.name, pass.target, pass.reads, pass.writes, passIndex});
  }

  return out;
}

CompiledFrameGraph FrameGraph::compile() const {
  return compile(GraphResourceRegistry::makeDefault());
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
