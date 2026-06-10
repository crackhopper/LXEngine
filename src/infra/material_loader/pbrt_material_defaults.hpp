#pragma once

#include "core/asset/material_parameter_envelope.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace LX_infra {

class PbrtMaterialDefaultTable final {
public:
  std::vector<std::string> diagnostics;

  void set(std::string bsdfType, std::string parameterName,
           LX_core::MaterialParameterEnvelope envelope);

  [[nodiscard]] std::optional<
      std::reference_wrapper<const LX_core::MaterialParameterEnvelope>>
  find(std::string_view bsdfType, std::string_view parameterName) const;

private:
  std::unordered_map<std::string, LX_core::MaterialParameterEnvelope> m_defaults;
};

[[nodiscard]] PbrtMaterialDefaultTable
loadPbrtMaterialDefaultsFromYaml(std::string_view yamlText);

} // namespace LX_infra
