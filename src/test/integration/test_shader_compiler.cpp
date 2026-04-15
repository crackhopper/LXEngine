#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
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
  auto f = static_cast<uint32_t>(flags);
  if (f & static_cast<uint32_t>(ShaderStage::Vertex))
    result += "VERTEX ";
  if (f & static_cast<uint32_t>(ShaderStage::Fragment))
    result += "FRAGMENT ";
  if (f & static_cast<uint32_t>(ShaderStage::Compute))
    result += "COMPUTE ";
  if (f & static_cast<uint32_t>(ShaderStage::Geometry))
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
  uint32_t currentSet = UINT32_MAX;
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
  std::cout << "  Reflection found " << bindings.size() << " bindings\n";

  // Create CompiledShader
  auto shader =
      std::make_shared<CompiledShader>(std::move(compileResult.stages), bindings);
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

int main(int argc, char *argv[]) {
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
