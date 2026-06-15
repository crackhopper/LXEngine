#include "core/asset/material_contract.hpp"

namespace LX_core {

std::optional<std::reference_wrapper<const MaterialContractParameter>>
MaterialContractReflection::findParameter(std::string_view name) const {
  for (const MaterialContractParameter &parameter : parameters) {
    if (parameter.name == name) {
      return std::cref(parameter);
    }
  }
  return std::nullopt;
}

StringID MaterialContractReflection::sourceSignature() const {
  StringID fields[] = {
      StringID(sourceUri.string()),
      StringID(declaredType),
      StringID(reflectionHash),
      StringID(storageAbiHash),
      StringID(accessorAbiHash),
  };
  return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
}

StringID MaterialContractReflection::materialSignature(
    StringID passShaderSignature, StringID renderStateSignature) const {
  StringID fields[] = {
      sourceSignature(),
      passShaderSignature,
      renderStateSignature,
  };
  return GlobalStringTable::get().compose(TypeTag::MaterialRender, fields);
}

} // namespace LX_core
