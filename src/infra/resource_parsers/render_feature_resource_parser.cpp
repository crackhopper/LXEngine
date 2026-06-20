#include "infra/resource_parsers/render_feature_resource_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <limits>
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

bool validateNumericScalar(ParsedRenderFeatureResource &result,
                           const LX_core::ResourceUri &uri,
                           const YAML::Node &node, const std::string &field) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(result, uri, field, "must be a numeric scalar");
    return false;
  }
  try {
    (void)node.as<double>();
  } catch (const YAML::Exception &) {
    addDiagnostic(result, uri, field, "must be a numeric scalar");
    return false;
  }
  return true;
}

bool validateIntegerScalar(ParsedRenderFeatureResource &result,
                           const LX_core::ResourceUri &uri,
                           const YAML::Node &node, const std::string &field,
                           bool unsignedOnly) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(result, uri, field, "must be an integer scalar");
    return false;
  }
  try {
    if (unsignedOnly) {
      (void)node.as<unsigned long long>();
    } else {
      (void)node.as<long long>();
    }
  } catch (const YAML::Exception &) {
    addDiagnostic(result, uri, field, "must be an integer scalar");
    return false;
  }
  return true;
}

bool validateVec3Value(ParsedRenderFeatureResource &result,
                       const LX_core::ResourceUri &uri, const YAML::Node &node,
                       const std::string &field) {
  if (!node || !node.IsSequence() || node.size() != 3) {
    addDiagnostic(result, uri, field,
                  "must be a sequence of three numeric scalars");
    return false;
  }
  for (usize i = 0; i < node.size(); ++i) {
    if (!validateNumericScalar(result, uri, node[i],
                               field + "[" + std::to_string(i) + "]")) {
      return false;
    }
  }
  return true;
}

bool validateParameterValue(ParsedRenderFeatureResource &result,
                            const LX_core::ResourceUri &uri,
                            const YAML::Node &node, const std::string &field,
                            const std::string &kind) {
  if (kind == "bool") {
    if (!node || !node.IsScalar()) {
      addDiagnostic(result, uri, field, "must be a boolean");
      return false;
    }
    const std::string value = node.as<std::string>();
    if (value == "true" || value == "false") {
      return true;
    }
    addDiagnostic(result, uri, field, "expected true or false");
    return false;
  }
  if (kind == "float") {
    return validateNumericScalar(result, uri, node, field);
  }
  if (kind == "integer") {
    return validateIntegerScalar(result, uri, node, field, false);
  }
  if (kind == "u32" || kind == "uint") {
    return validateIntegerScalar(result, uri, node, field, true);
  }
  if (kind == "vec3") {
    return validateVec3Value(result, uri, node, field);
  }
  if (kind == "enum") {
    if (!node || !node.IsScalar()) {
      addDiagnostic(result, uri, field, "must be a scalar string");
      return false;
    }
  }
  return true;
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
  return kind == "textureCube";
}

std::vector<std::string>
parseScalarStringSequence(ParsedRenderFeatureResource &result,
                          const LX_core::ResourceUri &uri,
                          const YAML::Node &node, const std::string &field) {
  std::vector<std::string> values;
  if (!node || !node.IsSequence()) {
    addDiagnostic(result, uri, field, "must be a sequence");
    return values;
  }
  for (usize i = 0; i < node.size(); ++i) {
    if (!node[i].IsScalar()) {
      addDiagnostic(result, uri, field, "all entries must be scalar strings");
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
        key == "level" || key == "shader" || key == "parameters" ||
        key == "resources" || key == "hitShaderTable") {
      continue;
    }
    if (key == "rayPrograms") {
      addDiagnostic(result, uri, key,
                    "unsupported render feature field; use hitShaderTable");
    } else if (key == "pass" || key == "passes" || key == "phase" ||
               key == "renderState" || key == "techniques") {
      addDiagnostic(result, uri, key,
                    "RenderFeature shader is ABI metadata only; render-flow "
                    "fields belong in RenderPathGraph");
    } else {
      addDiagnostic(result, uri, key, "unsupported render feature field");
    }
  }
}

