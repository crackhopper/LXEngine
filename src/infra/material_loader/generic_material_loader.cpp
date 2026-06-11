#include "generic_material_loader.hpp"
#include "core/asset/material_surface_schema.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/scene/scene_system_abi_validation.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/material_resource_parser.hpp"
#include "infra/resource_parsers/material_pass_contract_parser.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"
#include "infra/texture_loader/placeholder_textures.hpp"
#include "infra/texture_loader/texture_loader.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LX_infra {

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fatalLoader(const std::string &reason) {
  throw std::logic_error("GenericMaterialLoader " + reason);
}

[[nodiscard]] bool isMaterialV2Contract(const YAML::Node &root) {
  const YAML::Node schemaNode = root["schema"];
  return schemaNode && schemaNode.IsScalar() &&
         schemaNode.as<std::string>() == "lxe.material.v2";
}

ParsedMaterialResource
parseMaterialV2Contract(const YAML::Node &root,
                        const LX_core::ResourceUri &uri,
                        LX_core::SceneResourceTable &table) {
  MaterialResourceParser parser;
  return parser.parse(table, uri, YAML::Dump(root));
}

void validateParsedMaterialV2Envelope(const ParsedMaterialResource &parsed,
                                      const LX_core::ResourceUri &uri) {
  if (!parsed.diagnostics.empty() || !parsed.instance) {
    std::ostringstream message;
    message << uri.string() << ": invalid material v2 contract";
    for (const std::string &diagnostic : parsed.diagnostics) {
      message << "\n  " << diagnostic;
    }
    fatalLoader(message.str());
  }
}

LX_core::MaterialInstanceSharedPtr
loadMaterialV2EnvelopeContract(const YAML::Node &root,
                               const LX_core::ResourceUri &uri,
                               LX_core::SceneResourceTable &table) {
  ParsedMaterialResource parsed = parseMaterialV2Contract(root, uri, table);
  validateParsedMaterialV2Envelope(parsed, uri);
  return parsed.instance->cloneInstanceData();
}

void applyMaterialV2Envelope(LX_core::MaterialInstance &material,
                             const YAML::Node &root,
                             const LX_core::ResourceUri &uri,
                             LX_core::SceneResourceTable &table) {
  if (!isMaterialV2Contract(root)) {
    return;
  }

  ParsedMaterialResource parsed = parseMaterialV2Contract(root, uri, table);
  validateParsedMaterialV2Envelope(parsed, uri);

  const LX_core::MaterialInstance &v2Material = *parsed.instance;
  material.setBsdfType(v2Material.getBsdfType());
  material.setRenderClass(v2Material.getRenderClass());
  material.setMaterialTags(v2Material.getMaterialTags());
  material.setAuthoringMetadata(v2Material.getAuthoringMetadata());
  const LX_core::MaterialSurfaceSchema *surfaceSchema =
      LX_core::findMaterialSurfaceSchema(v2Material.getBsdfType());
  if (surfaceSchema == nullptr) {
    fatalLoader(uri.string() + ": unknown material v2 BSDF type '" +
                v2Material.getBsdfType() + "'");
  }

  for (const LX_core::MaterialParameterSchema &parameter :
       surfaceSchema->parameters) {
    auto envelope = v2Material.getMaterialEnvelope(
        LX_core::StringID(parameter.name));
    if (envelope.has_value()) {
      material.setMaterialEnvelope(LX_core::StringID(parameter.name),
                                   envelope->get());
    }
  }
  for (const LX_core::MaterialResourceDependency &dependency :
       v2Material.getMaterialDependencies()) {
    material.addMaterialDependency(dependency);
  }
}

/*****************************************************************
 * Variant helpers
 *****************************************************************/

std::vector<LX_core::ShaderVariant> mergeVariants(const YAML::Node &globalNode,
                                                  const YAML::Node &passNode) {
  std::unordered_map<std::string, bool> merged;
  if (globalNode && globalNode.IsMap()) {
    for (auto it = globalNode.begin(); it != globalNode.end(); ++it)
      merged[it->first.as<std::string>()] = it->second.as<bool>();
  }
  if (passNode && passNode.IsMap()) {
    for (auto it = passNode.begin(); it != passNode.end(); ++it)
      merged[it->first.as<std::string>()] = it->second.as<bool>();
  }
  std::vector<LX_core::ShaderVariant> result;
  result.reserve(merged.size());
  for (const auto &[name, enabled] : merged)
    result.push_back({name, enabled});
  return result;
}

