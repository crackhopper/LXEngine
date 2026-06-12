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
#include <optional>
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

static std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream ifs(path);
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  return buffer.str();
}

static std::string joinedToken(const char *head, const char *tail) {
  return std::string(head) + tail;
}

static bool
testPostProcessShaderContract(const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: Post-process shader contract\n";
  std::cout << "========================================\n";

  const auto vertPath = shaderDir / "post_process.vert";
  const auto fragPath = shaderDir / "post_process.frag";
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

  const auto thresholdPath = shaderDir / "bloom_threshold.frag";
  auto thresholdResult = ShaderCompiler::compileProgram(
      shaderDir / "bloom_threshold.vert", thresholdPath, {});
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
    auto blurResult = ShaderCompiler::compileProgram(shaderDir / vertName,
                                                     shaderDir / fragName, {});
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

  std::cout << "  PASS: bloom shaders reflect threshold and blur inputs\n";
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
                     0) ||
      !expectBinding(bindings, "EnvironmentUBO",
                     ShaderPropertyType::UniformBuffer, 2, 0)) {
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

static bool testPbrIblContract(const std::filesystem::path &shaderDir,
                               const std::filesystem::path &vertPath,
                               const std::filesystem::path &fragPath) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: PBR IBL contract\n";
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
      ShaderCompiler::compileProgram(vertPath, fragPath, {{"HAS_IBL", true}});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto findBinding = [&](const std::string &name) {
    return std::find_if(
        bindings.begin(), bindings.end(),
        [&](const auto &binding) { return binding.name == name; });
  };

  const auto irradiance = findBinding("IrradianceMap");
  const auto prefiltered = findBinding("PrefilteredEnvMap");
  const auto brdf = findBinding("BrdfLut");
  const auto environment = findBinding("EnvironmentUBO");
  if (irradiance == bindings.end() ||
      irradiance->type != ShaderPropertyType::TextureCube ||
      irradiance->set != 3 || irradiance->binding != 0) {
    std::cerr << "  FAIL: IrradianceMap TextureCube set=3 binding=0 missing\n";
    return false;
  }
  if (prefiltered == bindings.end() ||
      prefiltered->type != ShaderPropertyType::TextureCube ||
      prefiltered->set != 3 || prefiltered->binding != 1) {
    std::cerr
        << "  FAIL: PrefilteredEnvMap TextureCube set=3 binding=1 missing\n";
    return false;
  }
  if (brdf == bindings.end() || brdf->type != ShaderPropertyType::Texture2D ||
      brdf->set != 3 || brdf->binding != 2) {
    std::cerr << "  FAIL: BrdfLut Texture2D set=3 binding=2 missing\n";
    return false;
  }
  if (environment == bindings.end() ||
      environment->type != ShaderPropertyType::UniformBuffer ||
      environment->set != 3 || environment->binding != 3 ||
      environment->size != 16) {
    std::cerr << "  FAIL: EnvironmentUBO set=3 binding=3 size=16 missing\n";
    return false;
  }

  std::cout << "  PASS: PBR outputs HDR and reflects IBL bindings\n";
  return true;
}

static bool testPbrMaterialGpuRecordContract(
    const std::filesystem::path &vertPath,
    const std::filesystem::path &fragPath,
    const std::string &label = "PBR material GPU record contract") {
  std::cout << "  Test: " << label << "\n";
  auto compileResult =
      ShaderCompiler::compileProgram(vertPath, fragPath,
                                     {{"HAS_NORMAL_MAP", true},
                                      {"HAS_METALLIC_ROUGHNESS", true},
                                      {"HAS_AO_MAP", true},
                                      {"HAS_EMISSIVE_MAP", true}});
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
testPbrClearcoatShaderContract(const std::filesystem::path &shaderDir) {
  std::cout << "  Test: PBR clearcoat shader compiles and reflects layered "
               "parameters\n";
  const auto vertPath =
      shaderDir / "techniques" / "Forward" / "pbr_clearcoat.vert";
  const auto fragPath =
      shaderDir / "techniques" / "Forward" / "pbr_clearcoat.frag";
  if (!std::filesystem::exists(vertPath) ||
      !std::filesystem::exists(fragPath)) {
    std::cerr << "  FAIL: pbr_clearcoat shader files should exist\n";
    return false;
  }

  const auto compileResult =
      ShaderCompiler::compileProgram(vertPath, fragPath, {});
  if (!compileResult.success) {
    std::cerr << "  FAIL: pbr_clearcoat shader compile failed: "
              << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto materialBinding =
      std::find_if(bindings.begin(), bindings.end(),
                   [](const ShaderResourceBinding &binding) {
                     return binding.name == "SceneMaterials";
                   });
  if (materialBinding == bindings.end() ||
      materialBinding->type != ShaderPropertyType::StorageBuffer) {
    std::cerr << "  FAIL: pbr_clearcoat SceneMaterials SSBO binding missing\n";
    return false;
  }

  const auto source = readTextFile(fragPath);
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
      std::cerr << "  FAIL: pbr_clearcoat.frag still contains legacy token "
                << token << "\n";
      return false;
    }
  }
  if (source.find("materials[0]") != std::string::npos) {
    std::cerr << "  FAIL: pbr_clearcoat.frag hard-codes materials[0]\n";
    return false;
  }
  if (source.find("materials[vMaterialIndex]") == std::string::npos) {
    std::cerr << "  FAIL: pbr_clearcoat.frag should index SceneMaterials "
                 "with the per-draw material index\n";
    return false;
  }
  if (source.find("lxPbrLayeredClearcoatDirectLight") == std::string::npos) {
    std::cerr << "  FAIL: pbr_clearcoat.frag should call shared layered "
                 "clearcoat BRDF helper\n";
    return false;
  }

  std::cout << "  PASS: PBR clearcoat shader contract\n";
  return true;
}

