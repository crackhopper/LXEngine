# GPU-Driven Rendering + Clustered Lighting 技术调研

> 状态：**LX 完全空白 / 待 Phase 1 REQ-119 + REQ-109 触发立项**
> 最后更新：2026-04-27
> 目的：把"GPU-driven rendering"（meshlet + GPU culling + indirect draw + mesh shader）和"Clustered Deferred Rendering"（G-buffer + light culling）合并成一个演进路径调研。两个主题共享大量基础设施（compute pipeline / multi-RT / indirect command / frame graph 多 pass / 跨 pass 资源），分开调研会重复，合并后能看清完整时序。
>
> 参考材料：*Mastering Graphics Programming with Vulkan* · Chapter 6 + Chapter 7。

## 这个目录是什么

LX 当前的渲染管线有几个根本性 gap：

- 单 forward pass，无 G-buffer
- 单光源（最多 2-3 个）
- CPU 一帧发上百 draw call，无 indirect
- 整 mesh 是 draw 单位，无 meshlet 切分
- 无 GPU culling（frustum / occlusion 都不做）
- 无 light culling（朴素遍历全部光源）

第六章 + 第七章合起来描绘了 *现代渲染管线* 的核心形态：

- **Ch6 GPU-driven**：CPU 不再 per-mesh 决策，GPU compute 完成 culling 并写 indirect command
- **Ch7 Clustered**：G-buffer + 高效 light culling，让数百光源能 real-time 跑

两章的实施时机高度耦合（同一组基础设施支撑），所以放在一起调研。

## 建议阅读顺序

| # | 文档 | 来源 | 讲什么 |
|---|------|------|------|
| 01 | [GPU-driven 范式](01-gpu-driven-范式.md) | Ch6 | 为什么需要、跟传统 CPU-driven 的本质区别、能力层级 |
| 02 | [Meshlet 资产层](02-meshlet资产层.md) | Ch6 | meshoptimizer + Mesh 数据结构扩展 + 压缩格式 |
| 03 | [Culling pipeline](03-culling-pipeline.md) | Ch6 | compute culling + Hi-Z + 两阶段 + mesh shader vs compute 两条路径 + fallback |
| 04 | [Clustered deferred lighting](04-clustered-deferred-lighting.md) | Ch7 | G-buffer + Activision 1D bin + 2D tile 算法 |
| 05 | [LX 当前状态对照](05-LX当前状态对照.md) | 综合 | 字段级 / 能力级 gap + 跟现有 REQ-119/109 关系 |
| 06 | [演进路径](06-演进路径.md) | 综合 | 候选 REQ-A..H 切分 + 时序约束 + 跟其它三个调研的耦合 |

## TL;DR

