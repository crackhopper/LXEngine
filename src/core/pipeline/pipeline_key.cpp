#include "core/pipeline/pipeline_key.hpp"

namespace LX_core {

PipelineKey PipelineKey::build(StringID objectSig, StringID materialSig) {
  StringID fields[] = {objectSig, materialSig};
  return PipelineKey{
      GlobalStringTable::get().compose(TypeTag::PipelineKey, fields)};
}

} // namespace LX_core
