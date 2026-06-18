#include "infra/resource_parsers/render_feature_resource_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace LX_infra {
namespace {

void addDiagnostic(ParsedRenderFeatureResource &result,
                   const LX_core::ResourceUri &uri, const std::string &field,
                   const std::string &message) {
  result.diagnostics.push_back(uri.string() + ": " + field + ": " + message);
}

bool requireField(ParsedRenderFeatureResource &result,
                  const LX_core::ResourceUri &uri, const YAML::Node &node,
                  const std::string &field) {
  if (node) {
    return true;
  }
  addDiagnostic(result, uri, field, "missing required field");
  return false;
}

std::string nodeToStoredValue(const YAML::Node &node) {
  if (!node) {
    return {};
  }
  if (!node.IsScalar()) {
    return YAML::Dump(node);
  }
  return node.as<std::string>();
}

bool parseBoolScalar(ParsedRenderFeatureResource &result,
                     const LX_core::ResourceUri &uri, const YAML::Node &node,
                     const std::string &field) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(result, uri, field, "must be a boolean");
    return false;
  }
  const std::string value = node.as<std::string>();
  if (value == "true" || value == "false") {
    return value == "true";
  }
  addDiagnostic(result, uri, field, "expected true or false");
  return false;
}

bool kindAllowsUri(const std::string &kind) {
  return kind == "textureCube" || kind == "texture2D" ||
         kind == "texture3D" || kind == "buffer";
}

std::vector<std::string> parseScalarStringSequence(
    ParsedRenderFeatureResource &result, const LX_core::ResourceUri &uri,
    const YAML::Node &node, const std::string &field) {
  std::vector<std::string> values;
  if (!node || !node.IsSequence()) {
    addDiagnostic(result, uri, field, "must be a sequence");
    return values;
  }
  for (usize i = 0; i < node.size(); ++i) {
    if (!node[i].IsScalar()) {
      addDiagnostic(result, uri, field,
                    "all entries must be scalar strings");
      return {};
    }
    values.push_back(node[i].as<std::string>());
  }
  return values;
}

bool containsValue(const std::vector<std::string> &values,
                   const std::string &value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

void rejectRenderFeatureFlowFields(ParsedRenderFeatureResource &result,
                                   const LX_core::ResourceUri &uri,
                                   const YAML::Node &root) {
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, uri, "$", "field names must be scalar strings");
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "schema" || key == "name" || key == "feature" ||
        key == "level" || key == "shader" || key == "parameters") {
      continue;
    }
    if (key == "resources") {
      addDiagnostic(result, uri, key,
                    "resources not implemented; RenderFeature resources need "
                    "an explicit model field before they can be accepted");
    } else if (key == "pass" || key == "passes" || key == "phase" ||
               key == "renderState" || key == "techniques") {
      addDiagnostic(result, uri, key,
                    "RenderFeature shader is ABI metadata only; render-flow "
                    "fields belong in RenderPathGraph");
    } else {
      addDiagnostic(result, uri, key, "unsupported render feature field");
    }
  }
}

