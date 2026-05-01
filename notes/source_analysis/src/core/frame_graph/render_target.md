# RenderTarget：attachment 形状如何成为 REQ-009 的匹配键

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/frame_graph/render_target.hpp](../../../../../src/core/frame_graph/render_target.hpp)
和它的实现
[src/core/frame_graph/render_target.cpp](../../../../../src/core/frame_graph/render_target.cpp)
出发，关注的不是"它有哪几个字段"，而是：为什么 `RenderTarget` 被刻意做成
一个不持有句柄、不参与 PipelineKey 的薄 POD，以及它怎么作为 REQ-009 两轴
筛选里的 *target 轴* 在 Scene、Camera、RenderQueue 之间穿过。

可以先带着一个问题阅读：既然 backend 最终要的是 attachment 句柄，为什么
`RenderTarget` 不直接持有句柄？答案是，句柄随 swapchain 重建抖动，而
"camera 匹配哪个 target" 是配置层的事实 — 把这两件事捏在一起会让 REQ-009
的匹配判断跟着 backend 状态一起抖。

源码入口：[render_target.hpp](../../../../src/core/frame_graph/render_target.hpp)

关联源码：

- [render_target.cpp](../../../../src/core/frame_graph/render_target.cpp)

## render_target.hpp

源码位置：[render_target.hpp](../../../../src/core/frame_graph/render_target.hpp)

### RenderTarget：未完成的蓝图占位

当前 `RenderTarget` 是早期占位实现，不是设计成熟的类型。它持有三个字段
（colorFormat、depthFormat、sampleCount），既没区分 *descriptor*（结构性形状）
和 *binding*（实际 attachment 句柄），也不持有任何 GPU 资源 — 因为下游真正
消费它的代码路径还没写完。

之所以现在文档单独把它列出来，是因为虽然类型很薄，但 `Camera::matchesTarget`、
`Scene::getSceneLevelResources`、`RenderQueue::buildFromScene` 都已经在依赖它做
REQ-009 的"target 轴"筛选。也就是说：契约入口已经摆好，但契约本身还没发育完整。

详细的设计走向、字段缺口、与 PipelineKey 的接入方式由 REQ-034 收口，
正在用文档先于代码的方式拍板。本类型在 REQ-034 落地后会拆为
`RenderTargetDesc`（intern-friendly 形状，参与 PipelineKey 三级 compose）和
`RenderTarget`（持有 desc + IGpuResource 句柄 + extent）两个类型。

### operator==：当前 REQ-009 target 轴的事实层

`RenderTarget::operator==` 是 field-by-field 比较，被 `Camera::matchesTarget`
作为 REQ-009 两轴筛选 *target 轴* 的判定。

但要老实说：现状下整条 target 轴几乎是占位 hook —— 全工程实际只用到一种默认
构造的 RenderTarget，所有 pass 和 seed Camera 默认值相同，`matchesTarget`
永远返回 true，没有真实筛选发生。这不是设计成果，是因为 RenderTarget 还没长出
足够字段（MRT、layer、自定义 extent、load/store ops 都缺）来产生真实差异。

REQ-034 落地后，这个 `==` 会被 `RenderTargetDesc::operator==` 取代，进入真实
工作状态。届时字段扩展时同步更新 `==` 与 `getPipelineSignature` 是必须项。

## render_target.cpp

源码位置：[render_target.cpp](../../../../src/core/frame_graph/render_target.cpp)

### getHash：遗留代码，待删除

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

<!-- SOURCE_ANALYSIS:EXTRA -->

## 文档先于代码的一次拆解

本页和大多数源码分析页不同 — 它不是回头总结一个已经成型的设计，而是作为
"代码不足、文档先动" 的一次拍板尝试的副产品。

要点：

- 当前 `RenderTarget` 是早期占位实现，不是设计成果
- `getHash` 是没有生产调用方的遗留 API，应当走前置 cleanup 删除
- REQ-009 *target 轴* 在工程实际数据上几乎没有真实筛选 — 因为只有一种默认
  RenderTarget 在跑
- 真正的设计在 [REQ-034: RenderTarget 拆分为 descriptor 与 binding](../../../../requirements/034-render-target-desc-and-target.md) 收口

读到这里如果想知道"为什么不直接改代码改成熟"，请先读 REQ-034 — 它把
descriptor / binding 的拆分、字段表、interning 路径、Camera 绑定语义、跨子系统
影响都拍清楚了。本页的任务只是诚实记录现状。

## 现状 vs REQ-034 目标的对照

| 维度 | 当前 | REQ-034 后 |
|------|------|------------|
| 类型 | 单一 `RenderTarget`（角色不清） | `RenderTargetDesc` + `RenderTarget` 拆分 |
| color attachment | 单 format 字段 | `vector<ColorAttachmentDesc>`，v1 长度可 ≥ 1 |
| depth/stencil | 单 `depthFormat` | `optional<DepthStencilAttachmentDesc>`，含 packed / pure-depth / pure-stencil |
| sampleCount | 字段存在但未启用 | 字段保留，> 1 显式不支持，等 REQ-035+ |
| layer count | 没有 | `layerCount` 字段，v1 默认 1 |
| extent | 没有 | binding 层字段（不进 desc） |
| identity | `getHash`（runtime hash，未使用） | `getPipelineSignature() -> StringID`，进 PipelineKey 第三级 compose |
| GPU 资源 | 不持有 | binding 层持 `IGpuResourceSharedPtr` per attachment |
| `Camera::matchesTarget` | 比 `RenderTarget` 字段全匹配 | 比 `RenderTargetDesc`，nullopt 通配 |
| swapchain resize | 未定义 | v1 不支持 resize，REQ-035+ 解决 |

## REQ-009 路径上 target 轴的真相

REQ-009 在三处依赖 target 轴：

| 位置 | 目的 | 当前真实状态 |
|------|------|------------------|
| `Scene::getSceneLevelResources(pass, target)` | 拿 scene-level camera 资源 | 默认 target 全相等 → 永远命中 |
| `Scene::getCombinedCameraCullingMask(target)` | OR-combine 可见性掩码 | 同上，命中后掩码即所有 camera 的并集 |
| `RenderQueue::buildFromScene(scene, pass, target)` | 调度上面两个调用 | 直接透传 default-constructed `RenderTarget{}` |

也就是说 target 轴是预留 hook，等 REQ-034 让 RenderTargetDesc 长出真实差异后才有
非平凡的过滤行为。在那之前，调试这条路径时不要假设 target 轴正在做事。

## 跟 REQ-034 的衔接顺序

建议的实施顺序（不在本页范围，仅作导航）：

1. 前置 cleanup PR：删 `RenderTarget::getHash` + `testRenderTargetHashStability`
2. REQ-034 R1：引入 `RenderTargetDesc`，原 `RenderTarget` 改名 + 拆字段
3. REQ-034 R2..Rn：MRT、stencil 拆字段、layerCount、IGpuResource 接入、PipelineKey 三级 compose、Camera 改持 desc
4. 同步更新依赖 REQ：REQ-002 / REQ-007 / REQ-009 / REQ-026 banner 标 REQ-034 影响
5. 本页同步重写 — 那时本页才会描述成型的设计，而非记录过渡期
