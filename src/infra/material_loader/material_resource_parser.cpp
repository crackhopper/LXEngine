#include "infra/material_loader/material_resource_parser.hpp"

#include "core/asset/material_surface_schema.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

[[nodiscard]] MaterialEnvelopeValueType parseValueType(const std::string &value,
                                                       bool &ok) {
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

[[nodiscard]] bool
hasSchemaParameter(const LX_core::MaterialSurfaceSchema &schema,
                   const std::string &name) {
  return std::find_if(schema.parameters.begin(), schema.parameters.end(),
                      [&name](const LX_core::MaterialParameterSchema &item) {
                        return item.name == name;
                      }) != schema.parameters.end();
}

[[nodiscard]] bool isAllowedEnvelopeField(std::string_view name) {
  return name == "kind" || name == "value" || name == "uri" ||
         name == "valueType";
}

[[nodiscard]] bool validateEnvelopeFields(const YAML::Node &node,
                                          const std::string &uri,
                                          const std::string &field,
                                          ParsedMaterialResource &result) {
  bool valid = true;
  for (auto it = node.begin(); it != node.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, uri, field,
                    "envelope field names must be scalar strings");
      valid = false;
      continue;
    }

    const std::string fieldName = it->first.as<std::string>();
    if (!isAllowedEnvelopeField(fieldName)) {
      addDiagnostic(result, uri, field + "." + fieldName,
                    "unsupported runtime envelope field; provenance belongs "
                    "in converter diagnostics or manifest");
      valid = false;
    }
  }
  return valid;
}

[[nodiscard]] bool isRemovedRootPbrParameterClass(std::string_view name) {
  return name == "baseColor" || name == "baseColorFactor" ||
         name == "metallic" || name == "metallicFactor" ||
         name == "roughness" || name == "roughnessFactor" || name == "ao";
}

[[nodiscard]] bool
validateNoLegacyRootParameterModel(const YAML::Node &root,
                                   const LX_core::ResourceUri &uri,
                                   ParsedMaterialResource &result) {
  bool valid = true;
  if (const YAML::Node parametersNode = root["parameters"]) {
    addDiagnostic(
        result, uri, "root.parameter-map",
        "root-level shader parameter maps are not part of material v2; use "
        "bsdf.parameters envelopes");
    valid = false;

    if (parametersNode.IsMap()) {
      for (auto it = parametersNode.begin(); it != parametersNode.end(); ++it) {
        if (!it->first.IsScalar()) {
          continue;
        }
        addDiagnostic(result, uri,
                      "root.parameter-map." + it->first.as<std::string>(),
                      "shader-binding parameter entry is not runtime material "
                      "truth");
      }
    }
  }

  for (auto it = root.begin(); it != root.end(); ++it) {
    if (!it->first.IsScalar()) {
      continue;
    }
    const std::string key = it->first.as<std::string>();
    if (isRemovedRootPbrParameterClass(key)) {
      addDiagnostic(result, uri, "root.pbr-parameter-field." + key,
                    "root-level PBR parameter field is not part of material "
                    "v2; use PBRT BSDF parameter envelopes");
      valid = false;
    }
  }
  return valid;
}

[[nodiscard]] bool
validateNoLegacyRootResources(const YAML::Node &root,
                              const LX_core::ResourceUri &uri,
                              ParsedMaterialResource &result) {
  const YAML::Node resourcesNode = root["resources"];
  if (!resourcesNode) {
    return true;
  }

  addDiagnostic(
      result, uri, "root.resource-map",
      "root-level shader resource maps are not part of material v2; use "
      "resource envelopes under bsdf.parameters");
  if (resourcesNode.IsMap()) {
    for (auto it = resourcesNode.begin(); it != resourcesNode.end(); ++it) {
      if (!it->first.IsScalar()) {
        continue;
      }
      addDiagnostic(result, uri,
                    "root.resource-map." + it->first.as<std::string>(),
                    "shader resource binding entry is not runtime material "
                    "truth");
    }
  }
  return false;
}

[[nodiscard]] bool isRenderFlowRootField(std::string_view name) {
  return name == "shader" || name == "variants" || name == "variantRules" ||
         name == "defaultTechnique" || name == "techniques" ||
         name == "passes" || name == "renderPaths" || name == "sources" ||
         name == "targets" || name == "renderState" || name == "shadingModel" ||
         name == "meshOverlay";
}

