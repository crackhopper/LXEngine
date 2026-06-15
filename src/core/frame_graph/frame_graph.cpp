#include "core/frame_graph/frame_graph.hpp"

#include "core/frame_graph/graph_resource_registry.hpp"
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace LX_core {
namespace {

[[nodiscard]] StringID
framePassRenderingModeSignature(RenderPathNodeRenderingMode mode) {
  return StringID(mode == RenderPathNodeRenderingMode::Dynamic
                      ? "rendering=dynamic"
                      : "rendering=traditional");
}

[[nodiscard]] StringID
framePassGeometryVertexSignature(RenderPathGeometryVertexContract vertex) {
  switch (vertex) {
  case RenderPathGeometryVertexContract::PositionOnly:
    return StringID("vertex=position-only");
  case RenderPathGeometryVertexContract::PositionNormalUvTangent:
    return StringID("vertex=position-normal-uv-tangent");
  }
  return StringID("vertex=unknown");
}

[[nodiscard]] StringID
framePassGeometrySignature(const RenderPathGeometryContract &geometry) {
  StringID fields[] = {
      framePassGeometryVertexSignature(geometry.vertex),
      topologyPipelineSignature(geometry.topology),
  };
  return GlobalStringTable::get().compose(TypeTag::RenderPathGeometry, fields);
}

[[nodiscard]] StringID
framePassAttachmentSignature(const RenderPathAttachmentContract &attachment) {
  return StringID("attachment:target=" + attachment.target + ";format=" +
                  std::to_string(static_cast<u32>(attachment.format)) +
                  ";samples=" + std::to_string(attachment.samples) +
                  ";layers=" + std::to_string(attachment.layers) +
                  ";depth=" + (attachment.depth ? "true" : "false"));
}

} // namespace

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

StringID getFramePassRenderPathNodeSignature(const FramePass &pass) {
  if (pass.renderPathNodeSignature.id != 0) {
    return pass.renderPathNodeSignature;
  }

  std::vector<StringID> fields;
  fields.reserve(14 + pass.input.object.renderClasses.size() +
                 pass.input.material.types.size() + pass.reads.size() +
                 pass.writes.size() + pass.attachments.size());
  fields.push_back(
      StringID("pass=" + GlobalStringTable::get().toDebugString(pass.name)));
  fields.push_back(StringID("shader=" + pass.shaderUri.string()));
  fields.push_back(StringID(pass.stage == RenderPassStage::Raster
                                ? "stage=raster"
                                : "stage=compute"));
  fields.push_back(StringID(
      pass.dispatch == RenderPassDispatch::Draw         ? "dispatch=draw"
      : pass.dispatch == RenderPassDispatch::Fullscreen ? "dispatch=fullscreen"
                                                        : "dispatch=compute"));
  fields.push_back(pass.renderState.getPipelineSignature());
  fields.push_back(pass.target.getPipelineSignature());
  if (pass.renderingMode.has_value()) {
    fields.push_back(framePassRenderingModeSignature(*pass.renderingMode));
  }
  fields.push_back(
      StringID(pass.input.kind == RenderPassInputKind::SceneRenderables
                   ? "input=scene-renderables"
               : pass.input.kind == RenderPassInputKind::FullscreenTriangle
                   ? "input=fullscreen-triangle"
                   : "input=compute-dispatch"));
  for (const std::string &renderClass : pass.input.object.renderClasses) {
    fields.push_back(StringID("object.renderClass=" + renderClass));
  }
  fields.push_back(StringID(pass.input.material.required
                                ? "material.required=true"
                                : "material.required=false"));
  for (const std::string &type : pass.input.material.types) {
    fields.push_back(StringID("material.type=" + type));
  }
  if (pass.input.geometry.has_value()) {
    fields.push_back(framePassGeometrySignature(*pass.input.geometry));
  }
  for (const FrameGraphRead &read : pass.reads) {
    fields.push_back(StringID(
        "source=" + GlobalStringTable::get().toDebugString(read.resource)));
  }
  for (const FrameGraphWrite &write : pass.writes) {
    fields.push_back(
        StringID("target=" +
                 GlobalStringTable::get().toDebugString(write.resource.name)));
  }
  for (const RenderPathAttachmentContract &attachment : pass.attachments) {
    fields.push_back(framePassAttachmentSignature(attachment));
  }

  return GlobalStringTable::get().compose(TypeTag::RenderPathNode, fields);
}

void syncFramePassAttachmentContractsWithTarget(FramePass &pass) {
  const std::vector<ImageFormat> colorFormats = pass.target.getColorFormats();
  usize colorIndex = 0;
  for (RenderPathAttachmentContract &attachment : pass.attachments) {
    if (attachment.depth) {
      if (pass.target.depthFormat.has_value()) {
        attachment.format = *pass.target.depthFormat;
      }
      continue;
    }
    if (colorIndex < colorFormats.size()) {
      attachment.format = colorFormats[colorIndex];
    }
    ++colorIndex;
  }
  pass.renderPathNodeSignature = {};
}

void FrameGraph::addPass(FramePass pass) {
  m_passes.push_back(std::move(pass));
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
        const auto previousWriteIt = std::find_if(
            previousPass.writes.begin(), previousPass.writes.end(),
            [&](const FrameGraphWrite &candidate) {
              return candidate.resource.name == write.resource.name;
            });
        if (previousWriteIt == previousPass.writes.end() ||
            !sameAllowedWriteMode(*previousWriteIt, write)) {
          out.m_errors.push_back("pass " + debugName(pass.name) +
                                 " writes duplicate resource " + targetName +
                                 " (resource was already written by pass " +
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
    const auto bestIt =
        std::min_element(ready.begin(), ready.end(), [&](usize lhs, usize rhs) {
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

} // namespace LX_core
