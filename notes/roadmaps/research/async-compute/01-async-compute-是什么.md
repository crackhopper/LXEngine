# 01 · Async Compute 是什么

> 阅读前提：理解 GPU 内部有大量并行计算单元，但渲染管线某些阶段只用一部分。

## 1.1 GPU 利用率模型

现代 GPU 内部有几千个通用计算单元（compute units / streaming multiprocessors）。一帧渲染里这些单元 *很少* 时刻全满负载。

常见的"低利用率窗口"：

```
graphics workload 时间线：

│ 顶点变换 ████░░░░░░  ← rasterizer / fixed-function 在工作，
│                       compute units 部分闲置
│ 阴影 pass ██░░░░░░░░  ← depth-only，没 fragment 工作，大量闲置
│ depth pre ██░░░░░░░░
│ gbuffer  ████████░░
│ lighting ████████████ ← 终于满载
│ tonemap  ████░░░░░░
```

很多阶段只用到 GPU 的一部分硬件（顶点处理用 vertex unit、depth-only 不用 fragment unit、tonemap 是简单 fragment 运算 register pressure 低）。剩下的 compute units 完全空闲。

## 1.2 Async Compute 的核心思想

**让 compute 工作和 graphics 工作 *物理上并行* 跑在 GPU 上**。

不是"先算 compute、再算 graphics"，而是"compute 和 graphics 同时在不同硬件引擎上跑"。GPU scheduler 会把空闲的 compute units 分配给 compute queue，不打扰 graphics queue 在用的资源。

关键事实：现代 GPU（NVIDIA Pascal+ / AMD GCN+ / Intel Arc）内部有 *物理独立* 的 graphics engine 和 compute engine。Async compute 正是利用这个硬件特性。

## 1.3 适合 / 不适合 async 的工作

### 适合 async

- 物理模拟（布料、粒子、流体）
- GPU 端 culling（视锥剔除、occlusion）
- AO、SSAO 这类 screen-space 后期
- light cluster build
- mip 链生成
- shadow map 准备

特征：与主渲染流程 *没有强 frame 内依赖*，可以跨 frame 拉长执行窗口。

### 不适合 async

- 主 lighting pass（依赖 GBuffer，graphics 刚写完）
- 任何"必须立刻消费的"计算
- 计算量极小的工作（submit overhead > 收益）

特征：与 graphics 有强 frame 内依赖，串行等待开销大于并行收益。

## 1.4 为什么不只是"用 compute shader"

值得澄清两个常被混淆的概念：

| 概念 | 定义 | 是否需要 async |
|------|------|---------------|
| Compute shader | 一种 shader 类型，可在 graphics queue 或 compute queue 上跑 | 否 |
| Async compute | 把 compute 工作 *提交到独立 compute queue*，与 graphics 并行执行 | 是 |

也就是说：

- 写一个 compute shader → 不需要 async，graphics queue 上 dispatch 就行
- async compute → 需要独立 queue + 跨 queue 同步 + ownership transfer

很多引擎一开始 *只用 compute shader 不用 async compute*，把所有工作都串行提交到 graphics queue。这能拿到 "GPU 上算" 的优势（vs CPU 算 + 上传），但拿不到 "并行 GPU" 的优势。

LX 当前 *两个都没有*，需要先拿到第一个（compute pipeline 路径），再考虑第二个（async compute 集成）。

## 1.5 LX 当前的位置

按上面的层级，LX 在：

```
Level 0: 没 compute shader 路径               ← LX 在这里
Level 1: 有 compute shader，单 queue 串行
Level 2: compute 提到独立 queue，但手工 barrier
Level 3: frame graph 自动 barrier + ownership transfer
Level 4: 自动 workload 调度（哪些 pass 适合 async 自动决定）
```

REQ-A 把 LX 推到 Level 1。REQ-B + REQ-C 推到 Level 2。REQ-D（跨 queue barrier 推导）推到 Level 3。Level 4 不在本调研范围。

## 1.6 触发条件回顾

[README §"何时触发本调研立项"](README.md#何时触发本调研立项) 已列触发条件。简短再述：

- 强触发：[Phase 5](../../main-roadmap/phase-5-physics.md) 物理走 GPU
- 中触发：GPU culling / compute-based post-process
- 弱触发：纯学习

LX 当前 [Phase 1](../../main-roadmap/phase-1-rendering-depth.md) 的所有条目都是 graphics-only，没有强触发。

## 1.7 接下来读什么

- [02 多 queue 机制](02-多queue机制.md) — Vulkan queue family / submit / 跨 queue 同步
