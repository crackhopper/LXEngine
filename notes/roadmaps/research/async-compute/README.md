# Async Compute 技术调研

> 状态：**LX 完全没有 compute 路径 / 待 Phase 5 触发立项**
> 最后更新：2026-04-27
> 目的：记录 async compute 在业界的形态、LX 当前 compute 能力的空白盘点、以及与 multi-threading / frame-graph 两套已有调研的耦合点；给出按 REQ 切分的渐进推进路径。
>
> 注：timeline semaphore 的设计模型不在本调研范围 —— 已由
> [`multi-threading/08-Timeline与资源退休模型.md`](../multi-threading/08-Timeline与资源退休模型.md) 完整收口；本调研只补 *async compute 自身* 的设计空间。

## 这个目录是什么

LX 当前在"GPU compute"维度上 **完全空白**：

- 只查询 graphics queue，没有 compute queue handle
- 没有 `ComputePipeline` 路径
- shader 加载只走 vertex + fragment stage
- `RenderingItem` 是 draw call 中心，没有 dispatch 抽象
- `FramePass` 没有"队列亲和性"概念
- 跨 queue 资源 ownership transfer 没碰过

但 LX 在 *同步原语* 层已经做对了：[`multi-threading/08`](../multi-threading/08-Timeline与资源退休模型.md) 已经设计了 `IGpuTimeline` 接口并预留 `computeTimeline` 字段，async compute 落地时可以直接消费这套设计，不需要重新发明同步模型。

本目录系统记录：

1. async compute 在业界的形态（多 queue 物理 / 逻辑分离、ownership transfer、SIMT 执行模型）
2. LX 现状到完整形态之间的 gap
3. 与 [multi-threading 调研](../multi-threading/README.md) 和 [frame-graph 调研](../frame-graph/README.md) 的耦合关系
4. 推进路径如何切分成可独立交付的 REQ

参考材料主要来自 *Mastering Graphics Programming with Vulkan* · Chapter 5。

## 建议阅读顺序

| # | 文档 | 讲什么 |
|---|------|-------|
| 01 | [async compute 是什么](01-async-compute-是什么.md) | GPU 利用率模型、为什么需要、什么 workload 适合 |
| 02 | [多 queue 机制](02-多queue机制.md) | Vulkan queue family / submit / 跨 queue 同步（含与 multi-threading/08 的引用边界） |
| 03 | [compute shader 基础](03-compute-shader基础.md) | SIMT / local-global group size / dispatch / SoA-AoS / GLSL 陷阱 |
| 04 | [LX 当前状态对照](04-LX当前状态对照.md) | 字段级 / 能力级 gap；已有可对接资产 |
| 05 | [与 frame graph 的耦合](05-与frame-graph的耦合.md) | 跨 queue barrier / 队列亲和性 / 拓扑排序扩展；为什么必须等 REQ-038 |
| 06 | [演进路径](06-演进路径.md) | 候选 REQ-A..F 切分 + 触发条件 + 风险点 |

## TL;DR

- **async compute** = 让 compute 工作和 graphics 工作 *物理上并行* 跑在 GPU 上不同硬件引擎上（独立 compute queue），消化 graphics-only 渲染没用满的 compute units
- **LX 现状**：compute 路径完全空白；但同步原语层的 timeline 模型已经在 multi-threading/08 设计好，可以直接消费
- **推进路径**：REQ-A（compute queue + pipeline 基础设施）→ REQ-B（queue ownership transfer）→ REQ-C（FramePass 队列亲和性）→ REQ-D（跨 queue barrier 推导，**风险点 #1**，依赖 frame-graph REQ-038）→ REQ-E（实战 demo）→ REQ-F（硬件 fallback）
- **不紧迫**：当前 LX 渲染管线没有 compute 工作，没有强触发条件。真正需要它是在 GPU 物理 / GPU culling / compute-based post-process 立项时
- **跟 multi-threading 不重复**：timeline / RetirePoint / 资源退休模型已在 multi-threading/08 设计完毕，本调研直接引用，不重写

## 业界形态速查

| 维度 | LX 现状 | 教科书 / async compute 需要 |
|------|---------|----------------------------|
| 队列 | 仅 graphics queue handle | 至少 graphics + compute（独立 family 优先）+ 可选 transfer |
| Pipeline 类型 | 仅 graphics pipeline | + compute pipeline |
| Shader stage 加载 | vertex + fragment | + compute |
| 命令录制 | `bind_pipeline / draw*` | + `bind_pipeline(compute) / dispatch` |
| 工作单元抽象 | `RenderingItem`（draw 中心） | + dispatch item，或 RenderingItem 加 dispatch 字段 |
| FramePass 队列亲和性 | 默认 graphics | + queue type 字段 |
| 跨 queue barrier | 不需要（单 queue） | 必须（ownership transfer） |
| Storage buffer 角色 | 不单独建模 | 单独 ResourceType + descriptor type |
| 同步原语模型 | `IGpuTimeline` 已设计（multi-threading/08）但未接入 | 需要把 backend 实现接入 + 添加 computeTimeline |
| 硬件 fallback | 无 | 必须（移动 / 集显 / 老硬件） |

## 与其它两个调研的关系

```
multi-threading 调研          frame-graph 调研          async-compute 调研
──────────────                ────────────              ──────────────────
08-Timeline 模型              06-演进路径                本调研
   提供                          REQ-038 barrier            消费两者
   IGpuTimeline /               ↑                         + 自己的：
   RetirePoint                  必须先完成                  - 多 queue
   ↓                            才能做 REQ-D               - ownership transfer
   被本调研                                                 - compute pipeline
   直接消费                                                 - dispatch item
```

- **multi-threading/08** 已经把 timeline + retire 模型 + computeTimeline 字段都设计好。本调研的 REQ-A 只需要把 backend 实现接进去
- **frame-graph 06** 的候选 REQ-038（barrier 自动推导）是本调研 REQ-D（跨 queue 自动 barrier）的硬前置 —— async compute 要 *等 frame graph 走到一定阶段* 再做深度集成，否则会做无用功

## 何时触发本调研立项

LX 当前路线图（[Phase 1](../../main-roadmap/phase-1-rendering-depth.md)）的 PBR / shadow / IBL / bloom 都是 graphics-only 工作，**没有强触发条件**。

强触发：

- [Phase 5 物理](../../main-roadmap/phase-5-physics.md) 决定走 GPU 实现（如 GPU 布料 / 粒子 / 流体）
- 性能瓶颈分析显示 GPU 利用率有大量空闲窗口 + 有 compute-suitable workload 可以并入

中触发：

- 引入 GPU culling
- 引入 compute-based post-process（compute bloom、tonemap 等）

弱触发：纯学习 / 探索目的。

## 参考

- *Mastering Graphics Programming with Vulkan* · Chapter 5 · "Unlocking Async Compute"
- LX 已有调研：[`multi-threading/08-Timeline与资源退休模型.md`](../multi-threading/08-Timeline与资源退休模型.md) — 同步原语模型
- LX 已有调研：[`frame-graph/06-演进路径.md`](../frame-graph/06-演进路径.md) — barrier 推导路径，本调研 REQ-D 的硬前置
- AMD GPUOpen "Async Compute" 系列文章
- NVIDIA "Async Compute" 与 Vulkan multi-queue 最佳实践