[[nodiscard]] bool validateNoRenderFlowFields(const YAML::Node &root,
                                              const LX_core::ResourceUri &uri,
                                              ParsedMaterialResource &result) {
  bool valid = true;
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (!it->first.IsScalar()) {
      continue;
    }

    const std::string key = it->first.as<std::string>();
    if (isRenderFlowRootField(key)) {
      addDiagnostic(result, uri, "root.render-flow-field." + key,
                    "render-flow and shader fields are not part of "
                    "MaterialResourceParser; keep them in technique/effect "
                    "contracts");
      valid = false;
    }
  }
  return valid;
}

[[nodiscard]] std::vector<std::string>
parseTags(const YAML::Node &root, const LX_core::ResourceUri &uri,
          ParsedMaterialResource &result) {
  std::vector<std::string> tags;
  const YAML::Node tagsNode = root["tags"];
  if (!tagsNode) {
    return tags;
  }
  if (!tagsNode.IsSequence()) {
    addDiagnostic(result, uri, "tags", "tags must be a sequence of strings");
    return tags;
  }
  tags.reserve(tagsNode.size());
  for (std::size_t i = 0; i < tagsNode.size(); ++i) {
    const YAML::Node tagNode = tagsNode[i];
    if (!tagNode.IsScalar()) {
      addDiagnostic(result, uri, "tags." + std::to_string(i),
                    "tag must be a scalar string");
      continue;
    }
    tags.push_back(tagNode.as<std::string>());
  }
  return tags;
}

[[nodiscard]] std::unordered_map<std::string, std::string>
parseAuthoringMetadata(const YAML::Node &root, const LX_core::ResourceUri &uri,
                       ParsedMaterialResource &result) {
  std::unordered_map<std::string, std::string> metadata;
  const YAML::Node metadataNode = root["metadata"];
  if (!metadataNode) {
    return metadata;
  }
  if (!metadataNode.IsMap()) {
    addDiagnostic(result, uri, "metadata",
                  "metadata must be a map of scalar strings");
    return metadata;
  }
  for (auto it = metadataNode.begin(); it != metadataNode.end(); ++it) {
    if (!it->first.IsScalar() || !it->second.IsScalar()) {
      addDiagnostic(result, uri, "metadata",
                    "metadata keys and values must be scalar strings");
      continue;
    }
    metadata.emplace(it->first.as<std::string>(), it->second.as<std::string>());
  }
  return metadata;
}

