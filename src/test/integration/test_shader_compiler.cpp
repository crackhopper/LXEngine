#include "core/utils/env.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  DescriptorSetIndex32 currentSet = u32_max;
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

  const ShaderResourceBinding *materialBinding = nullptr;
  for (const auto &b : bindings) {
    if (b.name == "MaterialUBO" ||
        (b.set == 2 && b.binding == 0 &&
         b.type == ShaderPropertyType::UniformBuffer)) {
      materialBinding = &b;
      break;
    }
  }
  if (!materialBinding) {
    std::cerr << "  FAIL: MaterialUBO binding not found\n";
    return false;
  }

  // 4.2 basic shape
  if (materialBinding->type != ShaderPropertyType::UniformBuffer) {
    std::cerr << "  FAIL: MaterialUBO is not UniformBuffer type\n";
    return false;
  }
  if (materialBinding->members.size() < 5) {
    std::cerr << "  FAIL: expected >= 5 members, got "
              << materialBinding->members.size() << "\n";
    return false;
  }
  std::cout << "  MaterialUBO has " << materialBinding->members.size()
            << " members\n";
  for (const auto &m : materialBinding->members) {
    std::cout << "    - " << m.name
              << "  type=" << shaderPropertyTypeName(m.type)
              << "  offset=" << m.offset << "  size=" << m.size << "\n";
  }

  // 4.3 baseColor: Vec3 at offset 0
  const auto *baseColor = findMember(*materialBinding, "baseColor");
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
  const auto *shininess = findMember(*materialBinding, "shininess");
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
  const auto *enableAlbedo = findMember(*materialBinding, "enableAlbedo");
  if (!enableAlbedo) {
    std::cerr << "  FAIL: enableAlbedo member missing\n";
    return false;
  }
  if (enableAlbedo->type != ShaderPropertyType::Int) {
    std::cerr << "  FAIL: enableAlbedo expected Int, got "
              << shaderPropertyTypeName(enableAlbedo->type) << "\n";
    return false;
  }

  // 4.6 non-UBO bindings have empty members (check sampler2D bindings)
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
         const std::string &name, VertexAttributeLocation32 location,
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

int main(int argc, char *argv[]) {
  expSetEnvVK();
  // Determine shader directory
  std::filesystem::path shaderDir;
  if (argc > 1) {
    shaderDir = argv[1];
  } else {
    // Default: relative to executable, try common locations
    shaderDir = std::filesystem::current_path() / "shaders" / "glsl";
    if (!std::filesystem::exists(shaderDir)) {
      // Try source tree location
      shaderDir = std::filesystem::path(__FILE__).parent_path().parent_path()
                      .parent_path().parent_path() / "shaders" / "glsl";
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

  // Test 4: BlinnPhong MaterialUBO member reflection (REQ-004)
  auto blinnVert = shaderDir / "blinnphong_0.vert";
  auto blinnFrag = shaderDir / "blinnphong_0.frag";
  if (std::filesystem::exists(blinnVert) && std::filesystem::exists(blinnFrag)) {
    if (!testBlinnPhongMaterialUboMembers(blinnVert, blinnFrag))
      ++failures;
    if (!testBlinnPhongVariantVertexInputs(blinnVert, blinnFrag))
      ++failures;
    if (!testBlinnPhongPushConstantAbi(blinnVert, blinnFrag))
      ++failures;
    if (!testBlinnPhongRuntimeFallbacks(blinnFrag))
      ++failures;
  } else {
    std::cerr << "  SKIP: blinnphong_0 shaders not found at " << shaderDir
              << "\n";
  }

  std::cout << "\n========================================\n";
  if (failures == 0) {
    std::cout << "All tests PASSED\n";
  } else {
    std::cout << failures << " test(s) FAILED\n";
  }
  std::cout << "========================================\n";

  return failures;
}