LX_core::ShadingModel parseShadingModel(const YAML::Node &node) {
  if (!node || !node.IsDefined() || node.IsNull())
    return LX_core::ShadingModel::Smooth;

  const auto value = node.as<std::string>();
  if (value == "Smooth")
    return LX_core::ShadingModel::Smooth;
  if (value == "Flat")
    return LX_core::ShadingModel::Flat;

  fatalLoader("unknown shadingModel '" + value + "'");
}

LX_core::MeshOverlayState parseMeshOverlay(const YAML::Node &node) {
  LX_core::MeshOverlayState state;
  if (!node || !node.IsDefined() || node.IsNull())
    return state;

  if (!node.IsMap())
    fatalLoader("meshOverlay must be a map");

  if (auto enabled = node["enabled"]) {
    if (!enabled.IsScalar())
      fatalLoader("meshOverlay.enabled requires a boolean");
    try {
      state.enabled = enabled.as<bool>();
    } catch (const YAML::Exception &) {
      fatalLoader("meshOverlay.enabled requires a boolean");
    }
  }

  if (auto color = node["color"]) {
    if (!color.IsSequence() || color.size() != 4)
      fatalLoader("meshOverlay.color requires 4 values");
    std::vector<float> values;
    values.reserve(4);
    for (const auto &component : color) {
      if (!component.IsScalar())
        fatalLoader("meshOverlay.color requires 4 values");
      try {
        values.push_back(component.as<float>());
      } catch (const YAML::Exception &) {
        fatalLoader("meshOverlay.color requires 4 values");
      }
    }
    state.color = LX_core::Vec4f{values[0], values[1], values[2], values[3]};
  }

  return state;
}

void upsertVariant(std::vector<LX_core::ShaderVariant> &variants,
                   const std::string &macroName, bool enabled) {
  for (auto &variant : variants) {
    if (variant.macroName == macroName) {
      variant.enabled = enabled;
      return;
    }
  }
  variants.push_back({macroName, enabled});
}

void applyShadingModelVariants(std::vector<LX_core::ShaderVariant> &variants,
                               LX_core::ShadingModel shadingModel) {
  upsertVariant(variants, "USE_FLAT_SHADING",
                shadingModel == LX_core::ShadingModel::Flat);
}

[[nodiscard]] bool supportsIblVariant(const std::string &shaderName) {
  return shaderName == "techniques/Forward/pbr" ||
         shaderName == "techniques/Forward/pbr_clearcoat";
}

[[nodiscard]] bool isDefaultRuntimePbrShader(const std::string &shaderName) {
  return shaderName == "techniques/Forward/pbr";
}

void applyLoadOptionVariants(std::vector<LX_core::ShaderVariant> &variants,
                             const std::string &shaderName,
                             const GenericMaterialLoadOptions &options) {
  if (options.forceIbl.has_value() && supportsIblVariant(shaderName)) {
    upsertVariant(variants, "HAS_IBL", *options.forceIbl);
  }
}

void applyLoadOptionRenderState(LX_core::RenderState &renderState,
                                const GenericMaterialLoadOptions &options) {
  if (options.alphaTransparency.has_value() && !*options.alphaTransparency &&
      renderState.blendEnable) {
    renderState.blendEnable = false;
    renderState.depthWriteEnable = true;
  }
}

/*****************************************************************
 * Parameter key parsing
 *****************************************************************/

struct ParsedParam {
  std::string bindingName;
  std::string memberName;
};

ParsedParam parseParamKey(const std::string &key) {
  auto dot = key.find('.');
  if (dot == std::string::npos || dot == 0 || dot == key.size() - 1)
    fatalLoader("invalid parameter key '" + key +
                "' (expected bindingName.memberName)");
  return {key.substr(0, dot), key.substr(dot + 1)};
}

[[nodiscard]] bool isLegacyRootPbrParameterKey(const std::string &key) {
  return key == "MaterialUBO.baseColorFactor" ||
         key == "MaterialUBO.metallicFactor" ||
         key == "MaterialUBO.roughnessFactor" || key == "MaterialUBO.ao" ||
         key == "baseColorFactor" || key == "metallicFactor" ||
         key == "roughnessFactor" || key == "ao";
}

