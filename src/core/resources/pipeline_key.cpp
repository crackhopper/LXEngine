#include "core/resources/pipeline_key.hpp"
#include <iomanip>

namespace LX_core {

namespace {

std::string topologyTag(PrimitiveTopology t) {
  switch (t) {
  case PrimitiveTopology::PointList:
    return "point";
  case PrimitiveTopology::LineList:
    return "line";
  case PrimitiveTopology::LineStrip:
    return "line_strip";
  case PrimitiveTopology::TriangleList:
    return "tri";
  case PrimitiveTopology::TriangleStrip:
    return "tri_strip";
  case PrimitiveTopology::TriangleFan:
    return "tri_fan";
  default:
    return "topo";
  }
}

std::string variantSegment(const ShaderProgramSet &shaderSet) {
  std::vector<std::string> enabled;
  enabled.reserve(shaderSet.variants.size());
  for (const auto &v : shaderSet.variants) {
    if (v.enabled)
      enabled.push_back(v.macroName);
  }
  std::sort(enabled.begin(), enabled.end());
  std::string out;
  for (size_t i = 0; i < enabled.size(); ++i) {
    if (i)
      out += ',';
    out += enabled[i];
  }
  return out;
}

} // namespace

PipelineKey PipelineKey::build(const ShaderProgramSet &shaderSet,
                               size_t meshPipelineHash,
                               const RenderState &renderState,
                               PrimitiveTopology topology,
                               bool hasSkeleton) {
  std::ostringstream oss;
  oss << shaderSet.shaderName << '|' << variantSegment(shaderSet) << '|';
  oss << "vl:0x" << std::hex << meshPipelineHash << std::dec;
  oss << "|rs:0x" << std::hex << renderState.getPipelineHash() << std::dec;
  oss << '|' << topologyTag(topology);
  if (hasSkeleton)
    oss << "|+skel";
  return PipelineKey{StringID(oss.str())};
}

} // namespace LX_core