std::optional<LX_core::RenderFeatureResourceApi>
parseResourceApi(ParsedRenderFeatureResource &result,
                 const LX_core::ResourceUri &uri, const YAML::Node &node,
                 const std::string &field) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(result, uri, field, "missing required field");
    return std::nullopt;
  }
  const std::string value = node.as<std::string>();
  if (value == "scene-acceleration") {
    return LX_core::RenderFeatureResourceApi::SceneAcceleration;
  }
  addDiagnostic(result, uri, field,
                "unsupported resource api; expected scene-acceleration");
  return std::nullopt;
}

std::optional<LX_core::RenderFeatureResourceImplementation>
parseResourceImplementation(ParsedRenderFeatureResource &result,
                            const LX_core::ResourceUri &uri,
                            const YAML::Node &node,
                            const std::string &field) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(result, uri, field, "missing required field");
    return std::nullopt;
  }
  const std::string value = node.as<std::string>();
  if (value == "software-bvh") {
    return LX_core::RenderFeatureResourceImplementation::SoftwareBvh;
  }
  if (value == "hardware-rt") {
    return LX_core::RenderFeatureResourceImplementation::HardwareRayTracing;
  }
  addDiagnostic(result, uri, field,
                "unsupported resource implementation; expected software-bvh "
                "or hardware-rt");
  return std::nullopt;
}

bool isIblBakeOutputResourceName(const std::string &name) {
  return name == "diffuse_sh9" || name == "specular_prefilter" ||
         name == "brdf_lut" || name == "scene.environmentBake" ||
         name == "scene.materialIblBake";
}

bool isSystemOwnedResourceBinding(const std::string &binding) {
  static constexpr const char *kSystemOwnedBindings[] = {
      "SceneObjects",    "SceneDraws",          "SceneMaterials",
      "SceneTextures",   "SceneMaterialRefs",   "SceneSourceMaterialRecords",
      "SceneGeometry",   "SceneMeshlets",       "SceneLights",
      "SceneAttributes", "ScenePrimitiveData",  "PrimitiveHitGroups",
  };
  return std::find(std::begin(kSystemOwnedBindings),
                   std::end(kSystemOwnedBindings),
                   binding) != std::end(kSystemOwnedBindings);
}

