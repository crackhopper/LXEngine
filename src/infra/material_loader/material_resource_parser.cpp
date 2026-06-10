#include "infra/material_loader/material_resource_parser.hpp"

#include "core/asset/material_surface_schema.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <sstream>
#include <string>

namespace LX_infra {
namespace {

using LX_core::MaterialEnvelopeKind;
using LX_core::MaterialEnvelopeValueType;
using LX_core::MaterialParameterEnvelope;

void addDiagnostic(ParsedMaterialResource &result, const std::string &uri,
                   const std::string &field, const std::string &message) {
  result.diagnostics.push_back(uri + ": " + field + ": " + message);
}

void addDiagnostic(ParsedMaterialResource &result,
                   const LX_core::ResourceUri &uri, const std::string &field,
                   const std::string &message) {
  addDiagnostic(result, uri.string(), field, message);
}

[[nodiscard]] MaterialEnvelopeKind parseKind(const std::string &value,
                                             bool &ok) {
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

[[nodiscard]] MaterialEnvelopeValueType
parseValueType(const std::string &value, bool &ok) {
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

[[nodiscard]] bool parseRgb(const YAML::Node &node, LX_core::Vec3f &out) {
  if (!node.IsSequence() || node.size() != 3) {
    return false;
  }
  out = LX_core::Vec3f{node[0].as<float>(), node[1].as<float>(),
                       node[2].as<float>()};
  return true;
}

[[nodiscard]] bool isAllowedKind(const LX_core::MaterialParameterSchema &schema,
                                 MaterialEnvelopeKind kind) {
  return std::find(schema.allowedKinds.begin(), schema.allowedKinds.end(),
                   kind) != schema.allowedKinds.end();
}

[[nodiscard]] std::optional<MaterialParameterEnvelope>
parseEnvelope(const YAML::Node &node, const std::string &uri,
              const std::string &field, ParsedMaterialResource &result) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, uri, field, "envelope must be a map");
    return std::nullopt;
  }
  const YAML::Node kindNode = node["kind"];
  if (!kindNode || !kindNode.IsScalar()) {
    addDiagnostic(result, uri, field, "missing scalar kind");
    return std::nullopt;
  }

  bool kindOk = false;
  MaterialParameterEnvelope envelope;
  envelope.kind = parseKind(kindNode.as<std::string>(), kindOk);
  if (!kindOk) {
    addDiagnostic(result, uri, field,
                  "unknown envelope kind '" + kindNode.as<std::string>() + "'");
    return std::nullopt;
  }

  if (const YAML::Node valueTypeNode = node["valueType"]) {
    if (!valueTypeNode.IsScalar()) {
      addDiagnostic(result, uri, field, "valueType must be scalar");
      return std::nullopt;
    }
    bool valueTypeOk = false;
    envelope.valueType =
        parseValueType(valueTypeNode.as<std::string>(), valueTypeOk);
    if (!valueTypeOk) {
      addDiagnostic(result, uri, field,
                    "unknown valueType '" + valueTypeNode.as<std::string>() +
                        "'");
      return std::nullopt;
    }
  }

  if (const YAML::Node uriNode = node["uri"]) {
    if (!uriNode.IsScalar()) {
      addDiagnostic(result, uri, field, "uri must be scalar");
      return std::nullopt;
    }
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
        addDiagnostic(result, uri, field, "rgb value requires three floats");
        return std::nullopt;
      }
      envelope.rgbValue = rgb;
      break;
    }
    case MaterialEnvelopeKind::Spectrum: {
      if (valueNode.IsSequence()) {
        LX_core::Vec3f rgb;
        if (!parseRgb(valueNode, rgb)) {
          addDiagnostic(result, uri, field,
                        "spectrum inline value requires three floats");
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
      addDiagnostic(result, uri, field,
                    "resource envelope values must use uri, not value");
      return std::nullopt;
    }
  }

  const std::string shapeError = validateEnvelopeShape(envelope);
  if (!shapeError.empty()) {
    addDiagnostic(result, uri, field, shapeError);
    return std::nullopt;
  }

  return envelope;
}

[[nodiscard]] std::optional<MaterialParameterEnvelope>
parseEnvelope(const YAML::Node &node, const LX_core::ResourceUri &uri,
              const std::string &field, ParsedMaterialResource &result) {
  return parseEnvelope(node, uri.string(), field, result);
}

[[nodiscard]] bool isDependencyKind(MaterialEnvelopeKind kind) {
  return kind == MaterialEnvelopeKind::Texture ||
         kind == MaterialEnvelopeKind::Spectrum ||
         kind == MaterialEnvelopeKind::MaterialRef ||
         kind == MaterialEnvelopeKind::BsdfTable;
}

} // namespace

