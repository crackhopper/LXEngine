#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/resource_parsers/render_pass_node_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>
#include <utility>

namespace LX_infra {
namespace {

void addDiagnostic(ParsedRenderPathGraphResource &result,
                   const LX_core::ResourceUri &uri, const std::string &field,
                   const std::string &message) {
  result.diagnostics.push_back(uri.string() + ": " + field + ": " + message);
}

bool requireField(ParsedRenderPathGraphResource &result,
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

void rejectRootFields(ParsedRenderPathGraphResource &result,
                      const LX_core::ResourceUri &uri, const YAML::Node &root) {
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, uri, "$", "field names must be scalar strings");
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "schema" || key == "name" || key == "renderPath" ||
        key == "features" || key == "passes") {
      continue;
    }
    addDiagnostic(result, uri, key, "unsupported render path graph field");
  }
}

std::optional<LX_core::RenderPassNode>
parseRenderPassNode(ParsedRenderPathGraphResource &result,
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
  auto parsedPass = parseRenderPassNodeContract(id, node, fieldPrefix);
  for (const std::string &diagnostic : parsedPass.diagnostics) {
    result.diagnostics.push_back(uri.string() + ": " + diagnostic);
  }
  if (!parsedPass.pass.has_value()) {
    return std::nullopt;
  }

  LX_core::RenderPassNode pass = std::move(*parsedPass.pass);
  return pass;
}

void parseFeatureDependencies(ParsedRenderPathGraphResource &result,
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
    if (!it->first.IsScalar()) {
      addDiagnostic(result, uri, "features",
                    "feature dependency names must be scalar strings");
      continue;
    }
    const std::string slot = it->first.as<std::string>();
    const YAML::Node featureNode = it->second;
    const std::string fieldPrefix = "features." + slot;
    if (!featureNode || !featureNode.IsMap()) {
      addDiagnostic(result, uri, fieldPrefix,
                    "feature dependency must be a map");
      continue;
    }
    for (auto field = featureNode.begin(); field != featureNode.end();
         ++field) {
      if (!field->first.IsScalar()) {
        addDiagnostic(result, uri, fieldPrefix,
                      "feature dependency field names must be scalar strings");
        continue;
      }
      const std::string key = field->first.as<std::string>();
      if (key == "uri") {
        continue;
      }
      addDiagnostic(result, uri, fieldPrefix + "." + key,
                    "unsupported feature dependency field");
    }
    if (!featureNode["uri"]) {
      addDiagnostic(result, uri, fieldPrefix + ".uri",
                    "missing required field");
      continue;
    }
    graph.features.push_back(LX_core::RenderPathFeatureDependency{
        slot, featureNode["uri"].as<std::string>()});
  }
}

void parseRenderPathGraph(ParsedRenderPathGraphResource &result,
                          const LX_core::ResourceUri &uri,
                          const YAML::Node &root) {
  rejectRootFields(result, uri, root);

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

} // namespace

ParsedRenderPathGraphResource
RenderPathGraphResourceParser::parse(const LX_core::ResourceUri &uri,
                                     std::string_view yamlText) const {
  ParsedRenderPathGraphResource result;
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
    addDiagnostic(result, uri, "schema", "expected lxe.render-path-graph.v1");
    return result;
  }

  const std::string schema = root["schema"].as<std::string>();
  if (schema != "lxe.render-path-graph.v1") {
    addDiagnostic(result, uri, "schema", "expected lxe.render-path-graph.v1");
    return result;
  }

  parseRenderPathGraph(result, uri, root);
  return result;
}

} // namespace LX_infra
