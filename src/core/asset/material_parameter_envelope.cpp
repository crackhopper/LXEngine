#include "core/asset/material_parameter_envelope.hpp"

namespace LX_core {
namespace {

[[nodiscard]] bool isMaterialUri(const std::string &uri) {
  return uri.ends_with(".material");
}

} // namespace

bool hasInlineValue(const MaterialParameterEnvelope &envelope) {
  return envelope.floatValue.has_value() || envelope.integerValue.has_value() ||
         envelope.rgbValue.has_value() || envelope.boolValue.has_value() ||
         envelope.stringValue.has_value();
}

bool hasResourceUri(const MaterialParameterEnvelope &envelope) {
  return envelope.uri.has_value() && !envelope.uri->empty();
}

std::string validateEnvelopeShape(const MaterialParameterEnvelope &envelope) {
  const bool inlineValue = hasInlineValue(envelope);
  const bool resourceUri = hasResourceUri(envelope);
  if (inlineValue && resourceUri) {
    return "material parameter envelope cannot specify both inline value and "
           "uri";
  }

  switch (envelope.kind) {
  case MaterialEnvelopeKind::Float:
    if (!envelope.floatValue.has_value()) {
      return "float envelope requires floatValue";
    }
    return {};
  case MaterialEnvelopeKind::Rgb:
    if (!envelope.rgbValue.has_value()) {
      return "rgb envelope requires rgbValue";
    }
    return {};
  case MaterialEnvelopeKind::Spectrum:
    if (!resourceUri && !envelope.rgbValue.has_value()) {
      return "spectrum envelope requires rgbValue or uri";
    }
    return {};
  case MaterialEnvelopeKind::Bool:
    if (!envelope.boolValue.has_value()) {
      return "bool envelope requires boolValue";
    }
    return {};
  case MaterialEnvelopeKind::String:
    if (!envelope.stringValue.has_value()) {
      return "string envelope requires stringValue";
    }
    return {};
  case MaterialEnvelopeKind::Texture:
    if (!resourceUri) {
      return "texture envelope requires uri";
    }
    if (envelope.valueType == MaterialEnvelopeValueType::None) {
      return "texture envelope requires sampled value type";
    }
    return {};
  case MaterialEnvelopeKind::Integer:
    if (!envelope.integerValue.has_value()) {
      return "integer envelope requires integerValue";
    }
    return {};
  case MaterialEnvelopeKind::MaterialRef:
    if (!resourceUri) {
      return "material reference envelope requires uri";
    }
    if (!isMaterialUri(*envelope.uri)) {
      return "material reference envelope requires .material uri";
    }
    return {};
  case MaterialEnvelopeKind::BsdfTable:
    if (!resourceUri) {
      return "BSDF table envelope requires uri";
    }
    return {};
  }

  return "unknown material parameter envelope kind";
}

} // namespace LX_core
