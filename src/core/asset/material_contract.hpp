#pragma once

#include "core/math/vec.hpp"
#include "core/resource/resource_uri.hpp"
#include "core/utils/string_table.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core {

enum class MaterialContractSupportStatus {
  Supported,
  Unsupported,
};

enum class MaterialContractParameterKind {
  Float,
  Rgb,
  Spectrum,
  Texture,
  Integer,
  Bool,
  String,
  MaterialRef,
  BsdfTable,
};

struct MaterialContractParameter final {
  std::string name;
  bool required = false;
  std::vector<MaterialContractParameterKind> allowedKinds;
};

enum class MaterialContractStorageFieldType {
  Float,
  Vec4,
  TextureSlot,
  ChannelSelector,
  Flags,
};

enum class MaterialContractStorageInputKind {
  ParameterValue,
  ParameterTexture,
  ParameterChannel,
  Constant,
};

struct MaterialContractStorageField final {
  std::string name;
  MaterialContractStorageFieldType type =
      MaterialContractStorageFieldType::Float;
  MaterialContractStorageInputKind inputKind =
      MaterialContractStorageInputKind::ParameterValue;
  std::string parameterName;
  std::string defaultTextureSemantic;
  Vec4f defaultValue{0.0f, 0.0f, 0.0f, 0.0f};
  std::string defaultChannel = "rgba";
};

struct MaterialContractAccessorAbi final {
  std::string entryPoint = "lxLoadMaterialSurface";
  std::vector<std::string> requiredFields{
      "baseColor", "alpha", "metallic", "roughness", "normal", "ao",
      "emissive"};
};

struct MaterialContractReflection final {
  ResourceUri sourceUri;
  std::string declaredType;
  MaterialContractSupportStatus supportStatus =
      MaterialContractSupportStatus::Supported;
  std::string reflectionHash;
  std::string storageAbiHash;
  std::string accessorAbiHash;
  MaterialContractAccessorAbi accessorAbi;
  std::vector<MaterialContractParameter> parameters;
  std::vector<MaterialContractStorageField> storageFields;

  [[nodiscard]] std::optional<
      std::reference_wrapper<const MaterialContractParameter>>
  findParameter(std::string_view name) const;
  [[nodiscard]] StringID sourceSignature() const;
  [[nodiscard]] StringID materialSignature(StringID passShaderSignature,
                                           StringID renderStateSignature) const;
};

} // namespace LX_core
