#include "core/rhi/gpu_resource.hpp"
#include "core/scene/scene_system_abi.hpp"
#include "core/scene/scene_system_abi_validation.hpp"
#include "core/utils/env.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace LX_core;
using namespace LX_infra;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char *shaderPropertyTypeName(ShaderPropertyType t) {
  switch (t) {
  case ShaderPropertyType::Float:
    return "Float";
  case ShaderPropertyType::Vec2:
    return "Vec2";
  case ShaderPropertyType::Vec3:
    return "Vec3";
  case ShaderPropertyType::Vec4:
    return "Vec4";
  case ShaderPropertyType::Mat4:
    return "Mat4";
  case ShaderPropertyType::Int:
    return "Int";
  case ShaderPropertyType::UniformBuffer:
    return "UniformBuffer";
  case ShaderPropertyType::StorageBuffer:
    return "StorageBuffer";
  case ShaderPropertyType::Texture2D:
    return "Texture2D";
  case ShaderPropertyType::TextureCube:
    return "TextureCube";
  case ShaderPropertyType::Sampler:
    return "Sampler";
  }
  return "Unknown";
}

static const char *vkDescriptorTypeName(ShaderPropertyType t) {
  switch (t) {
  case ShaderPropertyType::UniformBuffer:
    return "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER";
  case ShaderPropertyType::StorageBuffer:
    return "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER";
  case ShaderPropertyType::Texture2D:
  case ShaderPropertyType::TextureCube:
    return "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER";
  case ShaderPropertyType::Sampler:
    return "VK_DESCRIPTOR_TYPE_SAMPLER";
  default:
    return "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER";
  }
}

static std::string stageFlagsToString(ShaderStage flags) {
  std::string result;
  auto f = static_cast<ShaderStageMask32>(flags);
  if (f & static_cast<ShaderStageMask32>(ShaderStage::Vertex))
    result += "VERTEX ";
  if (f & static_cast<ShaderStageMask32>(ShaderStage::Fragment))
    result += "FRAGMENT ";
  if (f & static_cast<ShaderStageMask32>(ShaderStage::Compute))
    result += "COMPUTE ";
  if (f & static_cast<ShaderStageMask32>(ShaderStage::Geometry))
    result += "GEOMETRY ";
  if (result.empty())
    result = "NONE";
  return result;
}

static void printBindings(const std::vector<ShaderResourceBinding> &bindings) {
  std::cout << "\n  === ShaderResourceBinding list ===\n";
  for (const auto &b : bindings) {
    std::cout << "  [set=" << b.set << ", binding=" << b.binding << "] "
              << "name=\"" << b.name << "\"  "
              << "type=" << shaderPropertyTypeName(b.type) << "  "
              << "count=" << b.descriptorCount << "  " << "size=" << b.size
              << "  " << "stages=" << stageFlagsToString(b.stageFlags) << "\n";
  }
}

static void printDescriptorSetLayoutMapping(
    const std::vector<ShaderResourceBinding> &bindings) {
  std::cout << "\n  === VkDescriptorSetLayoutBinding mapping ===\n";

  // Group by set
  u32 currentSet = u32_max;
  for (const auto &b : bindings) {
    if (b.set != currentSet) {
      currentSet = b.set;
      std::cout << "\n  --- Descriptor Set " << currentSet << " ---\n";
    }
    std::cout << "  VkDescriptorSetLayoutBinding {\n"
              << "    .binding         = " << b.binding << ",\n"
              << "    .descriptorType  = " << vkDescriptorTypeName(b.type)
              << ",\n"
              << "    .descriptorCount = " << b.descriptorCount << ",\n"
              << "    .stageFlags      = " << stageFlagsToString(b.stageFlags)
              << ",\n"
              << "  }\n";
  }
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

static bool testVariantCombination(const std::filesystem::path &vertPath,
                                   const std::filesystem::path &fragPath,
                                   const std::string &label,
                                   const std::vector<ShaderVariant> &variants) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: " << label << "\n";
  std::cout << "========================================\n";

  // Print active macros
  std::cout << "  Macros: ";
  bool anyEnabled = false;
  for (const auto &v : variants) {
    if (v.enabled) {
      std::cout << v.macroName << " ";
      anyEnabled = true;
    }
  }
  if (!anyEnabled)
    std::cout << "(none)";
  std::cout << "\n";

  // Compile
  auto compileResult =
      ShaderCompiler::compileProgram(vertPath, fragPath, variants);
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }
  std::cout << "  Compilation OK — " << compileResult.stages.size()
            << " stages\n";

  // Reflect
  auto bindings = ShaderReflector::reflect(compileResult.stages);
  auto vertexInputs =
      ShaderReflector::reflectVertexInputs(compileResult.stages);
  std::cout << "  Reflection found " << bindings.size() << " bindings\n";

  // Create CompiledShader
  auto shader = std::make_shared<CompiledShader>(
      std::move(compileResult.stages), bindings, vertexInputs);
  std::cout << "  Program hash: 0x" << std::hex << shader->getProgramHash()
            << std::dec << "\n";

  // Print bindings
  printBindings(shader->getReflectionBindings());

  // Print descriptor set layout mapping
  printDescriptorSetLayoutMapping(shader->getReflectionBindings());

  // Test findBinding
  std::cout << "\n  === findBinding tests ===\n";
  auto cameraUBO = shader->findBinding(0, 0);
  if (cameraUBO) {
    std::cout << "  findBinding(0,0) -> \"" << cameraUBO->get().name << "\"\n";
  } else {
    std::cout << "  findBinding(0,0) -> not found\n";
  }

  auto byName = shader->findBinding("SceneMaterials");
  if (byName) {
    std::cout << "  findBinding(\"SceneMaterials\") -> set="
              << byName->get().set
              << " binding=" << byName->get().binding << "\n";
  } else {
    std::cout << "  findBinding(\"SceneMaterials\") -> not found\n";
  }

  return true;
}

// ---------------------------------------------------------------------------
// UBO member reflection tests (REQ-004)
// ---------------------------------------------------------------------------

static const StructMemberInfo *findMember(const ShaderResourceBinding &b,
                                          const std::string &name) {
  for (const auto &m : b.members) {
    if (m.name == name)
      return &m;
  }
  return nullptr;
}

static bool hasBinding(const std::vector<ShaderResourceBinding> &bindings,
                       const std::string &name, ShaderPropertyType type,
                       u32 set, u32 binding) {
  const auto it =
      std::find_if(bindings.begin(), bindings.end(),
                   [&](const auto &candidate) { return candidate.name == name; });
  return it != bindings.end() && it->type == type && it->set == set &&
         it->binding == binding;
}

static std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream ifs(path);
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  return buffer.str();
}