void parseRenderFeatureResources(ParsedRenderFeatureResource &result,
                                 const LX_core::ResourceUri &uri,
                                 const YAML::Node &resources,
                                 LX_core::RenderFeature &feature) {
  if (!resources) {
    return;
  }
  if (!resources.IsMap()) {
    addDiagnostic(result, uri, "resources", "resources must be a map");
    return;
  }
  for (auto it = resources.begin(); it != resources.end(); ++it) {
    if (!it->first.IsScalar()) {
      addDiagnostic(result, uri, "resources",
                    "resource names must be scalar strings");
      continue;
    }
    const std::string name = it->first.as<std::string>();
    const YAML::Node resourceNode = it->second;
    const std::string field = "resources." + name;
    if (isIblBakeOutputResourceName(name)) {
      addDiagnostic(result, uri, field,
                    "IBL bake outputs belong to graph readbacks, not "
                    "RenderFeature resources");
      continue;
    }
    if (!resourceNode || !resourceNode.IsMap()) {
      addDiagnostic(result, uri, field, "resource must be a map");
      continue;
    }
    for (auto resourceField = resourceNode.begin();
         resourceField != resourceNode.end(); ++resourceField) {
      if (!resourceField->first.IsScalar()) {
        addDiagnostic(result, uri, field,
                      "resource field names must be scalar strings");
        continue;
      }
      const std::string key = resourceField->first.as<std::string>();
      if (key == "api" || key == "function" || key == "implementation" ||
          key == "derived" || key == "volatile" || key == "source" ||
          key == "output" || key == "required") {
        continue;
      }
      addDiagnostic(result, uri, field + "." + key,
                    "unsupported render feature resource field");
    }

    LX_core::RenderFeatureResourceRequirement requirement;
    const auto api =
        parseResourceApi(result, uri, resourceNode["api"], field + ".api");
    const auto implementation = parseResourceImplementation(
        result, uri, resourceNode["implementation"],
        field + ".implementation");
    if (api.has_value()) {
      requirement.api = *api;
    }
    if (implementation.has_value()) {
      requirement.implementation = *implementation;
    }
    if (!resourceNode["function"] || !resourceNode["function"].IsScalar()) {
      addDiagnostic(result, uri, field + ".function",
                    "missing required field");
    } else {
      requirement.function = resourceNode["function"].as<std::string>();
      if (requirement.function != "buildSceneAcceleration") {
        addDiagnostic(result, uri, field + ".function",
                      "unsupported resource function; expected "
                      "buildSceneAcceleration");
      }
    }
    if (resourceNode["derived"]) {
      requirement.derived = parseBoolScalar(
          result, uri, resourceNode["derived"], field + ".derived");
      if (!requirement.derived) {
        addDiagnostic(result, uri, field + ".derived",
                      "feature resources must be derived");
      }
    }
    if (resourceNode["volatile"]) {
      requirement.volatileRuntime = parseBoolScalar(
          result, uri, resourceNode["volatile"], field + ".volatile");
      if (!requirement.volatileRuntime) {
        addDiagnostic(result, uri, field + ".volatile",
                      "feature resources must be volatile");
      }
    }
    if (!resourceNode["source"] || !resourceNode["source"].IsScalar()) {
      addDiagnostic(result, uri, field + ".source", "missing required field");
    } else {
      requirement.source = resourceNode["source"].as<std::string>();
      if (requirement.source != "scene.selection") {
        addDiagnostic(result, uri, field + ".source",
                      "unsupported resource source; expected scene.selection");
      }
    }
    if (resourceNode["required"]) {
      requirement.required = parseBoolScalar(
          result, uri, resourceNode["required"], field + ".required");
    }

    const YAML::Node output = resourceNode["output"];
    if (!output || !output.IsMap()) {
      addDiagnostic(result, uri, field + ".output",
                    "missing required field");
    } else {
      for (auto outputField = output.begin(); outputField != output.end();
           ++outputField) {
        if (!outputField->first.IsScalar()) {
          addDiagnostic(result, uri, field + ".output",
                        "output field names must be scalar strings");
          continue;
        }
        const std::string key = outputField->first.as<std::string>();
        if (key == "kind" || key == "binding" || key == "layout" ||
            key == "elementType") {
          continue;
        }
        addDiagnostic(result, uri, field + ".output." + key,
                      "unsupported render feature resource output field");
      }
      if (!output["kind"] || !output["kind"].IsScalar()) {
        addDiagnostic(result, uri, field + ".output.kind",
                      "missing required field");
      } else {
        requirement.output.kind = output["kind"].as<std::string>();
      }
      if (!output["binding"] || !output["binding"].IsScalar()) {
        addDiagnostic(result, uri, field + ".output.binding",
                      "missing required field");
      } else {
        requirement.output.binding = output["binding"].as<std::string>();
      }
      if (output["layout"]) {
        requirement.output.layout = output["layout"].as<std::string>();
      }
      if (output["elementType"]) {
        requirement.output.elementType = output["elementType"].as<std::string>();
      }
      if (isSystemOwnedResourceBinding(requirement.output.binding)) {
        addDiagnostic(result, uri, field + ".output.binding",
                      "system-owned scene/material bindings are not authored "
                      "as RenderFeature resources");
      }
      if (requirement.implementation ==
              LX_core::RenderFeatureResourceImplementation::SoftwareBvh &&
          requirement.output.kind != "storage-buffer") {
        addDiagnostic(result, uri, field + ".output.kind",
                      "software-bvh resource output must be storage-buffer");
      }
      if (requirement.implementation ==
              LX_core::RenderFeatureResourceImplementation::HardwareRayTracing &&
          requirement.output.kind != "acceleration-structure") {
        addDiagnostic(
            result, uri, field + ".output.kind",
            "hardware-rt resource output must be acceleration-structure");
      }
    }
    feature.resources.emplace(name, std::move(requirement));
  }
}

std::optional<u32> parseU32Scalar(ParsedRenderFeatureResource &result,
                                  const LX_core::ResourceUri &uri,
                                  const YAML::Node &node,
                                  const std::string &field) {
  if (!node || !node.IsScalar()) {
    addDiagnostic(result, uri, field, "missing required field");
    return std::nullopt;
  }
  try {
    const auto value = node.as<unsigned long long>();
    if (value > std::numeric_limits<u32>::max()) {
      addDiagnostic(result, uri, field, "must fit in u32");
      return std::nullopt;
    }
    return static_cast<u32>(value);
  } catch (const YAML::Exception &) {
    addDiagnostic(result, uri, field, "must be an unsigned integer scalar");
    return std::nullopt;
  }
}

