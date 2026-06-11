#include "core/scene/scene_gpu_records.hpp"

#include "core/asset/material_instance.hpp"
#include "core/frame_graph/pass.hpp"

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

[[nodiscard]] std::optional<Vec4f>
materialEnvelopeAsColor(const MaterialParameterEnvelope &envelope,
                        const f32 alpha) {
  if (envelope.kind == MaterialEnvelopeKind::Rgb &&
      envelope.rgbValue.has_value()) {
    const Vec3f &rgb = *envelope.rgbValue;
    return Vec4f{rgb.x, rgb.y, rgb.z, alpha};
  }
  if (envelope.kind == MaterialEnvelopeKind::Spectrum &&
      envelope.rgbValue.has_value()) {
    const Vec3f &rgb = *envelope.rgbValue;
    return Vec4f{rgb.x, rgb.y, rgb.z, alpha};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<f32>
materialEnvelopeAsFloat(const MaterialParameterEnvelope &envelope) {
  if (envelope.kind == MaterialEnvelopeKind::Float &&
      envelope.floatValue.has_value()) {
    return *envelope.floatValue;
  }
  if (envelope.kind == MaterialEnvelopeKind::Integer &&
      envelope.integerValue.has_value()) {
    return static_cast<f32>(*envelope.integerValue);
  }
  return std::nullopt;
}

void applyEnvelopeColor(const MaterialInstance &material,
                        SceneGpuMaterialRecord &record, const char *name,
                        const f32 alpha) {
  const auto envelope = material.getMaterialEnvelope(StringID(name));
  if (!envelope.has_value()) {
    return;
  }
  if (const auto color = materialEnvelopeAsColor(envelope->get(), alpha)) {
    record.baseColor = *color;
  }
}

void applyEnvelopeRoughness(const MaterialInstance &material,
                            SceneGpuMaterialRecord &record) {
  const auto uEnvelope = material.getMaterialEnvelope(StringID("uroughness"));
  const auto vEnvelope = material.getMaterialEnvelope(StringID("vroughness"));
  const std::optional<f32> u =
      uEnvelope.has_value() ? materialEnvelopeAsFloat(uEnvelope->get())
                            : std::nullopt;
  const std::optional<f32> v =
      vEnvelope.has_value() ? materialEnvelopeAsFloat(vEnvelope->get())
                            : std::nullopt;
  if (u.has_value() && v.has_value()) {
    record.pbrParams.y = (*u + *v) * 0.5f;
  } else if (u.has_value()) {
    record.pbrParams.y = *u;
  } else if (v.has_value()) {
    record.pbrParams.y = *v;
  }
}

[[nodiscard]] bool applyMaterialV2EnvelopeRecord(
    const MaterialInstance &material, SceneGpuMaterialRecord &record) {
  if (material.getMaterialEnvelopeCount() == 0) {
    return false;
  }

  const std::string &bsdfType = material.getBsdfType();
  if (bsdfType == "matte") {
    applyEnvelopeColor(material, record, "Kd", 1.0f);
    record.pbrParams.x = 0.0f;
    record.pbrParams.y = 0.5f;
    record.pbrParams.z = 0.0f;
    record.pbrParams.w = 0.0f;
    return true;
  }

  if (bsdfType == "uber") {
    applyEnvelopeColor(material, record, "Kd", 1.0f);
    record.pbrParams.x = 0.0f;
    applyEnvelopeRoughness(material, record);
    record.pbrParams.w = 0.0f;
    return true;
  }

  if (bsdfType == "metal") {
    applyEnvelopeColor(material, record, "eta", 1.0f);
    record.pbrParams.x = 1.0f;
    applyEnvelopeRoughness(material, record);
    record.pbrParams.w = 0.0f;
    return true;
  }

  if (bsdfType == "substrate") {
    applyEnvelopeColor(material, record, "Kd", 1.0f);
    applyEnvelopeRoughness(material, record);
    record.pbrParams.x = 0.0f;
    record.pbrParams.w = 0.0f;
    return true;
  }

  applyEnvelopeColor(material, record, "Kd", 1.0f);
  return true;
}

[[nodiscard]] u32 materialCullModeAsGpuFlag(const CullMode mode) {
  switch (mode) {
  case CullMode::None:
    return kSceneGpuMaterialCullModeNone;
  case CullMode::Front:
    return kSceneGpuMaterialCullModeFront;
  case CullMode::Back:
    return kSceneGpuMaterialCullModeBack;
  }
  return kSceneGpuMaterialCullModeBack;
}

} // namespace

std::array<Vec4f, 4> toGpuColumns(const Mat4f &matrix) {
  return {{
      {matrix(0, 0), matrix(1, 0), matrix(2, 0), matrix(3, 0)},
      {matrix(0, 1), matrix(1, 1), matrix(2, 1), matrix(3, 1)},
      {matrix(0, 2), matrix(1, 2), matrix(2, 2), matrix(3, 2)},
      {matrix(0, 3), matrix(1, 3), matrix(2, 3), matrix(3, 3)},
  }};
}

Vec4f toGpuBoundsMin(const BoundingBox &bounds) {
  if (!bounds.isValid()) {
    return {};
  }
  return {bounds.min.x, bounds.min.y, bounds.min.z, 0.0f};
}

Vec4f toGpuBoundsMax(const BoundingBox &bounds) {
  if (!bounds.isValid()) {
    return {};
  }
  return {bounds.max.x, bounds.max.y, bounds.max.z, 0.0f};
}

SceneGpuMaterialRecord toGpuMaterialRecord(const MaterialInstance &material) {
  SceneGpuMaterialRecord record;
  constexpr std::array kMaterialBindings{"MaterialUBO", "SurfaceParams"};
  const auto renderState = material.getPassRenderState(Pass_OfflineRayTrace);
  record.flags = (record.flags & ~kSceneGpuMaterialCullModeMask) |
                 materialCullModeAsGpuFlag(renderState.cullMode);

  if (applyMaterialV2EnvelopeRecord(material, record)) {
    return record;
  }

  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings,
          std::array{"baseColor", "surfaceColor"})) {
    record.baseColor = materialValueAsColor(*value, record.baseColor);
  }

  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings, std::array{"specularIntensity"})) {
    record.pbrParams.z = materialValueAsFloat(*value, record.pbrParams.z);
  }
  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings, std::array{"ambientIntensity"})) {
    record.pbrParams.w = materialValueAsFloat(*value, record.pbrParams.w);
  }
  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings, std::array{"clearcoatFactor"})) {
    record.clearcoatParams.x =
        materialValueAsFloat(*value, record.clearcoatParams.x);
  }
  if (const auto value = readFirstMaterialParameter(
          material, kMaterialBindings, std::array{"clearcoatRoughness"})) {
    record.clearcoatParams.y =
        materialValueAsFloat(*value, record.clearcoatParams.y);
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
