#include "core/utils/env.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"
#include "core/rhi/gpu_resource.hpp"

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
              << "count=" << b.descriptorCount << "  "
              << "size=" << b.size << "  "
              << "stages=" << stageFlagsToString(b.stageFlags) << "\n";
  }
}

static void
printDescriptorSetLayoutMapping(const std::vector<ShaderResourceBinding> &bindings) {
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

static bool testVariantCombination(
    const std::filesystem::path &vertPath,
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
  auto vertexInputs = ShaderReflector::reflectVertexInputs(compileResult.stages);
  std::cout << "  Reflection found " << bindings.size() << " bindings\n";

  // Create CompiledShader
  auto shader = std::make_shared<CompiledShader>(std::move(compileResult.stages),
                                                 bindings, vertexInputs);
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

  auto byName = shader->findBinding("MaterialUBO");
  if (byName) {
    std::cout << "  findBinding(\"MaterialUBO\") -> set=" << byName->get().set
              << " binding=" << byName->get().binding << "\n";
  } else {
    std::cout << "  findBinding(\"MaterialUBO\") -> not found\n";
  }

  return true;
}

// ---------------------------------------------------------------------------
// UBO member reflection tests (REQ-004)
// ---------------------------------------------------------------------------

static const StructMemberInfo *
findMember(const ShaderResourceBinding &b, const std::string &name) {
  for (const auto &m : b.members) {
    if (m.name == name)
      return &m;
  }
  return nullptr;
}

static bool testBlinnPhongMaterialUboMembers(const std::filesystem::path &vertPath,
                                             const std::filesystem::path &fragPath) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: BlinnPhong MaterialUBO members\n";
  std::cout << "========================================\n";

  auto compileResult = ShaderCompiler::compileProgram(vertPath, fragPath, {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }
  auto bindings = ShaderReflector::reflect(compileResult.stages);

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
      materialBinding;
  for (const auto &b : bindings) {
    if (b.name == "MaterialUBO" ||
        (b.set == 2 && b.binding == 0 &&
         b.type == ShaderPropertyType::UniformBuffer)) {
      materialBinding = std::cref(b);
      break;
    }
  }
  if (!materialBinding) {
    std::cerr << "  FAIL: MaterialUBO binding not found\n";
    return false;
  }

  // 4.2 basic shape
  if (materialBinding->get().type != ShaderPropertyType::UniformBuffer) {
    std::cerr << "  FAIL: MaterialUBO is not UniformBuffer type\n";
    return false;
  }
  if (materialBinding->get().members.size() < 5) {
    std::cerr << "  FAIL: expected >= 5 members, got "
              << materialBinding->get().members.size() << "\n";
    return false;
  }
  std::cout << "  MaterialUBO has " << materialBinding->get().members.size()
            << " members\n";
  for (const auto &m : materialBinding->get().members) {
    std::cout << "    - " << m.name
              << "  type=" << shaderPropertyTypeName(m.type)
              << "  offset=" << m.offset << "  size=" << m.size << "\n";
  }

  // 4.3 baseColor: Vec3 at offset 0
  const auto *baseColor = findMember(materialBinding->get(), "baseColor");
  if (!baseColor) {
    std::cerr << "  FAIL: baseColor member missing\n";
    return false;
  }
  if (baseColor->type != ShaderPropertyType::Vec3 || baseColor->offset != 0) {
    std::cerr << "  FAIL: baseColor expected Vec3@0, got "
              << shaderPropertyTypeName(baseColor->type) << "@"
              << baseColor->offset << "\n";
    return false;
  }

  // 4.4 shininess: Float, std140 packs it right after vec3 at offset 12
  const auto *shininess = findMember(materialBinding->get(), "shininess");
  if (!shininess) {
    std::cerr << "  FAIL: shininess member missing\n";
    return false;
  }
  if (shininess->type != ShaderPropertyType::Float) {
    std::cerr << "  FAIL: shininess expected Float, got "
              << shaderPropertyTypeName(shininess->type) << "\n";
    return false;
  }
  if (shininess->offset != 12 && shininess->offset != 16) {
    std::cerr << "  FAIL: shininess expected offset 12 or 16, got "
              << shininess->offset << "\n";
    return false;
  }

  // 4.5 enableAlbedo: Int
  const auto *enableAlbedo = findMember(materialBinding->get(), "enableAlbedo");
  if (!enableAlbedo) {
    std::cerr << "  FAIL: enableAlbedo member missing\n";
    return false;
  }
  if (enableAlbedo->type != ShaderPropertyType::Int) {
    std::cerr << "  FAIL: enableAlbedo expected Int, got "
              << shaderPropertyTypeName(enableAlbedo->type) << "\n";
    return false;
  }

  // 4.6 debugShadowMode: Int diagnostic switch for shadow sampling.
  const auto *debugShadowMode =
      findMember(materialBinding->get(), "debugShadowMode");
  if (!debugShadowMode) {
    std::cerr << "  FAIL: debugShadowMode member missing\n";
    return false;
  }
  if (debugShadowMode->type != ShaderPropertyType::Int) {
    std::cerr << "  FAIL: debugShadowMode expected Int, got "
              << shaderPropertyTypeName(debugShadowMode->type) << "\n";
    return false;
  }

  // 4.7 non-UBO bindings have empty members (check sampler2D bindings)
  for (const auto &b : bindings) {
    if (b.type == ShaderPropertyType::Texture2D && !b.members.empty()) {
      std::cerr << "  FAIL: Texture2D binding '" << b.name
                << "' unexpectedly has " << b.members.size() << " members\n";
      return false;
    }
  }

  std::cout << "  PASS: MaterialUBO members reflected correctly\n";
  return true;
}

static bool testBlinnPhongVariantVertexInputs(
    const std::filesystem::path &vertPath,
    const std::filesystem::path &fragPath) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: BlinnPhong variant contracts\n";
  std::cout << "========================================\n";

  const auto compileVariant =
      [&](std::initializer_list<ShaderVariant> variants) -> CompileResult {
    return ShaderCompiler::compileProgram(vertPath, fragPath,
                                          std::vector<ShaderVariant>(variants));
  };
  const auto hasInput =
      [](const std::vector<VertexInputAttribute> &inputs,
         const std::string &name, u32 location,
         DataType type) {
        for (const auto &input : inputs) {
          if (input.name == name && input.location == location &&
              input.type == type) {
            return true;
          }
        }
        return false;
      };
  const auto hasBinding =
      [](const std::vector<ShaderResourceBinding> &bindings,
         const std::string &name) {
        for (const auto &binding : bindings) {
          if (binding.name == name)
            return true;
        }
        return false;
      };

  const auto unlitCompile = compileVariant({});
  const auto vertexColorCompile =
      compileVariant({{"USE_VERTEX_COLOR", true}, {"USE_LIGHTING", false}});
  const auto uvCompile =
      compileVariant({{"USE_UV", true}, {"USE_LIGHTING", false}});
  const auto lightingCompile = compileVariant({{"USE_LIGHTING", true}});
  const auto normalMapCompile =
      compileVariant({{"USE_UV", true},
                      {"USE_LIGHTING", true},
                      {"USE_NORMAL_MAP", true}});
  const auto skinnedCompile =
      compileVariant({{"USE_LIGHTING", true}, {"USE_SKINNING", true}});

  if (!unlitCompile.success || !vertexColorCompile.success || !uvCompile.success ||
      !lightingCompile.success || !normalMapCompile.success ||
      !skinnedCompile.success) {
    std::cerr << "  FAIL: compile failed for variant contract test\n";
    return false;
  }

  const auto unlitInputs = ShaderReflector::reflectVertexInputs(unlitCompile.stages);
  const auto vertexColorInputs =
      ShaderReflector::reflectVertexInputs(vertexColorCompile.stages);
  const auto uvInputs = ShaderReflector::reflectVertexInputs(uvCompile.stages);
  const auto lightingInputs =
      ShaderReflector::reflectVertexInputs(lightingCompile.stages);
  const auto normalMapInputs =
      ShaderReflector::reflectVertexInputs(normalMapCompile.stages);
  const auto skinnedInputs =
      ShaderReflector::reflectVertexInputs(skinnedCompile.stages);

  if (unlitInputs.size() != 1 ||
      !hasInput(unlitInputs, "inPosition", 0, DataType::Float3)) {
    std::cerr << "  FAIL: unlit variant should only require inPosition\n";
    return false;
  }
  if (vertexColorInputs.size() != 2 ||
      !hasInput(vertexColorInputs, "inColor", 6, DataType::Float4)) {
    std::cerr << "  FAIL: vertex-color variant should require inColor@6\n";
    return false;
  }
  if (uvInputs.size() != 2 ||
      !hasInput(uvInputs, "inUV", 2, DataType::Float2)) {
    std::cerr << "  FAIL: UV variant should require inUV@2\n";
    return false;
  }
  if (lightingInputs.size() != 2 ||
      !hasInput(lightingInputs, "inNormal", 1, DataType::Float3)) {
    std::cerr << "  FAIL: lighting variant should require inNormal@1\n";
    return false;
  }
  if (normalMapInputs.size() != 4 ||
      !hasInput(normalMapInputs, "inTangent", 3, DataType::Float4) ||
      !hasInput(normalMapInputs, "inUV", 2, DataType::Float2)) {
    std::cerr << "  FAIL: normal-map variant should require tangent and uv\n";
    return false;
  }
  if (skinnedInputs.size() != 4 ||
      !hasInput(skinnedInputs, "inBoneIDs", 4, DataType::Int4) ||
      !hasInput(skinnedInputs, "inBoneWeights", 5, DataType::Float4)) {
    std::cerr << "  FAIL: skinned variant should require bone inputs\n";
    return false;
  }

  const auto unlitBindings = ShaderReflector::reflect(unlitCompile.stages);
  const auto skinnedBindings = ShaderReflector::reflect(skinnedCompile.stages);
  if (hasBinding(unlitBindings, "Bones")) {
    std::cerr << "  FAIL: unskinned variant must not reflect Bones UBO\n";
    return false;
  }
  if (!hasBinding(skinnedBindings, "Bones")) {
    std::cerr << "  FAIL: skinned variant must reflect Bones UBO\n";
    return false;
  }

  std::cout << "  PASS: forward variants expose the expected contracts\n";
  return true;
}

