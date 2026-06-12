#include "core/asset/material_contract_packer.hpp"

namespace LX_core {

namespace {

void requireDefaultTextureSlot(u32 slot, const char *name,
                               std::vector<std::string> &diagnostics) {
  if (slot == u32_max) {
    diagnostics.push_back(std::string("missing default texture slot: ") + name);
  }
}

} // namespace

MaterialContractPackResult
packMaterialContractRecord(const MaterialContractPackInput &input) {
  MaterialContractPackResult result;
  result.record.sourceSignature = input.contract.sourceSignature();
  result.record.defaultWhiteTextureSlot = input.defaultTextureSlots.white;
  result.record.defaultBlackTextureSlot = input.defaultTextureSlots.black;
  result.record.defaultFlatNormalTextureSlot =
      input.defaultTextureSlots.flatNormal;

  requireDefaultTextureSlot(input.defaultTextureSlots.white, "white",
                            result.diagnostics);
  requireDefaultTextureSlot(input.defaultTextureSlots.black, "black",
                            result.diagnostics);
  requireDefaultTextureSlot(input.defaultTextureSlots.flatNormal, "flatNormal",
                            result.diagnostics);
  return result;
}

} // namespace LX_core
