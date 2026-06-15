#pragma once

#include "core/utils/string_table.hpp"

namespace LX_core {

/*
@source_analysis.section PipelineKey：pipeline 身份的最终句柄
`PipelineKey` 故意只包一个结构化 `StringID`。073-c 之后它只接受两类已经
归约好的事实：

- MaterialTypeVariant：材质类型、source 契约和已解析 shader variant 身份。
- RenderPathNode：pass id、shader、render state、rendering mode、attachment
  contract、resource flow 和 geometry/topology 合约。

object/mesh 不再是 key 轴；它只参与 RenderPathNode geometry contract 的兼容性
校验。target 格式也不再作为独立 TargetRender 轴，而是由 RenderPathNode 的
attachment contract 包含。
*/
struct PipelineKey {
  StringID id;

  bool operator==(const PipelineKey &rhs) const { return id == rhs.id; }
  bool operator!=(const PipelineKey &rhs) const { return id != rhs.id; }

  struct Hash {
    usize operator()(const PipelineKey &k) const {
      return StringID::Hash{}(k.id);
    }
  };

  static PipelineKey build(StringID materialTypeVariant,
                           StringID renderPathNodeSignature);
};

} // namespace LX_core