[[nodiscard]] bool isLegacyRootPbrResourceKey(const std::string &key) {
  return key == "albedoMap" || key == "normalMap" ||
         key == "metallicRoughnessMap" || key == "aoMap" ||
         key == "emissiveMap";
}

void rejectLegacyRootPbrTruthForDefaultPbr(
    const YAML::Node &paramsNode, const YAML::Node &resourcesNode,
    const std::string &rootShaderName, const std::string &passShaderName,
    const std::string &context) {
  if (!isDefaultRuntimePbrShader(rootShaderName) &&
      !isDefaultRuntimePbrShader(passShaderName)) {
    return;
  }

  if (paramsNode && paramsNode.IsMap()) {
    for (auto it = paramsNode.begin(); it != paramsNode.end(); ++it) {
      const std::string key = it->first.as<std::string>();
      if (isLegacyRootPbrParameterKey(key)) {
        fatalLoader(context + ": legacy PBR root material truth parameter '" +
                    key + "' is not accepted for default/runtime PBR");
      }
    }
  }

  if (resourcesNode && resourcesNode.IsMap()) {
    for (auto it = resourcesNode.begin(); it != resourcesNode.end(); ++it) {
      const std::string key = it->first.as<std::string>();
      if (isLegacyRootPbrResourceKey(key)) {
        fatalLoader(context + ": legacy PBR root material truth resource '" +
                    key + "' is not accepted for default/runtime PBR");
      }
    }
  }
}

/*****************************************************************
 * Validation: YAML declarations vs shader reflection
 *****************************************************************/

void validateParametersAgainstReflection(
    const YAML::Node &paramsNode,
    const std::vector<LX_core::ShaderResourceBinding> &matBindings,
    const std::string &context) {
  if (!paramsNode || !paramsNode.IsMap())
    return;

  // Build a lookup: bindingName -> set of member names.
  std::unordered_map<std::string, std::unordered_set<std::string>> reflected;
  for (const auto &binding : matBindings) {
    auto &members = reflected[binding.name];
    for (const auto &m : binding.members)
      members.insert(m.name);
  }

  for (auto it = paramsNode.begin(); it != paramsNode.end(); ++it) {
    auto [bindingName, memberName] = parseParamKey(it->first.as<std::string>());
    auto bindIt = reflected.find(bindingName);
    if (bindIt == reflected.end())
      fatalLoader(context + ": parameter binding '" + bindingName +
                  "' not found in shader reflection");
    if (bindIt->second.find(memberName) == bindIt->second.end())
      fatalLoader(context + ": member '" + memberName +
                  "' not found in binding '" + bindingName + "'");
  }
}

void validateResourcesAgainstReflection(
    const YAML::Node &resourcesNode,
    const std::vector<LX_core::ShaderResourceBinding> &matBindings,
    const std::string &context) {
  if (!resourcesNode || !resourcesNode.IsMap())
    return;

  std::unordered_set<std::string> textureBindings;
  for (const auto &binding : matBindings) {
    if (binding.type == LX_core::ShaderPropertyType::Texture2D ||
        binding.type == LX_core::ShaderPropertyType::TextureCube)
      textureBindings.insert(binding.name);
  }

  for (auto it = resourcesNode.begin(); it != resourcesNode.end(); ++it) {
    const auto name = it->first.as<std::string>();
    if (textureBindings.find(name) == textureBindings.end())
      fatalLoader(context + ": resource '" + name +
                  "' not found as a texture binding in shader reflection");
  }
}

/*****************************************************************
 * Parameter application
 *****************************************************************/

