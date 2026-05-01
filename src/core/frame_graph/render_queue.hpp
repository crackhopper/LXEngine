#pragma once

#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/scene/scene.hpp"
#include <vector>

namespace LX_core {

/*
@source_analysis.section RenderQueue：一个 pass 内的 draw 列表与 pipeline 收口
RenderQueue 是 per-pass 的，不是全局的 — 每个 `FramePass` 自己持有一个。
之所以做成 per-pass，是因为同一个 renderable 在不同 pass 下产出的 RenderingItem
本就是不同的（不同 shader、不同 binding 集合、不同 pipelineKey），强行合并会让
"按 pipelineKey 聚合" 的语义在跨 pass 时丢失。

它在数据流里的位置只有两个：
- 写入侧：`buildFromScene` 在加载期 / 重建期把当前 scene × pass 翻译成 RenderingItem 列表
- 读出侧：backend 拿 `getItems()` 提交 draw，预构建拿 `collectUniquePipelineBuildDescs()` 去重

队列只对 RenderingItem 做了"按 pipelineKey 聚合 + 去重"两件事，不参与材质验证、
不参与 pass 是否支持的判定 — 那些在 `IRenderable::getValidatedPassData` /
`supportsPass` 这一层就已经收口完毕，进到队列里的都是 backend 可以直接消费的事实。
*/
class RenderQueue {
public:
  void addItem(RenderingItem item);
  void clearItems();

  void sort();

  const std::vector<RenderingItem> &getItems() const { return m_items; }

  std::vector<PipelineBuildDesc> collectUniquePipelineBuildDescs() const;

  void buildFromScene(const Scene &scene, StringID pass,
                      const RenderTarget &target);

private:
  std::vector<RenderingItem> m_items;
};

/*
@source_analysis.section sort：按 pipelineKey 稳定聚合
`RenderQueue::sort` 使用 `stable_sort` 而不是 `sort` 是有意的：当两个 RenderingItem
的 pipelineKey 相同时，它们在 `m_items` 里的相对顺序保留 `buildFromScene` 时
`scene.getRenderables()` 的入场顺序，让 backend 看到的同 pipeline draw 序列是确定的，
便于 frame-to-frame 对比和回放。

排序键只用 `pipelineKey.id.id` 这个底层整型 StringID，不做语义比较 — 目的是把相同 pipeline
的项排到一起，降低 pipeline 切换开销，而不是按"材质名字典序"这类业务语义排序。
*/

/*
@source_analysis.section collectUniquePipelineBuildDescs：预构建去重
这一步发生在加载期 / 重建期，不在 hot path：拿到去重后的 `PipelineBuildDesc` 列表
交给 backend 一次性构建 pipeline 缓存。去重粒度是 `PipelineKey` — 同一个 key 的多个
RenderingItem 共用同一条 pipeline，没必要重复构建。

注意这里的去重 *只在本队列内* 生效。跨 pass 的 PipelineKey 全局去重由
`FrameGraph::collectAllPipelineBuildDescs` 再做一遍 — 这是分层去重，避免单个 pass
的预构建在拿不到全局视角时反复做无意义的工作。
*/

/*
@source_analysis.section buildFromScene：REQ-009 两轴筛选与 scene-level 资源拼接
`RenderQueue::buildFromScene` 是 RenderQueue 唯一面向 Scene 的入口，也是把
"scene 视角" 翻译成 "pass 视角" 的收口点。三步固定流程：

1. 取 scene-level 资源：`scene.getSceneLevelResources(pass, target)` — 这一步本身
   就是 REQ-009 的两轴筛选（camera 按 target 匹配，light 按 pass 匹配）的产物。
2. 取该 target 上所有 camera 的 OR-combined 可见性掩码，作为本 pass 的可见性下界。
3. 遍历 `scene.getRenderables()`，对每个 renderable 串联三个独立条件：
   `supportsPass(pass)`、`getVisibilityLayerMask() & visibleMask != 0`、
   `getValidatedPassData(pass)` 返回非空。三者同时满足才入队。

入队前还要把 sceneResources 追加到 RenderingItem 的 `descriptorResources` 末尾 —
顺序是 "renderable 自带在前、scene-level 在后"，但 backend 按 binding name 命中，
不依赖位置；这里固定顺序只是为了让日志和断言可重现。

最后调用一次 `sort()`，把队列变成 backend 直接消费的形态。`buildFromScene` 自身
不做增量 — 每次调用都先 `clearItems()`，重建语义优先于增量正确性。
*/

} // namespace LX_core
