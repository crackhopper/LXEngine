#include "infra/resource_parsers/render_feature_resource_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <string>
#include <utility>

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

std::string scalarNodeToString(const YAML::Node &node) {
  if (!node) {
    return {};
  }
  return node.as<std::string>();
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
          key == "valueType") {
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
      parameter.value = scalarNodeToString(parameterNode["value"]);
    }
    if (parameterNode["uri"]) {
      parameter.uri = parameterNode["uri"].as<std::string>();
    }
    if (parameterNode["valueType"]) {
      parameter.valueType = parameterNode["valueType"].as<std::string>();
    }
    feature.parameters.emplace(name, std::move(parameter));
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
