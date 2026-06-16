#include "core/frame_graph/render_feature_shader_validation.hpp"

#include <algorithm>
#include <utility>

namespace LX_core {
namespace {

RenderFeatureShaderValidationDiagnostic makeError(std::string parameter,
                                                  std::string message) {
  return RenderFeatureShaderValidationDiagnostic{
      .severity = RenderFeatureShaderValidationSeverity::Error,
      .parameter = std::move(parameter),
      .message = std::move(message),
  };
}

std::string featureParameterTypeName(const RenderFeatureParameter &parameter) {
  if (parameter.kind == "enum") {
    return "int";
  }
  if (parameter.kind == "integer") {
    return "int";
  }
  return parameter.kind;
}

bool parameterKindMatchesBinding(const RenderFeatureParameter &parameter,
                                 ShaderPropertyType type) {
  if (parameter.kind == "textureCube") {
    return type == ShaderPropertyType::TextureCube;
  }
  if (parameter.kind == "texture2D") {
    return type == ShaderPropertyType::Texture2D;
  }
  if (parameter.kind == "float") {
    return type == ShaderPropertyType::Float;
  }
  if (parameter.kind == "vec3") {
    return type == ShaderPropertyType::Vec3;
  }
  if (parameter.kind == "enum" || parameter.kind == "integer") {
    return type == ShaderPropertyType::Int;
  }
  return true;
}

bool parameterKindMatchesSpecialization(
    const RenderFeatureParameter &parameter,
    ShaderSpecializationValueType type) {
  if (parameter.kind == "bool") {
    return type == ShaderSpecializationValueType::Bool;
  }
  if (parameter.kind == "float") {
    return type == ShaderSpecializationValueType::Float;
  }
  if (parameter.kind == "integer" || parameter.kind == "enum") {
    return type == ShaderSpecializationValueType::Int;
  }
  if (parameter.kind == "u32" || parameter.kind == "uint") {
    return type == ShaderSpecializationValueType::UInt;
  }
  return false;
}

const StructMemberInfo *findMember(const ShaderResourceBinding &binding,
                                   const std::string &memberName) {
  const auto member = std::find_if(
      binding.members.begin(), binding.members.end(),
      [&](const StructMemberInfo &candidate) {
        return candidate.name == memberName;
      });
  return member == binding.members.end() ? nullptr : &*member;
}

void validateShaderLevelParameter(
    const IShader &shader, const std::string &name,
    const RenderFeatureParameter &parameter,
    std::vector<RenderFeatureShaderValidationDiagnostic> &diagnostics) {
  if (parameter.binding.empty()) {
    return;
  }

  const auto binding = shader.findBinding(parameter.binding);
  if (!binding.has_value()) {
    diagnostics.push_back(makeError(
        name, "shader ABI binding '" + parameter.binding + "' was not reflected"));
    return;
  }

  if (parameter.kind == "textureCube" || parameter.kind == "texture2D") {
    if (!parameterKindMatchesBinding(parameter, binding->get().type)) {
      diagnostics.push_back(makeError(
          name, "shader ABI binding '" + parameter.binding +
                    "' has incompatible texture shape"));
    }
    return;
  }

  if (!parameter.member.empty()) {
    if (binding->get().type != ShaderPropertyType::UniformBuffer) {
      diagnostics.push_back(makeError(
          name, "shader ABI binding '" + parameter.binding +
                    "' is not a UniformBuffer"));
      return;
    }
    const StructMemberInfo *member = findMember(binding->get(), parameter.member);
    if (member == nullptr) {
      diagnostics.push_back(makeError(
          name, "shader ABI member '" + parameter.member +
                    "' was not reflected in binding '" + parameter.binding + "'"));
      return;
    }
    if (!parameterKindMatchesBinding(parameter, member->type)) {
      diagnostics.push_back(makeError(
          name, "shader ABI member '" + parameter.member +
                    "' type does not match parameter kind '" +
                    featureParameterTypeName(parameter) + "'"));
    }
  }
}

void validatePassLevelParameter(
    const IShader &shader, const std::string &name,
    const RenderFeatureParameter &parameter,
    std::vector<RenderFeatureShaderValidationDiagnostic> &diagnostics) {
  const auto &constants = shader.getSpecializationConstants();
  const auto constant = std::find_if(
      constants.begin(), constants.end(),
      [&](const ShaderSpecializationConstantInfo &candidate) {
        return candidate.name == name;
      });
  if (constant == constants.end()) {
    diagnostics.push_back(makeError(
        name, "shader specialization constant '" + name + "' was not reflected"));
    return;
  }
  if (!parameterKindMatchesSpecialization(parameter, constant->type)) {
    diagnostics.push_back(makeError(
        name, "shader specialization constant '" + name +
                  "' type does not match parameter kind '" + parameter.kind + "'"));
  }
}

} // namespace

std::vector<RenderFeatureShaderValidationDiagnostic>
validateRenderFeatureShaderAbi(const RenderFeature &feature,
                               const IShader &shader) {
  std::vector<RenderFeatureShaderValidationDiagnostic> diagnostics;
  if (feature.level == RenderFeatureLevel::Unknown) {
    diagnostics.push_back(makeError("level", "render feature level is unknown"));
    return diagnostics;
  }

  for (const auto &[name, parameter] : feature.parameters) {
    if (feature.level == RenderFeatureLevel::Pass) {
      validatePassLevelParameter(shader, name, parameter, diagnostics);
    } else {
      validateShaderLevelParameter(shader, name, parameter, diagnostics);
    }
  }
  return diagnostics;
}

} // namespace LX_core
