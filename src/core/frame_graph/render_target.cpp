#include "core/frame_graph/render_target.hpp"

#include "core/utils/hash.hpp"

namespace LX_core {

/*
@source_analysis.section getHash：遗留代码，待删除
`RenderTarget::getHash` 是早期阶段的遗留 API，目前 *没有任何生产代码调用*。
唯一消费者是 `src/test/integration/test_pipeline_build_info.cpp` 里的
`testRenderTargetHashStability`，而这个测试本身就只是测哈希自己，不测任何
依赖哈希的行为。

仓库的 identity / cache key 路径走的是 `StringID` + `GlobalStringTable::compose`
的 string interning（参见 REQ-006 / REQ-007），动态 hash 是绕路。REQ-034 会把
这套 interning 路径正式延伸到 RenderTargetDesc：
`RenderTargetDesc::getPipelineSignature() -> StringID`，参与 PipelineKey 第三级
compose。届时 `RenderTarget::getHash` 与对应自测一并删除。

在 REQ-034 实施前，本函数与对应自测可作为前置 cleanup PR 提前删 — 不依赖
REQ-034 的任何决策。
*/
usize RenderTarget::getHash() const {
  usize h = 0;
  hash_combine(h, static_cast<u32>(colorFormat));
  hash_combine(h, static_cast<u32>(depthFormat));
  hash_combine(h, static_cast<u32>(sampleCount));
  return h;
}

} // namespace LX_core
