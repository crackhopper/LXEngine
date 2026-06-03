#include "core/scene/scene_gpu_records.hpp"

#include "core/asset/material_instance.hpp"

#include <optional>

namespace LX_core {
namespace {

template <usize BindingCount, usize MemberCount>
[[nodiscard]] std::optional<MaterialParameterValue> readFirstMaterialParameter(
    const MaterialInstance &material,
    const std::array<const char *, BindingCount> &bindingNames,
    const std::array<const char *, MemberCount> &memberNames) {
  for (const auto *bindingName : bindingNames) {
    const StringID bindingId(bindingName);
    for (const auto *memberName : memberNames) {
      auto value = material.readParameterValue(bindingId, StringID(memberName));
      if (value.has_value()) {
        return value;
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] Vec4f materialValueAsColor(const MaterialParameterValue &value,
                                         Vec4f fallback) {
  switch (value.type) {
  case MaterialParameterValueType::Vec3:
    return {value.vectorValue.x, value.vectorValue.y, value.vectorValue.z,
            fallback.w};
  case MaterialParameterValueType::Vec4:
    return value.vectorValue;
  case MaterialParameterValueType::Float:
    return {value.floatValue, value.floatValue, value.floatValue, fallback.w};
  case MaterialParameterValueType::Int:
    return {static_cast<f32>(value.intValue), static_cast<f32>(value.intValue),
            static_cast<f32>(value.intValue), fallback.w};
  }
  return fallback;
}

[[nodiscard]] f32 materialValueAsFloat(const MaterialParameterValue &value,
                                       const f32 fallback) {
  switch (value.type) {
  case MaterialParameterValueType::Float:
    return value.floatValue;
  case MaterialParameterValueType::Int:
    return static_cast<f32>(value.intValue);
  case MaterialParameterValueType::Vec3:
  case MaterialParameterValueType::Vec4:
    return value.vectorValue.x;
  }
  return fallback;
}

} // namespace

std::array<Vec4f, 4> toGpuRows(const Mat4f &matrix) {
  return {{
      {matrix(0, 0), matrix(0, 1), matrix(0, 2), matrix(0, 3)},
      {matrix(1, 0), matrix(1, 1), matrix(1, 2), matrix(1, 3)},
      {matrix(2, 0), matrix(2, 1), matrix(2, 2), matrix(2, 3)},
      {matrix(3, 0), matrix(3, 1), matrix(3, 2), matrix(3, 3)},
  }};
}

Vec3f toGpuBoundsMin(const BoundingBox &bounds) {
  if (!bounds.isValid()) {
    return {};
  }
  return bounds.min;
}

Vec3f toGpuBoundsMax(const BoundingBox &bounds) {
  if (!bounds.isValid()) {
    return {};
  }
  return bounds.max;
}

SceneGpuMaterialRecord toGpuMaterialRecord(const MaterialInstance &material) {
  SceneGpuMaterialRecord record;
  constexpr std::array kMaterialBindings{"MaterialUBO", "SurfaceParams"};

  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings,
          std::array{"baseColor", "baseColorFactor", "surfaceColor"})) {
    record.baseColor = materialValueAsColor(*value, record.baseColor);
  }

  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings,
          std::array{"metallicFactor", "metallic"})) {
    record.pbrParams.x = materialValueAsFloat(*value, record.pbrParams.x);
  }
  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings,
          std::array{"roughnessFactor", "roughness"})) {
    record.pbrParams.y = materialValueAsFloat(*value, record.pbrParams.y);
  }
  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings, std::array{"specularIntensity"})) {
    record.pbrParams.z = materialValueAsFloat(*value, record.pbrParams.z);
  }
  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings, std::array{"ambientIntensity", "ao"})) {
    record.pbrParams.w = materialValueAsFloat(*value, record.pbrParams.w);
  }

  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings,
          std::array{"emissive", "emissiveFactor"})) {
    record.emissive = materialValueAsColor(*value, record.emissive);
  }
  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings, std::array{"shininess"})) {
    record.emissive.w = materialValueAsFloat(*value, record.emissive.w);
  }

  return record;
}

} // namespace LX_core