void applyParameters(LX_core::MaterialInstance &mat,
                     const YAML::Node &paramsNode) {
  if (!paramsNode || !paramsNode.IsMap())
    return;

  for (auto it = paramsNode.begin(); it != paramsNode.end(); ++it) {
    const auto key = it->first.as<std::string>();
    const YAML::Node valueNode = it->second;
    auto [bindingName, memberName] = parseParamKey(key);

    const auto bindingId = LX_core::StringID(bindingName);
    const auto memberId = LX_core::StringID(memberName);

    // Find member type from binding.
    auto binding = mat.getParameterBufferLayout(bindingId);
    if (!binding) {
      fatalLoader("parameter binding '" + bindingName +
                  "' has no canonical parameter data");
    }

    LX_core::ShaderPropertyType memberType = LX_core::ShaderPropertyType::Float;
    bool found = false;
    for (const auto &m : binding->get().members) {
      if (m.name == memberName) {
        memberType = m.type;
        found = true;
        break;
      }
    }
    if (!found)
      fatalLoader("member '" + memberName + "' not found in binding '" +
                  bindingName + "'");

    switch (memberType) {
    case LX_core::ShaderPropertyType::Float:
      mat.setParameter(bindingId, memberId, valueNode.as<float>());
      break;
    case LX_core::ShaderPropertyType::Int:
      mat.setParameter(bindingId, memberId, valueNode.as<i32>());
      break;
    case LX_core::ShaderPropertyType::Vec3: {
      auto seq = valueNode.as<std::vector<float>>();
      if (seq.size() != 3)
        fatalLoader("Vec3 parameter '" + key + "' requires 3 values");
      LX_core::Vec3f v3{seq[0], seq[1], seq[2]};
      mat.setParameter(bindingId, memberId, v3);
      break;
    }
    case LX_core::ShaderPropertyType::Vec4: {
      auto seq = valueNode.as<std::vector<float>>();
      if (seq.size() != 4)
        fatalLoader("Vec4 parameter '" + key + "' requires 4 values");
      LX_core::Vec4f v4{seq[0], seq[1], seq[2], seq[3]};
      mat.setParameter(bindingId, memberId, v4);
      break;
    }
    default:
      fatalLoader("unsupported parameter type for '" + key + "'");
    }
  }
}

void applyLoadOptionParameters(LX_core::MaterialInstance &mat,
                               const GenericMaterialLoadOptions &options) {
  if (!options.alphaTransparency.has_value() || *options.alphaTransparency) {
    return;
  }
  const auto bindingId = LX_core::StringID("MaterialUBO");
  const auto memberId = LX_core::StringID("baseColorFactor");
  const auto value = mat.readParameterValue(bindingId, memberId);
  if (!value.has_value() ||
      value->type != LX_core::MaterialParameterValueType::Vec4 ||
      value->vectorValue.w >= 1.0f) {
    return;
  }
  LX_core::Vec4f opaque = value->vectorValue;
  opaque.w = 1.0f;
  mat.setParameter(bindingId, memberId, opaque);
}

/*****************************************************************
 * Resource (texture) application
 *****************************************************************/

void applyResources(LX_core::MaterialInstance &mat,
                    const YAML::Node &resourcesNode, const fs::path &baseDir) {
  if (!resourcesNode || !resourcesNode.IsMap())
    return;

  for (auto it = resourcesNode.begin(); it != resourcesNode.end(); ++it) {
    const auto bindingName = it->first.as<std::string>();
    const auto value = it->second.as<std::string>();
    const auto bindingId = LX_core::StringID(bindingName);

    auto placeholder = resolvePlaceholder(value);
    if (placeholder) {
      mat.setTexture(bindingId, std::move(placeholder));
      continue;
    }

    fs::path texPath = baseDir / value;
    if (!fs::exists(texPath))
      texPath = fs::path(value);
    if (!fs::exists(texPath))
      fatalLoader("texture file not found: " + value);

    infra::TextureLoader loader;
    loader.load(texPath.string());
    LX_core::TextureDesc desc;
    desc.width = static_cast<u32>(loader.getWidth());
    desc.height = static_cast<u32>(loader.getHeight());
    desc.format = LX_core::TextureFormat::RGBA8;
    auto *rawData = static_cast<const u8 *>(loader.getData());
    std::vector<u8> texData(rawData, rawData + desc.width * desc.height * 4);
    auto tex = std::make_shared<LX_core::Texture>(desc, std::move(texData));
    auto sampler =
        std::make_shared<LX_core::CombinedTextureSampler>(std::move(tex));
    mat.setTexture(bindingId, std::move(sampler));
  }
}

/*****************************************************************
 * Variant rule validation
 *****************************************************************/

struct VariantRule {
  std::vector<std::string> ifEnabled;
  std::vector<std::string> depends;
};

