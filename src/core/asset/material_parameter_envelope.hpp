#pragma once

#include "core/math/vec.hpp"
#include "core/platform/types.hpp"

#include <optional>
#include <string>

namespace LX_core {

enum class MaterialEnvelopeKind {
    Float,
    Rgb,
    Spectrum,
    Bool,
    String,
    Texture,
    Integer,
    MaterialRef,
    BsdfTable,
};

enum class MaterialEnvelopeValueType {
    None,
    Float,
    Rgb,
};

struct MaterialParameterEnvelope final {
    MaterialEnvelopeKind kind = MaterialEnvelopeKind::Float;
    MaterialEnvelopeValueType valueType = MaterialEnvelopeValueType::None;
    std::optional<float> floatValue;
    std::optional<i32> integerValue;
    std::optional<Vec3f> rgbValue;
    std::optional<bool> boolValue;
    std::optional<std::string> stringValue;
    std::optional<std::string> uri;
};

[[nodiscard]] bool hasInlineValue(const MaterialParameterEnvelope &envelope);
[[nodiscard]] bool hasResourceUri(const MaterialParameterEnvelope &envelope);
[[nodiscard]] std::string
validateEnvelopeShape(const MaterialParameterEnvelope &envelope);

} // namespace LX_core