static std::string trim(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

static bool startsWith(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

static std::filesystem::path repoRootFromShaderDir(
    const std::filesystem::path &shaderDir) {
  return shaderDir.parent_path().parent_path().parent_path();
}

struct FeatureParameterProbe {
  std::string kind;
  std::string binding;
  std::string member;
  bool volatileRuntime = false;
};

struct FeatureProbe {
  std::string level;
  std::string shaderUri;
  std::map<std::string, FeatureParameterProbe> parameters;
};

static FeatureProbe parseFeatureProbe(const std::filesystem::path &path) {
  FeatureProbe feature;
  std::ifstream in(path);
  std::string line;
  std::string activeParameter;
  bool inShader = false;
  bool inParameters = false;
  while (std::getline(in, line)) {
    const std::size_t indent = line.find_first_not_of(' ');
    const std::string stripped = trim(line);
    if (stripped.empty() || startsWith(stripped, "#")) {
      continue;
    }
    if (indent == 0) {
      inShader = stripped == "shader:";
      inParameters = stripped == "parameters:";
      activeParameter.clear();
      if (startsWith(stripped, "level:")) {
        feature.level = trim(stripped.substr(std::string("level:").size()));
      }
      continue;
    }
    if (inShader && indent == 2 && startsWith(stripped, "uri:")) {
      feature.shaderUri = trim(stripped.substr(std::string("uri:").size()));
      continue;
    }
    if (inParameters && indent == 2 && stripped.back() == ':') {
      activeParameter = stripped.substr(0, stripped.size() - 1);
      feature.parameters[activeParameter] = FeatureParameterProbe{};
      continue;
    }
    if (inParameters && indent == 4 && !activeParameter.empty()) {
      const auto colon = stripped.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      const std::string key = stripped.substr(0, colon);
      const std::string value = trim(stripped.substr(colon + 1));
      if (key == "kind") {
        feature.parameters[activeParameter].kind = value;
      } else if (key == "binding") {
        feature.parameters[activeParameter].binding = value;
      } else if (key == "member") {
        feature.parameters[activeParameter].member = value;
      } else if (key == "volatile") {
        feature.parameters[activeParameter].volatileRuntime = value == "true";
      }
    }
  }
  return feature;
}

static std::vector<std::filesystem::path> parseForwardMainFeatureUris(
    const std::filesystem::path &repoRoot) {
  std::vector<std::filesystem::path> uris;
  std::ifstream in(repoRoot / "assets" / "render_paths" /
                   "forward_main.render-path.yaml");
  std::string line;
  bool inFeatures = false;
  while (std::getline(in, line)) {
    const std::size_t indent = line.find_first_not_of(' ');
    const std::string stripped = trim(line);
    if (stripped.empty()) {
      continue;
    }
    if (indent == 0) {
      inFeatures = stripped == "features:";
      continue;
    }
    if (inFeatures && indent == 4 && startsWith(stripped, "uri:")) {
      uris.emplace_back(trim(stripped.substr(std::string("uri:").size())));
    }
  }
  return uris;
}

static std::filesystem::path shaderPathForUri(
    const std::filesystem::path &shaderDir, const std::string &shaderUri) {
  const auto base = shaderDir / shaderUri;
  if (std::filesystem::exists(base)) {
    return base;
  }
  for (const auto *extension : {".frag", ".vert", ".comp", ".glsl"}) {
    auto candidate = base;
    candidate += extension;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return base;
}

static bool featureParameterKindMatchesBinding(const std::string &kind,
                                               ShaderPropertyType type) {
  if (kind == "textureCube") {
    return type == ShaderPropertyType::TextureCube;
  }
  if (kind == "texture2D" || kind == "texture") {
    return type == ShaderPropertyType::Texture2D;
  }
  if (kind == "float") {
    return type == ShaderPropertyType::Float;
  }
  if (kind == "vec3") {
    return type == ShaderPropertyType::Vec3;
  }
  if (kind == "enum" || kind == "integer") {
    return type == ShaderPropertyType::Int;
  }
  return true;
}

static bool featureParameterKindMatchesSpecialization(
    const std::string &kind, ShaderSpecializationValueType type) {
  if (kind == "bool") {
    return type == ShaderSpecializationValueType::Bool;
  }
  if (kind == "float") {
    return type == ShaderSpecializationValueType::Float;
  }
  if (kind == "enum" || kind == "integer") {
    return type == ShaderSpecializationValueType::Int;
  }
  if (kind == "u32" || kind == "uint") {
    return type == ShaderSpecializationValueType::UInt;
  }
  return false;
}

static std::string joinedToken(const char *head, const char *tail) {
  return std::string(head) + tail;
}

static ShaderVariant materialContractSourceVariant() {
  return ShaderVariant{
      .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
      .enabled = true,
      .macroValue = "\"common/materials/standard_pbr.contract.glsl\"",
  };
}

static std::vector<ShaderVariant>
withMaterialContractSource(std::vector<ShaderVariant> variants = {}) {
  variants.push_back(materialContractSourceVariant());
  return variants;
}

static bool testPbrShadersUseMaterialAccessorAbi(
    const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: PBR shaders use Material Accessor ABI\n";
  std::cout << "========================================\n";

  const struct {
    const char *label;
    std::filesystem::path path;
  } shaders[] = {
      {"Forward PBR",
       shaderDir / "render_paths" / "Forward" / "pbr.frag"},
      {"Deferred GBuffer",
       shaderDir / "render_paths" / "Deferred" / "pbr_gbuffer.frag"},
      {"OfflineRT direct ray",
       shaderDir / "techniques" / "OfflineRT" / "offline_pbr_direct_ray.comp"},
  };

  for (const auto &shader : shaders) {
    const auto source = readTextFile(shader.path);
    if (source.empty()) {
      std::cerr << "  FAIL: " << shader.label << " source is empty or "
                << "unreadable: " << shader.path << "\n";
      return false;
    }
    if (source.find("#include \"common/material_surface.glsl\"") ==
        std::string::npos) {
      std::cerr << "  FAIL: " << shader.label
                << " should include common/material_surface.glsl\n";
      return false;
    }
    if (source.find("#include LX_MATERIAL_CONTRACT_SOURCE") ==
        std::string::npos) {
      std::cerr << "  FAIL: " << shader.label
                << " should expose the material contract source hook\n";
      return false;
    }
    if (source.find("#include \"common/material_bsdf.glsl\"") ==
        std::string::npos) {
      std::cerr << "  FAIL: " << shader.label
                << " should include common/material_bsdf.glsl\n";
      return false;
    }
    if (source.find("lxLoadMaterialSurface") == std::string::npos) {
      std::cerr << "  FAIL: " << shader.label
                << " should call lxLoadMaterialSurface\n";
      return false;
    }
    const bool expectsDirectBsdfEvaluate =
        std::string(shader.label) != "Deferred GBuffer";
    if (expectsDirectBsdfEvaluate &&
        source.find("lxEvaluateBsdf") == std::string::npos) {
      std::cerr << "  FAIL: " << shader.label
                << " should call lxEvaluateBsdf\n";
      return false;
    }
    if (!expectsDirectBsdfEvaluate &&
        source.find("outNormalRoughness") == std::string::npos) {
      std::cerr << "  FAIL: " << shader.label
                << " should write material properties to the GBuffer\n";
      return false;
    }
    if (std::string(shader.label) == "OfflineRT direct ray" &&
        source.find("lxSampleBsdf") == std::string::npos) {
      std::cerr << "  FAIL: " << shader.label
                << " should call lxSampleBsdf\n";
      return false;
    }
    if (source.find("struct lxSceneMaterialRecord") != std::string::npos) {
      std::cerr << "  FAIL: " << shader.label
                << " still declares legacy lxSceneMaterialRecord\n";
      return false;
    }
  }

  std::cout << "  PASS: PBR shaders use Material Accessor ABI source hook\n";
  return true;
}

static bool testVariantOnlyShaderNakedCompileFailsWithDiagnostic(
    const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: Variant-only shader naked compile diagnostic\n";
  std::cout << "========================================\n";

  const auto fragPath =
      shaderDir / "render_paths" / "Forward" / "pbr.frag";
  const auto compileResult = ShaderCompiler::compileFile(fragPath, {});
  if (compileResult.success) {
    std::cerr << "  FAIL: pbr.frag naked compile should fail without "
                 "LX_MATERIAL_CONTRACT_SOURCE\n";
    return false;
  }
  if (compileResult.errorMessage.find("LX_MATERIAL_CONTRACT_SOURCE") ==
      std::string::npos) {
    std::cerr << "  FAIL: naked compile diagnostic should name "
                 "LX_MATERIAL_CONTRACT_SOURCE: "
              << compileResult.errorMessage << "\n";
    return false;
  }

  std::cout << "  PASS: naked variant-only shader compile fails clearly\n";
  return true;
}

static bool testMaterialSourceVariantCompilesVariantOnlyShaders(
    const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: Material source variants compile variant-only shaders\n";
  std::cout << "========================================\n";

  const std::vector<ShaderVariant> variants = withMaterialContractSource();
  const auto forward = ShaderCompiler::compileProgram(
      shaderDir / "render_paths" / "Forward" / "pbr.vert",
      shaderDir / "render_paths" / "Forward" / "pbr.frag", variants);
  if (!forward.success) {
    std::cerr << "  FAIL: Forward material source variant compile failed: "
              << forward.errorMessage << "\n";
    return false;
  }

  const auto deferred = ShaderCompiler::compileProgram(
      shaderDir / "render_paths" / "Deferred" / "pbr_gbuffer.vert",
      shaderDir / "render_paths" / "Deferred" / "pbr_gbuffer.frag", variants);
  if (!deferred.success) {
    std::cerr << "  FAIL: Deferred material source variant compile failed: "
              << deferred.errorMessage << "\n";
    return false;
  }

  const auto offline = ShaderCompiler::compileFile(
      shaderDir / "techniques" / "OfflineRT" / "offline_pbr_direct_ray.comp",
      variants);
  if (!offline.success) {
    std::cerr << "  FAIL: OfflineRT material source variant compile failed: "
              << offline.errorMessage << "\n";
    return false;
  }

  std::cout << "  PASS: Forward, Deferred, and OfflineRT material source "
               "variants compile\n";
  return true;
}

static bool
testPostProcessShaderContract(const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: Post-process shader contract\n";
  std::cout << "========================================\n";

  const auto vertPath =
      shaderDir / "render_paths" / "Post" / "post_process.vert";
  const auto fragPath =
      shaderDir / "render_paths" / "Post" / "post_process.frag";
  auto compileResult = ShaderCompiler::compileProgram(vertPath, fragPath, {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto sceneColor =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "SceneColor";
      });
  if (sceneColor == bindings.end() ||
      sceneColor->type != ShaderPropertyType::Texture2D) {
    std::cerr << "  FAIL: SceneColor Texture2D binding missing\n";
    return false;
  }
  if (sceneColor->set != 0 || sceneColor->binding != 0) {
    std::cerr << "  FAIL: SceneColor expected set=0 binding=0, got set="
              << sceneColor->set << " binding=" << sceneColor->binding << "\n";
    return false;
  }
  const auto bloomColor =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "BloomColor";
      });
  if (bloomColor == bindings.end() ||
      bloomColor->type != ShaderPropertyType::Texture2D) {
    std::cerr << "  FAIL: BloomColor Texture2D binding missing\n";
    return false;
  }
  if (bloomColor->set != 0 || bloomColor->binding != 2) {
    std::cerr << "  FAIL: BloomColor expected set=0 binding=2, got set="
              << bloomColor->set << " binding=" << bloomColor->binding << "\n";
    return false;
  }

  const auto postUbo =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "PostProcessUBO";
      });
  if (postUbo == bindings.end() ||
      postUbo->type != ShaderPropertyType::UniformBuffer) {
    std::cerr << "  FAIL: PostProcessUBO uniform buffer missing\n";
    return false;
  }
  if (postUbo->set != 0 || postUbo->binding != 1 || postUbo->size != 16) {
    std::cerr << "  FAIL: PostProcessUBO expected set=0 binding=1 size=16, "
                 "got set="
              << postUbo->set << " binding=" << postUbo->binding
              << " size=" << postUbo->size << "\n";
    return false;
  }
  const auto *exposure = findMember(*postUbo, "exposure");
  const auto *mode = findMember(*postUbo, "toneMappingMode");
  const auto *gamma = findMember(*postUbo, "gamma");
  const auto *bloomIntensity = findMember(*postUbo, "bloomIntensity");
  if (!exposure || exposure->type != ShaderPropertyType::Float) {
    std::cerr << "  FAIL: PostProcessUBO.exposure Float member missing\n";
    return false;
  }
  if (exposure->offset != 0 || exposure->size != 4) {
    std::cerr << "  FAIL: PostProcessUBO.exposure layout mismatch\n";
    return false;
  }
  if (!mode || mode->type != ShaderPropertyType::Int) {
    std::cerr << "  FAIL: PostProcessUBO.toneMappingMode Int member missing\n";
    return false;
  }
  if (mode->offset != 4 || mode->size != 4) {
    std::cerr << "  FAIL: PostProcessUBO.toneMappingMode layout mismatch\n";
    return false;
  }
  if (!gamma || gamma->type != ShaderPropertyType::Float) {
    std::cerr << "  FAIL: PostProcessUBO.gamma Float member missing\n";
    return false;
  }
  if (gamma->offset != 8 || gamma->size != 4) {
    std::cerr << "  FAIL: PostProcessUBO.gamma layout mismatch\n";
    return false;
  }
  if (!bloomIntensity || bloomIntensity->type != ShaderPropertyType::Float) {
    std::cerr << "  FAIL: PostProcessUBO.bloomIntensity Float member missing\n";
    return false;
  }
  if (bloomIntensity->offset != 12 || bloomIntensity->size != 4) {
    std::cerr << "  FAIL: PostProcessUBO.bloomIntensity layout mismatch\n";
    return false;
  }

  std::cout << "  PASS: post-process shader reflects SceneColor and tone "
               "mapping params\n";
  return true;
}