std::vector<VariantRule> parseVariantRules(const YAML::Node &node) {
  std::vector<VariantRule> rules;
  if (!node.IsDefined() || !node.IsSequence())
    return rules;
  for (const auto &entry : node) {
    VariantRule rule;
    if (entry["requires"] && entry["requires"].IsSequence()) {
      for (const auto &v : entry["requires"])
        rule.ifEnabled.push_back(v.as<std::string>());
    }
    if (entry["depends"] && entry["depends"].IsSequence()) {
      for (const auto &v : entry["depends"])
        rule.depends.push_back(v.as<std::string>());
    }
    rules.push_back(std::move(rule));
  }
  return rules;
}

bool isVariantEnabled(const std::vector<LX_core::ShaderVariant> &variants,
                      const std::string &name) {
  for (const auto &v : variants)
    if (v.macroName == name)
      return v.enabled;
  return false;
}

void validateVariantRules(const std::vector<VariantRule> &rules,
                          const std::vector<LX_core::ShaderVariant> &variants,
                          const std::string &passContext) {
  for (const auto &rule : rules) {
    bool allRequiresMet = true;
    for (const auto &req : rule.ifEnabled) {
      if (!isVariantEnabled(variants, req)) {
        allRequiresMet = false;
        break;
      }
    }
    if (!allRequiresMet)
      continue;
    for (const auto &dep : rule.depends) {
      if (!isVariantEnabled(variants, dep)) {
        std::string reqStr;
        for (const auto &r : rule.ifEnabled)
          reqStr += (reqStr.empty() ? "" : "+") + r;
        fatalLoader(passContext + ": variant rule violated: " + reqStr +
                    " requires " + dep + " but it is not enabled");
      }
    }
  }
}

bool hasMeshOverlayColorBinding(const LX_core::IShader &shader) {
  const auto binding = shader.findBinding("MeshOverlayUBO");
  if (!binding ||
      binding->get().type != LX_core::ShaderPropertyType::UniformBuffer) {
    return false;
  }
  for (const auto &member : binding->get().members) {
    if (member.name == "color" &&
        member.type == LX_core::ShaderPropertyType::Vec4) {
      return true;
    }
  }
  return false;
}

void validateRequiredTechniques(const YAML::Node &techniquesNode,
                                const std::string &defaultTechnique,
                                const std::string &context) {
  if (!techniquesNode || !techniquesNode.IsMap()) {
    fatalLoader(context + ": missing required 'techniques' map");
  }
  if (!techniquesNode[defaultTechnique]) {
    fatalLoader(context + ": defaultTechnique '" + defaultTechnique +
                "' is not defined in techniques");
  }
}

/*****************************************************************
 * Shader compilation helper
 *****************************************************************/

struct CompiledPass {
  LX_core::StringID passId;
  std::string shaderName;
  std::string stage = "raster";
  std::vector<LX_core::ShaderVariant> variants;
  LX_core::RenderState renderState;
  LX_core::ShadingModel shadingModel = LX_core::ShadingModel::Smooth;
  LX_core::MeshOverlayState meshOverlay;
  std::shared_ptr<CompiledShader> shader;
  YAML::Node parameters;
  YAML::Node resources;
};

CompiledPass
compilePassShader(const LX_core::StringID &passId,
                  const std::string &shaderName, const std::string &stage,
                  const std::vector<LX_core::ShaderVariant> &variants,
                  const LX_core::RenderState &renderState,
                  const fs::path &shaderDir) {
  CompileResult compiled;
  if (stage == "compute") {
    const fs::path compPath = shaderDir / (shaderName + ".comp");
    if (!fs::exists(compPath)) {
      fatalLoader("compute shader file not found for '" + shaderName +
                  "': " + compPath.string());
    }
    compiled = ShaderCompiler::compileFile(compPath, variants);
  } else if (stage == "raster") {
    const fs::path vertPath = shaderDir / (shaderName + ".vert");
    const fs::path fragPath = shaderDir / (shaderName + ".frag");

    if (!fs::exists(vertPath) || !fs::exists(fragPath))
      fatalLoader("shader files not found for '" + shaderName +
                  "': " + vertPath.string() + " / " + fragPath.string());

    compiled = ShaderCompiler::compileProgram(vertPath, fragPath, variants);
  } else {
    fatalLoader("unknown pass stage '" + stage + "' for shader '" + shaderName +
                "'");
  }
  if (!compiled.success)
    fatalLoader("shader compile failed for pass " +
                LX_core::GlobalStringTable::get().toDebugString(passId) + ": " +
                compiled.errorMessage);

  auto bindings = ShaderReflector::reflect(compiled.stages);
  std::vector<LX_core::VertexInputAttribute> vertexInputs;
  if (stage == "raster") {
    vertexInputs = ShaderReflector::reflectVertexInputs(compiled.stages);
  }
  auto shader = std::make_shared<CompiledShader>(
      std::move(compiled.stages), bindings, vertexInputs, shaderName);

  CompiledPass cp;
  cp.passId = passId;
  cp.shaderName = shaderName;
  cp.stage = stage;
  cp.variants = variants;
  cp.renderState = renderState;
  cp.shader = std::move(shader);
  return cp;
}

} // namespace

