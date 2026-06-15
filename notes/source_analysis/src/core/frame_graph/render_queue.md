# RenderQueue：把 scene × pass 收口成可消费的 RenderWorkItem 列表

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/frame_graph/render_queue.hpp](../../../../../src/core/frame_graph/render_queue.hpp)
和它的实现
[src/core/frame_graph/render_queue.cpp](../../../../../src/core/frame_graph/render_queue.cpp)
出发，关注的不是"队列里有哪些 API"，而是 `RenderQueue` 在数据流里的位置：
它是 per-pass 的 RenderWorkItem 收口点，把 `Scene` 的扁平容器视角翻译成
backend 能直接消费的 draw / dispatch 列表。

可以先带着一个问题阅读：为什么 `RenderQueue` 不是一个全局队列？答案是，
同一个 renderable 在不同 pass 下的 RenderWorkItem 本就不同（不同 shader、
不同 binding、不同 pipelineKey），合并会让"按 pipelineKey 聚合"的语义崩塌；
这里只在单个 pass 内部完成 REQ-009 两轴筛选、稳定排序、和 pipeline 去重。

源码入口：[render_queue.hpp](../../../../src/core/frame_graph/render_queue.hpp)

关联源码：

- [render_queue.cpp](../../../../src/core/frame_graph/render_queue.cpp)

## render_queue.hpp

源码位置：[render_queue.hpp](../../../../src/core/frame_graph/render_queue.hpp)

### RenderWorkQueue：一个 pass 内的 draw 列表与 pipeline

收口 RenderWorkQueue 是 per-pass 的，不是全局的 — 每个 `FramePass`
自己持有一个。 之所以做成 per-pass，是因为同一个 renderable 在不同 pass 下产出的
RenderWorkItem 本就是不同的（不同 shader、不同 binding 集合、不同
pipelineKey），强行合并会让 "按 pipelineKey 聚合" 的语义在跨 pass 时丢失。

它在数据流里的位置只有两个：
- 写入侧：`build` 在加载期 / 重建期把当前 scene × pass 翻译成 RenderWorkItem
列表
- 读出侧：backend 拿 `getItems()` 提交 draw，预构建拿
`collectUniquePipelineBuildDescs()` 去重

队列只对 RenderWorkItem 做了"按 pipelineKey 聚合 + 去重"两件事，不参与材质验证、
不参与 pass 是否支持的判定 — 那些在 `IRenderable::getValidatedPassData` /
`supportsPass` 这一层就已经收口完毕，进到队列里的都是 backend
可以直接消费的事实。

### sort：按 pipelineKey 稳定聚合

`RenderWorkQueue::sort` 使用 `stable_sort` 而不是 `sort` 是有意的：当两个
RenderWorkItem 的 pipelineKey 相同时，它们在 `m_items` 里的相对顺序保留 `build`
时 `scene.getRenderables()` 的入场顺序，让 backend 看到的同 pipeline draw
序列是确定的， 便于 frame-to-frame 对比和回放。

排序键只用 `pipelineKey.id.id` 这个底层整型 StringID，不做语义比较 —
目的是把相同 pipeline 的项排到一起，降低 pipeline
切换开销，而不是按"材质名字典序"这类业务语义排序。

### collectUniquePipelineBuildDescs：预构建去重

这一步发生在加载期 / 重建期，不在 hot path：拿到去重后的 `PipelineBuildDesc`
列表 交给 backend 一次性构建 pipeline 缓存。去重粒度是 `PipelineKey` — 同一个
key 的多个 RenderWorkItem 共用同一条 pipeline，没必要重复构建。

注意这里的去重 *只在本队列内* 生效。跨 pass 的 PipelineKey 全局去重由
`FrameGraph::collectAllPipelineBuildDescs` 再做一遍 — 这是分层去重，避免单个
pass 的预构建在拿不到全局视角时反复做无意义的工作。

### build：REQ-009 两轴筛选与 scene-level 资源拼接

`RenderWorkQueue::build` 是 RenderWorkQueue 唯一面向 Scene 的入口，也是把
"scene 视角" 翻译成 "pass 视角" 的收口点。三步固定流程：

1. 取 scene-level 资源：`scene.getSceneLevelResources(pass, target)` —
这一步本身 就是 REQ-009 的两轴筛选（camera 按 target 匹配，light 按 pass
匹配）的产物。
2. 取该 target 上所有 camera 的 OR-combined 可见性掩码，作为本 pass
的可见性下界。
3. 遍历 `scene.getRenderables()`，对每个 renderable 串联三个独立条件：
   `supportsPass(pass)`、`getVisibilityLayerMask() & visibleMask != 0`、
   `getValidatedPassData(pass)` 返回非空。三者同时满足才入队。

