#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_impl.hpp"
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

  // Create ShaderImpl
  auto shader =
      std::make_shared<ShaderImpl>(std::move(compileResult.stages), bindings);
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

  std::cout << "\n========================================\n";
  if (failures == 0) {
    std::cout << "All tests PASSED\n";
  } else {
    std::cout << failures << " test(s) FAILED\n";
  }
  std::cout << "========================================\n";

  return failures;
}
