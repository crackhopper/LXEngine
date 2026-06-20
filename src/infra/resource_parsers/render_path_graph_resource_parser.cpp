#include "infra/resource_parsers/render_path_graph_resource_parser.hpp"
#include "infra/resource_parsers/render_pass_node_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
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
        key == "features" || key == "bake" || key == "passes") {
      continue;
    }
    addDiagnostic(result, uri, key, "unsupported render path graph field");
  }
}

bool rejectUnsupportedBakeFields(ParsedRenderPathGraphResource &result,
                                 const LX_core::ResourceUri &uri,
                                 const YAML::Node &node,
                                 const std::string &field,
                                 std::initializer_list<std::string_view>
                                     allowedFields);

std::optional<LX_core::RenderPathBakeConfig>
parseBakeConfig(ParsedRenderPathGraphResource &result,
                const LX_core::ResourceUri &uri, const YAML::Node &bake) {
  if (!bake) {
    return std::nullopt;
  }
  if (!bake.IsMap()) {
    addDiagnostic(result, uri, "bake", "bake must be a map");
    return std::nullopt;
  }
  bool valid = true;
  for (auto it = bake.begin(); it != bake.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, uri, "bake",
                    "bake field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "kind" || key == "environment" || key == "brdfLut") {
      continue;
    }
    addDiagnostic(result, uri, "bake." + key,
                  "unsupported bake parameter field");
    valid = false;
  }
  valid &= requireField(result, uri, bake["kind"], "bake.kind");
  if (!valid) {
    return std::nullopt;
  }

  LX_core::RenderPathBakeConfig config;
  config.kind = bake["kind"].as<std::string>();
  if (config.kind == "environment-ibl") {
    const YAML::Node environment = bake["environment"];
    if (!environment || !environment.IsMap()) {
      addDiagnostic(result, uri, "bake.environment",
                    "environment-ibl bake requires environment block");
      return std::nullopt;
    }
    valid &= rejectUnsupportedBakeFields(
        result, uri, environment, "bake.environment",
        {"cubemap", "diffuse", "specular"});
    const YAML::Node cubemap = environment["cubemap"];
    const YAML::Node diffuse = environment["diffuse"];
    const YAML::Node specular = environment["specular"];
    valid &= requireField(result, uri, cubemap, "bake.environment.cubemap");
    valid &= requireField(result, uri, diffuse, "bake.environment.diffuse");
    valid &= requireField(result, uri, specular, "bake.environment.specular");
    if (!valid || !cubemap.IsMap() || !diffuse.IsMap() || !specular.IsMap()) {
      addDiagnostic(result, uri, "bake.environment",
                    "cubemap, diffuse, and specular must be maps");
      return std::nullopt;
    }
    valid &= rejectUnsupportedBakeFields(
        result, uri, cubemap, "bake.environment.cubemap",
        {"resolution", "faces"});
    valid &= rejectUnsupportedBakeFields(
        result, uri, diffuse, "bake.environment.diffuse", {"kind", "order"});
    valid &= rejectUnsupportedBakeFields(
        result, uri, specular, "bake.environment.specular",
        {"resolution", "mips", "faces"});
    LX_core::EnvironmentIblBakeConfig environmentConfig;
    valid &= requireField(result, uri, cubemap["resolution"],
                          "bake.environment.cubemap.resolution");
    valid &= requireField(result, uri, cubemap["faces"],
                          "bake.environment.cubemap.faces");
    valid &= requireField(result, uri, diffuse["kind"],
                          "bake.environment.diffuse.kind");
    valid &= requireField(result, uri, diffuse["order"],
                          "bake.environment.diffuse.order");
    valid &= requireField(result, uri, specular["resolution"],
                          "bake.environment.specular.resolution");
    valid &= requireField(result, uri, specular["mips"],
                          "bake.environment.specular.mips");
    valid &= requireField(result, uri, specular["faces"],
                          "bake.environment.specular.faces");
    if (!valid) {
      return std::nullopt;
    }
    environmentConfig.cubemapResolution = cubemap["resolution"].as<u32>();
    environmentConfig.cubemapFaces = cubemap["faces"].as<u32>();
    environmentConfig.diffuseKind = diffuse["kind"].as<std::string>();
    environmentConfig.diffuseOrder = diffuse["order"].as<u32>();
    environmentConfig.specularResolution =
        specular["resolution"].as<u32>();
    environmentConfig.specularMips = specular["mips"].as<std::string>();
    environmentConfig.specularFaces = specular["faces"].as<u32>();
    if (environmentConfig.diffuseKind != "sh9") {
      addDiagnostic(result, uri, "bake.environment.diffuse.kind",
                    "expected sh9");
      return std::nullopt;
    }
    config.environment = environmentConfig;
    return config;
  }

  if (config.kind == "standard-pbr-brdf-lut") {
    const YAML::Node brdfLut = bake["brdfLut"];
    if (!brdfLut || !brdfLut.IsMap()) {
      addDiagnostic(result, uri, "bake.brdfLut",
                    "standard-pbr-brdf-lut bake requires brdfLut block");
      return std::nullopt;
    }
    valid &= rejectUnsupportedBakeFields(result, uri, brdfLut, "bake.brdfLut",
                                         {"resolution"});
    valid &= requireField(result, uri, brdfLut["resolution"],
                          "bake.brdfLut.resolution");
    if (!valid) {
      return std::nullopt;
    }
    LX_core::StandardPbrBrdfLutBakeConfig brdfConfig;
    brdfConfig.resolution = brdfLut["resolution"].as<u32>();
    config.standardPbrBrdfLut = brdfConfig;
    return config;
  }

  addDiagnostic(result, uri, "bake.kind",
                "expected environment-ibl or standard-pbr-brdf-lut");
  return std::nullopt;
}

bool rejectUnsupportedBakeFields(ParsedRenderPathGraphResource &result,
                                 const LX_core::ResourceUri &uri,
                                 const YAML::Node &node,
                                 const std::string &field,
                                 std::initializer_list<std::string_view>
                                     allowedFields) {
  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, uri, field,
                    "bake field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    const bool allowed =
        std::any_of(allowedFields.begin(), allowedFields.end(),
                    [&](std::string_view allowed) { return key == allowed; });
    if (!allowed) {
      addDiagnostic(result, uri, field + "." + key,
                    "unsupported bake parameter field");
      valid = false;
    }
  }
  return valid;
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
  graph.bake = parseBakeConfig(result, uri, root["bake"]);
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