入队前通过 scene descriptor resolver 构建 RenderWorkItem 的
`descriptorResources`：material binding、renderable-owned system binding
（例如 Bones）、scene-level resources 和 IBL resources 都在这里收口。backend
按 binding name 命中，不依赖位置；这里固定顺序只是为了让日志和断言可重现。

最后调用一次 `sort()`，把队列变成 backend 直接消费的形态。`build` 自身
不做增量 — 每次调用都先 `clearItems()`，重建语义优先于增量正确性。

## render_queue.cpp

源码位置：[render_queue.cpp](../../../../src/core/frame_graph/render_queue.cpp)

### makeItemFromValidatedData：把 validated 结构数据翻译成

RenderWorkItem 这是一个 anonymous-namespace
内的纯字段拷贝函数，存在的理由是把"翻译"这件事 和"过滤 + 入队"分开：`build`
只关心条件判断和 sceneResources 追加， 逐字段拷贝从这里走。

不做合并、不做校验，因为 `ValidatedRenderablePassData` 的命名已经承诺了
"pass-level validation 已经完成"。这里把 pass 内可验证的结构事实转成 backend
消费的 `RenderWorkItem`；target-dependent 的 `pipelineKey` 不在 validated
数据里缓存，而是在 `build` 拿到 target 后再组合。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 推荐阅读顺序

1. 先读 [Scene：场景容器与 scene-level 资源筛选](../scene/scene.md) 里的 `RenderWorkItem`
   一节 — 它解释了 backend 真正消费的契约长什么样、为什么 RenderWorkQueue 把它当作
   现成事实而不再重新组装。
2. 回到本页 `RenderWorkQueue：一个 pass 内的 draw 列表与 pipeline 收口` 一节，看清
   它在数据流里只占两个端点：写入侧的 `build` 和读出侧的
   `getItems / collectUniquePipelineBuildDescs`。
3. 看 `build` 一节 — 两轴筛选第一次出现是在 `Scene::getSceneLevelResources`
   做 camera × target 与 light × pass 的资源筛选；本页里它再一次出现，是用 camera 的
   OR-combined 可见性掩码反过来筛 renderable。两次筛选的"轴"不同，但属于同一条 scene resource 过滤路径。
4. 最后读 `sort` 与 `collectUniquePipelineBuildDescs` — 它们是同一组事实的两次利用：
   排序让相同 pipelineKey 的 draw 相邻，预构建去重让相同 pipelineKey 只被 backend 构建一次。

## 与 FrameGraph 的边界

RenderQueue 只负责 *单个 pass* 内的收口。跨 pass 的两件事都在 `FrameGraph` 上：

- `FrameGraph::build` 调度每个 `FramePass` 上的 `RenderWorkQueue::build`，
  把 pass × target 的二维参数注入进去。
- `FrameGraph::collectAllPipelineBuildDescs` 在所有队列输出的 `PipelineBuildDesc` 上
  再做一遍跨 pass 的全局 PipelineKey 去重。

这种"先 per-pass 去重，再 per-frame 去重"的分层，避免了让单个 RenderQueue 知道
其它 pass 的存在；每一层只在自己的视角内做最少的整理。

## 关于 sceneResources 的拼接顺序

`build` 把 scene-level resources 拼进 `descriptorResources`，看起来像
顺序契约，但实际上 backend 是按 binding name 匹配 — 和顺序无关。这里坚持
"renderable 自带在前、scene-level 在后" 只是为了让日志和断言中 RenderWorkItem
的 `descriptorResources` 字段输出可重现，便于 diff。如果未来 backend 改成按位置匹配，
这条隐含约束需要被显式化，否则会成为难以发现的耦合点。

## 当前 pipeline key 语义

REQ-073-c 之后，`RenderWorkItem::pipelineKey` 不再由 object/material/target 三轴组成。`RenderWorkQueue::build` 会在拿到 pass 的 `renderPathNodeSignature` 和 material-side `materialTypeVariant` 后调用：

```text
PipelineKey::build(materialTypeVariant, renderPathNodeSignature)
```

mesh/object 仍会通过 geometry contract 做兼容性校验；target/attachment 结构则包含在 RenderPathNode signature 中。
