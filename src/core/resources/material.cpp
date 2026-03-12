#include "material.hpp"
#include <string>

namespace LX_core {

std::string getPipelineId(PipelineType type, i32 subid = 0) {
  switch (type) {
  case PipelineType::PT_BlinnPhong:
    return "blinnphong_" + std::to_string(subid);
  default:
    return "";
  }
}

std::string getPredefinedShaderName(const std::string &pipelineId,
                                    ShaderType type) {
  switch (type) {
  case ShaderType::ST_Vertex:
    return pipelineId + ".vert";
  case ShaderType::ST_Fragment:
    return pipelineId + ".frag";
  default:
    return "";
  }
}