static bool
testBlinnPhongFlatShadingVariant(const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: BlinnPhong flat shading variant\n";
  std::cout << "========================================\n";

  const auto vertPath = shaderDir / "blinnphong_0.vert";
  const auto fragPath = shaderDir / "blinnphong_0.frag";
  const std::vector<ShaderVariant> variants = {
      {"USE_LIGHTING", true},
      {"USE_FLAT_SHADING", true},
  };

  auto compileResult =
      ShaderCompiler::compileProgram(vertPath, fragPath, variants);
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  if (bindings.empty()) {
    std::cerr << "  FAIL: flat shading variant reflected no bindings\n";
    return false;
  }

  bool hasMaterialUbo = false;
  for (const auto &binding : bindings) {
    if (binding.name == "MaterialUBO") {
      hasMaterialUbo = true;
      break;
    }
  }
  if (!hasMaterialUbo) {
    std::cerr << "  FAIL: MaterialUBO binding missing\n";
    return false;
  }

  std::cout << "  PASS: flat shading variant compiles and reflects MaterialUBO\n";
  return true;
}

static std::string readTextFile(const std::filesystem::path &path) {
  std::ifstream ifs(path);
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  return buffer.str();
}

static bool testBlinnPhongPushConstantAbi(const std::filesystem::path &vertPath,
                                          const std::filesystem::path &fragPath) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: BlinnPhong push constant ABI\n";
  std::cout << "========================================\n";

  if (sizeof(PerDrawLayoutBase) != sizeof(Mat4f) ||
      sizeof(PerDrawLayout) != sizeof(Mat4f)) {
    std::cerr << "  FAIL: PerDrawLayoutBase/PerDrawLayout size mismatch with Mat4f\n";
    return false;
  }

  const auto vertSource = readTextFile(vertPath);
  const auto fragSource = readTextFile(fragPath);
  const auto hasModelOnlyBlock = [](const std::string &source) {
    return source.find("layout(push_constant) uniform ObjectPC") !=
               std::string::npos &&
           source.find("mat4 model;") != std::string::npos &&
           source.find("enableLighting") == std::string::npos &&
           source.find("enableSkinning") == std::string::npos;
  };

  if (!hasModelOnlyBlock(vertSource) || !hasModelOnlyBlock(fragSource)) {
    std::cerr << "  FAIL: push constant block is not model-only in shader\n";
    return false;
  }

  std::cout << "  PASS: per-draw ABI is model-only in C++ and GLSL\n";
  return true;
}

