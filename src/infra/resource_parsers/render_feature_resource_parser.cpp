#include "infra/resource_parsers/render_feature_resource_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
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

std::optional<std::array<float, 6>>
parseVec6Value(ParsedRenderFeatureResource &result,
               const LX_core::ResourceUri &uri,
               const LX_core::RenderFeatureParameter &parameter,
               const std::string &field) {
  YAML::Node valueNode;
  try {
    valueNode = YAML::Load(parameter.value);
  } catch (const YAML::Exception &e) {
    addDiagnostic(result, uri, field, e.what());
    return std::nullopt;
  }
  if (!valueNode || !valueNode.IsSequence() || valueNode.size() != 6) {
    addDiagnostic(result, uri, field, "must be a six-number sequence");
    return std::nullopt;
  }
  std::array<float, 6> out{};
  for (usize i = 0; i < out.size(); ++i) {
    if (!valueNode[i].IsScalar()) {
      addDiagnostic(result, uri, field, "all entries must be numeric scalars");
      return std::nullopt;
    }
    try {
      out[i] = valueNode[i].as<float>();
    } catch (const YAML::Exception &e) {
      addDiagnostic(result, uri, field, e.what());
      return std::nullopt;
    }
  }
  return out;
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
        key == "parameters") {
      continue;
    }
    if (key == "resources") {
      addDiagnostic(result, uri, key,
                    "resources not implemented; RenderFeature resources need "
                    "an explicit model field before they can be accepted");
    } else if (key == "shader" || key == "pass" || key == "passes" ||
               key == "phase" || key == "renderState" ||
               key == "techniques") {
      addDiagnostic(result, uri, key,
                    "RenderFeature is a pure envelope; render-flow fields "
                    "belong in RenderPathGraph");
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
          key == "required" || key == "allowedValues" ||
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

void validateEnvironmentLightingFeature(ParsedRenderFeatureResource &result,
                                        const LX_core::ResourceUri &uri,
                                        const LX_core::RenderFeature &feature) {
  if (feature.feature != "environmentLighting") {
    return;
  }
  const auto environmentMap = feature.parameters.find("environmentMap");
  if (environmentMap == feature.parameters.end()) {
    addDiagnostic(result, uri, "parameters.environmentMap",
                  "missing required parameter");
    return;
  }
  if (environmentMap->second.uri.empty()) {
    addDiagnostic(result, uri, "parameters.environmentMap.uri",
                  "missing required field");
  }

  if (feature.parameters.find("visibleInBackground") !=
      feature.parameters.end()) {
    addDiagnostic(result, uri, "parameters.visibleInBackground",
                  "removed; use parameters.backgroundMode");
  }

  const auto backgroundMode = feature.parameters.find("backgroundMode");
  if (backgroundMode == feature.parameters.end()) {
    addDiagnostic(result, uri, "parameters.backgroundMode",
                  "missing required parameter");
    return;
  }
  if (backgroundMode->second.kind != "enum") {
    addDiagnostic(result, uri, "parameters.backgroundMode.kind",
                  "expected enum");
  }
  if (backgroundMode->second.value.empty()) {
    addDiagnostic(result, uri, "parameters.backgroundMode.value",
                  "missing required field");
  }
  if (!backgroundMode->second.allowedValues.empty() &&
      !containsValue(backgroundMode->second.allowedValues,
                     backgroundMode->second.value)) {
    addDiagnostic(result, uri, "parameters.backgroundMode.value",
                  "unsupported value");
  }
  if (backgroundMode->second.allowedValues.empty()) {
    addDiagnostic(result, uri, "parameters.backgroundMode.allowedValues",
                  "missing required field");
  }
  for (const std::string &requiredValue : {"none", "infinite", "finiteBox"}) {
    if (!containsValue(backgroundMode->second.allowedValues, requiredValue)) {
      addDiagnostic(result, uri, "parameters.backgroundMode.allowedValues",
                    "must include none, infinite, and finiteBox");
      break;
    }
  }

  const auto finiteBoxBounds = feature.parameters.find("finiteBoxBounds");
  if (backgroundMode->second.value == "finiteBox" &&
      finiteBoxBounds == feature.parameters.end()) {
    addDiagnostic(result, uri, "parameters.finiteBoxBounds",
                  "required when backgroundMode is finiteBox");
    return;
  }
  if (finiteBoxBounds == feature.parameters.end()) {
    return;
  }
  if (finiteBoxBounds->second.kind != "vec6") {
    addDiagnostic(result, uri, "parameters.finiteBoxBounds.kind",
                  "expected vec6");
  }
  if (finiteBoxBounds->second.requiredWhenParameter != "backgroundMode" ||
      finiteBoxBounds->second.requiredWhenEquals != "finiteBox") {
    addDiagnostic(result, uri, "parameters.finiteBoxBounds.requiredWhen",
                  "expected parameter=backgroundMode equals=finiteBox");
  }
  const auto bounds =
      parseVec6Value(result, uri, finiteBoxBounds->second,
                     "parameters.finiteBoxBounds.value");
  if (!bounds.has_value()) {
    return;
  }
  if ((*bounds)[0] >= (*bounds)[1] || (*bounds)[2] >= (*bounds)[3] ||
      (*bounds)[4] >= (*bounds)[5]) {
    addDiagnostic(result, uri, "parameters.finiteBoxBounds.value",
                  "expected xmin<xmax, ymin<ymax, zmin<zmax");
  }
}

void parseRenderFeature(ParsedRenderFeatureResource &result,
                        const LX_core::ResourceUri &uri,
                        const YAML::Node &root) {
  rejectRenderFeatureFlowFields(result, uri, root);

  bool requiredFields = true;
  requiredFields &= requireField(result, uri, root["name"], "name");
  requiredFields &= requireField(result, uri, root["feature"], "feature");
  if (!requiredFields) {
    return;
  }

  LX_core::RenderFeature feature;
  feature.name = root["name"].as<std::string>();
  feature.feature = root["feature"].as<std::string>();
  parseRenderFeatureParameters(result, uri, root["parameters"], feature);
  validateEnvironmentLightingFeature(result, uri, feature);

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
