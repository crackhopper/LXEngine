#include "infra/resource_parsers/material_pass_contract_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <string>

namespace LX_infra {
namespace {

void addDiagnostic(MaterialPassContractParseResult &result,
                   const std::string &field, const std::string &message) {
  result.diagnostics.push_back(field + ": " + message);
}

bool requireField(const YAML::Node &node,
                  MaterialPassContractParseResult &result,
                  const std::string &field) {
  if (node) {
    return true;
  }
  addDiagnostic(result, field, "missing required field");
  return false;
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

std::optional<LX_core::BlendFactor> parseBlendFactor(const YAML::Node &node) {
  const auto value = node.as<std::string>();
  if (value == "Zero") {
    return LX_core::BlendFactor::Zero;
  }
  if (value == "One") {
    return LX_core::BlendFactor::One;
  }
  if (value == "SrcAlpha") {
    return LX_core::BlendFactor::SrcAlpha;
  }
  if (value == "OneMinusSrcAlpha") {
    return LX_core::BlendFactor::OneMinusSrcAlpha;
  }
  return std::nullopt;
}

std::optional<LX_core::CompareOp> parseCompareOp(const YAML::Node &node) {
  const auto value = node.as<std::string>();
  if (value == "Less") {
    return LX_core::CompareOp::Less;
  }
  if (value == "LessEqual") {
    return LX_core::CompareOp::LessEqual;
  }
  if (value == "Greater") {
    return LX_core::CompareOp::Greater;
  }
  if (value == "Equal") {
    return LX_core::CompareOp::Equal;
  }
  if (value == "Always") {
    return LX_core::CompareOp::Always;
  }
  return std::nullopt;
}

std::vector<std::string> parseStringList(const YAML::Node &node) {
  std::vector<std::string> values;
  values.reserve(node.size());
  for (const auto &entry : node) {
    values.push_back(entry.as<std::string>());
  }
  return values;
}

bool rejectUnsupportedFields(const YAML::Node &node,
                             MaterialPassContractParseResult &result,
                             const std::string &fieldPrefix) {
  bool ok = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, fieldPrefix,
                    "pass field names must be scalar strings");
      ok = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "id" || key == "shader" || key == "stage" || key == "dispatch" ||
        key == "sources" || key == "targets" || key == "renderState" ||
        key == "writeMode" || key == "filters" || key == "variants" ||
        key == "parameters" || key == "resources") {
      continue;
    }
    if (key == "enginePass") {
      addDiagnostic(result, fieldPrefix + ".enginePass",
                    "legacy enginePass bridge is removed; pass name is the "
                    "runtime pass identity");
    } else {
      addDiagnostic(result, fieldPrefix + "." + key,
                    "unsupported pass contract field");
    }
    ok = false;
  }
  return ok;
}

std::optional<LX_core::RenderState>
parseRenderState(const YAML::Node &node,
                 MaterialPassContractParseResult &result,
                 const std::string &field) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, field, "missing required field");
    return std::nullopt;
  }

  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, field,
                    "renderState field names must be scalar strings");
      valid = false;
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (key == "cullMode" || key == "depthTest" || key == "depthWrite" ||
        key == "depthOp" || key == "blendEnable" || key == "srcBlend" ||
        key == "dstBlend") {
      continue;
    }
    addDiagnostic(result, field + "." + key,
                  "unsupported renderState contract field");
    valid = false;
  }
  valid &= requireField(node["cullMode"], result, field + ".cullMode");
  valid &= requireField(node["depthTest"], result, field + ".depthTest");
  valid &= requireField(node["depthWrite"], result, field + ".depthWrite");
  valid &= requireField(node["depthOp"], result, field + ".depthOp");
  if (!valid) {
    return std::nullopt;
  }

  auto cullMode = parseCullMode(node["cullMode"]);
  if (!cullMode.has_value()) {
    addDiagnostic(result, field + ".cullMode", "unknown cull mode");
    return std::nullopt;
  }
  auto depthOp = parseCompareOp(node["depthOp"]);
  if (!depthOp.has_value()) {
    addDiagnostic(result, field + ".depthOp", "unknown compare op");
    return std::nullopt;
  }

  LX_core::RenderState state;
  state.cullMode = *cullMode;
  state.depthTestEnable = node["depthTest"].as<bool>();
  state.depthWriteEnable = node["depthWrite"].as<bool>();
  state.depthOp = *depthOp;
  if (const auto blend = node["blendEnable"]) {
    state.blendEnable = blend.as<bool>();
  }
  if (const auto srcBlend = node["srcBlend"]) {
    auto parsed = parseBlendFactor(srcBlend);
    if (!parsed.has_value()) {
      addDiagnostic(result, field + ".srcBlend", "unknown blend factor");
      return std::nullopt;
    }
    state.srcBlend = *parsed;
  }
  if (const auto dstBlend = node["dstBlend"]) {
    auto parsed = parseBlendFactor(dstBlend);
    if (!parsed.has_value()) {
      addDiagnostic(result, field + ".dstBlend", "unknown blend factor");
      return std::nullopt;
    }
    state.dstBlend = *parsed;
  }
  return state;
}

} // namespace

MaterialPassContractParseResult parseMaterialPassContract(
    const std::string &passName, const YAML::Node &node,
    const std::string &fieldPrefix) {
  MaterialPassContractParseResult result;
  if (!node || !node.IsMap()) {
    addDiagnostic(result, fieldPrefix, "pass must be a map");
    return result;
  }

  rejectUnsupportedFields(node, result, fieldPrefix);
  requireField(node["shader"], result, fieldPrefix + ".shader");
  requireField(node["stage"], result, fieldPrefix + ".stage");
  requireField(node["dispatch"], result, fieldPrefix + ".dispatch");
  requireField(node["sources"], result, fieldPrefix + ".sources");
  requireField(node["targets"], result, fieldPrefix + ".targets");
  auto renderState =
      parseRenderState(node["renderState"], result, fieldPrefix + ".renderState");
  if (!result.diagnostics.empty()) {
    return result;
  }

  auto stage = parseStage(node["stage"].as<std::string>());
  if (!stage.has_value()) {
    addDiagnostic(result, fieldPrefix + ".stage", "unknown stage");
    return result;
  }
  auto dispatch = parseDispatch(node["dispatch"].as<std::string>());
  if (!dispatch.has_value()) {
    addDiagnostic(result, fieldPrefix + ".dispatch", "unknown dispatch");
    return result;
  }
  if (!node["sources"].IsSequence()) {
    addDiagnostic(result, fieldPrefix + ".sources", "must be a sequence");
    return result;
  }
  if (!node["targets"].IsSequence()) {
    addDiagnostic(result, fieldPrefix + ".targets", "must be a sequence");
    return result;
  }
  if (node["shader"].as<std::string>().empty()) {
    addDiagnostic(result, fieldPrefix + ".shader", "must not be empty");
    return result;
  }
  if (node["sources"].size() == 0) {
    addDiagnostic(result, fieldPrefix + ".sources", "must not be empty");
    return result;
  }
  if (node["targets"].size() == 0) {
    addDiagnostic(result, fieldPrefix + ".targets", "must not be empty");
    return result;
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
  result.pass = std::move(pass);
  return result;
}

} // namespace LX_infra