static bool testBlinnPhongRuntimeFallbacks(const std::filesystem::path &fragPath) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: BlinnPhong runtime fallbacks\n";
  std::cout << "========================================\n";

  const auto fragSource = readTextFile(fragPath);

  if (fragSource.find("if (material.enableAlbedo == 1)") ==
      std::string::npos) {
    std::cerr << "  FAIL: albedo sampling is no longer gated by enableAlbedo\n";
    return false;
  }
  if (fragSource.find("if (material.enableNormal == 1)") ==
      std::string::npos) {
    std::cerr << "  FAIL: normal-map sampling is no longer gated by enableNormal\n";
    return false;
  }
  if (fragSource.find("vec3 ambient = baseCol * 0.1;") ==
      std::string::npos) {
    std::cerr << "  FAIL: ambient fallback term missing from lit path\n";
    return false;
  }

  std::cout << "  PASS: runtime texture fallbacks and ambient term preserved\n";
  return true;
}

static bool testShadertoyQuantumCoreContract(
    const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: Shadertoy quantum core contract\n";
  std::cout << "========================================\n";

  const auto vertPath = shaderDir / "rtr_shadertoy_quantum_core.vert";
  const auto fragPath = shaderDir / "rtr_shadertoy_quantum_core.frag";
  auto compileResult = ShaderCompiler::compileProgram(vertPath, fragPath, {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto bindingIt =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "ShadertoyUBO";
      });
  if (bindingIt == bindings.end()) {
    std::cerr << "  FAIL: ShadertoyUBO binding missing\n";
    return false;
  }
  if (bindingIt->type != ShaderPropertyType::UniformBuffer) {
    std::cerr << "  FAIL: ShadertoyUBO should be a uniform buffer\n";
    return false;
  }

  const auto *time = findMember(*bindingIt, "time");
  const auto *resolution = findMember(*bindingIt, "resolution");
  const auto *audioBands = findMember(*bindingIt, "audioBands");
  if (!time || time->type != ShaderPropertyType::Float) {
    std::cerr << "  FAIL: ShadertoyUBO.time Float member missing\n";
    return false;
  }
  if (!resolution || resolution->type != ShaderPropertyType::Vec4) {
    std::cerr << "  FAIL: ShadertoyUBO.resolution Vec4 member missing\n";
    return false;
  }
  if (!audioBands || audioBands->type != ShaderPropertyType::Vec4) {
    std::cerr << "  FAIL: ShadertoyUBO.audioBands Vec4 member missing\n";
    return false;
  }
  const auto channelIt =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "iChannel0";
      });
  if (channelIt == bindings.end() ||
      channelIt->type != ShaderPropertyType::Texture2D) {
    std::cerr << "  FAIL: iChannel0 Texture2D binding missing\n";
    return false;
  }

  std::cout << "  PASS: Shadertoy quantum core shader reflects runtime params\n";
  return true;
}