[[nodiscard]] std::optional<MaterialParameterEnvelope>
parseEnvelope(const YAML::Node &node, const std::string &uri,
              const std::string &field, ParsedMaterialResource &result) {
  if (!node || !node.IsMap()) {
    addDiagnostic(result, uri, field, "envelope must be a map");
    return std::nullopt;
  }
  if (!validateEnvelopeFields(node, uri, field, result)) {
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

[[nodiscard]] LX_core::SceneResourceType
sceneResourceTypeForDependency(MaterialEnvelopeKind kind) {
  switch (kind) {
  case MaterialEnvelopeKind::Texture:
    return LX_core::SceneResourceType::Texture;
  case MaterialEnvelopeKind::Spectrum:
    return LX_core::SceneResourceType::Spectrum;
  case MaterialEnvelopeKind::MaterialRef:
    return LX_core::SceneResourceType::MaterialHeader;
  case MaterialEnvelopeKind::BsdfTable:
    return LX_core::SceneResourceType::BsdfTable;
  case MaterialEnvelopeKind::Float:
  case MaterialEnvelopeKind::Rgb:
  case MaterialEnvelopeKind::Bool:
  case MaterialEnvelopeKind::String:
  case MaterialEnvelopeKind::Integer:
    break;
  }
  return LX_core::SceneResourceType::Material;
}

[[nodiscard]] bool hasUriScheme(const LX_core::ResourceUri &uri) {
  return uri.string().find("://") != std::string::npos;
}

[[nodiscard]] bool validateMaterialRefHeaderIfLocal(
    const LX_core::ResourceUri &ownerUri, const LX_core::ResourceUri &targetUri,
    const std::string &field, ParsedMaterialResource &result) {
  const std::filesystem::path path(targetUri.string());
  if (path.extension() != ".material") {
    addDiagnostic(
        result, ownerUri, field,
        "parser=MaterialResourceParser resource=" + targetUri.string() +
            ": material reference must target a .material URI");
    return false;
  }
  if (hasUriScheme(targetUri)) {
    return true;
  }
  if (!std::filesystem::exists(path)) {
    addDiagnostic(
        result, ownerUri, field,
        "parser=MaterialResourceParser resource=" + targetUri.string() +
            ": material reference header not found");
    return false;
  }

  YAML::Node root;
  try {
    root = YAML::LoadFile(targetUri.string());
  } catch (const YAML::Exception &e) {
    addDiagnostic(
        result, ownerUri, field,
        "parser=MaterialResourceParser resource=" + targetUri.string() +
            ": failed to read material reference header: " + e.what());
    return false;
  }

  if (!root || !root.IsMap() || !root["schema"] ||
      root["schema"].as<std::string>() != "lxe.material.v2") {
    addDiagnostic(
        result, ownerUri, field,
        "parser=MaterialResourceParser resource=" + targetUri.string() +
            ": material reference header must use schema "
            "lxe.material.v2");
    return false;
  }
  const YAML::Node bsdfNode = root["bsdf"];
  if (!bsdfNode || !bsdfNode.IsMap() || !bsdfNode["type"] ||
      !bsdfNode["type"].IsScalar()) {
    addDiagnostic(
        result, ownerUri, field,
        "parser=MaterialResourceParser resource=" + targetUri.string() +
            ": material reference header missing bsdf.type");
    return false;
  }
  if (bsdfNode["type"].as<std::string>() == "mix") {
    addDiagnostic(
        result, ownerUri, field,
        "parser=MaterialResourceParser resource=" + targetUri.string() +
            ": mix child material cannot also be mix");
    return false;
  }
  return true;
}

} // namespace

ParsedMaterialResource
MaterialResourceParser::parse(LX_core::SceneResourceTable &table,
                              const LX_core::ResourceUri &uri,
                              std::string_view yamlText) const {
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
  if (!root["schema"] ||
      root["schema"].as<std::string>() != "lxe.material.v2") {
    addDiagnostic(result, uri, "schema", "expected lxe.material.v2");
    return result;
  }
  if (!validateNoLegacyRootParameterModel(root, uri, result)) {
    return result;
  }
  if (!validateNoLegacyRootResources(root, uri, result)) {
    return result;
  }
  if (!validateNoRenderFlowFields(root, uri, result)) {
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
  if (const YAML::Node renderClassNode = root["renderClass"]) {
    if (!renderClassNode.IsScalar()) {
      addDiagnostic(result, uri, "renderClass",
                    "renderClass must be a scalar string");
    } else {
      instance->setRenderClass(renderClassNode.as<std::string>());
    }
  }
  instance->setMaterialTags(parseTags(root, uri, result));
  instance->setAuthoringMetadata(parseAuthoringMetadata(root, uri, result));
  const LX_core::ResourceIdentityHandle ownerHandle =
      table.loadOrGetResource(LX_core::SceneResourceType::Material, uri);

  for (auto parameterIt = parametersNode.begin();
       parameterIt != parametersNode.end(); ++parameterIt) {
    const std::string parameterName = parameterIt->first.as<std::string>();
    if (!hasSchemaParameter(*schema, parameterName)) {
      addDiagnostic(result, uri, "bsdf.parameters." + parameterName,
                    "unknown BSDF parameter; use the PBRT schema name and "
                    "resource envelope instead of legacy parallel aliases");
    }
  }

  for (const LX_core::MaterialParameterSchema &parameter : schema->parameters) {
    const YAML::Node parameterNode = parametersNode[parameter.name];
    if (!parameterNode) {
      if (parameter.required) {
        addDiagnostic(result, uri, "bsdf.parameters." + parameter.name,
                      "missing required parameter");
      }
      continue;
    }

    auto envelope = parseEnvelope(parameterNode, uri,
                                  "bsdf.parameters." + parameter.name, result);
    if (!envelope.has_value()) {
      continue;
    }
    if (!isAllowedKind(parameter, envelope->kind)) {
      addDiagnostic(result, uri, "bsdf.parameters." + parameter.name,
                    "envelope kind is not allowed for parameter");
      continue;
    }
    if (envelope->uri.has_value() && isDependencyKind(envelope->kind)) {
      const LX_core::ResourceUri canonicalUri =
          table.resolveUri(uri, LX_core::ResourceUri(*envelope->uri));
      const LX_core::ResourceIdentityHandle dependencyHandle =
          table.loadOrGetResource(
              sceneResourceTypeForDependency(envelope->kind), canonicalUri);
      table.registerDependency(ownerHandle, dependencyHandle);

      const std::string field = "bsdf.parameters." + parameter.name;
      if (bsdfType == "mix" &&
          envelope->kind == MaterialEnvelopeKind::MaterialRef &&
          !validateMaterialRefHeaderIfLocal(uri, canonicalUri, field, result)) {
        continue;
      }

      LX_core::MaterialResourceDependency dependency;
      dependency.kind = envelope->kind;
      dependency.uri = canonicalUri;
      dependency.resourceHandle = dependencyHandle;
      dependency.parameterName = parameter.name;
      result.dependencies.push_back(dependency);
      instance->addMaterialDependency(dependency);
      envelope->uri = canonicalUri.string();
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
