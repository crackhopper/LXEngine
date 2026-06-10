#include "infra/resource_parsers/render_effect_resource_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <string>

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

std::optional<LX_core::MaterialPassStage>
parseStage(const std::string &value) {
  if (value == "raster") {
    return LX_core::MaterialPassStage::Raster;
  }
  if (value == "compute") {
    return LX_core::MaterialPassStage::Compute;
  }
  return std::nullopt;
}

std::optional<LX_core::MaterialPassDispatch>
parseDispatch(const std::string &value) {
  if (value == "draw") {
    return LX_core::MaterialPassDispatch::Draw;
  }
  if (value == "fullscreen") {
    return LX_core::MaterialPassDispatch::Fullscreen;
  }
  if (value == "compute") {
    return LX_core::MaterialPassDispatch::Compute;
  }
  return std::nullopt;
}

std::optional<LX_core::CullMode> parseCullMode(const YAML::Node &node) {
  const auto value = node.as<std::string>();
  if (value == "None") {
    return LX_core::CullMode::None;
  }
  if (value == "Front") {
    return LX_core::CullMode::Front;
  }
  if (value == "Back") {
    return LX_core::CullMode::Back;
  }
  return std::nullopt;
}

std::vector<std::string> parseStringList(const YAML::Node &node) {
  std::vector<std::string> values;
  for (const auto &entry : node) {
    values.push_back(entry.as<std::string>());
  }
  return values;
}

bool requireField(const YAML::Node &node, ParsedRenderEffectResource &result,
                  const std::string &uri, const std::string &field) {
  if (node) {
    return true;
  }
  addDiagnostic(result, uri, field, "missing required field");
  return false;
}

std::optional<LX_core::RenderState>
parseRenderState(const YAML::Node &node, ParsedRenderEffectResource &result,
                 const std::string &uri, const std::string &field) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, uri, field, "missing required field");
    return std::nullopt;
  }

  LX_core::RenderState state;
  if (!requireField(node["cullMode"], result, uri, field + ".cullMode") ||
      !requireField(node["depthTest"], result, uri, field + ".depthTest") ||
      !requireField(node["depthWrite"], result, uri, field + ".depthWrite")) {
    return std::nullopt;
  }

  const auto cullMode = parseCullMode(node["cullMode"]);
  if (!cullMode.has_value()) {
    addDiagnostic(result, uri, field + ".cullMode", "unknown cull mode");
    return std::nullopt;
  }
  state.cullMode = *cullMode;
  state.depthTestEnable = node["depthTest"].as<bool>();
  state.depthWriteEnable = node["depthWrite"].as<bool>();
  if (const auto blend = node["blendEnable"]) {
    state.blendEnable = blend.as<bool>();
  }
  return state;
}

std::optional<LX_core::MaterialPassContract>
parsePass(const std::string &passName, const YAML::Node &node,
          ParsedRenderEffectResource &result, const std::string &uri) {
  const std::string base = "passes." + passName;
  if (!node || !node.IsMap()) {
    addDiagnostic(result, uri, base, "pass must be a map");
    return std::nullopt;
  }
  if (!requireField(node["shader"], result, uri, base + ".shader") ||
      !requireField(node["stage"], result, uri, base + ".stage") ||
      !requireField(node["dispatch"], result, uri, base + ".dispatch") ||
      !requireField(node["sources"], result, uri, base + ".sources") ||
      !requireField(node["targets"], result, uri, base + ".targets")) {
    return std::nullopt;
  }
  auto renderState =
      parseRenderState(node["renderState"], result, uri, base + ".renderState");
  if (!renderState.has_value()) {
    return std::nullopt;
  }

  const auto stage = parseStage(node["stage"].as<std::string>());
  if (!stage.has_value()) {
    addDiagnostic(result, uri, base + ".stage", "unknown stage");
    return std::nullopt;
  }
  const auto dispatch = parseDispatch(node["dispatch"].as<std::string>());
  if (!dispatch.has_value()) {
    addDiagnostic(result, uri, base + ".dispatch", "unknown dispatch");
    return std::nullopt;
  }
  if (!node["sources"].IsSequence()) {
    addDiagnostic(result, uri, base + ".sources", "must be a sequence");
    return std::nullopt;
  }
  if (!node["targets"].IsSequence()) {
    addDiagnostic(result, uri, base + ".targets", "must be a sequence");
    return std::nullopt;
  }

  LX_core::MaterialPassContract pass;
  pass.name = passName;
  pass.shaderUri = node["shader"].as<std::string>();
  pass.stage = *stage;
  pass.dispatch = *dispatch;
  pass.sources = parseStringList(node["sources"]);
  pass.targets = parseStringList(node["targets"]);
  pass.renderState = *renderState;
  if (const auto writeMode = node["writeMode"]) {
    pass.writeMode = writeMode.as<std::string>();
  }
  return pass;
}

std::optional<LX_core::MaterialPassContract>
parsePass(const std::string &passName, const YAML::Node &node,
          ParsedRenderEffectResource &result, const LX_core::ResourceUri &uri) {
  return parsePass(passName, node, result, uri.string());
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
  if (!root["schema"] || root["schema"].as<std::string>() !=
                             "lxe.render-effect.v1") {
    addDiagnostic(result, uri, "schema", "expected lxe.render-effect.v1");
    return result;
  }
  if (!root["phase"] || !root["phase"].IsScalar()) {
    addDiagnostic(result, uri, "phase", "missing required field");
    return result;
  }

  LX_core::RenderEffect effect;
  const std::string phase = root["phase"].as<std::string>();
  if (phase == "pre") {
    effect.phase = LX_core::RenderEffectPhase::Pre;
  } else if (phase == "post") {
    effect.phase = LX_core::RenderEffectPhase::Post;
  } else {
    addDiagnostic(result, uri, "phase", "expected pre or post");
    return result;
  }
  if (const auto name = root["name"]) {
    effect.name = name.as<std::string>();
  }
  effect.technique.name = effect.name.empty() ? "default" : effect.name;

  const YAML::Node passes = root["passes"];
  if (!passes || !passes.IsMap() || passes.size() == 0) {
    addDiagnostic(result, uri, "passes", "missing non-empty passes map");
    return result;
  }
  for (auto it = passes.begin(); it != passes.end(); ++it) {
    const std::string passName = it->first.as<std::string>();
    auto pass = parsePass(passName, it->second, result, uri);
    if (pass.has_value()) {
      effect.technique.passes.push_back(std::move(*pass));
    }
  }

  if (!result.diagnostics.empty()) {
    return result;
  }
  result.effect = std::move(effect);
  return result;
}

} // namespace LX_infra