ParsedMaterialResource
MaterialResourceParser::parse(LX_core::SceneResourceTable &table,
                              const LX_core::ResourceUri &uri,
                              std::string_view yamlText) const {
  (void)table;
  ParsedMaterialResource result;

  YAML::Node root;
  try {
    root = YAML::Load(std::string(yamlText));
  } catch (const YAML::Exception &e) {
    addDiagnostic(result, uri, "$", e.what());
    return result;
  }

  if (!root || !root.IsMap()) {
    addDiagnostic(result, uri, "$", "root must be a YAML map");
    return result;
  }
  if (!root["schema"] || root["schema"].as<std::string>() != "lxe.material.v2") {
    addDiagnostic(result, uri, "schema", "expected lxe.material.v2");
    return result;
  }
  const YAML::Node bsdfNode = root["bsdf"];
  if (!bsdfNode || !bsdfNode.IsMap()) {
    addDiagnostic(result, uri, "bsdf", "missing BSDF map");
    return result;
  }
  if (!bsdfNode["type"] || !bsdfNode["type"].IsScalar()) {
    addDiagnostic(result, uri, "bsdf.type", "missing scalar BSDF type");
    return result;
  }

  const std::string bsdfType = bsdfNode["type"].as<std::string>();
  const LX_core::MaterialSurfaceSchema *schema =
      LX_core::findMaterialSurfaceSchema(bsdfType);
  if (schema == nullptr) {
    addDiagnostic(result, uri, "bsdf.type",
                  "unknown BSDF type '" + bsdfType + "'");
    return result;
  }

  const YAML::Node parametersNode = bsdfNode["parameters"];
  if (!parametersNode || !parametersNode.IsMap()) {
    addDiagnostic(result, uri, "bsdf.parameters", "missing parameters map");
    return result;
  }

  auto tmpl = LX_core::MaterialTemplate::create(bsdfType);
  auto instance = LX_core::MaterialInstance::createUnique(std::move(tmpl));
  instance->setBsdfType(bsdfType);

  for (const LX_core::MaterialParameterSchema &parameter :
       schema->parameters) {
    const YAML::Node parameterNode = parametersNode[parameter.name];
    if (!parameterNode) {
      if (parameter.required) {
        addDiagnostic(result, uri, "bsdf.parameters." + parameter.name,
                      "missing required parameter");
      }
      continue;
    }

    auto envelope =
        parseEnvelope(parameterNode, uri, "bsdf.parameters." + parameter.name,
                      result);
    if (!envelope.has_value()) {
      continue;
    }
    if (!isAllowedKind(parameter, envelope->kind)) {
      addDiagnostic(result, uri, "bsdf.parameters." + parameter.name,
                    "envelope kind is not allowed for parameter");
      continue;
    }
    if (bsdfType == "mix" &&
        (parameter.name == "namedmaterial1" ||
         parameter.name == "namedmaterial2")) {
      const YAML::Node childType = parameterNode["bsdfType"];
      if (childType && childType.IsScalar() &&
          childType.as<std::string>() == "mix") {
        addDiagnostic(result, uri, "bsdf.parameters." + parameter.name,
                      "mix child material cannot also be mix");
        continue;
      }
    }

    if (envelope->uri.has_value() && isDependencyKind(envelope->kind)) {
      LX_core::MaterialResourceDependency dependency;
      dependency.kind = envelope->kind;
      dependency.uri = LX_core::ResourceUri(*envelope->uri);
      dependency.parameterName = parameter.name;
      result.dependencies.push_back(dependency);
      instance->addMaterialDependency(dependency);
    }
    instance->setMaterialEnvelope(LX_core::StringID(parameter.name),
                                  std::move(*envelope));
  }

  if (!result.diagnostics.empty()) {
    return result;
  }

  result.instance = std::move(instance);
  return result;
}

} // namespace LX_infra
