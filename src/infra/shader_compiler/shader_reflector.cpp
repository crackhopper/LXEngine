#include "shader_reflector.hpp"
#include <spirv_cross.hpp>
#include <unordered_map>

namespace LX_infra {

static LX_core::ShaderPropertyType
mapSpvType(const spirv_cross::Compiler &compiler,
           const spirv_cross::Resource &res, spv::StorageClass storageClass) {
  auto &type = compiler.get_type(res.type_id);

  if (storageClass == spv::StorageClassUniform) {
    // Buffer type (struct) vs plain uniform
    if (type.basetype == spirv_cross::SPIRType::Struct) {
      auto bufferFlags =
          compiler.get_buffer_block_flags(res.id);
      // storage buffer has BufferBlock decoration in GLSL 450
      // For uniform blocks this returns the block decorations
      return LX_core::ShaderPropertyType::UniformBuffer;
    }
  }

  if (storageClass == spv::StorageClassStorageBuffer) {
    return LX_core::ShaderPropertyType::StorageBuffer;
  }

  // Sampled image (combined image sampler / texture)
  if (type.basetype == spirv_cross::SPIRType::SampledImage) {
    if (type.image.dim == spv::DimCube)
      return LX_core::ShaderPropertyType::TextureCube;
    return LX_core::ShaderPropertyType::Texture2D;
  }

  // Separate image
  if (type.basetype == spirv_cross::SPIRType::Image) {
    if (type.image.dim == spv::DimCube)
      return LX_core::ShaderPropertyType::TextureCube;
    return LX_core::ShaderPropertyType::Texture2D;
  }

  // Separate sampler
  if (type.basetype == spirv_cross::SPIRType::Sampler) {
    return LX_core::ShaderPropertyType::Sampler;
  }

  // Fallback for non-buffer uniforms
  return LX_core::ShaderPropertyType::Float;
}

static uint32_t computeBufferSize(const spirv_cross::Compiler &compiler,
                                  const spirv_cross::Resource &res) {
  auto &type = compiler.get_type(res.base_type_id);
  if (type.basetype == spirv_cross::SPIRType::Struct) {
    return static_cast<uint32_t>(compiler.get_declared_struct_size(type));
  }
  return 0;
}

// Key for merging: (set, binding)
struct SetBindingKey {
  uint32_t set;
  uint32_t binding;
  bool operator==(const SetBindingKey &rhs) const {
    return set == rhs.set && binding == rhs.binding;
  }
};

struct SetBindingHash {
  size_t operator()(const SetBindingKey &k) const {
    return std::hash<uint64_t>{}((uint64_t(k.set) << 32) | k.binding);
  }
};

std::vector<LX_core::ShaderResourceBinding>
ShaderReflector::reflectSingleStage(const LX_core::ShaderStageCode &stage) {
  std::vector<LX_core::ShaderResourceBinding> bindings;

  spirv_cross::Compiler compiler(stage.bytecode);
  auto resources = compiler.get_shader_resources();

  // Helper lambda to extract bindings from a resource list
  auto extractBindings =
      [&](const spirv_cross::SmallVector<spirv_cross::Resource> &resList,
          spv::StorageClass storageClass) {
        for (const auto &res : resList) {
          LX_core::ShaderResourceBinding b;
          b.name = res.name;
          b.set = compiler.get_decoration(res.id, spv::DecorationDescriptorSet);
          b.binding = compiler.get_decoration(res.id, spv::DecorationBinding);
          b.type = mapSpvType(compiler, res, storageClass);
          b.stageFlags = stage.stage;

          // Descriptor count (array)
          auto &type = compiler.get_type(res.type_id);
          if (!type.array.empty()) {
            b.descriptorCount = type.array[0];
          }

          // Buffer size
          b.size = computeBufferSize(compiler, res);

          bindings.push_back(std::move(b));
        }
      };

  extractBindings(resources.uniform_buffers, spv::StorageClassUniform);
  extractBindings(resources.storage_buffers, spv::StorageClassStorageBuffer);
  extractBindings(resources.sampled_images, spv::StorageClassUniformConstant);
  extractBindings(resources.separate_images, spv::StorageClassUniformConstant);
  extractBindings(resources.separate_samplers,
                  spv::StorageClassUniformConstant);

  return bindings;
}

std::vector<LX_core::ShaderResourceBinding>
ShaderReflector::reflect(const std::vector<LX_core::ShaderStageCode> &stages) {
  std::unordered_map<SetBindingKey, LX_core::ShaderResourceBinding,
                     SetBindingHash>
      merged;

  for (const auto &stage : stages) {
    auto stageBindings = reflectSingleStage(stage);
    for (auto &b : stageBindings) {
      SetBindingKey key{b.set, b.binding};
      auto it = merged.find(key);
      if (it != merged.end()) {
        // Merge stageFlags
        it->second.stageFlags = it->second.stageFlags | b.stageFlags;
      } else {
        merged.emplace(key, std::move(b));
      }
    }
  }

  std::vector<LX_core::ShaderResourceBinding> result;
  result.reserve(merged.size());
  for (auto &[_, b] : merged) {
    result.push_back(std::move(b));
  }

  // Sort by (set, binding) for deterministic output
  std::sort(result.begin(), result.end(),
            [](const LX_core::ShaderResourceBinding &a,
               const LX_core::ShaderResourceBinding &b) {
              if (a.set != b.set)
                return a.set < b.set;
              return a.binding < b.binding;
            });

  return result;
}

} // namespace LX_infra
