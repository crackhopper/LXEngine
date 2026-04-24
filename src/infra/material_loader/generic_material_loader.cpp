#include "generic_material_loader.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"
#include "infra/texture_loader/placeholder_textures.hpp"
#include "infra/texture_loader/texture_loader.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <iostream>
#include <optional>
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

/*****************************************************************
 * Variant helpers
 *****************************************************************/

std::vector<LX_core::ShaderVariant>
mergeVariants(const YAML::Node &globalNode, const YAML::Node &passNode) {
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

/*****************************************************************
 * RenderState parsing
 *****************************************************************/

LX_core::RenderState parseRenderState(const YAML::Node &node) {
  LX_core::RenderState rs;
  if (!node || !node.IsMap())
    return rs;
  if (auto v = node["cullMode"]) {
    auto s = v.as<std::string>();
    if (s == "None")
      rs.cullMode = LX_core::CullMode::None;
    else if (s == "Front")
      rs.cullMode = LX_core::CullMode::Front;
    else if (s == "Back")
      rs.cullMode = LX_core::CullMode::Back;
  }
  if (auto v = node["depthTest"])
    rs.depthTestEnable = v.as<bool>();
  if (auto v = node["depthWrite"])
    rs.depthWriteEnable = v.as<bool>();
  if (auto v = node["blendEnable"])
    rs.blendEnable = v.as<bool>();
  return rs;
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
    const auto &val = it->second;
    auto [bindingName, memberName] = parseParamKey(key);

    const auto bindingId = LX_core::StringID(bindingName);
    const auto memberId = LX_core::StringID(memberName);

    // Find member type from binding.
    auto *binding = mat.getParameterBufferLayout(bindingId);
    if (!binding) {
      fatalLoader("parameter binding '" + bindingName +
                  "' has no canonical parameter data");
    }

    LX_core::ShaderPropertyType memberType = LX_core::ShaderPropertyType::Float;
    bool found = false;
    for (const auto &m : binding->members) {
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
      mat.setParameter(bindingId, memberId, val.as<float>());
      break;
    case LX_core::ShaderPropertyType::Int:
      mat.setParameter(bindingId, memberId, val.as<i32>());
      break;
    case LX_core::ShaderPropertyType::Vec3: {
      auto seq = val.as<std::vector<float>>();
      if (seq.size() != 3)
        fatalLoader("Vec3 parameter '" + key + "' requires 3 values");
      LX_core::Vec3f v3{seq[0], seq[1], seq[2]};
      mat.setParameter(bindingId, memberId, v3);
      break;
    }
    case LX_core::ShaderPropertyType::Vec4: {
      auto seq = val.as<std::vector<float>>();
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

/*****************************************************************
 * Resource (texture) application
 *****************************************************************/

void applyResources(LX_core::MaterialInstance &mat,
                    const YAML::Node &resourcesNode,
                    const fs::path &baseDir) {
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
    std::vector<u8> texData(rawData,
                            rawData + desc.width * desc.height * 4);
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

void validateVariantRules(
    const std::vector<VariantRule> &rules,
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

/*****************************************************************
 * Shader compilation helper
 *****************************************************************/

struct CompiledPass {
  LX_core::StringID passId;
  std::string shaderName;
  std::vector<LX_core::ShaderVariant> variants;
  LX_core::RenderState renderState;
  std::shared_ptr<CompiledShader> shader;
  YAML::Node parameters;
  YAML::Node resources;
};

CompiledPass compilePassShader(const LX_core::StringID &passId,
                               const std::string &shaderName,
                               const std::vector<LX_core::ShaderVariant> &variants,
                               const LX_core::RenderState &renderState,
                               const fs::path &shaderDir) {
  const fs::path vertPath = shaderDir / (shaderName + ".vert");
  const fs::path fragPath = shaderDir / (shaderName + ".frag");

  if (!fs::exists(vertPath) || !fs::exists(fragPath))
    fatalLoader("shader files not found for '" + shaderName + "': " +
                vertPath.string() + " / " + fragPath.string());

  auto compiled =
      ShaderCompiler::compileProgram(vertPath, fragPath, variants);
  if (!compiled.success)
    fatalLoader("shader compile failed for pass " +
                LX_core::GlobalStringTable::get().toDebugString(passId) +
                ": " + compiled.errorMessage);

  auto bindings = ShaderReflector::reflect(compiled.stages);
  auto vertexInputs = ShaderReflector::reflectVertexInputs(compiled.stages);
  auto shader = std::make_shared<CompiledShader>(
      std::move(compiled.stages), bindings, vertexInputs, shaderName);

  CompiledPass cp;
  cp.passId = passId;
  cp.shaderName = shaderName;
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
loadGenericMaterial(const fs::path &materialPath) {
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

  std::string globalShaderName;
  YAML::Node globalVariantsNode;
  YAML::Node globalParamsNode;
  YAML::Node globalResourcesNode;
  YAML::Node passesNode;
  YAML::Node variantRulesNode;

  for (auto it = root.begin(); it != root.end(); ++it) {
    const auto key = it->first.as<std::string>();
    if (key == "shader")
      globalShaderName = it->second.as<std::string>();
    else if (key == "variants")
      globalVariantsNode = YAML::Clone(it->second);
    else if (key == "parameters")
      globalParamsNode = YAML::Clone(it->second);
    else if (key == "resources")
      globalResourcesNode = YAML::Clone(it->second);
    else if (key == "passes")
      passesNode = YAML::Clone(it->second);
    else if (key == "variantRules")
      variantRulesNode = YAML::Clone(it->second);
  }

  if (globalShaderName.empty())
    fatalLoader("missing required 'shader' field in " +
                resolvedMaterialPath.string());

  // 2. Find shader directory.
  const fs::path materialDir = resolvedMaterialPath.parent_path();
  const fs::path shaderDir = getRuntimeShaderSourceDir();
  if (shaderDir.empty())
    fatalLoader("shader directory not found (expected .../assets/shaders/glsl/)");

  // 3. Parse variant rules and compile each pass.
  const auto variantRules = parseVariantRules(variantRulesNode);
  std::vector<CompiledPass> compiledPasses;

  if (passesNode.IsMap()) {
    for (auto passIt = passesNode.begin(); passIt != passesNode.end();
         ++passIt) {
      const auto passName = passIt->first.as<std::string>();

      // Extract pass-level fields by iterating keys.
      std::string passShader = globalShaderName;
      YAML::Node passVariantsNode;
      YAML::Node passRenderStateNode;
      YAML::Node passParamsNode;
      YAML::Node passResourcesNode;

      if (passIt->second.IsMap()) {
        for (auto kv = passIt->second.begin(); kv != passIt->second.end();
             ++kv) {
          const auto k = kv->first.as<std::string>();
          if (k == "shader")
            passShader = kv->second.as<std::string>();
          else if (k == "variants")
            passVariantsNode = YAML::Clone(kv->second);
          else if (k == "renderState")
            passRenderStateNode = YAML::Clone(kv->second);
          else if (k == "parameters")
            passParamsNode = YAML::Clone(kv->second);
          else if (k == "resources")
            passResourcesNode = YAML::Clone(kv->second);
        }
      }

      auto variants = mergeVariants(globalVariantsNode, passVariantsNode);
      validateVariantRules(variantRules, variants, "pass " + passName);
      auto renderState = parseRenderState(passRenderStateNode);

      auto cp = compilePassShader(LX_core::StringID(passName), passShader,
                                  variants, renderState, shaderDir);
      cp.parameters = std::move(passParamsNode);
      cp.resources = std::move(passResourcesNode);
      compiledPasses.push_back(std::move(cp));
    }
  } else {
    // No passes block → single Forward pass with global shader.
    auto variants = mergeVariants(globalVariantsNode, YAML::Node());
    validateVariantRules(variantRules, variants, "pass Forward (default)");
    auto cp = compilePassShader(LX_core::Pass_Forward, globalShaderName,
                                variants, LX_core::RenderState{}, shaderDir);
    compiledPasses.push_back(std::move(cp));
  }

  // 4. Validate YAML declarations against shader reflection.
  //    Collect all material-owned bindings across all passes for global
  //    validation, and per-pass bindings for pass-level validation.
  std::vector<LX_core::ShaderResourceBinding> allMatBindings;
  for (const auto &cp : compiledPasses) {
    for (const auto &binding : cp.shader->getReflectionBindings()) {
      if (!LX_core::isSystemOwnedBinding(binding.name))
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
  auto firstShader = compiledPasses.front().shader;
  auto tmpl =
      LX_core::MaterialTemplate::create(globalShaderName);

  for (const auto &cp : compiledPasses) {
    LX_core::ShaderProgramSet programSet;
    programSet.shaderName = cp.shaderName;
    programSet.variants = cp.variants;
    programSet.shader = cp.shader;

    LX_core::MaterialPassDefinition entry;
    entry.shaderProgram = programSet;
    entry.renderState = cp.renderState;
    tmpl->setPassDefinition(cp.passId, std::move(entry));
  }
  tmpl->rebuildMaterialInterface();

  // 6. Create MaterialInstance.
  auto mat = LX_core::MaterialInstance::create(tmpl);

  // 7. Apply global defaults.
  if (globalParamsNode.IsMap())
    applyParameters(*mat, globalParamsNode);
  if (globalResourcesNode.IsMap())
    applyResources(*mat, globalResourcesNode, materialDir);

  mat->syncGpuData();
  return mat;
}

} // namespace LX_infra
