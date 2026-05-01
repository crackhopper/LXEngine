# Frame Graph 技术调研

> 状态：**LX 仅有最初阶骨架 / 完整 frame graph 未接入**
> 最后更新：2026-04-27
> 目的：记录"现代 frame graph"在业界的形态、LX 当前 `FrameGraph` / `FramePass` 的实际位置（远未到位）、以及把它推进到自动 DAG + barrier + aliasing 的渐进路径。
>
> 注：当前 LX 的 `FrameGraph` / `FramePass` / `RenderQueue` 实现的源码导读在
> `notes/source_analysis/src/core/frame_graph/`；本目录只讨论"业界完整形态 vs LX 现状的差距 + 怎么走"，不重复源码层细节。

## 这个目录是什么

LX 当前的 `FrameGraph` 名字与业界 *frame graph* 重叠，但它实际上只承担了：

- 持有 `vector<FramePass>`，按 `addPass` 顺序硬定执行序
- 在 `buildFromScene` 时按 pass 透传 `target` 给 `RenderQueue`
- 跨 pass 做一次全局 `PipelineKey` 去重

它 **不感知资源依赖、不构边、不做拓扑排序、不自动插 barrier、不做 attachment aliasing、不数据驱动**。换句话说，今天的 LX `FrameGraph` 大致只对应业界 frame graph 在 *parse 完但还没 compile* 的最早期状态。

本目录系统记录：

1. 完整 frame graph 在业界长什么样（数据驱动 + 资源边 + 自动调度）
2. LX 现状到完整形态之间还差哪些机制
3. 推进路径如何切分成可独立交付的 REQ

参考材料主要来自 *Mastering Graphics Programming with Vulkan* · Chapter 4。

## 建议阅读顺序

| # | 文档 | 讲什么 |
|---|------|-------|
| 01 | [frame graph 是什么](01-frame-graph-是什么.md) | 概念入门：DAG 模型、要解决的三类问题（依赖 / barrier / aliasing） |
| 02 | [数据驱动方式](02-数据驱动方式.md) | JSON 节点结构 + 四种资源类型（attachment / texture / buffer / reference）+ 为什么需要 reference |
| 03 | [实现层数据结构](03-实现层数据结构.md) | `FrameGraphResource` / `FrameGraphNode` 字段拆解 + Builder 模式 + 四步生命周期 |
| 04 | [compile 阶段](04-compile-阶段.md) | 构边 / 拓扑排序（DFS + 三态 visited）/ resource aliasing（ref_count + free_list） |
| 05 | [LX 当前状态对照](05-LX当前状态对照.md) | 字段级 / 能力级 gap 分析；REQ-009 target 轴的占位现状 |
| 06 | [演进路径](06-演进路径.md) | REQ-A..G 切分；风险点与可独立交付的中间里程碑 |

## TL;DR

- **完整 frame graph** = 数据驱动声明（JSON）+ 自动 DAG（构边 + 拓扑排序）+ 自动 backend 资源构造（renderpass / framebuffer）+ 自动 barrier 推导 + attachment aliasing
- **LX 现状**：只有 `vector<FramePass>` + 按调用顺序执行 + 跨 pass `PipelineKey` 去重；其它能力全缺
- **REQ-042 的位置**：把 `RenderTarget` 拆为 `RenderTargetDesc` + `RenderTarget`，是后续 frame graph 演进的 *字段层前置* —— attachment 形状要先变得可比、可哈希、能进 PipelineKey，才有资格作为资源边的载体
- **推进路径**：REQ-A（FrameGraphResource 数据结构）→ REQ-B（构边 + 拓扑 + cycle）→ REQ-C（自动 renderpass / framebuffer）→ REQ-D（barrier 自动推导，**风险点**）→ REQ-E（JSON parser）→ REQ-F（aliasing，**风险点**）→ REQ-G（runtime register_pass）
- **不紧迫**：当前 forward-only + 单 attachment 路径下，没有 frame graph 也能跑；真正需要它是在引入 deferred / multi-pass post-process / shadow + GBuffer 共用 attachment 之后

## 业界形态速查

| 维度 | LX 现状 | 教科书 frame graph | 工业级 frame graph (Frostbite / Unreal) |
|------|---------|--------------------|----------------------------------------|
| 定义方式 | 代码 `addPass` | JSON parse | 代码 DSL（C++ lambda）或资源驱动 |
| 资源依赖 | 无 | input/output 列表 + 名字解析 | 同左 + 类型系统 |
| pass 顺序 | `addPass` 调用序 | DAG 拓扑排序 | DAG 拓扑排序 + 异步队列调度 |
| renderpass / framebuffer | backend 手工 | compile 自动 | compile 自动 + subpass 合并 |
| barrier 插入 | backend ad-hoc | compile 推导 | compile 推导 + queue ownership |
| 资源 aliasing | 无 | greedy free_list | bin-packing / 约束求解器 |
| dead pass elimination | 无 | 可选 | 默认 |
| 多 queue (graphics / compute / transfer) | 无 | 通常无 | 有 |
| resize / live editing | 无 | 重 compile | 增量 invalidate |

LX 离教科书形态还有 6 个 REQ 的距离；离工业级再加几个量级。本调研的目标是教科书形态，工业级延伸只在风险段提及。

## 参考

- *Mastering Graphics Programming with Vulkan* · Chapter 4 · "Implementing a Frame Graph"
- LX 当前源码导读：`notes/source_analysis/src/core/frame_graph/`（`frame_graph.md` / `render_queue.md` / `render_target.md`）
- LX REQ-042 草稿：[`notes/requirements/042-render-target-desc-and-target.md`](../../../requirements/042-render-target-desc-and-target.md)
- Frostbite GDC 2017 talk: "FrameGraph: Extensible Rendering Architecture in Frostbite"
- Unreal Engine RDG（Render Dependency Graph）官方文档
