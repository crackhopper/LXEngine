#include "shader_reflector.hpp"
#include <cassert>
#include <iostream>
#include <spirv_cross.hpp>
#include <unordered_map>

namespace LX_infra {

static LX_core::ShaderPropertyType
mapMemberType(const spirv_cross::SPIRType &type) {
  // Only handles flat numeric members: scalars, vectors, mat4.
  // Non-numeric / nested shapes fall through to Float as a conservative default;
  // callers should guard against nested structs before reaching this.
  using BT = spirv_cross::SPIRType::BaseType;
  if (type.basetype == BT::Int || type.basetype == BT::UInt) {
    return LX_core::ShaderPropertyType::Int;
  }
  if (type.basetype == BT::Float) {
    if (type.columns == 4 && type.vecsize == 4) {
      return LX_core::ShaderPropertyType::Mat4;
    }
    if (type.columns == 1) {
      switch (type.vecsize) {
      case 1:
        return LX_core::ShaderPropertyType::Float;
      case 2:
        return LX_core::ShaderPropertyType::Vec2;
      case 3:
        return LX_core::ShaderPropertyType::Vec3;
      case 4:
        return LX_core::ShaderPropertyType::Vec4;
      default:
        break;
      }
    }
  }
  return LX_core::ShaderPropertyType::Float;
}

static LX_core::DataType mapVertexInputType(const spirv_cross::SPIRType &type) {
  using BT = spirv_cross::SPIRType::BaseType;
  if ((type.basetype == BT::Int || type.basetype == BT::UInt) &&
      type.columns == 1 && type.vecsize == 4) {
    return LX_core::DataType::Int4;
  }
  if (type.basetype == BT::Float && type.columns == 1) {
    switch (type.vecsize) {
    case 1:
      return LX_core::DataType::Float1;
    case 2:
      return LX_core::DataType::Float2;
    case 3:
      return LX_core::DataType::Float3;
    case 4:
      return LX_core::DataType::Float4;
    default:
      break;
    }
  }
  return LX_core::DataType::Float1;
}

/// Walks the top-level members of a UBO struct and fills `out` with
/// std140 layout info. On unsupported shapes (nested struct, array-of-struct,
/// or any member with non-empty `array`), clears `out`, logs a warning, and
/// returns early.
static void extractStructMembers(const spirv_cross::Compiler &compiler,
                                 const spirv_cross::SPIRType &type,
                                 std::vector<LX_core::StructMemberInfo> &out) {
  using BT = spirv_cross::SPIRType::BaseType;
  const MemberCount count = type.member_types.size();
  out.clear();
  out.reserve(count);
  for (MemberCount i = 0; i < count; ++i) {
    const auto &memberType = compiler.get_type(type.member_types[i]);

    // Reject unsupported shapes: nested struct or any array member.
    if (memberType.basetype == BT::Struct || !memberType.array.empty()) {
      std::cerr << "[ShaderReflector] Unsupported UBO member shape at index "
                << i << " (nested struct or array); falling back to empty "
                << "members for this binding.\n";
      out.clear();
      return;
    }

    LX_core::StructMemberInfo info;
    info.name = compiler.get_member_name(type.self, i);
    if (info.name.empty()) {
      info.name = "_member" + std::to_string(i);
    }
    info.type = mapMemberType(memberType);
    info.offset = compiler.get_member_decoration(type.self, i,
                                                 spv::DecorationOffset);
    info.size = static_cast<ByteSize32>(
        compiler.get_declared_struct_member_size(type, i));
    out.push_back(std::move(info));
  }
}

static LX_core::ShaderPropertyType
mapSpvType(const spirv_cross::Compiler &compiler,
           const spirv_cross::Resource &res, spv::StorageClass storageClass) {
  auto &type = compiler.get_type(res.type_id);

  if (storageClass == spv::StorageClassUniform) {
    // Struct under Uniform storage class = UBO block; plain uniforms fall through.
    if (type.basetype == spirv_cross::SPIRType::Struct) {
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

static ByteSize32 computeBufferSize(const spirv_cross::Compiler &compiler,
                                    const spirv_cross::Resource &res) {
  auto &type = compiler.get_type(res.base_type_id);
  if (type.basetype == spirv_cross::SPIRType::Struct) {
    return static_cast<ByteSize32>(compiler.get_declared_struct_size(type));
  }
  return 0;
}

// Key for merging: (set, binding)
struct SetBindingKey {
  DescriptorSetIndex32 set;
  DescriptorBindingIndex32 binding;
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

          // UBO member layout (flat blocks only; others remain empty)
          if (b.type == LX_core::ShaderPropertyType::UniformBuffer) {
            const auto &blockType = compiler.get_type(res.base_type_id);
            if (blockType.basetype == spirv_cross::SPIRType::Struct) {
              extractStructMembers(compiler, blockType, b.members);
            }
          }

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

std::vector<LX_core::VertexInputAttribute>
ShaderReflector::reflectSingleStageInputs(const LX_core::ShaderStageCode &stage) {
  if (stage.stage != LX_core::ShaderStage::Vertex) {
    return {};
  }

  std::vector<LX_core::VertexInputAttribute> inputs;
  spirv_cross::Compiler compiler(stage.bytecode);
  auto resources = compiler.get_shader_resources();
  inputs.reserve(resources.stage_inputs.size());

  for (const auto &res : resources.stage_inputs) {
    LX_core::VertexInputAttribute attr;
    attr.name = res.name;
    attr.location = compiler.get_decoration(res.id, spv::DecorationLocation);
    attr.type = mapVertexInputType(compiler.get_type(res.type_id));
    inputs.push_back(std::move(attr));
  }

  std::sort(inputs.begin(), inputs.end(),
            [](const LX_core::VertexInputAttribute &a,
               const LX_core::VertexInputAttribute &b) {
              return a.location < b.location;
            });
  return inputs;
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
        // Preserve the first non-empty members vector; verify subsequent
        // stages agree structurally (same UBO must have identical layout).
        if (it->second.members.empty() && !b.members.empty()) {
          it->second.members = std::move(b.members);
        } else if (!it->second.members.empty() && !b.members.empty()) {
          assert(it->second.members.size() == b.members.size() &&
                 "ShaderReflector: UBO member count mismatch across stages");
          for (size_t i = 0; i < it->second.members.size(); ++i) {
            const auto &a = it->second.members[i];
            const auto &c = b.members[i];
            (void)a;
            (void)c;
            assert(a.name == c.name && a.type == c.type &&
                   a.offset == c.offset && a.size == c.size &&
                   "ShaderReflector: UBO member layout mismatch across stages");
          }
        }
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

std::vector<LX_core::VertexInputAttribute>
ShaderReflector::reflectVertexInputs(
    const std::vector<LX_core::ShaderStageCode> &stages) {
  for (const auto &stage : stages) {
    if (stage.stage == LX_core::ShaderStage::Vertex) {
      return reflectSingleStageInputs(stage);
    }
  }
  return {};
}

} // namespace LX_infra