void parseRenderFeatureHitShaderTable(ParsedRenderFeatureResource &result,
                                      const LX_core::ResourceUri &uri,
                                      const YAML::Node &hitShaderTable,
                                      LX_core::RenderFeature &feature) {
  if (!hitShaderTable) {
    return;
  }
  if (!hitShaderTable.IsMap()) {
    addDiagnostic(result, uri, "hitShaderTable", "must be a map");
    return;
  }
  for (auto field = hitShaderTable.begin(); field != hitShaderTable.end();
       ++field) {
    if (!field->first.IsScalar()) {
      addDiagnostic(result, uri, "hitShaderTable",
                    "field names must be scalar strings");
      continue;
    }
    const std::string key = field->first.as<std::string>();
    if (key == "payload" || key == "dispatchFunction" || key == "entries") {
      continue;
    }
    addDiagnostic(result, uri, "hitShaderTable." + key,
                  "unsupported hit shader table field");
  }

  LX_core::RenderFeatureHitShaderTable table;
  if (!hitShaderTable["payload"] || !hitShaderTable["payload"].IsScalar()) {
    addDiagnostic(result, uri, "hitShaderTable.payload",
                  "missing required field");
  } else {
    table.payload = hitShaderTable["payload"].as<std::string>();
    if (table.payload != "radiance") {
      addDiagnostic(result, uri, "hitShaderTable.payload",
                    "unsupported payload; expected radiance");
    }
  }
  if (!hitShaderTable["dispatchFunction"] ||
      !hitShaderTable["dispatchFunction"].IsScalar()) {
    addDiagnostic(result, uri, "hitShaderTable.dispatchFunction",
                  "missing required field");
  } else {
    table.dispatchFunction =
        hitShaderTable["dispatchFunction"].as<std::string>();
  }

  const YAML::Node entries = hitShaderTable["entries"];
  if (!entries || !entries.IsSequence()) {
    addDiagnostic(result, uri, "hitShaderTable.entries",
                  "missing required sequence");
  } else {
    std::vector<u32> indices;
    std::vector<std::string> uris;
    for (usize i = 0; i < entries.size(); ++i) {
      const YAML::Node entryNode = entries[i];
      const std::string field =
          "hitShaderTable.entries[" + std::to_string(i) + "]";
      if (!entryNode || !entryNode.IsMap()) {
        addDiagnostic(result, uri, field, "entry must be a map");
        continue;
      }
      for (auto entryField = entryNode.begin(); entryField != entryNode.end();
           ++entryField) {
        if (!entryField->first.IsScalar()) {
          addDiagnostic(result, uri, field,
                        "entry field names must be scalar strings");
          continue;
        }
        const std::string key = entryField->first.as<std::string>();
        if (key == "index" || key == "materialType" || key == "uri" ||
            key == "function") {
          continue;
        }
        addDiagnostic(result, uri, field + "." + key,
                      "unsupported hit shader table entry field");
      }

      LX_core::RenderFeatureHitShaderTableEntry entry;
      const auto index =
          parseU32Scalar(result, uri, entryNode["index"], field + ".index");
      if (index.has_value()) {
        entry.index = *index;
        if (std::find(indices.begin(), indices.end(), entry.index) !=
            indices.end()) {
          addDiagnostic(result, uri, field + ".index",
                        "duplicate hit shader table index");
        }
        indices.push_back(entry.index);
      }
      if (!entryNode["materialType"] || !entryNode["materialType"].IsScalar()) {
        addDiagnostic(result, uri, field + ".materialType",
                      "missing required field");
      } else {
        entry.materialType = entryNode["materialType"].as<std::string>();
      }
      if (!entryNode["uri"] || !entryNode["uri"].IsScalar()) {
        addDiagnostic(result, uri, field + ".uri", "missing required field");
      } else {
        entry.uri = entryNode["uri"].as<std::string>();
        const std::string uriString = entry.uri.string();
        if (std::find(uris.begin(), uris.end(), uriString) != uris.end()) {
          addDiagnostic(result, uri, field + ".uri",
                        "duplicate hit shader table uri");
        }
        uris.push_back(uriString);
      }
      if (!entryNode["function"] || !entryNode["function"].IsScalar()) {
        addDiagnostic(result, uri, field + ".function",
                      "missing required field");
      } else {
        entry.function = entryNode["function"].as<std::string>();
      }
      table.entries.push_back(std::move(entry));
    }
  }
  feature.hitShaderTable = std::move(table);
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
          key == "sourceHash" || key == "valueType" || key == "binding" ||
          key == "member" || key == "required" || key == "volatile" ||
          key == "allowedValues" || key == "requiredWhen") {
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
    if (parameterNode["volatile"]) {
      const usize diagnosticCount = result.diagnostics.size();
      parameter.volatileRuntime = parseBoolScalar(
          result, uri, parameterNode["volatile"], field + ".volatile");
      if (result.diagnostics.size() != diagnosticCount) {
        continue;
      }
    }
    if (parameter.volatileRuntime && parameterNode["value"]) {
      addDiagnostic(result, uri, field + ".value",
                    "volatile parameter must not define value");
    }
    if (parameter.volatileRuntime && parameterNode["constantId"]) {
      addDiagnostic(result, uri, field + ".constantId",
                    "volatile parameter must not define constantId");
    }
    if (parameter.volatileRuntime && parameterNode["specialization"]) {
      addDiagnostic(result, uri, field + ".specialization",
                    "volatile parameter must not define specialization");
    }
    if (parameter.volatileRuntime && parameterNode["specializationConstant"]) {
      addDiagnostic(
          result, uri, field + ".specializationConstant",
          "volatile parameter must not define specializationConstant");
    }
    if (parameter.volatileRuntime && parameterNode["specializationConstants"]) {
      addDiagnostic(
          result, uri, field + ".specializationConstants",
          "volatile parameter must not define specializationConstants");
    }
    if (parameter.volatileRuntime && parameterNode["valueType"]) {
      addDiagnostic(result, uri, field + ".valueType",
                    "volatile parameter must not define valueType");
    }
    if (!result.diagnostics.empty()) {
      continue;
    }
    if (parameterNode["value"]) {
      const usize diagnosticCount = result.diagnostics.size();
      validateParameterValue(result, uri, parameterNode["value"],
                             field + ".value", parameter.kind);
      if (result.diagnostics.size() != diagnosticCount) {
        continue;
      }
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
    if (parameterNode["sourceHash"]) {
      if (!kindAllowsUri(parameter.kind)) {
        addDiagnostic(result, uri, field + ".sourceHash",
                      "sourceHash is only supported for resource-like "
                      "parameters");
        continue;
      }
      if (!parameterNode["sourceHash"].IsScalar()) {
        addDiagnostic(result, uri, field + ".sourceHash",
                      "must be a scalar string");
        continue;
      }
      parameter.sourceHash = parameterNode["sourceHash"].as<std::string>();
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
      parameter.required = parseBoolScalar(
          result, uri, parameterNode["required"], field + ".required");
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
        addDiagnostic(result, uri, field + ".requiredWhen", "must be a map");
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

std::optional<LX_core::RenderFeatureLevel>
parseRenderFeatureLevel(ParsedRenderFeatureResource &result,
                        const LX_core::ResourceUri &uri,
                        const YAML::Node &level) {
  if (!level || !level.IsScalar()) {
    addDiagnostic(result, uri, "level", "missing required field");
    return std::nullopt;
  }
  const std::string value = level.as<std::string>();
  if (value == "shader") {
    return LX_core::RenderFeatureLevel::Shader;
  }
  if (value == "pass") {
    return LX_core::RenderFeatureLevel::Pass;
  }
  addDiagnostic(result, uri, "level", "expected shader or pass");
  return std::nullopt;
}

std::optional<LX_core::RenderFeatureShaderContract>
parseRenderFeatureShaderContract(ParsedRenderFeatureResource &result,
                                 const LX_core::ResourceUri &uri,
                                 const YAML::Node &shader) {
  if (!shader) {
    return std::nullopt;
  }
  if (!shader.IsMap()) {
    addDiagnostic(result, uri, "shader", "must be a map");
    return std::nullopt;
  }
  for (auto field = shader.begin(); field != shader.end(); ++field) {
    if (!field->first.IsScalar()) {
      addDiagnostic(result, uri, "shader",
                    "shader field names must be scalar strings");
      continue;
    }
    const std::string key = field->first.as<std::string>();
    if (key != "uri") {
      addDiagnostic(result, uri, "shader." + key,
                    "unsupported shader contract field");
    }
  }
  if (!shader["uri"] || !shader["uri"].IsScalar()) {
    addDiagnostic(result, uri, "shader.uri", "missing required field");
    return std::nullopt;
  }
  return LX_core::RenderFeatureShaderContract{
      LX_core::ResourceUri(shader["uri"].as<std::string>())};
}

void validateRenderFeatureSchema(ParsedRenderFeatureResource &result,
                                 const LX_core::ResourceUri &uri,
                                 const LX_core::RenderFeature &feature) {
  const bool hasShaderUri =
      feature.shader.has_value() && !feature.shader->uri.empty();
  for (const auto &[name, parameter] : feature.parameters) {
    const std::string field = "parameters." + name;
    if (kindAllowsUri(parameter.kind)) {
      if (parameter.required && parameter.uri.empty()) {
        addDiagnostic(result, uri, field + ".uri", "missing required field");
      }
    } else if (parameter.required && !parameter.volatileRuntime &&
               parameter.value.empty()) {
      addDiagnostic(result, uri, field + ".value", "missing required field");
    }
    if (!parameter.binding.empty() || !parameter.member.empty()) {
      if (!hasShaderUri) {
        addDiagnostic(result, uri, "shader.uri",
                      "required for binding/member ABI parameters");
      }
      if (feature.level == LX_core::RenderFeatureLevel::Pass) {
        if (!parameter.volatileRuntime && !parameter.binding.empty()) {
          addDiagnostic(result, uri, field + ".binding",
                        "pass-level parameters must use shader reflection "
                        "specialization constants, not bindings");
        }
        if (!parameter.volatileRuntime && !parameter.member.empty()) {
          addDiagnostic(result, uri, field + ".member",
                        "pass-level parameters must use shader reflection "
                        "specialization constants, not UBO members");
        }
      }
    }
    if (parameter.volatileRuntime) {
      if (feature.level != LX_core::RenderFeatureLevel::Pass) {
        addDiagnostic(result, uri, field + ".volatile",
                      "volatile parameters are only supported for pass-level "
                      "runtime data");
      }
      if (parameter.binding.empty()) {
        addDiagnostic(result, uri, field + ".binding",
                      "volatile parameter must define binding");
      }
      if (parameter.member.empty()) {
        addDiagnostic(result, uri, field + ".member",
                      "volatile parameter must define member");
      }
    }
  }
  if (!feature.resources.empty() &&
      feature.level != LX_core::RenderFeatureLevel::Pass) {
    addDiagnostic(result, uri, "resources",
                  "RenderFeature resources are only supported for pass-level "
                  "derived resources");
  }
  if (feature.hitShaderTable.has_value() &&
      feature.level != LX_core::RenderFeatureLevel::Pass) {
    addDiagnostic(result, uri, "hitShaderTable",
                  "hitShaderTable is only supported for pass-level ray "
                  "features");
  }
}

void parseRenderFeature(ParsedRenderFeatureResource &result,
                        const LX_core::ResourceUri &uri,
                        const YAML::Node &root) {
  rejectRenderFeatureFlowFields(result, uri, root);

  bool requiredFields = true;
  requiredFields &= requireField(result, uri, root["name"], "name");
  requiredFields &= requireField(result, uri, root["feature"], "feature");
  const auto level = parseRenderFeatureLevel(result, uri, root["level"]);
  const auto shader =
      parseRenderFeatureShaderContract(result, uri, root["shader"]);
  if (!requiredFields) {
    return;
  }

  LX_core::RenderFeature feature;
  feature.name = root["name"].as<std::string>();
  feature.feature = root["feature"].as<std::string>();
  if (level.has_value()) {
    feature.level = *level;
  }
  if (shader.has_value()) {
    feature.shader = *shader;
  }
  parseRenderFeatureParameters(result, uri, root["parameters"], feature);
  parseRenderFeatureResources(result, uri, root["resources"], feature);
  parseRenderFeatureHitShaderTable(result, uri, root["hitShaderTable"],
                                   feature);
  validateRenderFeatureSchema(result, uri, feature);

  if (!result.diagnostics.empty()) {
    return;
  }
  result.renderFeature = std::move(feature);
}

} // namespace

ParsedRenderFeatureResource
RenderFeatureResourceParser::parse(const LX_core::ResourceUri &uri,
                                   std::string_view yamlText) const {
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
