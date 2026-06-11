#include "core/frame_graph/frame_graph_build_plan.hpp"

#include "core/frame_graph/graph_resource_registry.hpp"
#include <stdexcept>
#include <string>

namespace LX_core {
namespace {

FrameGraphResourceRef makeWriteRef(const std::string &target) {
  const bool isDepthTarget =
      target.find("depth") != std::string::npos || target.rfind("shadow.", 0) == 0;
  const StringID targetId(target);
  return isDepthTarget ? FrameGraphResourceRef::depthAttachment(targetId)
                       : FrameGraphResourceRef::colorAttachment(targetId);
}

FramePass makeFramePass(const MaterialPassContract &contract,
                        FrameGraphPhase phase, u32 stableOrder) {
  FramePass pass;
  pass.name = StringID(contract.name);
  pass.phase = phase;
  pass.stableOrder = stableOrder;
  pass.reads.reserve(contract.sources.size());
  for (const std::string &source : contract.sources) {
    pass.reads.push_back(FrameGraphRead::sampled(StringID(source)));
  }
  pass.writes.reserve(contract.targets.size());
  for (const std::string &target : contract.targets) {
    pass.writes.push_back(FrameGraphWrite{makeWriteRef(target),
                                          contract.writeMode});
  }
  return pass;
}

FramePass makeFramePass(const RenderPassNode &node, FrameGraphPhase phase,
                        u32 stableOrder) {
  FramePass pass;
  pass.name = StringID(node.id);
  pass.phase = phase;
  pass.stableOrder = stableOrder;
  pass.reads.reserve(node.sources.size());
  for (const std::string &source : node.sources) {
    pass.reads.push_back(FrameGraphRead::sampled(StringID(source)));
  }
  pass.writes.reserve(node.targets.size());
  for (const std::string &target : node.targets) {
    pass.writes.push_back(FrameGraphWrite{makeWriteRef(target),
                                          node.writeMode});
  }
  return pass;
}

void appendTechniquePasses(FrameGraph &graph, const MaterialTechnique &technique,
                           FrameGraphPhase phase, u32 &stableOrder) {
  for (const MaterialPassContract &contract : technique.passes) {
    graph.addPass(makeFramePass(contract, phase, stableOrder++));
  }
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

FrameGraph buildFrameGraphFromSourceTargetContracts(
    const FrameGraphBuildPlanInput &input,
    const GraphResourceRegistry &registry) {
  (void)registry;

  FrameGraph graph;
  u32 stableOrder = 0;
  for (const FrameGraphEffectInput &effect : input.preEffects) {
    appendTechniquePasses(graph, effect.technique, FrameGraphPhase::PreEffect,
                          stableOrder);
  }
  for (const FrameGraphMaterialTechniqueInput &material :
       input.materialTechniques) {
    appendTechniquePasses(graph, material.technique, FrameGraphPhase::Material,
                          stableOrder);
  }
  for (const FrameGraphEffectInput &effect : input.postEffects) {
    appendTechniquePasses(graph, effect.technique, FrameGraphPhase::PostEffect,
                          stableOrder);
  }
  return graph;
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