static bool testMeshDebugShaderContract(
    const std::filesystem::path &shaderDir) {
  std::cout << "\n========================================\n";
  std::cout << "  Test: Mesh debug shader contract\n";
  std::cout << "========================================\n";

  const auto vertPath = shaderDir / "mesh_debug.vert";
  const auto fragPath = shaderDir / "mesh_debug.frag";
  auto compileResult = ShaderCompiler::compileProgram(vertPath, fragPath, {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto bindingIt =
      std::find_if(bindings.begin(), bindings.end(), [](const auto &binding) {
        return binding.name == "MeshOverlayUBO";
      });
  if (bindingIt == bindings.end()) {
    std::cerr << "  FAIL: MeshOverlayUBO binding missing\n";
    return false;
  }
  if (bindingIt->type != ShaderPropertyType::UniformBuffer) {
    std::cerr << "  FAIL: MeshOverlayUBO should be a uniform buffer\n";
    return false;
  }
  if (bindingIt->set != 1 || bindingIt->binding != 0) {
    std::cerr << "  FAIL: MeshOverlayUBO expected set=1 binding=0, got set="
              << bindingIt->set << " binding=" << bindingIt->binding << "\n";
    return false;
  }

  const auto *color = findMember(*bindingIt, "color");
  if (!color || color->type != ShaderPropertyType::Vec4) {
    std::cerr << "  FAIL: MeshOverlayUBO.color Vec4 member missing\n";
    return false;
  }

  std::cout << "  PASS: mesh debug shader reflects overlay color\n";
  return true;
}

static bool testPostProcessShaderContract(
    const std::filesystem::path &shaderDir) {
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
              << sceneColor->set << " binding=" << sceneColor->binding
              << "\n";
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

static bool testTextureCubeReflectionContract(
    const std::filesystem::path &shaderDir) {
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

static bool testPbrIblContract(const std::filesystem::path &vertPath,
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

  auto compileResult = ShaderCompiler::compileProgram(vertPath, fragPath, {});
  if (!compileResult.success) {
    std::cerr << "  COMPILE FAILED: " << compileResult.errorMessage << "\n";
    return false;
  }

  const auto bindings = ShaderReflector::reflect(compileResult.stages);
  const auto findBinding = [&](const std::string &name) {
    return std::find_if(bindings.begin(), bindings.end(),
                        [&](const auto &binding) {
                          return binding.name == name;
                        });
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
      shaderDir = std::filesystem::path(__FILE__).parent_path().parent_path()
                      .parent_path().parent_path() / "assets" / "shaders" / "glsl";
    }
  }

  auto vertPath = shaderDir / "pbr.vert";
  auto fragPath = shaderDir / "pbr.frag";

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
  if (!testVariantCombination(vertPath, fragPath, "PBR + Normal Map",
                              {{"HAS_NORMAL_MAP", true},
                               {"HAS_METALLIC_ROUGHNESS", false}}))
    ++failures;

  // Test 3: All variants enabled
  if (!testVariantCombination(vertPath, fragPath, "PBR + All Variants",
                              {{"HAS_NORMAL_MAP", true},
                               {"HAS_METALLIC_ROUGHNESS", true}}))
    ++failures;
  if (!testPbrIblContract(vertPath, fragPath))
    ++failures;

  // Test 4: BlinnPhong MaterialUBO member reflection (REQ-004)
  auto blinnVert = shaderDir / "blinnphong_0.vert";
  auto blinnFrag = shaderDir / "blinnphong_0.frag";
  if (std::filesystem::exists(blinnVert) && std::filesystem::exists(blinnFrag)) {
    if (!testBlinnPhongMaterialUboMembers(blinnVert, blinnFrag))
      ++failures;
    if (!testBlinnPhongVariantVertexInputs(blinnVert, blinnFrag))
      ++failures;
    if (!testBlinnPhongFlatShadingVariant(shaderDir))
      ++failures;
    if (!testBlinnPhongPushConstantAbi(blinnVert, blinnFrag))
      ++failures;
    if (!testBlinnPhongRuntimeFallbacks(blinnFrag))
      ++failures;
  } else {
    std::cerr << "  SKIP: blinnphong_0 shaders not found at " << shaderDir
              << "\n";
  }

  if (!testShadertoyQuantumCoreContract(shaderDir))
    ++failures;
  if (!testMeshDebugShaderContract(shaderDir))
    ++failures;
  if (!testPostProcessShaderContract(shaderDir))
    ++failures;
  if (!testTextureCubeReflectionContract(shaderDir))
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
