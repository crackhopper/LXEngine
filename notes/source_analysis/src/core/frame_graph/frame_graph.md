# FrameGraph：把 scene 翻译成按 pass 组织的 RenderingItem 列表

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/frame_graph/frame_graph.hpp](../../../../../src/core/frame_graph/frame_graph.hpp)
和它的实现
[src/core/frame_graph/frame_graph.cpp](../../../../../src/core/frame_graph/frame_graph.cpp)
出发，关注的不是"FrameGraph 有哪些方法"，而是它在 frame_graph 子系统里的
位置：FramePass 把 (name, target, queue) 三件强绑定的字段打包成一条 pass 的
完整描述，FrameGraph 在加载期把每条 pass 上的 RenderQueue 各自填好，再做一次
跨 pass 的 PipelineKey 全局去重。

可以先带着一个问题阅读：为什么 core 层的 FrameGraph 不做 pass reorder、不做
attachment 复用、不做依赖分析？答案是这些都属于 backend 渲染图的职责；
core 层这层"frame graph"只承担"per-pass per-scene 预构建"的薄壳角色，
把语义留给 RenderQueue 和 backend。

源码入口：[frame_graph.hpp](../../../../src/core/frame_graph/frame_graph.hpp)

关联源码：

- [frame_graph.cpp](../../../../src/core/frame_graph/frame_graph.cpp)

## frame_graph.hpp

源码位置：[frame_graph.hpp](../../../../src/core/frame_graph/frame_graph.hpp)

### FramePass：三元组 (name, target, queue)

`FramePass` 把三件本来分散的事打包成一个结构体：

- `name`：StringID，匹配 REQ-007 的 `Pass_*` 常量；它是这条 pass 在 scene-level
  资源筛选、material pass 选择、shader 变体合并里的统一身份
- `target`：这条 pass 的输出形状。当前类型是 `RenderTarget`（占位实现，详见
  `render_target.md`），REQ-034 落地后会改为 `RenderTargetDesc`
- `queue`：这条 pass 内部的 RenderingItem 收口（见 `render_queue.md`）

之所以打包而不是让 `FrameGraph` 持有三个并行 vector，是因为这三个字段在每条
pass 上是强绑定的：`name` 决定 queue 怎么过滤，`target` 决定 queue 怎么注入
scene-level 资源；分开存就要在 `FrameGraph` 里维护"i-th name 对应 i-th target"
的隐式索引，容易写出 off-by-one。

注意 FramePass 不持有任何 backend 资源（renderpass / framebuffer / pipeline
都在 backend 侧）— 它纯粹是 core 层的"这条 pass 怎么从 scene 里挑出 draw
列表"的描述符。

### FrameGraph：加载期预构建的 per-pass 调度器

`FrameGraph` 是把 scene 翻译成"按 pass 组织的 RenderingItem 列表"的入口。
它本身只做两件事：

- 持有 `vector<FramePass>`：通过 `addPass` 累加，顺序即提交顺序
- 在 `buildFromScene` 时按 pass 顺序逐个调用 `RenderQueue::buildFromScene`，
  把 `pass.target` 透传下去（REQ-009 target 轴的入口）

注意它 *不* 做 pass 间依赖分析、不做 pass reorder、不做 attachment 复用 —
这些都是 backend 渲染图（render graph）的职责。core 层的 FrameGraph 只是
"per-pass per-scene 预构建"的薄壳：决定哪些 RenderingItem 进哪条 queue。

跨 pass 唯一的协调动作是 `collectAllPipelineBuildDescs`：在所有 queue 输出
的 PipelineBuildDesc 上做一次全局 PipelineKey 去重，避免相同 pipeline 在
不同 pass 里被 backend 重复构建。这是分层去重的外层 — 内层（同 pass 内）
由 `RenderQueue::collectUniquePipelineBuildDescs` 完成。

### buildFromScene：把 pass × scene 二维问题摊成一维循环

`FrameGraph::buildFromScene` 的实现核心是一行循环：每条 pass 上调用
`pass.queue.buildFromScene(scene, pass.name, pass.target)`，把"哪条 pass、
画到哪种 target"两个参数从 FramePass 解包后透传给 RenderQueue。

这种"FrameGraph 不做语义、只做调度"的写法把"pass × scene"二维问题摊成
一维循环。每一条 pass 的 RenderQueue 内部独立完成 REQ-009 两轴筛选，
FrameGraph 只负责保证 *每条 pass 都被处理一次* 这一条简单不变量。

调用语义上这是重建而非增量：每次 `buildFromScene` 都触发每个 queue 的
`clearItems()` + 重新填入，符合 RenderQueue 自身的"重建语义优先于增量
正确性"约定。