static bool
testDeferredPbrShaderContracts(const std::filesystem::path &shaderDir) {
  std::cout << "  Test: Deferred PBR shader GPU record contracts\n";
  const auto pbrVert =
      shaderDir / "techniques" / "Deferred" / "pbr_gbuffer.vert";
  const auto pbrFrag =
      shaderDir / "techniques" / "Deferred" / "pbr_gbuffer.frag";
  const auto clearcoatVert =
      shaderDir / "techniques" / "Deferred" / "pbr_clearcoat_gbuffer.vert";
  const auto clearcoatFrag =
      shaderDir / "techniques" / "Deferred" / "pbr_clearcoat_gbuffer.frag";
  if (!std::filesystem::exists(pbrVert) || !std::filesystem::exists(pbrFrag) ||
      !std::filesystem::exists(clearcoatVert) ||
      !std::filesystem::exists(clearcoatFrag)) {
    std::cerr << "  FAIL: deferred PBR shader files should exist\n";
    return false;
  }

  if (!testPbrMaterialGpuRecordContract(
          pbrVert, pbrFrag, "Deferred PBR material GPU record contract") ||
      !testPbrMaterialGpuRecordContract(
          clearcoatVert, clearcoatFrag,
          "Deferred clearcoat material GPU record contract")) {
    return false;
  }

  std::cout << "  PASS: Deferred PBR shaders use migrated material records\n";
  return true;
}

static bool
testShadowDepthOnlyUsesSceneDrawRecords(const std::filesystem::path &shaderDir) {
  std::cout << "  Test: shadow depth vertex uses scene draw/object records\n";
  const auto vertPath =
      shaderDir / "techniques" / "Forward" / "shadow_depth_only.vert";
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

  auto vertPath = shaderDir / "techniques" / "Forward" / "pbr.vert";
  auto fragPath = shaderDir / "techniques" / "Forward" / "pbr.frag";

  if (!std::filesystem::exists(vertPath) ||
      !std::filesystem::exists(fragPath)) {
    std::cerr << "PBR shader files not found at: " << shaderDir << "\n";
    std::cerr << "Usage: " << argv[0] << " [shader_directory]\n";
    return 1;
  }

  std::cout << "Shader directory: " << shaderDir << "\n";

  int failures = 0;

  // Test 1: No variants (base PBR)
  if (!testVariantCombination(vertPath, fragPath, "Base PBR (no variants)", {}))
    ++failures;

  // Test 2: HAS_NORMAL_MAP only
  if (!testVariantCombination(
          vertPath, fragPath, "PBR + Normal Map",
          {{"HAS_NORMAL_MAP", true}, {"HAS_METALLIC_ROUGHNESS", false}}))
    ++failures;

  // Test 3: All variants enabled
  if (!testVariantCombination(
          vertPath, fragPath, "PBR + All Variants",
          {{"HAS_NORMAL_MAP", true}, {"HAS_METALLIC_ROUGHNESS", true}}))
    ++failures;
  if (!testPbrIblContract(shaderDir, vertPath, fragPath))
    ++failures;
  if (!testPbrMaterialGpuRecordContract(vertPath, fragPath))
    ++failures;
  if (!testPbrFragmentUsesSharedCommon(fragPath))
    ++failures;
  if (!testPbrFragmentAppliesDirectionalLightIntensity(fragPath))
    ++failures;
  if (!testPbrClearcoatShaderContract(shaderDir))
    ++failures;
  if (!testDeferredPbrShaderContracts(shaderDir))
    ++failures;
  if (!testShadowDepthOnlyUsesSceneDrawRecords(shaderDir))
    ++failures;
  if (!testSharedPbrKeepsLowRoughnessHighlights(shaderDir))
    ++failures;
  if (!testSceneSystemAbiReflection(shaderDir))
    ++failures;
  if (!testSceneSystemAbiRejectsLegacyUboDeclaration())
    ++failures;

  // Test 4: post-process and supporting shader contract reflection
  if (!testPostProcessShaderContract(shaderDir))
    ++failures;
  if (!testBloomShaderContracts(shaderDir))
    ++failures;
  if (!testTextureCubeReflectionContract(shaderDir))
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
