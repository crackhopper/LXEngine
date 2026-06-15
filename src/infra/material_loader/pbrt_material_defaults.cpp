#include "infra/material_loader/pbrt_material_defaults.hpp"

#include <yaml-cpp/yaml.h>

namespace LX_infra {
namespace {

using LX_core::MaterialEnvelopeKind;
using LX_core::MaterialEnvelopeValueType;
using LX_core::MaterialParameterEnvelope;

std::string makeKey(std::string_view bsdfType, std::string_view parameterName) {
  return std::string(bsdfType) + "." + std::string(parameterName);
}

MaterialEnvelopeKind parseKind(const std::string &value, bool &ok) {
  ok = true;
  if (value == "float")
    return MaterialEnvelopeKind::Float;
  if (value == "rgb")
    return MaterialEnvelopeKind::Rgb;
  if (value == "spectrum")
    return MaterialEnvelopeKind::Spectrum;
  if (value == "bool")
    return MaterialEnvelopeKind::Bool;
  if (value == "string")
    return MaterialEnvelopeKind::String;
  if (value == "texture")
    return MaterialEnvelopeKind::Texture;
  if (value == "integer")
    return MaterialEnvelopeKind::Integer;
  if (value == "materialRef")
    return MaterialEnvelopeKind::MaterialRef;
  if (value == "bsdfTable")
    return MaterialEnvelopeKind::BsdfTable;
  ok = false;
  return MaterialEnvelopeKind::Float;
}

MaterialEnvelopeValueType parseValueType(const std::string &value, bool &ok) {
  ok = true;
  if (value == "float")
    return MaterialEnvelopeValueType::Float;
  if (value == "rgb")
    return MaterialEnvelopeValueType::Rgb;
  if (value == "none")
    return MaterialEnvelopeValueType::None;
  ok = false;
  return MaterialEnvelopeValueType::None;
}

bool parseRgb(const YAML::Node &node, LX_core::Vec3f &out) {
  if (!node.IsSequence() || node.size() != 3) {
    return false;
  }
  out = LX_core::Vec3f{node[0].as<float>(), node[1].as<float>(),
                       node[2].as<float>()};
  return true;
}

std::optional<MaterialParameterEnvelope>
parseEnvelope(const YAML::Node &node, std::vector<std::string> &diagnostics,
              const std::string &field) {
  if (!node || !node.IsMap()) {
    diagnostics.push_back(field + ": envelope must be a map");
    return std::nullopt;
  }
  if (!node["kind"] || !node["kind"].IsScalar()) {
    diagnostics.push_back(field + ": missing scalar kind");
    return std::nullopt;
  }

  bool kindOk = false;
  MaterialParameterEnvelope envelope;
  envelope.kind = parseKind(node["kind"].as<std::string>(), kindOk);
  if (!kindOk) {
    diagnostics.push_back(field + ": unknown kind");
    return std::nullopt;
  }

  if (const YAML::Node valueTypeNode = node["valueType"]) {
    bool valueTypeOk = false;
    envelope.valueType =
        parseValueType(valueTypeNode.as<std::string>(), valueTypeOk);
    if (!valueTypeOk) {
      diagnostics.push_back(field + ": unknown valueType");
      return std::nullopt;
    }
  }

  if (const YAML::Node uriNode = node["uri"]) {
    envelope.uri = uriNode.as<std::string>();
  }
  if (const YAML::Node valueNode = node["value"]) {
    switch (envelope.kind) {
    case MaterialEnvelopeKind::Float:
      envelope.floatValue = valueNode.as<float>();
      break;
    case MaterialEnvelopeKind::Rgb: {
      LX_core::Vec3f rgb;
      if (!parseRgb(valueNode, rgb)) {
        diagnostics.push_back(field + ": rgb value requires three floats");
        return std::nullopt;
      }
      envelope.rgbValue = rgb;
      break;
    }
    case MaterialEnvelopeKind::Spectrum: {
      if (valueNode.IsSequence()) {
        LX_core::Vec3f rgb;
        if (!parseRgb(valueNode, rgb)) {
          diagnostics.push_back(field +
                                ": spectrum value requires three floats");
          return std::nullopt;
        }
        envelope.rgbValue = rgb;
      }
      break;
    }
    case MaterialEnvelopeKind::Bool:
      envelope.boolValue = valueNode.as<bool>();
      break;
    case MaterialEnvelopeKind::String:
      envelope.stringValue = valueNode.as<std::string>();
      break;
    case MaterialEnvelopeKind::Integer:
      envelope.integerValue = valueNode.as<i32>();
      break;
    case MaterialEnvelopeKind::Texture:
    case MaterialEnvelopeKind::MaterialRef:
    case MaterialEnvelopeKind::BsdfTable:
      diagnostics.push_back(field + ": resource defaults must use uri");
      return std::nullopt;
    }
  }

  const std::string shapeError = LX_core::validateEnvelopeShape(envelope);
  if (!shapeError.empty()) {
    diagnostics.push_back(field + ": " + shapeError);
    return std::nullopt;
  }
  return envelope;
}

} // namespace

void PbrtMaterialDefaultTable::set(
    std::string bsdfType, std::string parameterName,
    LX_core::MaterialParameterEnvelope envelope) {
  m_defaults[makeKey(bsdfType, parameterName)] = std::move(envelope);
}

std::optional<std::reference_wrapper<const LX_core::MaterialParameterEnvelope>>
PbrtMaterialDefaultTable::find(std::string_view bsdfType,
                               std::string_view parameterName) const {
  const auto it = m_defaults.find(makeKey(bsdfType, parameterName));
  if (it == m_defaults.end()) {
    return std::nullopt;
  }
  return std::cref(it->second);
}

PbrtMaterialDefaultTable
loadPbrtMaterialDefaultsFromYaml(std::string_view yamlText) {
  PbrtMaterialDefaultTable table;
  YAML::Node root;
  try {
    root = YAML::Load(std::string(yamlText));
  } catch (const YAML::Exception &e) {
    table.diagnostics.push_back(e.what());
    return table;
  }

  if (!root || !root.IsMap()) {
    table.diagnostics.push_back("root must be a map");
    return table;
  }

  for (auto bsdfIt = root.begin(); bsdfIt != root.end(); ++bsdfIt) {
    const std::string bsdfType = bsdfIt->first.as<std::string>();
    if (!bsdfIt->second.IsMap()) {
      table.diagnostics.push_back(bsdfType + ": defaults must be a map");
      continue;
    }
    for (auto paramIt = bsdfIt->second.begin(); paramIt != bsdfIt->second.end();
         ++paramIt) {
      const std::string parameterName = paramIt->first.as<std::string>();
      auto envelope = parseEnvelope(paramIt->second, table.diagnostics,
                                    bsdfType + "." + parameterName);
      if (envelope.has_value()) {
        table.set(bsdfType, parameterName, std::move(*envelope));
      }
    }
  }
  return table;
}

} // namespace LX_infra