static bool testBloomShaderContracts(const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: Bloom shader contracts\n";
  std::cout << "========================================\n";

  const auto thresholdPath =
      shaderDir / "render_paths" / "Post" / "bloom_threshold.frag";
  auto thresholdResult = ShaderCompiler::compileProgram(
      shaderDir / "render_paths" / "Post" / "bloom_threshold.vert",
      thresholdPath, {});
  if (!thresholdResult.success) {
    std::cerr << "  COMPILE FAILED: " << thresholdResult.errorMessage << "\n";
    return false;
  }
  auto bindings = ShaderReflector::reflect(thresholdResult.stages);
  const auto sceneColor =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "SceneColor";
      });
  const auto thresholdUbo =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "BloomThresholdUBO";
      });
  if (sceneColor == bindings.end() ||
      sceneColor->type != ShaderPropertyType::Texture2D ||
      sceneColor->set != 0 || sceneColor->binding != 0) {
    std::cerr << "  FAIL: Bloom threshold SceneColor binding mismatch\n";
    return false;
  }
  if (thresholdUbo == bindings.end() ||
      thresholdUbo->type != ShaderPropertyType::UniformBuffer ||
      thresholdUbo->set != 0 || thresholdUbo->binding != 1 ||
      thresholdUbo->size != 16) {
    std::cerr << "  FAIL: BloomThresholdUBO binding mismatch\n";
    return false;
  }

  for (const auto *fragName : {"bloom_blur_h.frag", "bloom_blur_v.frag"}) {
    const auto vertName = std::string(fragName).find("_h.") != std::string::npos
                              ? "bloom_blur_h.vert"
                              : "bloom_blur_v.vert";
    auto blurResult = ShaderCompiler::compileProgram(
        shaderDir / "render_paths" / "Post" / vertName,
        shaderDir / "render_paths" / "Post" / fragName, {});
    if (!blurResult.success) {
      std::cerr << "  COMPILE FAILED: " << blurResult.errorMessage << "\n";
      return false;
    }
    bindings = ShaderReflector::reflect(blurResult.stages);
    const auto bloomSource =
        std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
          return binding.name == "BloomSource";
        });
    if (bloomSource == bindings.end() ||
        bloomSource->type != ShaderPropertyType::Texture2D ||
        bloomSource->set != 0 || bloomSource->binding != 0) {
      std::cerr << "  FAIL: " << fragName << " BloomSource binding mismatch\n";
      return false;
    }
  }

  auto blitResult = ShaderCompiler::compileProgram(
      shaderDir / "render_paths" / "Bloom" / "blit.vert",
      shaderDir / "render_paths" / "Bloom" / "blit.frag", {});
  if (!blitResult.success) {
    std::cerr << "  COMPILE FAILED: " << blitResult.errorMessage << "\n";
    return false;
  }
  bindings = ShaderReflector::reflect(blitResult.stages);
  const auto blitSceneColor =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "SceneColor";
      });
  const auto blitBloomUbo =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "BloomUBO";
      });
  if (blitSceneColor == bindings.end() ||
      blitSceneColor->type != ShaderPropertyType::Texture2D ||
      blitSceneColor->set != 0 || blitSceneColor->binding != 0) {
    std::cerr << "  FAIL: Bloom blit SceneColor binding mismatch\n";
    return false;
  }
  if (blitBloomUbo == bindings.end() ||
      blitBloomUbo->type != ShaderPropertyType::UniformBuffer ||
      blitBloomUbo->set != 0 || blitBloomUbo->binding != 1 ||
      blitBloomUbo->size != 16) {
    std::cerr << "  FAIL: Bloom blit BloomUBO binding mismatch\n";
    return false;
  }

  std::cout << "  PASS: bloom shaders reflect threshold, blur, and blit inputs\n";
  return true;
}

