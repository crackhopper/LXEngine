# 01 · GPU-Driven Rendering 范式

> 阅读前提：理解传统 graphics pipeline 的 CPU draw call 模型；理解 compute shader 基础（见 [`async-compute/03-compute-shader基础.md`](../async-compute/03-compute-shader基础.md)）。

## 1.1 传统 CPU-Driven Rendering 的痛点

每帧 CPU 做的事：

```cpp
for each mesh in scene:
    if (cull_test(mesh.AABB, camera.frustum))
        record_uniforms(mesh)
        record_descriptor_binding(mesh)
        vkCmdDrawIndexed(cmd, mesh.index_count, ...)
```

随着场景规模增长，三处崩溃：

### 痛点 1：CPU 是 draw call 的串行瓶颈

CPU 逐 mesh 处理。100k mesh 的场景在主线程上累计几 ms 的纯 CPU 时间。

multi-threading 调研里讲过 *并行命令录制*（多线程录 secondary command buffer）能缓解，但本质还是 CPU 决策。

### 痛点 2：Occlusion culling 算不出来

CPU 想做 occlusion 必须读 GPU 上的 depth buffer → readback → 跨帧延迟。代价大到不如不做。

结果：渲染 *视锥内但被前景挡住* 的所有物体，浪费 GPU 时间。

### 痛点 3：大 mesh 全有/全无

```
地形 mesh（百万 vertex）+ 视锥只覆盖一角
↓
传统 CPU 决策：要么画全部要么不画 → 浪费
```

### 痛点 4：Mesh 之间的 GPU 利用率不均

每个 vkCmdDraw 都有 fixed overhead（state 切换 / pipeline 验证 / etc.）。N 个小 mesh 比 1 个大 mesh 慢 5-10×。

## 1.2 GPU-Driven Rendering 的核心思想

**把 culling + draw call 决策全部搬到 GPU**。

```
CPU 每帧:
    vkCmdDispatch(culling_compute_shader)        // 1 个命令
    vkCmdDrawIndexedIndirectCount(...)           // 1 个命令

GPU 自动:
    Compute shader 跑 culling，写 indirect buffer
    Indirect command 自动展开成 N 个 draw
    Per-mesh 决策完全在 GPU 上
```

CPU 一帧只发 *几个* 命令，剩下交给 GPU 自治。

## 1.3 必须配套的三个能力

GPU-driven 不是单点优化，是 *多个能力组合* 才能实现：

| 能力 | 提供什么 |
|------|---------|
| **Meshlet 切分** | 让"整 mesh 全有/全无"细化为"meshlet 级别" |
| **Compute culling** | GPU 上并行跑 frustum / occlusion / back-face 测试 |
| **Indirect drawing** | GPU 自己写 draw command，CPU 不参与 per-mesh 决策 |
| **Hi-Z + 两阶段（可选）** | 把 occlusion culling 推到主流路径，不再依赖 CPU readback |
| **Mesh shader（可选）** | 替代传统 vertex shader，跟 meshlet 模型更原生 |

前三个 *必须*，后两个 *推荐*。

## 1.4 GPU-Driven 能力层级

```
Level 0: 纯 CPU-driven                        ← LX 在这里
Level 1: + indirect draw（CPU 还做 culling）  ← 入门
Level 2: + GPU frustum culling
Level 3: + GPU occlusion culling (Hi-Z 单阶段)
Level 4: + 两阶段 Hi-Z occlusion
Level 5: + meshlet 级别 culling
Level 6: + mesh shader（取代 vertex shader）
Level 7: + 全 GPU-driven dispatch chain（pass 都由 GPU 自决）
```

每一层都比上一层有非线性收益（不是 10% 优化，而是 *几倍* 的 throughput）。

## 1.5 经典工业引擎的位置

| 引擎 / 项目 | 大致 Level | 实战 |
|------------|-----------|------|
| Unity / 普通独立游戏 | 1 | indirect draw + CPU culling |
| Unreal Engine 4 | 2-3 | GPU culling，部分 occlusion |
| Unreal Engine 5 (Nanite) | 6-7 | meshlet + mesh shader + 软光栅 |
| Frostbite / id Tech 7 | 5-6 | meshlet + GPU culling |
| Activision / Doom Eternal | 5 | meshlet + 两阶段 occlusion |
| AMD Leo demo (2012) | 3 | Forward+ + 早期 GPU culling |

LX 处于 Level 0，要跟工业引擎对标至少要到 Level 4-5。

## 1.6 这一章 + Ch7 怎么把 LX 往上推

| 章节内容 | 推到哪一级 |
|---------|----------|
| Ch6 §1 meshlet | Level 5 资产前置 |
| Ch6 §2 task/mesh shader | Level 6（可选硬件路径） |
| Ch6 §3 compute culling + Hi-Z + indirect | Level 4 主路径 |
| Ch7 G-buffer + clustered lighting | 跟 Level 不直接对应，是 *正交的* 渲染管线选择（forward+ vs deferred）+ light 处理 |

Ch7 的 *clustered lighting 算法本身* 跟 GPU-driven 关系不大（CPU 也能做），但 *实施时机* 跟 GPU-driven 重合 —— 都需要 frame graph 多 pass + compute pipeline + indirect command。

## 1.7 一个常被混淆的点

> "GPU-driven rendering = 用 compute shader 做事？"

**不准确**。

- 用 compute shader 算 SSAO / bloom / postprocess 不算 GPU-driven，那只是 *用 compute shader 实现某个 effect*
- GPU-driven 的核心是 **draw call 决策搬到 GPU**，关键是 *indirect drawing + GPU 写 indirect command*
- 没 indirect command 的 compute culling 还得 CPU 等 readback 再发 draw，等于回到 CPU-driven

所以 *indirect drawing* 是 GPU-driven 的最基本前置。

## 1.8 接下来读什么

- [02 Meshlet 资产层](02-meshlet资产层.md) — 怎么把 mesh 切成 GPU 友好的小块
