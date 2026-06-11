#pragma once

#include "core/asset/shader.hpp"
#include "core/asset/shader_binding_ownership.hpp"
#include "core/scene/scene_system_abi.hpp"

#include <cstddef>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace LX_core {

namespace detail {

inline std::string shaderPropertyTypeDebugString(ShaderPropertyType type) {
  switch (type) {
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

struct ExpectedSceneSystemAbiMember final {
  std::string_view name;
  ShaderPropertyType type;
  u32 offset;
  u32 size;
};

struct ExpectedSceneSystemAbiBinding final {
  std::string_view name;
  u32 set;
  u32 binding;
  ShaderPropertyType type;
  u32 size;
  const ExpectedSceneSystemAbiMember *members;
  usize memberCount;
};

inline constexpr ExpectedSceneSystemAbiMember kCameraMembers[] = {
    {"view", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemCameraData, view)), 16},
    {"projection", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemCameraData, projection)), 16},
    {"eye", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemCameraData, eye)), 16},
};

inline constexpr ExpectedSceneSystemAbiMember kLightMembers[] = {
    {"directionIntensity", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemLightData, directionIntensity)), 16},
    {"colorEnvironment", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemLightData, colorEnvironment)), 16},
};

inline constexpr ExpectedSceneSystemAbiMember kObjectMembers[] = {
    {"objectToWorld0", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemObjectData, objectToWorld0)), 16},
    {"objectToWorld1", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemObjectData, objectToWorld1)), 16},
    {"objectToWorld2", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemObjectData, objectToWorld2)), 16},
    {"objectToWorld3", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemObjectData, objectToWorld3)), 16},
    {"worldToObject0", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemObjectData, worldToObject0)), 16},
    {"worldToObject1", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemObjectData, worldToObject1)), 16},
    {"worldToObject2", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemObjectData, worldToObject2)), 16},
    {"worldToObject3", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemObjectData, worldToObject3)), 16},
};

inline constexpr ExpectedSceneSystemAbiMember kMaterialMembers[] = {
    {"baseColor", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemMaterialInstanceData, baseColor)),
     16},
    {"bsdfParams0", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemMaterialInstanceData, bsdfParams0)),
     16},
    {"bsdfParams1", ShaderPropertyType::Vec4,
     static_cast<u32>(offsetof(SceneSystemMaterialInstanceData, bsdfParams1)),
     16},
    {"textureIndices", ShaderPropertyType::Vec4,
     static_cast<u32>(
         offsetof(SceneSystemMaterialInstanceData, textureIndices)),
     16},
};

inline constexpr ExpectedSceneSystemAbiBinding kSceneSystemAbiBindings[] = {
    {"SceneCameraData",
     kSceneSystemDescriptorSet,
     kSceneSystemCameraBinding,
     ShaderPropertyType::StorageBuffer,
     sizeof(SceneSystemCameraData),
     kCameraMembers,
     std::size(kCameraMembers)},
    {"SceneLightData",
     kSceneSystemDescriptorSet,
     kSceneSystemLightBinding,
     ShaderPropertyType::StorageBuffer,
     sizeof(SceneSystemLightData),
     kLightMembers,
     std::size(kLightMembers)},
    {"SceneObjectData",
     kSceneSystemDescriptorSet,
     kSceneSystemObjectBinding,
     ShaderPropertyType::StorageBuffer,
     sizeof(SceneSystemObjectData),
     kObjectMembers,
     std::size(kObjectMembers)},
    {"SceneMaterialInstanceData",
     kSceneSystemDescriptorSet,
     kSceneSystemMaterialBinding,
     ShaderPropertyType::StorageBuffer,
     sizeof(SceneSystemMaterialInstanceData),
     kMaterialMembers,
     std::size(kMaterialMembers)},
};

inline const ExpectedSceneSystemAbiBinding *
findExpectedSceneSystemAbiBinding(std::string_view name) {
  for (const auto &expected : kSceneSystemAbiBindings) {
    if (expected.name == name)
      return &expected;
  }
  return nullptr;
}

inline const StructMemberInfo *
findReflectedMember(const ShaderResourceBinding &binding,
                    std::string_view name) {
  for (const auto &member : binding.members) {
    if (member.name == name)
      return &member;
  }
  return nullptr;
}

inline std::string expectedSceneSystemAbiDebugString(
    const ExpectedSceneSystemAbiBinding &expected) {
  std::ostringstream oss;
  oss << expected.name << " set=" << expected.set
      << " binding=" << expected.binding
      << " type=" << shaderPropertyTypeDebugString(expected.type)
      << " size=" << expected.size;
  return oss.str();
}

inline std::string
reflectedBindingDebugString(const ShaderResourceBinding &binding) {
  std::ostringstream oss;
  oss << binding.name << " set=" << binding.set
      << " binding=" << binding.binding
      << " type=" << shaderPropertyTypeDebugString(binding.type)
      << " size=" << binding.size;
  return oss.str();
}

} // namespace detail

inline std::optional<std::string>
validateSystemAbiBindingContract(const ShaderResourceBinding &binding) {
  const auto *expected =
      detail::findExpectedSceneSystemAbiBinding(binding.name);
  if (expected == nullptr)
    return std::nullopt;

  const auto fail = [&](const std::string &reason) {
    std::ostringstream oss;
    oss << "system ABI binding '" << binding.name << "' mismatch: " << reason
        << "; expected " << detail::expectedSceneSystemAbiDebugString(*expected)
        << "; reflected " << detail::reflectedBindingDebugString(binding);
    return std::optional<std::string>{oss.str()};
  };

  if (binding.type != expected->type) {
    return fail("descriptor type expected " +
                detail::shaderPropertyTypeDebugString(expected->type) +
                " but reflected " +
                detail::shaderPropertyTypeDebugString(binding.type));
  }
  if (binding.set != expected->set || binding.binding != expected->binding) {
    return fail("slot expected set=" + std::to_string(expected->set) +
                " binding=" + std::to_string(expected->binding) +
                " but reflected set=" + std::to_string(binding.set) +
                " binding=" + std::to_string(binding.binding));
  }
  if (binding.size != expected->size) {
    return fail("block size expected " + std::to_string(expected->size) +
                " but reflected size=" + std::to_string(binding.size));
  }

  for (usize i = 0; i < expected->memberCount; ++i) {
    const auto &expectedMember = expected->members[i];
    const auto *reflectedMember =
        detail::findReflectedMember(binding, expectedMember.name);
    if (reflectedMember == nullptr) {
      return fail("missing member '" + std::string(expectedMember.name) + "'");
    }
    if (reflectedMember->type != expectedMember.type) {
      return fail("member '" + std::string(expectedMember.name) +
                  "' type expected " +
                  detail::shaderPropertyTypeDebugString(expectedMember.type) +
                  " but reflected " +
                  detail::shaderPropertyTypeDebugString(reflectedMember->type));
    }
    if (reflectedMember->offset != expectedMember.offset) {
      return fail("member '" + std::string(expectedMember.name) +
                  "' offset=" + std::to_string(expectedMember.offset) +
                  " but reflected offset=" +
                  std::to_string(reflectedMember->offset));
    }
    if (reflectedMember->size != expectedMember.size) {
      return fail("member '" + std::string(expectedMember.name) +
                  "' size=" + std::to_string(expectedMember.size) +
                  " but reflected size=" +
                  std::to_string(reflectedMember->size));
    }
  }

  return std::nullopt;
}

} // namespace LX_core