### collectAllPipelineBuildDescs：跨 pass 全局 PipelineKey 去重

这是 FrameGraph 唯一面向 backend 的输出：把所有 pass 的预构建需求汇总成
一份去重后的 `PipelineBuildDesc` 列表。

去重粒度是 `PipelineKey`，与 RenderQueue 内层去重一致。两层去重的关系：

- RenderQueue 内层：去掉同一条 queue 内重复的 RenderingItem（同材质同几何的
  多个实例只产出一条 build desc）
- FrameGraph 外层：去掉跨 pass 出现的相同 PipelineKey（理论上罕见，但当材质
  支持多 pass 且某两 pass 在 PipelineKey 上恰好碰撞时会出现）

之所以分两层而不是一次性全局去重，是因为 RenderQueue 不知道其它 pass 存在 —
让它去做全局判定会破坏单 pass 收口的封装。两层各自只看自己的视角，
FrameGraph 这一层只看到"队列已去重的输出"再做最少整理。

REQ-034 R5 落地后，`PipelineKey` 变三级 compose（含 targetSig）；本函数代码
不需要变，但跨 pass 重复 PipelineKey 的概率会进一步降低 — 不同 target
的 pass 在 PipelineKey 上自动不重叠。

<!-- SOURCE_ANALYSIS:EXTRA -->

## frame_graph 子目录的三层视角

frame_graph 子目录里的三个源码分析页对应三个抽象层，从底向上读最顺：

| 层 | 页 | 关注点 |
|---|---|---|
| 形状 | [RenderTarget](render_target.md) | 一条 pass 的输出形状（attachment 配置） |
| 单 pass 收口 | [RenderQueue](render_queue.md) | 一条 pass 内的 RenderingItem 过滤、排序、pipeline 去重 |
| 多 pass 调度 | 本页 FrameGraph | 把多条 pass 串起来，跨 pass 全局 PipelineKey 去重 |

读完三页就能回答："一帧渲染的 RenderingItem 列表是怎么从 Scene 里挤出来的、
按什么粒度去重、谁负责把它们交给 backend"这类问题。

## 与 backend 渲染图的边界

业界常说的"render graph / frame graph"通常指 backend 层的概念：跟踪 attachment
依赖、自动 alias 资源、reorder pass、做 barrier 推导。本仓库里这层叫
`FrameGraph`，但它 *不* 干这些事 — 它是 core 层的"per-pass per-scene 预构建
调度器"，不感知 GPU memory、不感知 pipeline barrier。

对应职责切分：

- **core/FrameGraph**：决定每条 pass 的 RenderQueue 内容（哪些 RenderingItem
  入队 / 哪些 pipeline 需要预构建）
- **backend (e.g. VulkanRenderer)**：决定怎么在 GPU 上提交（renderpass 创建、
  framebuffer 绑定、pipeline 构建、命令录制、barrier 插入）

这种切分让 core 层完全不依赖 Vulkan / D3D / Metal 的渲染图模型，但代价是
core 层 FrameGraph 的名字与业界惯例略有偏差 — 读者第一次看到容易期待
依赖分析 / 自动 alias，需要主动调整预期。

## REQ-034 落地后会变什么

[`REQ-034`](../../../../requirements/034-render-target-desc-and-target.md) 对
本页的影响主要落在 `FramePass::target` 字段类型与下游签名透传：

- **`FramePass::target` 字段类型**：从 `RenderTarget` 改为 `RenderTargetDesc`。
  FramePass 表达的是"这条 pass 要画到哪种形状的 target"，是 desc（形状）层面
  的事实，不是 binding（具体 attachment 句柄）层面的事实。
- **`buildFromScene` 透传链**：`pass.queue.buildFromScene(scene, pass.name,
  pass.target)` 中第三个参数从 `RenderTarget` 改为 `RenderTargetDesc`，与
  RenderQueue / Scene 的下游接口签名同步。
- **`collectAllPipelineBuildDescs` 行为不变但语义增强**：本函数代码本身
  不需要修改；但 REQ-034 R5 让 `PipelineKey` 包含 `targetSig` 后，跨 pass
  重复 PipelineKey 的概率会进一步降低 — 不同 target 的 pass 在 PipelineKey
  上自动不重叠，外层去重命中率下降，但这是显式化"pipeline 与 attachment
  format 强绑"的硬约束，不是缺陷。
- **binding 层 attachment 的归属**：`FramePass` 仍然不持有具体 attachment
  句柄；REQ-034 把 `RenderTarget`（binding 层）的所有权放到 backend / 调用
  方手里，`FramePass` 只通过 desc 描述"要画进什么形状"。

也就是说，REQ-034 让 FramePass 的字段类型变更，但 FrameGraph 的调度结构
和职责切分都保持不变。
