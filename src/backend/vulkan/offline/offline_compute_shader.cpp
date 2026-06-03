#include "offline_compute_shader.hpp"

#include "core/scene/scene_gpu_records.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "core/utils/hash.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace LX_core::backend::offline {
namespace {

[[nodiscard]] std::vector<char> loadComputeShader() {
  constexpr const char *shaderFile = "offline_primary_ray.comp.spv";
  std::string shaderPath;
  std::filesystem::path probe = std::filesystem::current_path();
  for (int i = 0; i < 8 && shaderPath.empty(); ++i) {
    const std::filesystem::path buildShaderPath =
        probe / "build" / "assets" / "shaders" / "glsl" / shaderFile;
    if (std::filesystem::exists(buildShaderPath)) {
      shaderPath = buildShaderPath.string();
      break;
    }
    const auto parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = parent;
  }
  if (shaderPath.empty()) {
    shaderPath = getShaderPath("offline_primary_ray", "comp.spv");
  }
  if (shaderPath.empty()) {
    throw std::runtime_error("failed to find offline compute shader SPIR-V");
  }
  return readFile(shaderPath);
}

[[nodiscard]] std::vector<u32> toSpirvWords(const std::vector<char> &code) {
  if (code.size() % sizeof(u32) != 0) {
    throw std::runtime_error("offline compute shader SPIR-V has invalid size");
  }
  std::vector<u32> words(code.size() / sizeof(u32));
  std::memcpy(words.data(), code.data(), code.size());
  return words;
}

[[nodiscard]] const ShaderResourceBinding &
findReflectedBinding(const std::vector<ShaderResourceBinding> &bindings,
                     const u32 set, const u32 binding) {
  const auto it = std::find_if(
      bindings.begin(), bindings.end(),
      [set, binding](const ShaderResourceBinding &reflected) {
        return reflected.set == set && reflected.binding == binding;
      });
  if (it == bindings.end()) {
    std::ostringstream msg;
    msg << "offline shader missing descriptor set " << set << " binding "
        << binding;
    throw std::runtime_error(msg.str());
  }
  return *it;
}

[[nodiscard]] std::vector<ShaderResourceBinding>
validateOfflineDescriptorContract(const std::vector<u32> &shaderCode) {
  struct ExpectedBinding {
    u32 binding = 0;
    const char *name = "";
    u32 blockSize = 0;
  };

  constexpr u32 kSet = 0;
  const std::array<ExpectedBinding, 9> expected{{
      {0, "SceneVertices", 0},
      {1, "SceneIndices", 0},
      {2, "SceneMeshes", 0},
      {3, "ScenePrimitives", 0},
      {4, "SceneObjects", 0},
      {5, "SceneMaterials", 0},
      {6, "SceneBvhNodes", 0},
      {7, "SceneFrameParams", static_cast<u32>(sizeof(SceneGpuFrameParams))},
      {8, "OutputPixels", 0},
  }};

  ShaderStageCode stageCode{};
  stageCode.stage = ShaderStage::Compute;
  stageCode.bytecode = shaderCode;
  const auto reflected = LX_infra::ShaderReflector::reflect({stageCode});
  if (reflected.size() != expected.size()) {
    std::ostringstream msg;
    msg << "offline shader descriptor count mismatch: expected "
        << expected.size() << ", reflected " << reflected.size();
    throw std::runtime_error(msg.str());
  }

  for (const ExpectedBinding &expectedBinding : expected) {
    const auto &binding =
        findReflectedBinding(reflected, kSet, expectedBinding.binding);
    if (binding.name != expectedBinding.name) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " name mismatch: expected " << expectedBinding.name
          << ", reflected " << binding.name;
      throw std::runtime_error(msg.str());
    }
    if (binding.type != ShaderPropertyType::StorageBuffer) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " must be a storage buffer";
      throw std::runtime_error(msg.str());
    }
    if (binding.descriptorCount != 1) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " descriptorCount mismatch";
      throw std::runtime_error(msg.str());
    }
    if ((static_cast<u32>(binding.stageFlags) &
         static_cast<u32>(ShaderStage::Compute)) == 0) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " is not visible to compute stage";
      throw std::runtime_error(msg.str());
    }
    if (expectedBinding.blockSize != 0 && binding.size != 0 &&
        binding.size != expectedBinding.blockSize) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " block size mismatch: expected " << expectedBinding.blockSize
          << ", reflected " << binding.size;
      throw std::runtime_error(msg.str());
    }
  }

  return reflected;
}

class OfflineComputeShader final : public IShader {
public:
  OfflineComputeShader() {
    ShaderStageCode stage{};
    stage.stage = ShaderStage::Compute;
    stage.bytecode = toSpirvWords(loadComputeShader());
    m_bindings = validateOfflineDescriptorContract(stage.bytecode);
    m_stages.push_back(std::move(stage));
  }

  const std::vector<ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    const auto it = std::find_if(
        m_bindings.begin(), m_bindings.end(),
        [set, binding](const ShaderResourceBinding &candidate) {
          return candidate.set == set && candidate.binding == binding;
        });
    if (it == m_bindings.end()) {
      return std::nullopt;
    }
    return std::cref(*it);
  }

  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    const auto it =
        std::find_if(m_bindings.begin(), m_bindings.end(),
                     [&name](const ShaderResourceBinding &candidate) {
                       return candidate.name == name;
                     });
    if (it == m_bindings.end()) {
      return std::nullopt;
    }
    return std::cref(*it);
  }

  usize getProgramHash() const override {
    usize hash = 0;
    for (const ShaderStageCode &stage : m_stages) {
      LX_core::hash_combine(hash, static_cast<u32>(stage.stage));
      LX_core::hash_combine(hash, stage.bytecode.size());
      for (const u32 word : stage.bytecode) {
        LX_core::hash_combine(hash, word);
      }
    }
    return hash;
  }

  std::string getShaderName() const override { return "offline_primary_ray"; }

private:
  std::vector<ShaderStageCode> m_stages;
  std::vector<ShaderResourceBinding> m_bindings;
};

} // namespace

LX_core::IShaderSharedPtr createOfflinePrimaryRayShader() {
  return std::make_shared<OfflineComputeShader>();
}

} // namespace LX_core::backend::offline
