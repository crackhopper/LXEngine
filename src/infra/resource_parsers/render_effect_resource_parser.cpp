#include "infra/resource_parsers/render_effect_resource_parser.hpp"
#include "infra/resource_parsers/material_pass_contract_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>
#include <utility>

namespace LX_infra {
namespace {

void addDiagnostic(ParsedRenderEffectResource &result, const std::string &uri,
                   const std::string &field, const std::string &message) {
  result.diagnostics.push_back(uri + ": " + field + ": " + message);
}

void addDiagnostic(ParsedRenderEffectResource &result,
                   const LX_core::ResourceUri &uri, const std::string &field,
                   const std::string &message) {
  addDiagnostic(result, uri.string(), field, message);
}

bool requireField(ParsedRenderEffectResource &result,
                  const LX_core::ResourceUri &uri, const YAML::Node &node,
                  const std::string &field) {
  if (node) {
    return true;
  }
  addDiagnostic(result, uri, field, "missing required field");
  return false;
}

std::optional<LX_core::RenderPath> parseRenderPath(std::string value) {
  if (value == "Forward") {
    return LX_core::RenderPath::Forward;
  }
  if (value == "Deferred") {
    return LX_core::RenderPath::Deferred;
  }
  if (value == "OfflineRT") {
    return LX_core::RenderPath::OfflineRT;
  }
  return std::nullopt;
}

std::vector<std::string> parseStringList(const YAML::Node &node) {
  std::vector<std::string> values;
  if (!node || !node.IsSequence()) {
    return values;
  }
  values.reserve(node.size());
  for (const auto &entry : node) {
    values.push_back(entry.as<std::string>());
  }
  return values;
}

LX_core::RenderPassNodeFilters parseFilters(const YAML::Node &filters) {
  LX_core::RenderPassNodeFilters parsed;
  if (!filters || !filters.IsMap()) {
    return parsed;
  }
  parsed.renderClasses = parseStringList(filters["renderClass"]);
  parsed.bsdfTypes = parseStringList(filters["bsdf"]);
  return parsed;
}

std::optional<LX_core::RenderPassNode>
parseRenderPassNode(ParsedRenderEffectResource &result,
                    const LX_core::ResourceUri &uri, const YAML::Node &node) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, uri, "passes", "pass must be a map");
    return std::nullopt;
  }
  if (!requireField(result, uri, node["id"], "passes[].id")) {
    return std::nullopt;
  }

  const std::string id = node["id"].as<std::string>();
  const std::string fieldPrefix = "passes." + id;
  auto parsedPass = parseMaterialPassContract(id, node, fieldPrefix);
  for (const std::string &diagnostic : parsedPass.diagnostics) {
    result.diagnostics.push_back(uri.string() + ": " + diagnostic);
  }
  if (!parsedPass.pass.has_value()) {
    return std::nullopt;
  }

  LX_core::RenderPassNode pass;
  pass.id = id;
  pass.shaderUri = parsedPass.pass->shaderUri;
  pass.stage = parsedPass.pass->stage;
  pass.dispatch = parsedPass.pass->dispatch;
  pass.sources = std::move(parsedPass.pass->sources);
  pass.targets = std::move(parsedPass.pass->targets);
  pass.renderState = parsedPass.pass->renderState;
  pass.writeMode = std::move(parsedPass.pass->writeMode);
  pass.filters = parseFilters(node["filters"]);
  return pass;
}

void parseFeatureDependencies(ParsedRenderEffectResource &result,
                              const LX_core::ResourceUri &uri,
                              const YAML::Node &features,
                              LX_core::RenderPathGraph &graph) {
  if (!features) {
    return;
  }
  if (!features.IsMap()) {
    addDiagnostic(result, uri, "features", "features must be a map");
    return;
  }
  for (auto it = features.begin(); it != features.end(); ++it) {
    const std::string slot = it->first.as<std::string>();
    const YAML::Node featureNode = it->second;
    const std::string field = "features." + slot + ".uri";
    if (!featureNode || !featureNode.IsMap() || !featureNode["uri"]) {
      addDiagnostic(result, uri, field, "missing required field");
      continue;
    }
    graph.features.push_back(
        LX_core::RenderPathFeatureDependency{slot,
                                             featureNode["uri"].as<std::string>()});
  }
}

void parseRenderPathGraph(ParsedRenderEffectResource &result,
                          const LX_core::ResourceUri &uri,
                          const YAML::Node &root) {
  bool requiredFields = true;
  requiredFields &= requireField(result, uri, root["name"], "name");
  requiredFields &= requireField(result, uri, root["renderPath"], "renderPath");
  requiredFields &= requireField(result, uri, root["passes"], "passes");
  if (!requiredFields) {
    return;
  }
  if (!root["passes"].IsSequence() || root["passes"].size() == 0) {
    addDiagnostic(result, uri, "passes", "missing non-empty pass sequence");
    return;
  }

  LX_core::RenderPathGraph graph;
  graph.name = root["name"].as<std::string>();
  auto renderPath = parseRenderPath(root["renderPath"].as<std::string>());
  if (!renderPath.has_value()) {
    addDiagnostic(result, uri, "renderPath",
                  "expected Forward, Deferred, or OfflineRT");
    return;
  }
  graph.renderPath = *renderPath;
  parseFeatureDependencies(result, uri, root["features"], graph);

  for (const auto &passNode : root["passes"]) {
    auto parsedPass = parseRenderPassNode(result, uri, passNode);
    if (parsedPass.has_value()) {
      graph.passes.push_back(std::move(*parsedPass));
    }
  }
  if (!result.diagnostics.empty()) {
    return;
  }
  result.renderPathGraph = std::move(graph);
}

std::string scalarNodeToString(const YAML::Node &node) {
  if (!node) {
    return {};
  }
  return node.as<std::string>();
}

void rejectRenderFeatureFlowFields(ParsedRenderEffectResource &result,
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
    if (key == "shader" || key == "passes" || key == "phase" ||
        key == "renderState" || key == "techniques") {
      addDiagnostic(result, uri, key,
                    "RenderFeature is a pure envelope; render-flow fields "
                    "belong in RenderPathGraph");
    } else {
      addDiagnostic(result, uri, key, "unsupported render feature field");
    }
  }
}

void parseRenderFeatureParameters(ParsedRenderEffectResource &result,
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

void parseRenderFeature(ParsedRenderEffectResource &result,
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

ParsedRenderEffectResource
RenderEffectResourceParser::parse(const LX_core::ResourceUri &uri,
                                  std::string_view yamlText) const {
  ParsedRenderEffectResource result;
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
    addDiagnostic(result, uri, "schema",
                  "expected lxe.render-path-graph.v1 or "
                  "lxe.render-feature.v1");
    return result;
  }

  const std::string schema = root["schema"].as<std::string>();
  if (schema == "lxe.render-path-graph.v1") {
    parseRenderPathGraph(result, uri, root);
    return result;
  }
  if (schema == "lxe.render-feature.v1") {
    parseRenderFeature(result, uri, root);
    return result;
  }

  addDiagnostic(result, uri, "schema",
                "expected lxe.render-path-graph.v1 or lxe.render-feature.v1");
  return result;
}

} // namespace LX_infra
