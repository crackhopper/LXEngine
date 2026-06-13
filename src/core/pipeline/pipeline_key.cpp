#include "core/pipeline/pipeline_key.hpp"

namespace LX_core {

PipelineKey PipelineKey::build(StringID materialTypeVariant,
                               StringID renderPathNodeSignature) {
  StringID fields[] = {materialTypeVariant, renderPathNodeSignature};
  return PipelineKey{
      GlobalStringTable::get().compose(TypeTag::PipelineKey, fields)};
}

} // namespace LX_core
