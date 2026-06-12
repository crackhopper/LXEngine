#pragma once

#include "core/asset/material_contract.hpp"
#include "core/platform/types.hpp"

#include <string>
#include <vector>

namespace LX_core {

struct MaterialContractDefaultTextureSlots final {
  u32 white = u32_max;
  u32 black = u32_max;
  u32 flatNormal = u32_max;
};

struct SourceLocalMaterialRecord final {
  StringID sourceSignature;
  u32 defaultWhiteTextureSlot = u32_max;
  u32 defaultBlackTextureSlot = u32_max;
  u32 defaultFlatNormalTextureSlot = u32_max;
  std::vector<u8> bytes;
};

struct MaterialContractPackInput final {
  MaterialContractReflection contract;
  MaterialContractDefaultTextureSlots defaultTextureSlots;
};

struct MaterialContractPackResult final {
  SourceLocalMaterialRecord record;
  std::vector<std::string> diagnostics;
};

[[nodiscard]] MaterialContractPackResult
packMaterialContractRecord(const MaterialContractPackInput &input);

} // namespace LX_core
