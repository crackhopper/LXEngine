#include "core/frame_graph/frame_graph_build_plan.hpp"

#include "core/frame_graph/graph_resource_registry.hpp"
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace LX_core {
namespace {

FrameGraphResourceRef makeWriteRef(const std::string &target) {
  const bool isDepthTarget =
      target.find("depth") != std::string::npos || target.rfind("shadow.", 0) == 0;
  const StringID targetId(target);
  return isDepthTarget ? FrameGraphResourceRef::depthAttachment(targetId)
                       : FrameGraphResourceRef::colorAttachment(targetId);
}

StringID bindingNameForSource(const RenderPassNode &node,
                              const std::string &source) {
  if (node.id == "BloomThreshold" && source == "hdr.color") {
    return StringID("SceneColor");
  }
  if ((node.id == "BloomBlurH" && source == "bloom.threshold") ||
      (node.id == "BloomBlurV" && source == "bloom.blurH")) {
    return StringID("BloomSource");
  }
  if (node.id == "PostProcess") {
    if (source == "hdr.color") {
      return StringID("SceneColor");
    }
    if (source == "bloom.blur") {
      return StringID("BloomColor");
    }
  }
  if (node.id == "DeferredLighting") {
    if (source == "gbuffer.albedoAlpha") {
      return StringID("GBufferAlbedoAlpha");
    }
    if (source == "gbuffer.normalRoughness") {
      return StringID("GBufferNormalRoughness");
    }
    if (source == "gbuffer.material") {
      return StringID("GBufferMaterial");
    }
    if (source == "depth.main") {
      return StringID("GBufferDepth");
    }
  }
  return {};
}

FramePass makeFramePass(const RenderPassNode &node, FrameGraphPhase phase,
                        u32 stableOrder) {
  FramePass pass;
  pass.name = StringID(node.id);
  pass.phase = phase;
  pass.stableOrder = stableOrder;
  pass.reads.reserve(node.sources.size());
  for (const std::string &source : node.sources) {
    pass.reads.push_back(FrameGraphRead::sampled(
        StringID(source), bindingNameForSource(node, source)));
  }
  pass.writes.reserve(node.targets.size());
  for (const std::string &target : node.targets) {
    pass.writes.push_back(FrameGraphWrite{makeWriteRef(target),
                                          node.writeMode});
  }
  pass.shaderUri = node.shaderUri;
  pass.stage = node.stage;
  pass.dispatch = node.dispatch;
  pass.filters = node.filters;
  pass.renderingMode = node.renderingMode;
  pass.geometry = node.geometry;
  pass.attachments = node.attachments;
  pass.renderState = node.renderState;
  pass.renderPathNodeSignature = getRenderPathNodeSignature(node);
  return pass;
}

void validateRenderPathPassNode(const RenderPathGraph &graphAsset,
                                const RenderPassNode &node) {
  const std::string graphName =
      graphAsset.name.empty() ? "<unnamed>" : graphAsset.name;
  const std::string passName = node.id.empty() ? "<unnamed>" : node.id;
  const std::string prefix = "RenderPathGraph '" + graphName + "' pass '" +
                             passName + "'";
  if (node.shaderUri.empty()) {
    throw std::invalid_argument(prefix + " missing shader");
  }
  if (node.sources.empty()) {
    throw std::invalid_argument(prefix + " missing sources");
  }
  if (node.targets.empty()) {
    throw std::invalid_argument(prefix + " missing targets");
  }
}

} // namespace

void validateRenderPathGraphPassSet(
    const RenderPathGraph &graph, const std::vector<StringID> &requiredPasses,
    const std::vector<StringID> &supportedPasses) {
  const std::string graphName = graph.name.empty() ? "<unnamed>" : graph.name;
  const auto passDebugName = [](StringID id) {
    return GlobalStringTable::get().toDebugString(id);
  };

  std::unordered_set<StringID> supported;
  supported.reserve(supportedPasses.size());
  for (StringID pass : supportedPasses) {
    supported.insert(pass);
  }

  std::unordered_set<StringID> seen;
  seen.reserve(graph.passes.size());
  for (const RenderPassNode &node : graph.passes) {
    const StringID passId(node.id);
    if (supported.find(passId) == supported.end()) {
      throw std::invalid_argument("RenderPathGraph '" + graphName +
                                  "' contains unsupported pass '" + node.id +
                                  "'");
    }
    if (!seen.insert(passId).second) {
      throw std::invalid_argument("RenderPathGraph '" + graphName +
                                  "' contains duplicate pass '" + node.id +
                                  "'");
    }
  }

  for (StringID required : requiredPasses) {
    if (seen.find(required) == seen.end()) {
      throw std::invalid_argument("RenderPathGraph '" + graphName +
                                  "' missing required pass '" +
                                  passDebugName(required) + "'");
    }
  }
}

FrameGraph
buildFrameGraphFromRenderPathGraph(const RenderPathGraph &graphAsset,
                                   const GraphResourceRegistry &registry) {
  FrameGraph graph;
  u32 stableOrder = 0;
  for (const RenderPassNode &node : graphAsset.passes) {
    validateRenderPathPassNode(graphAsset, node);
    graph.addPass(
        makeFramePass(node, FrameGraphPhase::Material, stableOrder++));
  }
  const CompiledFrameGraph compiled = graph.compile(registry);
  if (!compiled.isValid()) {
    throw std::invalid_argument("RenderPathGraph '" + graphAsset.name +
                                "' failed FrameGraph validation: " +
                                compiled.errorText());
  }
  return graph;
}

} // namespace LX_core
