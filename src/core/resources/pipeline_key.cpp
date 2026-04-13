#include "core/resources/pipeline_key.hpp"
#include <algorithm>
#include <iomanip>

namespace LX_core {

namespace {

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
                               const Mesh &mesh,
                               const RenderState &renderState,
                               const SkeletonPtr &skeleton) {
  const size_t skeletonHash = skeleton ? skeleton->getPipelineHash() : size_t{0};
  std::ostringstream oss;
  oss << shaderSet.shaderName << '|' << variantSegment(shaderSet) << '|';
  oss << "ml:0x" << std::hex << mesh.getPipelineHash() << std::dec;
  oss << "|rs:0x" << std::hex << renderState.getPipelineHash() << std::dec;
  oss << "|sk:0x" << std::hex << skeletonHash << std::dec;
  return PipelineKey{StringID(oss.str())};
}

} // namespace LX_core