void parseRenderFeatureParameters(ParsedRenderFeatureResource &result,
                                  const LX_core::ResourceUri &uri,
                                  const YAML::Node &parameters,
                                  LX_core::RenderFeature &feature) {
  if (!parameters) {
    return;
  }
  if (!parameters.IsMap()) {
    addDiagnostic(result, uri, "parameters", "parameters must be a map");
    return;
  }
  for (auto it = parameters.begin(); it != parameters.end(); ++it) {
    const std::string name = it->first.as<std::string>();
    const YAML::Node parameterNode = it->second;
    const std::string field = "parameters." + name;
    if (!parameterNode || !parameterNode.IsMap()) {
      addDiagnostic(result, uri, field, "parameter must be a map");
      continue;
    }
    for (auto parameterField = parameterNode.begin();
         parameterField != parameterNode.end(); ++parameterField) {
      if (!parameterField->first.IsScalar()) {
        addDiagnostic(result, uri, field,
                      "parameter field names must be scalar strings");
        continue;
      }
      const std::string key = parameterField->first.as<std::string>();
      if (key == "kind" || key == "value" || key == "uri" ||
          key == "valueType" || key == "binding" || key == "member" ||
          key == "required" || key == "volatile" || key == "allowedValues" ||
          key == "requiredWhen") {
        continue;
      }
      addDiagnostic(result, uri, field + "." + key,
                    "unsupported render feature parameter field");
    }
    if (!parameterNode["kind"] || !parameterNode["kind"].IsScalar()) {
      addDiagnostic(result, uri, field + ".kind", "missing required field");
      continue;
    }
    LX_core::RenderFeatureParameter parameter;
    parameter.kind = parameterNode["kind"].as<std::string>();
    if (parameterNode["volatile"]) {
      const usize diagnosticCount = result.diagnostics.size();
      parameter.volatileRuntime =
          parseBoolScalar(result, uri, parameterNode["volatile"],
                          field + ".volatile");
      if (result.diagnostics.size() != diagnosticCount) {
        continue;
      }
    }
    if (parameter.volatileRuntime && parameterNode["value"]) {
      addDiagnostic(result, uri, field + ".value",
                    "volatile parameter must not define value");
    }
    if (parameter.volatileRuntime && parameterNode["constantId"]) {
      addDiagnostic(result, uri, field + ".constantId",
                    "volatile parameter must not define constantId");
    }
    if (parameter.volatileRuntime && parameterNode["specialization"]) {
      addDiagnostic(result, uri, field + ".specialization",
                    "volatile parameter must not define specialization");
    }
    if (parameter.volatileRuntime &&
        parameterNode["specializationConstant"]) {
      addDiagnostic(
          result, uri, field + ".specializationConstant",
          "volatile parameter must not define specializationConstant");
    }
    if (parameter.volatileRuntime &&
        parameterNode["specializationConstants"]) {
      addDiagnostic(
          result, uri, field + ".specializationConstants",
          "volatile parameter must not define specializationConstants");
    }
    if (parameter.volatileRuntime && parameterNode["valueType"]) {
      addDiagnostic(result, uri, field + ".valueType",
                    "volatile parameter must not define valueType");
    }
    if (!result.diagnostics.empty()) {
      continue;
    }
    if (parameterNode["value"]) {
      parameter.value = nodeToStoredValue(parameterNode["value"]);
    }
    if (parameterNode["uri"]) {
      if (!kindAllowsUri(parameter.kind)) {
        addDiagnostic(result, uri, field + ".uri",
                      "uri is only supported for resource-like parameters");
        continue;
      }
      parameter.uri = parameterNode["uri"].as<std::string>();
    }
    if (parameterNode["valueType"]) {
      parameter.valueType = parameterNode["valueType"].as<std::string>();
    }
    if (parameterNode["binding"]) {
      parameter.binding = parameterNode["binding"].as<std::string>();
    }
    if (parameterNode["member"]) {
      parameter.member = parameterNode["member"].as<std::string>();
    }
    if (parameterNode["required"]) {
      parameter.required =
          parseBoolScalar(result, uri, parameterNode["required"],
                          field + ".required");
      if (!result.diagnostics.empty()) {
        continue;
      }
    }
    if (parameterNode["allowedValues"]) {
      parameter.allowedValues =
          parseScalarStringSequence(result, uri, parameterNode["allowedValues"],
                                    field + ".allowedValues");
      if (!result.diagnostics.empty()) {
        continue;
      }
    }
    if (parameterNode["requiredWhen"]) {
      const YAML::Node requiredWhen = parameterNode["requiredWhen"];
      if (!requiredWhen.IsMap()) {
        addDiagnostic(result, uri, field + ".requiredWhen",
                      "must be a map");
        continue;
      }
      for (auto requiredWhenField = requiredWhen.begin();
           requiredWhenField != requiredWhen.end(); ++requiredWhenField) {
        if (!requiredWhenField->first.IsScalar()) {
          addDiagnostic(result, uri, field + ".requiredWhen",
                        "field names must be scalar strings");
          continue;
        }
        const std::string key = requiredWhenField->first.as<std::string>();
        if (key != "parameter" && key != "equals") {
          addDiagnostic(result, uri, field + ".requiredWhen." + key,
                        "unsupported requiredWhen field");
        }
      }
      if (!requiredWhen["parameter"] || !requiredWhen["parameter"].IsScalar()) {
        addDiagnostic(result, uri, field + ".requiredWhen.parameter",
                      "missing required field");
        continue;
      }
      if (!requiredWhen["equals"] || !requiredWhen["equals"].IsScalar()) {
        addDiagnostic(result, uri, field + ".requiredWhen.equals",
                      "missing required field");
        continue;
      }
      parameter.requiredWhenParameter =
          requiredWhen["parameter"].as<std::string>();
      parameter.requiredWhenEquals = requiredWhen["equals"].as<std::string>();
      if (!result.diagnostics.empty()) {
        continue;
      }
    }
    feature.parameters.emplace(name, std::move(parameter));
  }
}

std::optional<LX_core::RenderFeatureLevel>
parseRenderFeatureLevel(ParsedRenderFeatureResource &result,
                        const LX_core::ResourceUri &uri,
                        const YAML::Node &level) {
  if (!level || !level.IsScalar()) {
    addDiagnostic(result, uri, "level", "missing required field");
    return std::nullopt;
  }
  const std::string value = level.as<std::string>();
  if (value == "shader") {
    return LX_core::RenderFeatureLevel::Shader;
  }
  if (value == "pass") {
    return LX_core::RenderFeatureLevel::Pass;
  }
  addDiagnostic(result, uri, "level", "expected shader or pass");
  return std::nullopt;
}

