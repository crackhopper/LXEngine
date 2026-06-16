#include "core/pipeline/pipeline_key.hpp"

#include <algorithm>
#include <string>

namespace LX_core {
namespace {

[[nodiscard]] StringID specializationSignature(
    const std::vector<ShaderSpecializationConstant> &specializations) {
  if (specializations.empty()) {
    return {};
  }

  std::vector<ShaderSpecializationConstant> sorted = specializations;
  std::sort(sorted.begin(), sorted.end(),
            [](const ShaderSpecializationConstant &a,
               const ShaderSpecializationConstant &b) {
              const auto aStage = static_cast<u32>(a.stage);
              const auto bStage = static_cast<u32>(b.stage);
              if (aStage != bStage) {
                return aStage < bStage;
              }
              return a.constantId < b.constantId;
            });

  std::string signature = "specialization";
  for (const ShaderSpecializationConstant &constant : sorted) {
    signature += ";stage=" + std::to_string(static_cast<u32>(constant.stage));
    signature += ",id=" + std::to_string(constant.constantId);
    signature += ",type=" + std::to_string(static_cast<u32>(constant.type));
    signature += ",value=" + std::to_string(constant.valueU32);
  }
  return StringID(signature);
}

} // namespace

PipelineKey PipelineKey::build(StringID materialTypeVariant,
                               StringID renderPathNodeSignature) {
  StringID fields[] = {materialTypeVariant, renderPathNodeSignature};
  return PipelineKey{
      GlobalStringTable::get().compose(TypeTag::PipelineKey, fields)};
}

PipelineKey PipelineKey::build(
    StringID materialTypeVariant, StringID renderPathNodeSignature,
    const std::vector<ShaderSpecializationConstant> &specializations) {
  const StringID specialization = specializationSignature(specializations);
  if (specialization.id == 0) {
    return build(materialTypeVariant, renderPathNodeSignature);
  }

  StringID fields[] = {materialTypeVariant, renderPathNodeSignature,
                       specialization};
  return PipelineKey{
      GlobalStringTable::get().compose(TypeTag::PipelineKey, fields)};
}

} // namespace LX_core
