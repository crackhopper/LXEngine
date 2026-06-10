#pragma once

#include "core/asset/material_parameter_envelope.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace LX_core {

struct MaterialParameterSchema final {
    std::string name;
    std::vector<MaterialEnvelopeKind> allowedKinds;
    bool required = true;
};

struct MaterialSurfaceSchema final {
    std::string bsdfType;
    std::vector<MaterialParameterSchema> parameters;
};

[[nodiscard]] const MaterialSurfaceSchema *
findMaterialSurfaceSchema(std::string_view bsdfType);

} // namespace LX_core