static bool testDefaultRealtimeShadersExposeToneMappingFeature(
    const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: default realtime shaders expose toneMapping feature\n";
  std::cout << "========================================\n";

  auto forward = ShaderCompiler::compileProgram(
      shaderDir / "render_paths" / "Forward" / "pbr.vert",
      shaderDir / "render_paths" / "Forward" / "pbr.frag",
      withMaterialContractSource());
  if (!forward.success) {
    std::cerr << "  COMPILE FAILED: " << forward.errorMessage << "\n";
    return false;
  }
  auto bindings = ShaderReflector::reflect(forward.stages);
  if (!hasBinding(bindings, "ToneMappingUBO",
                  ShaderPropertyType::UniformBuffer, 4, 0)) {
    std::cerr << "  FAIL: Forward pbr.frag should expose ToneMappingUBO at "
                 "set=4 binding=0\n";
    return false;
  }

  auto deferredLighting = ShaderCompiler::compileProgram(
      shaderDir / "render_paths" / "Post" / "post_process.vert",
      shaderDir / "render_paths" / "Deferred" / "deferred_lighting.frag", {});
  if (!deferredLighting.success) {
    std::cerr << "  COMPILE FAILED: " << deferredLighting.errorMessage << "\n";
    return false;
  }
  bindings = ShaderReflector::reflect(deferredLighting.stages);
  if (!hasBinding(bindings, "ToneMappingUBO",
                  ShaderPropertyType::UniformBuffer, 4, 0)) {
    std::cerr << "  FAIL: Deferred lighting should expose ToneMappingUBO at "
                 "set=4 binding=0\n";
    return false;
  }

  const std::string toneMappingSource =
      readTextFile(shaderDir / "common" / "tone_mapping.glsl");
  const auto applyStart = toneMappingSource.find("vec3 lxApplyToneMapping");
  const auto applyEnd = toneMappingSource.find("#endif", applyStart);
  if (applyStart == std::string::npos || applyEnd == std::string::npos) {
    std::cerr << "  FAIL: shared tone mapping function not found\n";
    return false;
  }
  const std::string applyBody =
      toneMappingSource.substr(applyStart, applyEnd - applyStart);
  if (applyBody.find("lxLinearToSrgbGamma") != std::string::npos) {
    std::cerr << "  FAIL: default realtime tone mapping should output linear "
                 "LDR for sRGB attachments, not manual sRGB gamma\n";
    return false;
  }

  std::cout
      << "  PASS: default realtime shaders use shared toneMapping feature ABI\n";
  return true;
}

static bool testIblBakeShaderContracts(const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: IBL bake shader contracts\n";
  std::cout << "========================================\n";

  const auto expectBinding =
      [](const std::vector<ShaderResourceBinding> &bindings,
         const std::string &name, ShaderPropertyType type, u32 set,
         u32 binding) {
        const auto it = std::find_if(
            bindings.begin(), bindings.end(),
            [&](const auto &candidate) { return candidate.name == name; });
        return it != bindings.end() && it->type == type && it->set == set &&
               it->binding == binding;
      };

  auto equirect = ShaderCompiler::compileProgram(
      shaderDir / "equirect_to_cubemap.vert",
      shaderDir / "equirect_to_cubemap.frag", {});
  if (!equirect.success) {
    std::cerr << "  COMPILE FAILED: " << equirect.errorMessage << "\n";
    return false;
  }
  auto bindings = ShaderReflector::reflect(equirect.stages);
  if (!expectBinding(bindings, "EquirectangularMap",
                     ShaderPropertyType::Texture2D, 0, 0) ||
      !expectBinding(bindings, "CaptureViewUBO",
                     ShaderPropertyType::UniformBuffer, 0, 1)) {
    std::cerr << "  FAIL: equirect_to_cubemap bindings mismatch\n";
    return false;
  }

  auto irradiance = ShaderCompiler::compileProgram(
      shaderDir / "ibl_irradiance_convolve.vert",
      shaderDir / "ibl_irradiance_convolve.frag", {});
  if (!irradiance.success) {
    std::cerr << "  COMPILE FAILED: " << irradiance.errorMessage << "\n";
    return false;
  }
  bindings = ShaderReflector::reflect(irradiance.stages);
  if (!expectBinding(bindings, "SkyboxMap", ShaderPropertyType::TextureCube, 0,
                     0) ||
      !expectBinding(bindings, "CaptureViewUBO",
                     ShaderPropertyType::UniformBuffer, 0, 1)) {
    std::cerr << "  FAIL: irradiance bake bindings mismatch\n";
    return false;
  }

  auto prefilter =
      ShaderCompiler::compileProgram(shaderDir / "ibl_prefilter_env.vert",
                                     shaderDir / "ibl_prefilter_env.frag", {});
  if (!prefilter.success) {
    std::cerr << "  COMPILE FAILED: " << prefilter.errorMessage << "\n";
    return false;
  }
  bindings = ShaderReflector::reflect(prefilter.stages);
  if (!expectBinding(bindings, "SkyboxMap", ShaderPropertyType::TextureCube, 0,
                     0) ||
      !expectBinding(bindings, "CaptureViewUBO",
                     ShaderPropertyType::UniformBuffer, 0, 1) ||
      !expectBinding(bindings, "PrefilterUBO",
                     ShaderPropertyType::UniformBuffer, 0, 2)) {
    std::cerr << "  FAIL: prefilter bake bindings mismatch\n";
    return false;
  }

  auto brdf = ShaderCompiler::compileProgram(
      shaderDir / "ibl_brdf_lut.vert", shaderDir / "ibl_brdf_lut.frag", {});
  if (!brdf.success) {
    std::cerr << "  COMPILE FAILED: " << brdf.errorMessage << "\n";
    return false;
  }

  auto skybox = ShaderCompiler::compileProgram(shaderDir / "skybox.vert",
                                               shaderDir / "skybox.frag", {});
  if (!skybox.success) {
    std::cerr << "  COMPILE FAILED: " << skybox.errorMessage << "\n";
    return false;
  }
  bindings = ShaderReflector::reflect(skybox.stages);
  if (!expectBinding(bindings, "CameraUBO", ShaderPropertyType::UniformBuffer,
                     0, 0) ||
      !expectBinding(bindings, "SkyboxMap", ShaderPropertyType::TextureCube, 1,
                     0)) {
    std::cerr << "  FAIL: skybox background bindings mismatch\n";
    return false;
  }

  std::cout << "  PASS: IBL bake shaders compile and expose expected inputs\n";
  return true;
}

static bool
testTextureCubeReflectionContract(const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: TextureCube reflection contract\n";
  std::cout << "========================================\n";

  const auto vertPath = shaderDir / "texture_cube_probe.vert";
  const auto fragPath = shaderDir / "texture_cube_probe.frag";
  auto compileResult = ShaderCompiler::compileProgram(vertPath, fragPath, {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto environment =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "EnvironmentMap";
      });
  if (environment == bindings.end() ||
      environment->type != ShaderPropertyType::TextureCube) {
    std::cerr << "  FAIL: EnvironmentMap TextureCube binding missing\n";
    return false;
  }
  if (environment->set != 1 || environment->binding != 0) {
    std::cerr << "  FAIL: EnvironmentMap expected set=1 binding=0, got set="
              << environment->set << " binding=" << environment->binding
              << "\n";
    return false;
  }

  std::cout << "  PASS: samplerCube reflects as TextureCube\n";
  return true;
}

