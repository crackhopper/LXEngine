#include "core/pipeline/pipeline_key.hpp"

namespace LX_core {

PipelineKey PipelineKey::build(StringID objectSig, StringID materialSig) {
  StringID fields[] = {objectSig, materialSig};
  return PipelineKey{
      GlobalStringTable::get().compose(TypeTag::PipelineKey, fields)};
}

PipelineKey PipelineKey::build(StringID objectSig, StringID materialSig,
                               StringID targetSig) {
  StringID targetFields[] = {targetSig};
  const StringID targetRender =
      GlobalStringTable::get().compose(TypeTag::TargetRender, targetFields);
  StringID fields[] = {objectSig, materialSig, targetRender};
  return PipelineKey{
      GlobalStringTable::get().compose(TypeTag::PipelineKey, fields)};
}

} // namespace LX_core