- **GPU-driven rendering** = 把 culling + draw 决策搬到 GPU；CPU 一帧只发几个 indirect command。最小调度单位是 *meshlet*（64 vertex / 124 triangle 的小块）
- **Clustered deferred** = G-buffer + 高效 light culling。Activision 算法用 *1D depth bins + 2D screen tiles* 而不是 3D cluster，**省 20×+ 内存**
- **LX 现状**：两块都完全空白，但路线图 [REQ-119 G-Buffer](../../main-roadmap/phase-1-rendering-depth.md#req-119--g-buffer--延迟渲染路径) + [REQ-109 多光源合同](../../main-roadmap/phase-1-rendering-depth.md#req-109--pointlight--spotlight--统一多光源合同) 已经预留位置
- **推进路径**：8 个候选 REQ 分三族 — *基础设施* (A/B/C) → *cross-vendor 主路径* (D/E/F) → *优化 / 平台特定* (G/H)。详见 [06](06-演进路径.md)
- **跟其它三个调研的耦合**：是 *消费方* —— frame graph / async-compute / multi-threading 提供的所有基础设施都被这里用上。所以不能孤立做，要等几个前置就位
- **关键时序**：REQ-E (Hi-Z + 两阶段) 依赖 frame-graph REQ-038（barrier 推导）；REQ-F (clustered lighting) 依赖 REQ-C (G-buffer)；其它内部时序见演进文档

## 业界形态速查

| 维度 | LX 现状 | Ch6+Ch7 推荐 | 差距 |
|------|---------|--------------|------|
| Mesh 切分 | 整 mesh 一块 | meshlet (64 vert / 124 tri) | 大 |
| Draw 命令 | 每 mesh 一个 | 每帧一个 indirect | 大 |
| Culling | CPU 简单 frustum | GPU 两阶段 frustum + occlusion | 大 |
| Pipeline 路径 | 仅 graphics | + compute + mesh (NV/EXT) | 大 |
| Render target | swapchain | G-buffer 4 RT | 大 |
| Normal 存储 | vec3 | octahedral 8-bit | 中 |
| Light 处理 | 朴素全光源 | 1D bin + 2D tile bitfield | 大 |
| RenderPass / Framebuffer | 老 API | dynamic rendering | 中 |

## 跟其它调研的关系

```
multi-threading/08         frame-graph/06          async-compute/06       gpu-driven-rendering（本调研）
─────────────              ──────────────          ────────────────       ─────────────────────
IGpuTimeline           REQ-035..041 frame graph    REQ-A..F async         消费上面三者 +
RetirePoint            演进路径                     compute 路径           自己的：
   ↓                        ↓                        ↓                    - meshlet 资产层
   被本调研                  被本调研                  被本调研             - Hi-Z（跨 pass + 跨帧资源）
   全部消费                  全部消费                  全部消费             - mesh shader pipeline
                                                                            - clustered lighting 算法
```

**关键点**：本调研是 *消费方*，几乎所有底层能力依赖前三个调研先就位：

- multi-threading/08：跨帧 Hi-Z 复用通过 RetirePoint 跟踪
- frame-graph：多 pass DAG（G-buffer → culling → 两阶段 draw → lighting）+ 跨 pass 资源依赖 + 自动 barrier
- async-compute：culling pass 是典型 async candidate，跟 graphics pass 并行

## 何时触发本调研立项

LX roadmap 中跟本调研直接对应的 REQ：

- [REQ-119 G-Buffer / 延迟渲染](../../main-roadmap/phase-1-rendering-depth.md#req-119--g-buffer--延迟渲染路径) — 触发 Ch7 G-buffer + clustered lighting
- [REQ-109 PointLight + SpotLight + 统一多光源合同](../../main-roadmap/phase-1-rendering-depth.md#req-109--pointlight--spotlight--统一多光源合同) — 触发 clustered lighting 算法部分
- [REQ-110 Frustum Culling](../../main-roadmap/phase-1-rendering-depth.md#req-110--frustum-culling) — 触发 GPU culling 中最浅层的部分

强触发：

- 任一上面 REQ 立项
- 性能瓶颈：draw call CPU 时间、shading 时间、light loop 时间任一是 hotspot

中触发：

- 引入大场景（建筑 / 地形）
- 引入复杂光照（>30 光源）

## 参考

- *Mastering Graphics Programming with Vulkan* · Chapter 6（GPU-Driven Rendering）+ Chapter 7（Clustered Deferred Rendering）
- Aaltonen & Haar, *GPU-Driven Rendering Pipelines*, SIGGRAPH 2015 — 两阶段 occlusion 算法原始 paper
- Activision *Improved Culling for Tiled and Clustered Rendering*, SIGGRAPH 2017 — 1D bin + 2D tile 算法
- Persson, *Practical Clustered Shading* — Just Cause 3 实践
- AMD Leo demo paper — Forward+ 起源
- LX 已有调研：[`frame-graph/06-演进路径.md`](../frame-graph/06-演进路径.md)、[`async-compute/06-演进路径.md`](../async-compute/06-演进路径.md)、[`multi-threading/08-Timeline与资源退休模型.md`](../multi-threading/08-Timeline与资源退休模型.md)