static bool
testSpecializationConstantReflectionContract(const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: specialization constant reflection contract\n";
  std::cout << "========================================\n";

  const auto vertPath = shaderDir / "specialization_constant_probe.vert";
  const auto fragPath = shaderDir / "specialization_constant_probe.frag";
  auto compileResult = ShaderCompiler::compileProgram(vertPath, fragPath, {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto constants =
      ShaderReflector::reflectSpecializationConstants(compileResult.stages);
  const auto findConstant = [&](const std::string &name) {
    return std::find_if(constants.begin(), constants.end(),
                        [&](const auto &constant) {
                          return constant.name == name;
                        });
  };

  const auto featureA = findConstant("test_feature_a");
  if (featureA == constants.end()) {
    std::cerr << "  FAIL: test_feature_a specialization constant missing\n";
    return false;
  }
  if (featureA->stage != ShaderStage::Vertex || featureA->constantId != 17 ||
      featureA->type != ShaderSpecializationValueType::Bool) {
    std::cerr << "  FAIL: test_feature_a reflection mismatch\n";
    return false;
  }

  const auto featureB = findConstant("test_feature_b");
  if (featureB == constants.end()) {
    std::cerr << "  FAIL: test_feature_b specialization constant missing\n";
    return false;
  }
  if (featureB->stage != ShaderStage::Fragment || featureB->constantId != 23 ||
      featureB->type != ShaderSpecializationValueType::Bool) {
    std::cerr << "  FAIL: test_feature_b reflection mismatch\n";
    return false;
  }

  auto bindings = ShaderReflector::reflect(compileResult.stages);
  auto vertexInputs = ShaderReflector::reflectVertexInputs(compileResult.stages);
  auto shader = std::make_shared<CompiledShader>(
      std::move(compileResult.stages), std::move(bindings),
      std::move(vertexInputs), constants);
  if (shader->getSpecializationConstants().size() != 2) {
    std::cerr << "  FAIL: CompiledShader should retain reflected "
                 "specialization constants\n";
    return false;
  }

  std::cout << "  PASS: specialization constants reflect by name, stage, id, "
               "and type\n";
  return true;
}

static bool testForwardMainFeatureShaderAbi(
    const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: ForwardMain feature shader ABI\n";
  std::cout << "========================================\n";

  const auto repoRoot = repoRootFromShaderDir(shaderDir);
  const auto featureUris = parseForwardMainFeatureUris(repoRoot);
  if (featureUris.empty()) {
    std::cerr << "  FAIL: forward_main.render-path.yaml has no feature URIs\n";
    return false;
  }

  for (const auto &featureUri : featureUris) {
    const auto featurePath = repoRoot / "assets" / featureUri;
    const FeatureProbe feature = parseFeatureProbe(featurePath);
    if (feature.level.empty() || feature.shaderUri.empty()) {
      std::cerr << "  FAIL: feature " << featurePath
                << " should declare level and shader.uri\n";
      return false;
    }

    const auto shaderPath = shaderPathForUri(shaderDir, feature.shaderUri);
    auto compileResult = feature.level == "pass"
                             ? ShaderCompiler::compileProgram(
                                   shaderDir / "render_paths" / "Forward" /
                                       "pbr.vert",
                                   shaderPath, withMaterialContractSource())
                             : ShaderCompiler::compileFile(shaderPath, {});
    if (!compileResult.success) {
      std::cerr << "  FAIL: feature shader compile failed for "
                << feature.shaderUri << ": " << compileResult.errorMessage
                << "\n";
      return false;
    }

    if (feature.level == "pass") {
      const auto constants =
          ShaderReflector::reflectSpecializationConstants(compileResult.stages);
      for (const auto &[name, parameter] : feature.parameters) {
        if (parameter.volatileRuntime) {
          const auto constant = std::find_if(
              constants.begin(), constants.end(), [&](const auto &candidate) {
                return candidate.name == name;
              });
          if (constant != constants.end()) {
            std::cerr << "  FAIL: volatile pass feature " << featureUri
                      << " parameter " << name
                      << " was reflected as a specialization constant\n";
            return false;
          }
          continue;
        }
        const auto constant = std::find_if(
            constants.begin(), constants.end(), [&](const auto &candidate) {
              return candidate.name == name;
            });
        if (constant == constants.end()) {
          std::cerr << "  FAIL: pass feature " << featureUri
                    << " parameter " << name
                    << " was not reflected as a specialization constant\n";
          return false;
        }
        if (!featureParameterKindMatchesSpecialization(parameter.kind,
                                                       constant->type)) {
          std::cerr << "  FAIL: pass feature " << featureUri
                    << " parameter " << name
                    << " specialization type mismatch\n";
          return false;
        }
      }
      continue;
    }

    if (feature.level != "shader") {
      std::cerr << "  FAIL: unsupported feature level '" << feature.level
                << "' in " << featurePath << "\n";
      return false;
    }

    const auto bindings = ShaderReflector::reflect(compileResult.stages);
    for (const auto &[name, parameter] : feature.parameters) {
      if (parameter.binding.empty()) {
        continue;
      }
      const auto binding = std::find_if(
          bindings.begin(), bindings.end(), [&](const auto &candidate) {
            return candidate.name == parameter.binding;
          });
      if (binding == bindings.end()) {
        std::cerr << "  FAIL: shader feature " << featureUri << " parameter "
                  << name << " binding " << parameter.binding
                  << " was not reflected\n";
        return false;
      }
      if (parameter.kind == "textureCube" || parameter.kind == "texture2D" ||
          parameter.kind == "texture") {
        if (!featureParameterKindMatchesBinding(parameter.kind, binding->type)) {
          std::cerr << "  FAIL: shader feature " << featureUri
                    << " parameter " << name << " binding texture mismatch\n";
          return false;
        }
        continue;
      }
      if (!parameter.member.empty()) {
        if (binding->type != ShaderPropertyType::UniformBuffer) {
          std::cerr << "  FAIL: shader feature " << featureUri
                    << " parameter " << name
                    << " expects a UniformBuffer member\n";
          return false;
        }
        const StructMemberInfo *member = findMember(*binding, parameter.member);
        if (member == nullptr) {
          std::cerr << "  FAIL: shader feature " << featureUri
                    << " parameter " << name << " member "
                    << parameter.member << " was not reflected\n";
          return false;
        }
        if (!featureParameterKindMatchesBinding(parameter.kind, member->type)) {
          std::cerr << "  FAIL: shader feature " << featureUri
                    << " parameter " << name << " member type mismatch\n";
          return false;
        }
      }
    }
  }

  std::cout << "  PASS: ForwardMain features validate against reflected shader "
               "ABI from shader.uri\n";
  return true;
}

static std::string contractMetadataValue(const std::string &source,
                                         const std::string &key) {
  std::istringstream stream(source);
  std::string line;
  const std::string prefix = "// " + key + ":";
  while (std::getline(stream, line)) {
    const std::string stripped = trim(line);
    if (startsWith(stripped, prefix)) {
      return trim(stripped.substr(prefix.size()));
    }
  }
  return {};
}

static std::size_t countRegexMatches(const std::string &source,
                                     const std::regex &pattern) {
  return static_cast<std::size_t>(
      std::distance(std::sregex_iterator(source.begin(), source.end(), pattern),
                    std::sregex_iterator()));
}

static bool testForwardMaterialTypeAbi(const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: Forward material type ABI\n";
  std::cout << "========================================\n";

  const auto materialSurface =
      readTextFile(shaderDir / "common" / "material_surface.glsl");
  if (materialSurface.find("LX_MATERIAL_TYPE_LIT") == std::string::npos ||
      materialSurface.find("LX_MATERIAL_TYPE_UNLIT") == std::string::npos) {
    std::cerr << "  FAIL: material_surface.glsl should define lit/unlit "
                 "material type ABI constants\n";
    return false;
  }

  const auto forwardSource =
      readTextFile(shaderDir / "render_paths" / "Forward" / "pbr.frag");
  const auto loadPos = forwardSource.find("lxLoadMaterialSurface");
  const auto typePos = forwardSource.find("lxGetMaterialType");
  const auto unlitPos = forwardSource.find("LX_MATERIAL_TYPE_UNLIT");
  const auto directPos = forwardSource.find("lxEvaluateBsdf");
  const auto iblPos = forwardSource.find("texture(IrradianceMap");
  const auto tonePos = forwardSource.find("if (LxForwardEnableTonemapping)");
  const auto gammaPos = forwardSource.find("if (LxForwardEnableGamma)");
  if (loadPos == std::string::npos || typePos == std::string::npos ||
      unlitPos == std::string::npos || directPos == std::string::npos ||
      tonePos == std::string::npos || gammaPos == std::string::npos) {
    std::cerr << "  FAIL: Forward shader should load material, branch on "
                 "material type, and use reflected tone/gamma flow constants\n";
    return false;
  }
  if (!(loadPos < typePos && typePos < unlitPos && unlitPos < directPos &&
        unlitPos < tonePos && gammaPos > unlitPos)) {
    std::cerr << "  FAIL: Forward unlit material branch should happen after "
                 "surface load and before direct lighting/tone mapping, with "
                 "one shared final gamma branch\n";
    return false;
  }
  if (iblPos != std::string::npos && !(unlitPos < iblPos)) {
    std::cerr << "  FAIL: Forward unlit material branch should precede IBL\n";
    return false;
  }

  bool sawSupported = false;
  bool sawUnlitTexture = false;
  for (const auto &entry :
       std::filesystem::directory_iterator(shaderDir / "common" / "materials")) {
    if (!entry.is_regular_file() ||
        entry.path().filename().string().find(".contract.glsl") ==
            std::string::npos) {
      continue;
    }
    const std::string source = readTextFile(entry.path());
    if (contractMetadataValue(source, "status") != "supported") {
      continue;
    }
    sawSupported = true;
    const std::string type = contractMetadataValue(source, "type");
    const std::size_t materialTypeFunctions = countRegexMatches(
        source, std::regex(R"(\buint\s+lxGetMaterialType\s*\()"));
    if (materialTypeFunctions != 1) {
      std::cerr << "  FAIL: supported material contract " << entry.path()
                << " should define exactly one lxGetMaterialType()\n";
      return false;
    }
    const bool isUnlit = type == "unlit-texture";
    if (isUnlit) {
      sawUnlitTexture = true;
      if (source.find("LX_MATERIAL_TYPE_UNLIT") == std::string::npos) {
        std::cerr << "  FAIL: unlit_texture contract should return "
                     "LX_MATERIAL_TYPE_UNLIT\n";
        return false;
      }
      if (source.find("parameter: baseColorTexture required texture") ==
              std::string::npos ||
          source.find("storageField: baseColorTexture textureSlot parameter "
                      "baseColorTexture texture") == std::string::npos ||
          source.find("lxSampleSceneTexture(material.baseColorTexture, uv)") ==
              std::string::npos) {
        std::cerr << "  FAIL: unlit_texture contract should declare and sample "
                     "required baseColorTexture storage\n";
        return false;
      }
    } else if (source.find("LX_MATERIAL_TYPE_LIT") == std::string::npos) {
      std::cerr << "  FAIL: lit material contract " << entry.path()
                << " should return LX_MATERIAL_TYPE_LIT\n";
      return false;
    }

    ShaderVariant variant{
        .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
        .enabled = true,
        .macroValue = "\"common/materials/" + entry.path().filename().string() +
                      "\"",
    };
    auto compileResult = ShaderCompiler::compileProgram(
        shaderDir / "render_paths" / "Forward" / "pbr.vert",
        shaderDir / "render_paths" / "Forward" / "pbr.frag", {variant});
    if (!compileResult.success) {
      std::cerr << "  FAIL: Forward should compile with supported material "
                << type << ": " << compileResult.errorMessage << "\n";
      return false;
    }
  }

  if (!sawSupported || !sawUnlitTexture) {
    std::cerr << "  FAIL: supported material contracts should include "
                 "unlit-texture\n";
    return false;
  }

  std::cout << "  PASS: Forward material contracts expose lit/unlit ABI and "
               "compile through Forward\n";
  return true;
}

static bool testPbrRuntimeLightingContract(
    const std::filesystem::path &shaderDir,
    const std::filesystem::path &vertPath,
    const std::filesystem::path &fragPath) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: PBR runtime lighting contract\n";
  std::cout << "========================================\n";

  std::string fragSource;
  {
    std::ifstream in(fragPath);
    fragSource.assign(std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>());
  }
  if (fragSource.find("color = color / (color + vec3(1.0))") !=
          std::string::npos ||
      fragSource.find("pow(color, vec3(1.0 / 2.2))") != std::string::npos) {
    std::cerr << "  FAIL: PBR shader still performs final tone/gamma mapping\n";
    return false;
  }
  if (fragSource.find("lxPbrFallbackAmbient") != std::string::npos) {
    std::cerr << "  FAIL: PBR direct-light path should not hard-code "
                 "fallback ambient\n";
    return false;
  }
  std::string commonPbrSource;
  {
    std::ifstream in(shaderDir / "common" / "pbr.glsl");
    commonPbrSource.assign(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
  }
  if (commonPbrSource.find("lxPbrFallbackAmbient") != std::string::npos) {
    std::cerr << "  FAIL: shared PBR common should not expose fallback "
                 "ambient\n";
    return false;
  }

  auto compileResult =
      ShaderCompiler::compileProgram(vertPath, fragPath,
                                     withMaterialContractSource());
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  if (!hasBinding(bindings, "LightUBO", ShaderPropertyType::UniformBuffer, 2,
                  0)) {
    std::cerr << "  FAIL: LightUBO set=2 binding=0 missing\n";
    return false;
  }

  std::cout << "  PASS: PBR outputs HDR and reflects direct lighting binding\n";
  return true;
}

static bool testPbrMaterialGpuRecordContract(
    const std::filesystem::path &vertPath,
    const std::filesystem::path &fragPath,
    const std::string &label = "PBR material GPU record contract") {
  std::cout << "  Test: " << label << "\n";
  auto compileResult =
      ShaderCompiler::compileProgram(
          vertPath, fragPath,
          withMaterialContractSource({{"HAS_NORMAL_MAP", true},
                                      {"HAS_METALLIC_ROUGHNESS", true},
                                      {"HAS_AO_MAP", true},
                                      {"HAS_EMISSIVE_MAP", true}}));
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto findBinding = [&](const std::string &name) {
    return std::find_if(
        bindings.begin(), bindings.end(),
        [&](const auto &candidate) { return candidate.name == name; });
  };
  const auto rejectBinding = [&](const std::string &name) {
    const auto it = findBinding(name);
    if (it != bindings.end()) {
      std::cerr << "  FAIL: PBR shader still reflects legacy binding " << name
                << "\n";
      return false;
    }
    return true;
  };

  if (!rejectBinding(joinedToken("Material", "UBO")) ||
      !rejectBinding("albedoMap") ||
      !rejectBinding("normalMap") || !rejectBinding("metallicRoughnessMap") ||
      !rejectBinding("aoMap") || !rejectBinding("emissiveMap")) {
    return false;
  }

  const auto expectBinding = [&](const std::string &name,
                                 ShaderPropertyType type, u32 set, u32 binding,
                                 u32 descriptorCount) {
    const auto it = std::find_if(
        bindings.begin(), bindings.end(),
        [&](const auto &candidate) { return candidate.name == name; });
    if (it == bindings.end() || it->type != type || it->set != set ||
        it->binding != binding || it->descriptorCount != descriptorCount) {
      std::cerr << "  FAIL: " << name
                << " expected type=" << shaderPropertyTypeName(type)
                << " set=" << set << " binding=" << binding
                << " count=" << descriptorCount << "\n";
      return false;
    }
    return true;
  };

  if (!expectBinding("SceneMaterials", ShaderPropertyType::StorageBuffer, 0, 7,
                     1) ||
      !expectBinding("SceneObjects", ShaderPropertyType::StorageBuffer, 0, 8,
                     1) ||
      !expectBinding("SceneDraws", ShaderPropertyType::StorageBuffer, 0, 9,
                     1) ||
      !expectBinding("SceneTextures", ShaderPropertyType::Texture2D, 0, 11,
                     256)) {
    return false;
  }

  const auto source = readTextFile(fragPath);
  const auto vertSource = readTextFile(vertPath);
  const std::vector<std::string> legacyTokens{
      joinedToken("Material", "UBO"),
      joinedToken("baseColor", "Factor"),
      joinedToken("metallic", "Factor"),
      joinedToken("roughness", "Factor"),
      "albedoMap",
      "normalMap",
      "metallicRoughnessMap",
      "aoMap",
      "emissiveMap",
  };
  for (const std::string &token : legacyTokens) {
    if (source.find(token) != std::string::npos) {
      std::cerr << "  FAIL: PBR fragment source still contains legacy token "
                << token << "\n";
      return false;
    }
  }
  if (source.find("materials[0]") != std::string::npos) {
    std::cerr << "  FAIL: PBR fragment source hard-codes materials[0]\n";
    return false;
  }
  if (source.find("materials[vMaterialIndex]") == std::string::npos) {
    std::cerr << "  FAIL: PBR fragment source should index SceneMaterials "
                 "with the per-draw material index\n";
    return false;
  }
  if (vertSource.find("mat4 model = mat4(1.0)") != std::string::npos) {
    std::cerr << "  FAIL: PBR vertex source should not use identity model as "
                 "the production transform\n";
    return false;
  }
  if (vertSource.find("draws[gl_InstanceIndex]") == std::string::npos ||
      vertSource.find("objects[draw.objectIndex].objectToWorld") ==
          std::string::npos ||
      vertSource.find("draw.materialIndex") == std::string::npos) {
    std::cerr << "  FAIL: PBR vertex source should read typed draw/object "
                 "records for transform and material index\n";
    return false;
  }

  std::cout << "  PASS: PBR reflects migrated material GPU record bindings\n";
  return true;
}

static bool
testPbrFragmentUsesSharedCommon(const std::filesystem::path &fragPath) {
  std::cout << "  Test: PBR fragment uses shared common\n";
  const auto source = readTextFile(fragPath);
  if (source.find("#include \"common/pbr.glsl\"") == std::string::npos) {
    std::cerr << "  FAIL: pbr.frag should include shared PBR common\n";
    return false;
  }

  std::cout << "  PASS: PBR fragment includes shared common\n";
  return true;
}

static bool testPbrFragmentAppliesDirectionalLightIntensity(
    const std::filesystem::path &fragPath) {
  std::cout << "  Test: PBR fragment applies directional light intensity\n";
  const auto source = readTextFile(fragPath);
  if (source.find("pbrInput.lightColor = light.color.rgb * light.color.a") ==
      std::string::npos) {
    std::cerr << "  FAIL: pbr.frag should multiply LightUBO color by "
                 "directional intensity in light.color.a\n";
    return false;
  }

  std::cout << "  PASS: PBR fragment applies directional intensity\n";
  return true;
}

static bool
testPbrFragmentAppliesConstantEnvironmentLight(
    const std::filesystem::path &fragPath) {
  std::cout << "  Test: PBR fragment applies constant environment light\n";
  const auto source = readTextFile(fragPath);
  if (source.find("lxEvaluateConstantEnvironmentLight") == std::string::npos) {
    std::cerr << "  FAIL: pbr.frag should apply constant environment "
                 "lighting before postprocess\n";
    return false;
  }
  if (source.find("environment.ambientColorIntensity") == std::string::npos) {
    std::cerr << "  FAIL: pbr.frag should source ambient lighting from "
                 "the environment uniform\n";
    return false;
  }

  std::cout << "  PASS: PBR fragment applies constant environment light\n";
  return true;
}

static bool
testIblFormulaUsesSharedCommon(const std::filesystem::path &shaderDir) {
  std::cout << "  Test: IBL formula uses shared common\n";
  const auto shaderSource = [&](const std::filesystem::path &relativePath) {
    return readTextFile(shaderDir / relativePath);
  };
  const auto expect = [](bool condition, const std::string &message) {
    if (!condition) {
      std::cerr << "  FAIL: " << message << "\n";
      return false;
    }
    return true;
  };

  bool passed = true;
  const std::string forwardSource =
      shaderSource("render_paths/Forward/pbr.frag");
  const std::string deferredSource =
      shaderSource("render_paths/Deferred/deferred_lighting.frag");

  passed &= expect(
      forwardSource.find("evaluateIblStandardPbr(") != std::string::npos,
      "Forward should call common IBL helper");
  passed &= expect(
      forwardSource.find("textureLod(PrefilteredEnvMap") == std::string::npos, // named-negative-ibl-formula-audit
      "Forward must not inline prefiltered env formula");
  passed &= expect(forwardSource.find("texture(BrdfLut") == std::string::npos, // named-negative-ibl-formula-audit
                   "Forward must not inline BRDF LUT formula");
  passed &= expect(
      deferredSource.find("common/ibl_lighting.glsl") != std::string::npos,
      "Deferred should include common IBL helper");
  passed &= expect(
      deferredSource.find("textureLod(PrefilteredEnvMap") == std::string::npos, // named-negative-ibl-formula-audit
      "Deferred must not inline prefiltered env formula");
  passed &= expect(deferredSource.find("texture(BrdfLut") == std::string::npos, // named-negative-ibl-formula-audit
                   "Deferred must not inline BRDF LUT formula");

  if (passed) {
    std::cout << "  PASS: Forward and Deferred use shared IBL formula\n";
  }
  return passed;
}

static bool
testShadowDepthOnlyUsesSceneDrawRecords(const std::filesystem::path &shaderDir) {
  std::cout << "  Test: shadow depth vertex uses scene draw/object records\n";
  const auto vertPath =
      shaderDir / "render_paths" / "Forward" / "shadow_depth_only.vert";
  if (!std::filesystem::exists(vertPath)) {
    std::cerr << "  FAIL: shadow_depth_only.vert should exist\n";
    return false;
  }

  auto compileResult = ShaderCompiler::compileFile(vertPath, {});
  if (!compileResult.success) {
    std::cerr << "  FAIL: shadow_depth_only.vert compile failed: "
              << compileResult.errorMessage << "\n";
    return false;
  }
  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto hasSsbo = [&](const std::string &name, u32 binding) {
    const auto it =
        std::find_if(bindings.begin(), bindings.end(),
                     [&](const ShaderResourceBinding &candidate) {
                       return candidate.name == name;
                     });
    if (it == bindings.end() || it->type != ShaderPropertyType::StorageBuffer ||
        it->set != 0 || it->binding != binding) {
      std::cerr << "  FAIL: shadow_depth_only.vert should reflect " << name
                << " as set 0 binding " << binding << " storage buffer\n";
      return false;
    }
    return true;
  };
  if (!hasSsbo("SceneObjects", 8) || !hasSsbo("SceneDraws", 9)) {
    return false;
  }

  const auto source = readTextFile(vertPath);
  if (source.find("mat4 model = mat4(1.0)") != std::string::npos) {
    std::cerr << "  FAIL: shadow vertex source should not use identity model "
                 "as the production transform\n";
    return false;
  }
  if (source.find("draws[gl_InstanceIndex]") == std::string::npos ||
      source.find("objects[draw.objectIndex].objectToWorld") ==
          std::string::npos) {
    std::cerr << "  FAIL: shadow vertex source should read typed draw/object "
                 "records\n";
    return false;
  }

  std::cout << "  PASS: shadow depth vertex reads scene draw/object records\n";
  return true;
}

static bool testSharedPbrKeepsLowRoughnessHighlights(
    const std::filesystem::path &shaderDir) {
  std::cout << "  Test: shared PBR keeps low-roughness clearcoat highlights\n";
  const auto source = readTextFile(shaderDir / "common" / "pbr.glsl");
  if (source.find("max(denom, 0.0001)") != std::string::npos) {
    std::cerr << "  FAIL: GGX distribution should not clamp the denominator "
                 "to 0.0001 because that erases low-roughness clearcoat "
                 "highlights\n";
    return false;
  }
  if (source.find("1.0e-7") == std::string::npos) {
    std::cerr << "  FAIL: GGX distribution should use the low denominator "
                 "epsilon expected by the clearcoat contract\n";
    return false;
  }

  std::cout << "  PASS: shared PBR preserves low-roughness highlights\n";
  return true;
}

static bool
testSceneSystemAbiReflection(const std::filesystem::path &shaderDir) {
  std::cout << "  Test: scene system ABI SSBO reflection\n";
  const auto tempDir =
      std::filesystem::temp_directory_path() / "lxe_shader_abi";
  std::filesystem::create_directories(tempDir);
  std::filesystem::create_directories(tempDir / "common");
  std::filesystem::copy_file(shaderDir / "common" / "scene_system_abi.glsl",
                             tempDir / "common" / "scene_system_abi.glsl",
                             std::filesystem::copy_options::overwrite_existing);
  const auto shaderPath = tempDir / "scene_system_abi_probe.comp";
  {
    std::ofstream out(shaderPath);
    out << "#version 450\n"
        << "#include \"common/scene_system_abi.glsl\"\n"
        << "layout(local_size_x = 1) in;\n"
        << "void main() {}\n";
  }

  auto compileResult = ShaderCompiler::compileFile(shaderPath, {});
  if (!compileResult.success) {
    std::cerr << "  FAIL: scene system ABI probe compile failed: "
              << compileResult.errorMessage << "\n";
    return false;
  }
  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto findBinding = [&](const std::string &name) {
    return std::find_if(bindings.begin(), bindings.end(),
                        [&](const ShaderResourceBinding &binding) {
                          return binding.name == name;
                        });
  };

  const auto expectSsbo = [&](const std::string &name, u32 binding, u32 size,
                              const std::string &member, u32 offset) {
    const auto it = findBinding(name);
    if (it == bindings.end()) {
      std::cerr << "  FAIL: " << name << " binding missing\n";
      return false;
    }
    if (it->type != ShaderPropertyType::StorageBuffer ||
        it->set != kSceneSystemDescriptorSet || it->binding != binding ||
        it->size != size) {
      std::cerr << "  FAIL: " << name
                << " expected SSBO set=" << kSceneSystemDescriptorSet
                << " binding=" << binding << " size=" << size
                << ", got set=" << it->set << " binding=" << it->binding
                << " size=" << it->size << "\n";
      return false;
    }
    const auto *reflectedMember = findMember(*it, member);
    if (!reflectedMember || reflectedMember->offset != offset) {
      std::cerr << "  FAIL: " << name << "." << member << " expected offset "
                << offset << "\n";
      return false;
    }
    return true;
  };

  if (!expectSsbo("SceneCameraData", kSceneSystemCameraBinding,
                  sizeof(SceneSystemCameraData), "view", 0) ||
      !expectSsbo("SceneLightData", kSceneSystemLightBinding,
                  sizeof(SceneSystemLightData), "directionIntensity", 0) ||
      !expectSsbo("SceneObjectData", kSceneSystemObjectBinding,
                  sizeof(SceneSystemObjectData), "objectToWorld0", 0) ||
      !expectSsbo("SceneMaterialInstanceData", kSceneSystemMaterialBinding,
                  sizeof(SceneSystemMaterialInstanceData), "baseColor", 0)) {
    return false;
  }

  std::cout << "  PASS: scene system ABI SSBOs reflect C++ mirror layout\n";
  return true;
}

static bool testSceneSystemAbiRejectsLegacyUboDeclaration() {
  std::cout << "  Test: scene system ABI rejects legacy UBO declaration\n";
  const auto tempDir =
      std::filesystem::temp_directory_path() / "lxe_shader_abi_legacy";
  std::filesystem::create_directories(tempDir);
  const auto shaderPath = tempDir / "scene_system_abi_legacy_ubo.comp";
  {
    std::ofstream out(shaderPath);
    out << "#version 450\n"
        << "layout(std140, set = 0, binding = 0) uniform SceneCameraData {\n"
        << "  vec4 view;\n"
        << "  vec4 projection;\n"
        << "  vec4 eye;\n"
        << "} cameraData;\n"
        << "layout(local_size_x = 1) in;\n"
        << "void main() {}\n";
  }

  auto compileResult = ShaderCompiler::compileFile(shaderPath, {});
  if (!compileResult.success) {
    std::cerr << "  FAIL: legacy UBO probe compile failed: "
              << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto it = std::find_if(bindings.begin(), bindings.end(),
                               [](const ShaderResourceBinding &binding) {
                                 return binding.name == "SceneCameraData";
                               });
  if (it == bindings.end()) {
    std::cerr << "  FAIL: SceneCameraData binding missing from legacy UBO "
                 "probe reflection\n";
    return false;
  }

  const auto diagnostic = validateSystemAbiBindingContract(*it);
  if (!diagnostic.has_value()) {
    std::cerr << "  FAIL: legacy UBO SceneCameraData was accepted\n";
    return false;
  }
  if (diagnostic->find("SceneCameraData") == std::string::npos ||
      diagnostic->find("StorageBuffer") == std::string::npos ||
      diagnostic->find("UniformBuffer") == std::string::npos) {
    std::cerr << "  FAIL: legacy UBO diagnostic lacks ABI detail: "
              << *diagnostic << "\n";
    return false;
  }

  std::cout << "  PASS: legacy SceneCameraData UBO is rejected by ABI "
               "validation\n";
  return true;
}

int main(int argc, char *argv[]) {
  expSetEnvVK();
  // Determine shader directory
  std::filesystem::path shaderDir;
  if (argc > 1) {
    shaderDir = argv[1];
  } else {
    // Default: relative to executable, try common locations
    shaderDir = std::filesystem::current_path() / "assets" / "shaders" / "glsl";
    if (!std::filesystem::exists(shaderDir)) {
      // Try source tree location
      shaderDir = std::filesystem::path(__FILE__)
                      .parent_path()
                      .parent_path()
                      .parent_path()
                      .parent_path() /
                  "assets" / "shaders" / "glsl";
    }
  }

  auto vertPath = shaderDir / "render_paths" / "Forward" / "pbr.vert";
  auto fragPath = shaderDir / "render_paths" / "Forward" / "pbr.frag";

  if (!std::filesystem::exists(vertPath) ||
      !std::filesystem::exists(fragPath)) {
    std::cerr << "PBR shader files not found at: " << shaderDir << "\n";
    std::cerr << "Usage: " << argv[0] << " [shader_directory]\n";
    return 1;
  }

  std::cout << "Shader directory: " << shaderDir << "\n";

  int failures = 0;

  if (!testPbrShadersUseMaterialAccessorAbi(shaderDir))
    ++failures;
  if (!testVariantOnlyShaderNakedCompileFailsWithDiagnostic(shaderDir))
    ++failures;
  if (!testMaterialSourceVariantCompilesVariantOnlyShaders(shaderDir))
    ++failures;

  // Test 1: Contract-source PBR
  if (!testVariantCombination(vertPath, fragPath, "PBR + Material Contract",
                              withMaterialContractSource()))
    ++failures;

  // Test 2: HAS_NORMAL_MAP only
  if (!testVariantCombination(
          vertPath, fragPath, "PBR + Normal Map",
          withMaterialContractSource(
              {{"HAS_NORMAL_MAP", true}, {"HAS_METALLIC_ROUGHNESS", false}})))
    ++failures;

  // Test 3: All variants enabled
  if (!testVariantCombination(
          vertPath, fragPath, "PBR + All Variants",
          withMaterialContractSource(
              {{"HAS_NORMAL_MAP", true}, {"HAS_METALLIC_ROUGHNESS", true}})))
    ++failures;
  if (!testPbrRuntimeLightingContract(shaderDir, vertPath, fragPath))
    ++failures;
  if (!testPbrFragmentUsesSharedCommon(fragPath))
    ++failures;
  if (!testPbrFragmentAppliesDirectionalLightIntensity(fragPath))
    ++failures;
  if (!testPbrFragmentAppliesConstantEnvironmentLight(fragPath))
    ++failures;
  if (!testIblFormulaUsesSharedCommon(shaderDir))
    ++failures;
  if (!testShadowDepthOnlyUsesSceneDrawRecords(shaderDir))
    ++failures;
  if (!testSharedPbrKeepsLowRoughnessHighlights(shaderDir))
    ++failures;
  if (!testSceneSystemAbiReflection(shaderDir))
    ++failures;
  if (!testSceneSystemAbiRejectsLegacyUboDeclaration())
    ++failures;

  // Test 4: default realtime shader feature contract reflection
  if (!testDefaultRealtimeShadersExposeToneMappingFeature(shaderDir))
    ++failures;
  if (!testTextureCubeReflectionContract(shaderDir))
    ++failures;
  if (!testSpecializationConstantReflectionContract(shaderDir))
    ++failures;
  if (!testForwardMainFeatureShaderAbi(shaderDir))
    ++failures;
  if (!testForwardMaterialTypeAbi(shaderDir))
    ++failures;
  if (!testIblBakeShaderContracts(shaderDir))
    ++failures;

  std::cout << "\n========================================\n";
  if (failures == 0) {
    std::cout << "All tests PASSED\n";
  } else {
    std::cout << failures << " test(s) FAILED\n";
  }
  std::cout << "========================================\n";

  return failures;
}
