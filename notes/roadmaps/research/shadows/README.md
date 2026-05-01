# Shadows + Sparse Resources 技术调研

> 状态：**LX 完全无阴影 / 待 Phase 1 REQ-103 + REQ-109 触发立项**
> 最后更新：2026-04-28
> 目的：把"shadow rendering"和"Vulkan sparse resources"组合成一个调研。两个主题在 Ch8 一起出现，因为 *多光源 cubemap shadow 的内存问题只能靠 sparse residency 解决*。
>
> 参考材料：*Mastering Graphics Programming with Vulkan* · Chapter 8（Adding Shadows Using Mesh Shaders）。

## 这个目录是什么

LX 当前完全无阴影渲染。Phase 1 路线图里有：

- [REQ-103 Shadow pass + depth-only pipeline](../../main-roadmap/phase-1-rendering-depth.md#req-103--shadow-pass--depth-only-pipeline) — 单 shadow map 入门
- [REQ-104 Cascaded Shadow Maps](../../main-roadmap/phase-1-rendering-depth.md#req-104--cascaded-shadow-maps) — directional light CSM
- [REQ-109 PointLight + SpotLight + 多光源](../../main-roadmap/phase-1-rendering-depth.md#req-109--pointlight--spotlight--统一多光源合同) — 多光源合同

Ch8 给出的不是这些 REQ 的传统实施方案，而是 **现代渲染管线下的统一阴影方案**：

- 用 cubemap array + layered rendering 让 N 光源共用一张 image
- 用 mesh shader（沿用 GPU-driven 调研）做 shadow geometry pass
- 用 sparse residency 让 256 光源同时投影成为可能
- 用 cluster importance metric 决定 per-light shadow 分辨率

**Sparse resources 单独占一篇**（[04](04-sparse-resources.md)），因为它是 *跨主题的 Vulkan 基础能力* —— 未来 streaming texture / virtual geometry / virtual UV 也都会用。

## 建议阅读顺序

| # | 文档 | 讲什么 |
|---|------|------|
| 01 | [shadow 技术全景](01-shadow技术全景.md) | volumes / mapping / raytraced 历史 + 选择 + 经典痛点 |
| 02 | [cubemap shadow](02-cubemap-shadow.md) | cubemap array + layered rendering + multiview + 投影 depth 公式 |
| 03 | [mesh shader shadow](03-mesh-shader-shadow.md) | 4 步流水线（per-light cull + indirect + face cull + 渲染）+ 跟 GPU-driven 调研的复用 |
| 04 | [sparse resources](04-sparse-resources.md) | Vulkan sparse residency 完整模型 + page pool + binding |
| 05 | [LX 当前状态对照](05-LX当前状态对照.md) | gap 分析 + 跟现有 REQ-103 / 104 / 109 关系 |
| 06 | [演进路径](06-演进路径.md) | 候选 REQ-A..G 切分 + 时序 + 风险 |

## TL;DR

- **shadow mapping 仍然是工业标准**，raytracing 是未来但未普及
- **Point light shadow 用 cubemap array + layered rendering**，N 光源共用一张 image
- **Mesh shader 阴影 = GPU-driven 范式的 shadow 实例**：复用 meshlet 资产、cone/frustum culling、indirect commands
- **Sparse residency 让 N 光源同时投影成为可能**：每个 light 按 *屏幕重要度* 动态分配 cubemap 内存，整体内存可控
- **跟前置调研强耦合**：multi-threading（sparse bind 走 timeline）+ frame-graph（多 pass）+ async-compute（compute culling）+ gpu-driven-rendering（mesh shader 路径 + meshlet 资产）
- **推进路径**：7 个候选 REQ 分两族 — *基础阴影族* (A/B/C/G) + *Sparse + 多光源族* (D/E/F)

## 业界形态速查

| 维度 | LX 现状 | Ch8 推荐 | 差距 |
|------|---------|--------|------|
| Shadow rendering | 无 | shadow mapping | 极大 |
| Cubemap textures | 无 | cubemap array | 大 |
| Layered rendering | 无 | `gl_Layer` 写入 | 中 |
| Shadow filtering | 无 | PCF / VSM / contact-hardening | 中（v1 可选 hard shadow） |
| 多光源 shadow | 无 | mesh shader + sparse | 大 |
| Sparse resources | 完全无 | residency + page pool | 大 |
| Per-light resolution | 无 | cluster importance metric | 中 |

## 跟其它调研的关系

```
multi-threading/08      frame-graph/06       async-compute/06     gpu-driven-rendering    shadows（本调研）
─────────────           ──────────────       ────────────────     ─────────────────       ──────────────
IGpuTimeline       REQ-035..041 frame      REQ-A..F async       REQ-A..H GPU-driven    消费上面四者 +
RetirePoint        graph 演进                compute               + clustered            自己的：
   ↓                   ↓                       ↓                        ↓                  - shadow mapping
   被本调研             被本调研                 被本调研                  被本调研           - cubemap array
   消费                 消费                    消费                     消费              - sparse residency
                                                                                          - importance metric
```

**关键点**：本调研是 *最下游消费方*，依赖前面四个调研全部就位才能完整实施。但 *基础 shadow*（候选 REQ-A + B）可以早做（不需要 mesh shader）。

## 何时触发本调研立项

强触发：

- [REQ-103 Shadow pass](../../main-roadmap/phase-1-rendering-depth.md#req-103--shadow-pass--depth-only-pipeline) 立项 → 触发 A + B
- [REQ-109 多光源合同](../../main-roadmap/phase-1-rendering-depth.md#req-109--pointlight--spotlight--统一多光源合同) 立项 + Phase 1 决定走 256 光源场景 → 触发 C + D + E + F
- 其它项目（streaming texture / virtual geometry）需要 sparse → 触发 D 单独立项

中触发：

- 引入 [REQ-104 CSM](../../main-roadmap/phase-1-rendering-depth.md#req-104--cascaded-shadow-maps) → 触发 G
- 性能瓶颈：单 shadow pass 占 frame time > 30%

## 参考

- *Mastering Graphics Programming with Vulkan* · Chapter 8（Adding Shadows Using Mesh Shaders）
- *More Efficient Virtual Shadow Maps for Many Lights*（Olsson et al.） — per-light resolution metric
- LX 已有调研：
  - [`gpu-driven-rendering/`](../gpu-driven-rendering/README.md) — mesh shader 路径
  - [`frame-graph/`](../frame-graph/README.md) — 多 pass 调度
  - [`multi-threading/08-Timeline与资源退休模型.md`](../multi-threading/08-Timeline与资源退休模型.md) — sparse bind semaphore 协议
  - [`async-compute/`](../async-compute/README.md) — culling compute pass async candidate
