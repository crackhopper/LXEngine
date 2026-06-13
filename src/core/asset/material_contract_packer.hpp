#pragma once

#include "core/asset/material_contract.hpp"
#include "core/platform/types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core {

class MaterialInstance;

struct MaterialContractDefaultTextureSlots final {
  u32 white = u32_max;
  u32 black = u32_max;
  u32 flatNormal = u32_max;
};

struct SourceLocalMaterialFieldLayout final {
  std::string name;
  MaterialContractStorageFieldType type =
      MaterialContractStorageFieldType::Float;
  u32 offset = 0;
  u32 size = 0;
};

struct SourceLocalMaterialRecordLayout final {
  std::vector<SourceLocalMaterialFieldLayout> fields;
  u32 byteSize = 0;

  [[nodiscard]] std::optional<
      std::reference_wrapper<const SourceLocalMaterialFieldLayout>>
  findField(std::string_view name) const;
};

struct SourceLocalMaterialRecord final {
  u32 sourceLocalMaterialIndex = u32_max;
  std::vector<u8> bytes;
};

struct MaterialContractPackInput final {
  const MaterialInstance *material = nullptr;
  MaterialContractReflection contract;
  MaterialContractDefaultTextureSlots defaultTextureSlots;
  u32 sourceLocalMaterialIndex = u32_max;
  std::function<u32(const ResourceUri &)> textureSlotForUri;
};

struct MaterialContractPackResult final {
  SourceLocalMaterialRecord record;
  SourceLocalMaterialRecordLayout layout;
  std::vector<std::string> diagnostics;
};

[[nodiscard]] MaterialContractPackResult
packMaterialContractRecord(const MaterialContractPackInput &input);

} // namespace LX_core