/*****************************************************************
 * Public API
 *****************************************************************/

LX_core::MaterialInstanceSharedPtr
loadGenericMaterial(const fs::path &materialPath,
                    const GenericMaterialLoadOptions &options) {
  LX_core::SceneResourceTable table;
  return loadGenericMaterial(materialPath, table, options);
}

LX_core::MaterialInstanceSharedPtr
loadGenericMaterial(const fs::path &materialPath,
                    LX_core::SceneResourceTable &resourceTable,
                    const GenericMaterialLoadOptions &options) {
  const fs::path resolvedMaterialPath = materialPath.is_absolute()
                                            ? materialPath
                                            : resolveRuntimePath(materialPath);
  if (!fs::exists(resolvedMaterialPath))
    fatalLoader("material file not found: " + materialPath.string());

  YAML::Node root;
  try {
    root = YAML::LoadFile(resolvedMaterialPath.string());
  } catch (const YAML::Exception &e) {
    fatalLoader("failed to parse material file: " + std::string(e.what()));
  }

  // 1. Extract top-level fields early.
  //    Clone all sub-trees we'll need later so the root node is not
  //    accessed again after this block (avoids yaml-cpp aliasing issues).
  if (!root.IsMap())
    fatalLoader("material file root is not a YAML map: " +
                resolvedMaterialPath.string());

  const LX_core::ResourceUri materialUri(
      fs::relative(resolvedMaterialPath).string());
  if (isMaterialV2Contract(root)) {
    return loadMaterialV2EnvelopeContract(root, materialUri, resourceTable);
  }

  std::string legacyRootShaderName;
  YAML::Node globalVariantsNode;
  YAML::Node globalParamsNode;
  YAML::Node globalResourcesNode;
  YAML::Node techniquesNode;
  YAML::Node variantRulesNode;
  YAML::Node shadingModelNode;
  YAML::Node meshOverlayNode;
  std::string defaultTechnique;

  for (auto it = root.begin(); it != root.end(); ++it) {
    const auto key = it->first.as<std::string>();
    if (isMaterialV2Contract(root) &&
        (key == "shader" || key == "parameters" || key == "resources")) {
      fatalLoader(materialUri.string() +
                  ": material v2 contract does not allow legacy root '" + key +
                  "'");
    }
    if (key == "shader")
      legacyRootShaderName = it->second.as<std::string>();
    else if (key == "defaultTechnique")
      defaultTechnique = it->second.as<std::string>();
    else if (key == "variants")
      globalVariantsNode = YAML::Clone(it->second);
    else if (key == "parameters")
      globalParamsNode = YAML::Clone(it->second);
    else if (key == "resources")
      globalResourcesNode = YAML::Clone(it->second);
    else if (key == "passes")
      fatalLoader(resolvedMaterialPath.string() +
                  ": top-level 'passes' is not supported; use techniques");
    else if (key == "techniques")
      techniquesNode = YAML::Clone(it->second);
    else if (key == "variantRules")
      variantRulesNode = YAML::Clone(it->second);
    else if (key == "shadingModel")
      shadingModelNode = YAML::Clone(it->second);
    else if (key == "meshOverlay")
      meshOverlayNode = YAML::Clone(it->second);
  }

  if (defaultTechnique.empty())
    fatalLoader("missing required 'defaultTechnique' field in " +
                resolvedMaterialPath.string());
  validateRequiredTechniques(techniquesNode, defaultTechnique,
                             resolvedMaterialPath.string());

  const auto globalShadingModel = parseShadingModel(shadingModelNode);
  const auto globalMeshOverlay = parseMeshOverlay(meshOverlayNode);

  // 2. Find shader directory.
  const fs::path materialDir = resolvedMaterialPath.parent_path();
  const fs::path shaderDir = getRuntimeShaderSourceDir();
  if (shaderDir.empty())
    fatalLoader(
        "shader directory not found (expected .../assets/shaders/glsl/)");

  // 3. Parse variant rules and compile each pass.
  const auto variantRules = parseVariantRules(variantRulesNode);
  std::vector<CompiledPass> compiledPasses;

  const std::string selectedTechnique =
      options.technique.value_or(defaultTechnique);
  const YAML::Node techniqueNode = techniquesNode[selectedTechnique];
  if (!techniqueNode || !techniqueNode.IsMap()) {
    fatalLoader(resolvedMaterialPath.string() + ": technique '" +
                selectedTechnique + "' is not defined");
  }
  const YAML::Node selectedPassesNode = techniqueNode["passes"];
  if (!selectedPassesNode || !selectedPassesNode.IsMap() ||
      selectedPassesNode.size() == 0) {
    fatalLoader(resolvedMaterialPath.string() + ": technique '" +
                selectedTechnique + "' requires a non-empty passes map");
  }

  std::unordered_set<std::string> runtimePassNames;
  for (auto passIt = selectedPassesNode.begin();
       passIt != selectedPassesNode.end(); ++passIt) {
    const auto passName = passIt->first.as<std::string>();
    if (!passIt->second.IsMap()) {
      fatalLoader(resolvedMaterialPath.string() + ": technique '" +
                  selectedTechnique + "' pass '" + passName +
                  "' must be a map");
    }

    const std::string passContext = resolvedMaterialPath.string() +
                                    ": technique '" + selectedTechnique +
                                    "' pass '" + passName + "'";
    auto parsedPass = parseMaterialPassContract(
        passName, passIt->second,
        "techniques." + selectedTechnique + ".passes." + passName);
    if (!parsedPass.diagnostics.empty() || !parsedPass.pass.has_value()) {
      std::ostringstream message;
      message << passContext << ": invalid pass contract";
      for (const std::string &diagnostic : parsedPass.diagnostics) {
        message << "\n  " << diagnostic;
      }
      fatalLoader(message.str());
    }
    const LX_core::MaterialPassContract &passContract = *parsedPass.pass;
    rejectLegacyRootPbrTruthForDefaultPbr(
        globalParamsNode, globalResourcesNode, legacyRootShaderName,
        passContract.shaderUri.string(), passContext);

    // Extract material-loader-only pass fields after graph/render-flow fields
    // have been validated by the shared 071-b pass contract parser.
    YAML::Node passVariantsNode;
    YAML::Node passParamsNode;
    YAML::Node passResourcesNode;

    for (auto kv = passIt->second.begin(); kv != passIt->second.end(); ++kv) {
      const auto k = kv->first.as<std::string>();
      if (k == "variants")
        passVariantsNode = YAML::Clone(kv->second);
      else if (k == "parameters")
        passParamsNode = YAML::Clone(kv->second);
      else if (k == "resources")
        passResourcesNode = YAML::Clone(kv->second);
    }

    const LX_core::StringID runtimePass(passContract.name);
    const std::string runtimePassName =
        LX_core::GlobalStringTable::get().toDebugString(runtimePass);
    if (!runtimePassNames.insert(runtimePassName).second) {
      fatalLoader(resolvedMaterialPath.string() + ": technique '" +
                  selectedTechnique + "' maps multiple passes to '" +
                  runtimePassName + "'");
    }

    auto variants = mergeVariants(globalVariantsNode, passVariantsNode);
    applyShadingModelVariants(variants, globalShadingModel);
    applyLoadOptionVariants(variants, passContract.shaderUri.string(), options);
    validateVariantRules(variantRules, variants,
                         "technique " + selectedTechnique + " pass " +
                             passName);
    auto renderState = passContract.renderState;
    applyLoadOptionRenderState(renderState, options);

    const std::string passStage =
        passContract.stage == LX_core::MaterialPassStage::Compute ? "compute"
                                                                  : "raster";
    auto cp = compilePassShader(runtimePass, passContract.shaderUri.string(),
                                passStage, variants, renderState, shaderDir);
    cp.shadingModel = globalShadingModel;
    cp.meshOverlay = globalMeshOverlay;
    cp.parameters = std::move(passParamsNode);
    cp.resources = std::move(passResourcesNode);
    compiledPasses.push_back(std::move(cp));
  }

  // 4. Validate YAML declarations against shader reflection.
  //    Collect all material-owned bindings across all passes for global
  //    validation, and per-pass bindings for pass-level validation.
  std::vector<LX_core::ShaderResourceBinding> allMatBindings;
  for (const auto &cp : compiledPasses) {
    for (const auto &binding : cp.shader->getReflectionBindings()) {
      if (auto abiDiagnostic =
              LX_core::validateSystemAbiBindingContract(binding)) {
        fatalLoader(resolvedMaterialPath.string() + " pass=" +
                    cp.shaderName + ": " + *abiDiagnostic);
      }
      if (LX_core::isMaterialOwnedBinding(binding.name))
        allMatBindings.push_back(binding);
    }
  }

  if (globalParamsNode.IsMap())
    validateParametersAgainstReflection(globalParamsNode, allMatBindings,
                                        resolvedMaterialPath.string());
  if (globalResourcesNode.IsMap())
    validateResourcesAgainstReflection(globalResourcesNode, allMatBindings,
                                       resolvedMaterialPath.string());

  for (const auto &cp : compiledPasses) {
    if (cp.meshOverlay.enabled && !hasMeshOverlayColorBinding(*cp.shader)) {
      fatalLoader(resolvedMaterialPath.string() + " pass=" +
                  LX_core::GlobalStringTable::get().toDebugString(cp.passId) +
                  ": meshOverlay.enabled requires MeshOverlayUBO.color vec4");
    }
    if (cp.parameters && cp.parameters.IsMap()) {
      fatalLoader(resolvedMaterialPath.string() + " pass=" + cp.shaderName +
                  ": pass-scoped parameters are no longer supported; "
                  "MaterialInstance stores one canonical parameter set");
    }
    if (cp.resources && cp.resources.IsMap()) {
      fatalLoader(resolvedMaterialPath.string() + " pass=" + cp.shaderName +
                  ": pass-scoped resources are no longer supported; "
                  "MaterialInstance stores one canonical resource set");
    }
  }

  // 5. Build MaterialTemplate.
  const std::string templateName =
      legacyRootShaderName.empty() ? resolvedMaterialPath.stem().string()
                                   : legacyRootShaderName;
  auto tmpl = LX_core::MaterialTemplate::create(templateName);

  for (const auto &cp : compiledPasses) {
    LX_core::ShaderProgramSet programSet;
    programSet.shaderName = cp.shaderName;
    programSet.variants = cp.variants;
    programSet.shader = cp.shader;

    LX_core::MaterialPassDefinition entry;
    entry.shaderProgram = programSet;
    entry.renderState = cp.renderState;
    entry.shadingModel = cp.shadingModel;
    entry.meshOverlay = cp.meshOverlay;
    tmpl->setPassDefinition(cp.passId, std::move(entry));
  }
  tmpl->rebuildMaterialInterface();

  // 6. Create MaterialInstance.
  auto mat = LX_core::MaterialInstance::create(tmpl);

  // 7. Apply global defaults.
  if (globalParamsNode.IsMap())
    applyParameters(*mat, globalParamsNode);
  applyLoadOptionParameters(*mat, options);
  if (globalResourcesNode.IsMap())
    applyResources(*mat, globalResourcesNode, materialDir);
  for (const auto &cp : compiledPasses) {
    if (cp.meshOverlay.enabled) {
      mat->setParameter(LX_core::StringID("MeshOverlayUBO"),
                        LX_core::StringID("color"), cp.meshOverlay.color);
    }
  }
  applyMaterialV2Envelope(*mat, root, materialUri, resourceTable);
  mat->syncGpuData();
  return mat;
}

} // namespace LX_infra
