#pragma once

#include "core/utils/string_table.hpp"

namespace LX_core {

/*
@source_analysis.section PipelineKey：pipeline 身份的最终句柄
`PipelineKey` 故意只包一个结构化 `StringID`。它不保存 shader、render state、
vertex layout 的副本，而是要求调用方先把 object-side 和 material-side 的结构事实
各自归约成 signature，并在 RenderQueue 已知 render target 时把 target signature
一起传入这里做最后一次 compose。

当前 queue build 之后的完整形状是：

```text
PipelineKey(
  ObjectRender(mesh signature),
  MaterialRender(material pass signature),
  TargetRender(render target signature)
)
```

这样 cache lookup 的键很小，调试时又可以通过
`GlobalStringTable::toDebugString(key.id)` 展开整棵树。
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

  static PipelineKey build(StringID objectSig, StringID materialSig);

  /// 三级 compose：object signature + material signature + target signature。
  /// 调用方通过 `IRenderable::getPipelineSignature(pass)`、
  /// `MaterialInstance::getPipelineSignature(pass)` 与
  /// `RenderTargetDesc::getPipelineSignature()` 组装结构化签名，再传入本函数。
  static PipelineKey build(StringID objectSig, StringID materialSig,
                           StringID targetSig);
};

} // namespace LX_core
