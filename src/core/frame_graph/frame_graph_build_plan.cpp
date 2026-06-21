#include "core/frame_graph/frame_graph_build_plan.hpp"

#include "core/frame_graph/graph_resource_registry.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace LX_core {
namespace {

FrameGraphResourceRef makeWriteRef(const std::string &target) {
  const bool isDepthTarget = target.find("depth") != std::string::npos ||
                             target.rfind("shadow.", 0) == 0;
  const StringID targetId(target);
  return isDepthTarget ? FrameGraphResourceRef::depthAttachment(targetId)
                       : FrameGraphResourceRef::colorAttachment(targetId);
}

StringID bindingNameForSource(const RenderPassNode &node,
                              const std::string &source) {
  if (source == "bake.environment.source") {
    return StringID("BakeEnvironmentSource");
  }
  if (source == "bake.environment.cubemap") {
    return StringID("BakeEnvironmentCubemap");
  }
  if (source == "bake.material.source") {
    return StringID("BakeMaterialSource");
  }
  if (source == "depth.main") {
    for (const auto &attachment : node.attachments) {
      if (attachment.target == source &&
          attachment.attachmentUsage ==
              RenderPathAttachmentUsage::DepthAttachmentReadOnly) {
        return {};
      }
    }
  }
  if (node.id == "BloomThreshold" && source == "hdr.color") {
    return StringID("SceneColor");
  }
  if ((node.id == "BloomBlurH" && source == "bloom.threshold") ||
      (node.id == "BloomBlurV" && source == "bloom.blurH")) {
    return StringID("BloomSource");
  }
  if (node.id == "Bloom" && source == "hdr.color") {
    return StringID("SceneColor");
  }
  if (node.id == "PostProcess") {
    if (source == "hdr.color") {
      return StringID("SceneColor");
    }
    if (source == "bloom.blur") {
      return StringID("BloomColor");
    }
  }
  if ((node.id == "DebugToneMapLinear" && source == "hdr.color") ||
      ((node.id == "DebugSrgbAttachment" ||
        node.id == "DebugUnormManualSrgb") &&
       source == "debug.ldr.linear")) {
    return StringID("SceneColor");
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

[[nodiscard]] RenderTargetDesc
makeTargetDescFromAttachments(const RenderPassNode &node) {
  RenderTargetDesc desc;
  desc.role = std::find(node.targets.begin(), node.targets.end(),
                        "swapchain.color") != node.targets.end()
                  ? RenderTargetRole::Swapchain
                  : RenderTargetRole::Offscreen;
  desc.colorFormat = std::nullopt;
  desc.colorFormats.clear();
  desc.depthFormat = std::nullopt;
  desc.sampleCount = 1;
  desc.layerCount = 1;

  bool firstAttachment = true;
  for (const RenderPathAttachmentContract &attachment : node.attachments) {
    if (firstAttachment) {
      desc.sampleCount = static_cast<u8>(attachment.samples);
      desc.layerCount = attachment.layers;
      firstAttachment = false;
    }
    if (attachment.depth) {
      desc.depthFormat = attachment.format;
      continue;
    }
    desc.colorFormats.push_back(attachment.format);
  }
  if (!desc.colorFormats.empty()) {
    desc.colorFormat = desc.colorFormats.front();
  }
  return desc;
}

[[nodiscard]] bool containsName(const std::vector<std::string> &names,
                                const std::string &name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

void validateAttachmentResourceFlow(const RenderPathGraph &graphAsset,
                                    const RenderPassNode &node) {
  const std::string graphName =
      graphAsset.name.empty() ? "<unnamed>" : graphAsset.name;
  const std::string passName = node.id.empty() ? "<unnamed>" : node.id;
  const std::string prefix =
      "RenderPathGraph '" + graphName + "' pass '" + passName + "'";

  for (const auto &attachment : node.attachments) {
    const bool inSources = containsName(node.sources, attachment.target);
    const bool inTargets = containsName(node.targets, attachment.target);
    switch (attachment.attachmentUsage) {
    case RenderPathAttachmentUsage::ColorAttachmentWrite:
      if (!inTargets) {
        throw std::invalid_argument(prefix + " attachment '" +
                                    attachment.target +
                                    "' must be listed in targets");
      }
      break;
    case RenderPathAttachmentUsage::DepthAttachmentReadOnly:
      if (!inSources) {
        throw std::invalid_argument(prefix + " read-only depth attachment '" +
                                    attachment.target +
                                    "' must be listed in sources");
      }
      if (inTargets) {
        throw std::invalid_argument(prefix + " read-only depth attachment '" +
                                    attachment.target +
                                    "' must not be listed in targets");
      }
      break;
    case RenderPathAttachmentUsage::DepthAttachmentWrite:
      if (!inTargets) {
        throw std::invalid_argument(prefix + " writable depth attachment '" +
                                    attachment.target +
                                    "' must be listed in targets");
      }
      break;
    case RenderPathAttachmentUsage::DepthAttachmentReadWrite:
      if (!inSources || !inTargets) {
        throw std::invalid_argument(prefix + " read-write depth attachment '" +
                                    attachment.target +
                                    "' must be listed in sources and targets");
      }
      break;
    }
  }
}

FramePass makeFramePass(const RenderPathGraph &graphAsset,
                        const RenderPassNode &node, FrameGraphPhase phase,
                        u32 stableOrder) {
  FramePass pass;
  pass.name = StringID(node.id);
  pass.target = makeTargetDescFromAttachments(node);
  pass.phase = phase;
  pass.stableOrder = stableOrder;
  pass.reads.reserve(node.sources.size());
  for (const std::string &source : node.sources) {
    pass.reads.push_back(FrameGraphRead::sampled(
        StringID(source), bindingNameForSource(node, source)));
  }
  pass.writes.reserve(node.targets.size());
  for (const std::string &target : node.targets) {
    pass.writes.push_back(
        FrameGraphWrite{makeWriteRef(target), node.writeMode});
  }
  pass.shaderUri = node.shaderUri;
  pass.stage = node.stage;
  pass.dispatch = node.dispatch;
  pass.compute = node.compute;
  pass.input = node.input;
  pass.renderingMode = node.renderingMode;
  pass.attachments = node.attachments;
  pass.readbacks = node.readbacks;
  pass.renderState = node.renderState;
  pass.renderPathNodeSignature = getRenderPathNodeSignature(node);
  pass.features.reserve(graphAsset.features.size());
  for (const RenderPathFeatureDependency &feature : graphAsset.features) {
    pass.features.push_back(RenderPathFeatureDependency{
        .slot = feature.slot,
        .uri = ResourceUri(feature.uri.string()),
    });
  }
  return pass;
}

void validateRenderPathPassNode(const RenderPathGraph &graphAsset,
                                const RenderPassNode &node) {
  const std::string graphName =
      graphAsset.name.empty() ? "<unnamed>" : graphAsset.name;
  const std::string passName = node.id.empty() ? "<unnamed>" : node.id;
  const std::string prefix =
      "RenderPathGraph '" + graphName + "' pass '" + passName + "'";
  if (node.shaderUri.empty()) {
    throw std::invalid_argument(prefix + " missing shader");
  }
  if (node.sources.empty()) {
    throw std::invalid_argument(prefix + " missing sources");
  }
  if (node.targets.empty()) {
    throw std::invalid_argument(prefix + " missing targets");
  }
  if (const auto inputError = validateRenderPassInputContract(node)) {
    throw std::invalid_argument(prefix + " " + *inputError);
  }
  for (const RenderPathReadbackContract &readback : node.readbacks) {
    if (!containsName(node.targets, readback.target)) {
      throw std::invalid_argument(prefix + " readback '" + readback.name +
                                  "' target '" + readback.target +
                                  "' is not listed in targets");
    }
  }
  validateAttachmentResourceFlow(graphAsset, node);
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
    graph.addPass(makeFramePass(graphAsset, node, FrameGraphPhase::Material,
                                stableOrder++));
  }
  const CompiledFrameGraph compiled = graph.compile(registry);
  if (!compiled.isValid()) {
    throw std::invalid_argument(
        "RenderPathGraph '" + graphAsset.name +
        "' failed FrameGraph validation: " + compiled.errorText());
  }
  return graph;
}

} // namespace LX_core