std::optional<LX_core::RenderFeatureShaderContract>
parseRenderFeatureShaderContract(ParsedRenderFeatureResource &result,
                                 const LX_core::ResourceUri &uri,
                                 const YAML::Node &shader) {
  if (!shader) {
    return std::nullopt;
  }
  if (!shader.IsMap()) {
    addDiagnostic(result, uri, "shader", "must be a map");
    return std::nullopt;
  }
  for (auto field = shader.begin(); field != shader.end(); ++field) {
    if (!field->first.IsScalar()) {
      addDiagnostic(result, uri, "shader",
                    "shader field names must be scalar strings");
      continue;
    }
    const std::string key = field->first.as<std::string>();
    if (key != "uri") {
      addDiagnostic(result, uri, "shader." + key,
                    "unsupported shader contract field");
    }
  }
  if (!shader["uri"] || !shader["uri"].IsScalar()) {
    addDiagnostic(result, uri, "shader.uri", "missing required field");
    return std::nullopt;
  }
  return LX_core::RenderFeatureShaderContract{
      LX_core::ResourceUri(shader["uri"].as<std::string>())};
}

void validateRenderFeatureSchema(ParsedRenderFeatureResource &result,
                                 const LX_core::ResourceUri &uri,
                                 const LX_core::RenderFeature &feature) {
  const bool hasShaderUri = feature.shader.has_value() &&
                            !feature.shader->uri.empty();
  for (const auto &[name, parameter] : feature.parameters) {
    const std::string field = "parameters." + name;
    if (parameter.kind == "textureCube" || parameter.kind == "texture2D" ||
        parameter.kind == "texture3D") {
      if (parameter.required && parameter.uri.empty()) {
        addDiagnostic(result, uri, field + ".uri", "missing required field");
      }
    }
    if (!parameter.binding.empty() || !parameter.member.empty()) {
      if (!hasShaderUri) {
        addDiagnostic(result, uri, "shader.uri",
                      "required for binding/member ABI parameters");
      }
      if (feature.level == LX_core::RenderFeatureLevel::Pass) {
        if (!parameter.volatileRuntime && !parameter.binding.empty()) {
          addDiagnostic(result, uri, field + ".binding",
                        "pass-level parameters must use shader reflection "
                        "specialization constants, not bindings");
        }
        if (!parameter.volatileRuntime && !parameter.member.empty()) {
          addDiagnostic(result, uri, field + ".member",
                        "pass-level parameters must use shader reflection "
                        "specialization constants, not UBO members");
        }
      }
    }
    if (parameter.volatileRuntime) {
      if (feature.level != LX_core::RenderFeatureLevel::Pass) {
        addDiagnostic(result, uri, field + ".volatile",
                      "volatile parameters are only supported for pass-level "
                      "runtime data");
      }
      if (parameter.binding.empty()) {
        addDiagnostic(result, uri, field + ".binding",
                      "volatile parameter must define binding");
      }
      if (parameter.member.empty()) {
        addDiagnostic(result, uri, field + ".member",
                      "volatile parameter must define member");
      }
    }
  }
}

void parseRenderFeature(ParsedRenderFeatureResource &result,
                        const LX_core::ResourceUri &uri,
                        const YAML::Node &root) {
  rejectRenderFeatureFlowFields(result, uri, root);

  bool requiredFields = true;
  requiredFields &= requireField(result, uri, root["name"], "name");
  requiredFields &= requireField(result, uri, root["feature"], "feature");
  const auto level = parseRenderFeatureLevel(result, uri, root["level"]);
  const auto shader =
      parseRenderFeatureShaderContract(result, uri, root["shader"]);
  if (!requiredFields) {
    return;
  }

  LX_core::RenderFeature feature;
  feature.name = root["name"].as<std::string>();
  feature.feature = root["feature"].as<std::string>();
  if (level.has_value()) {
    feature.level = *level;
  }
  if (shader.has_value()) {
    feature.shader = *shader;
  }
  parseRenderFeatureParameters(result, uri, root["parameters"], feature);
  validateRenderFeatureSchema(result, uri, feature);

  if (!result.diagnostics.empty()) {
    return;
  }
  result.renderFeature = std::move(feature);
}

} // namespace

ParsedRenderFeatureResource RenderFeatureResourceParser::parse(
    const LX_core::ResourceUri &uri, std::string_view yamlText) const {
  ParsedRenderFeatureResource result;
  YAML::Node root;
  try {
    root = YAML::Load(std::string(yamlText));
  } catch (const YAML::Exception &e) {
    addDiagnostic(result, uri, "$", e.what());
    return result;
  }

  if (!root || !root.IsMap()) {
    addDiagnostic(result, uri, "$", "root must be a map");
    return result;
  }
  if (!root["schema"] || !root["schema"].IsScalar()) {
    addDiagnostic(result, uri, "schema", "expected lxe.render-feature.v1");
    return result;
  }

  const std::string schema = root["schema"].as<std::string>();
  if (schema != "lxe.render-feature.v1") {
    addDiagnostic(result, uri, "schema", "expected lxe.render-feature.v1");
    return result;
  }

  parseRenderFeature(result, uri, root);
  return result;
}

} // namespace LX_infra
