# RenderTarget：attachment 形状如何成为 REQ-009 的匹配键

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/frame_graph/render_target.hpp](../../../../../src/core/frame_graph/render_target.hpp)
出发，关注的不是"它有哪几个字段"，而是：为什么 `RenderTarget` 被刻意做成
一个不持有句柄、不参与 PipelineKey 的薄 POD，以及它怎么作为 REQ-009 两轴
筛选里的 *target 轴* 在 Scene、Camera、RenderQueue 之间穿过。

可以先带着一个问题阅读：既然 backend 最终要的是 attachment 句柄，为什么
`RenderTarget` 不直接持有句柄？答案是，句柄随 swapchain 重建抖动，而
"camera 匹配哪个 target" 是配置层的事实 — 把这两件事捏在一起会让 REQ-009
的匹配判断跟着 backend 状态一起抖。

源码入口：[render_target.hpp](../../../../src/core/frame_graph/render_target.hpp)

## RenderTarget：旧 target 轴的兼容外壳

`FramePass` 已经用 `RenderTargetDesc` 保存完整 target 形状，但当前 scene/camera
筛选接口仍接收 `RenderTarget`。因此 `RenderTarget` 现在是兼容外壳：旧的
`colorFormat` / `depthFormat` / `sampleCount` 字段仍是 present attachment 的
格式来源，额外的 presence bit、role 和 layerCount 只补足旧结构表达不了的语义。
这样既有代码直接写 `target.colorFormat` / `target.depthFormat` 时不会被 `toDesc`
忽略，offscreen/depth-only target 也不会被误还原成默认 swapchain target。

## operator==：按完整描述比较 target 轴

`RenderTarget::operator==` 被 `Camera::matchesTarget` 用作 target 轴判定。比较
必须走 `toDesc()`，否则从 `RenderTargetDesc` 降级到兼容类型时会丢失
offscreen/depth-only 的 role、attachment presence 和 layerCount 语义。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 文档先于代码的一次拆解

本页和大多数源码分析页不同 — 它不是回头总结一个已经成型的设计，而是作为
"代码不足、文档先动" 的一次拍板尝试的副产品。

要点：

- 当前 `RenderTarget` 是早期占位实现，不是设计成果
- target 轴在工程实际数据上几乎没有真实筛选 — 因为只有一种默认
  RenderTarget 在跑
- 真正的设计在 [REQ-042-a: FrameGraph v1 resource / target / pass execution](../../../../requirements/finished/042-a-frame-graph-v1-resource-target-pass-execution.md) 收口

读到这里如果想知道"为什么不直接改代码改成熟"，请先读 REQ-042-a — 它把
descriptor / binding 的拆分、字段表、interning 路径、Camera 绑定语义、跨子系统
影响都拍清楚了。本页的任务只是诚实记录现状。

## 现状 vs REQ-042-a 目标的对照

| 维度 | 当前 | REQ-042-a 后 |
|------|------|------------|
| 类型 | 单一 `RenderTarget`（角色不清） | `RenderTargetDesc` + `RenderTarget` 拆分 |
| color attachment | 单 format 字段 | `vector<ColorAttachmentDesc>`，v1 长度可 ≥ 1 |
| depth/stencil | 单 `depthFormat` | `optional<DepthStencilAttachmentDesc>`，含 packed / pure-depth / pure-stencil |
| sampleCount | 字段存在但未启用 | 字段保留，> 1 显式不支持，等 REQ-035+ |
| layer count | 没有 | `layerCount` 字段，v1 默认 1 |
| extent | 没有 | binding 层字段（不进 desc） |
| identity | 无专用 identity API | `getPipelineSignature() -> StringID`，进 PipelineKey 第三级 compose |
| GPU 资源 | 不持有 | binding 层持 `IGpuResourceSharedPtr` per attachment |
| `Camera::matchesTarget` | 比 `RenderTarget` 字段全匹配 | 比 `RenderTargetDesc`，nullopt 通配 |
| swapchain resize | 未定义 | v1 不支持 resize，REQ-035+ 解决 |

## target 轴的真相

当前实现有三处依赖 target 轴：

| 位置 | 目的 | 当前真实状态 |
|------|------|------------------|
| `Scene::getSceneLevelResources(pass, target)` | 拿 scene-level camera 资源 | 默认 target 全相等 → 永远命中 |
| `Scene::getCombinedCameraCullingMask(target)` | OR-combine 可见性掩码 | 同上，命中后掩码即所有 camera 的并集 |
| `RenderQueue::buildFromScene(scene, pass, target)` | 调度上面两个调用 | 直接透传 default-constructed `RenderTarget{}` |

也就是说 target 轴是预留 hook，等 REQ-042-a 让 RenderTargetDesc 长出真实差异后才有
非平凡的过滤行为。在那之前，调试这条路径时不要假设 target 轴正在做事。

## 跟 REQ-042-a 的衔接顺序

建议的实施顺序（不在本页范围，仅作导航）：

1. REQ-042-a R1：引入 `RenderTargetDesc`，原 `RenderTarget` 改名 + 拆字段
2. REQ-042-a R2..Rn：resource 声明、offscreen attachment、barrier、PipelineKey target identity、Camera/target 相关接口同步
3. 同步更新依赖文档，标出 REQ-042-a 对 target 轴、pipeline identity 和 pass execution 的影响
4. 本页同步重写 — 那时本页才会描述成型的设计，而非记录过渡期
