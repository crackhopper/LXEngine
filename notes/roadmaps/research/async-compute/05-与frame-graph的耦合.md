# 05 · 与 Frame Graph 的耦合

> 阅读前提：[04](04-LX当前状态对照.md) 已经盘点 LX gap。本文聚焦 *为什么* async compute 必须等 frame graph 演进到一定阶段。
>
> 关联调研：[`frame-graph/06-演进路径.md`](../frame-graph/06-演进路径.md)（候选 REQ-035..041）。

## 5.1 为什么不能独立做 async compute

最直觉的想法是："compute queue 跟 frame graph 是两件事，可以分开做"。**这是错的**。理由有三：

### 理由 1：跨 queue 依赖必须用 graph 表达

当 compute 写资源 → graphics 读资源时，必须在 frame graph 里表达"这两个 pass 之间有依赖"。如果手写：

```cpp
// 手工模式：每次都要写
fg.addPass(compute_cloth_pass);
fg.addPass(graphics_draw_cloth_pass);
manually_record_barrier(cloth_buffer, COMPUTE→VERTEX);
manually_signal_timeline(computeTimeline, T);
manually_wait_timeline(graphicsTimeline, computeTimeline, T);
```

每加一对 compute / graphics pass 都要重复这套 boilerplate。pass 数一上去就崩。

如果 frame graph 已经有 inputs/outputs 表达资源依赖（候选 REQ-035），同样的代码变成：

```cpp
fg.addPass(compute_cloth_pass);   // 声明 cloth_buffer 是 output
fg.addPass(graphics_draw_cloth_pass);   // 声明 cloth_buffer 是 input
// frame graph 自动推导 barrier + timeline 同步
```

**没 frame graph 的资源依赖机制，async compute 的代码会变成 boilerplate 地狱**。

### 理由 2：Barrier 推导需要 queue 信息

candidates frame-graph REQ-038（自动 barrier 推导）的输入是 *资源 use → resource use* 的转换。

单 queue 时：

```
input:  pass A 写 image X (COLOR_ATTACHMENT_OPTIMAL)
        pass B 读 image X (SHADER_READ_ONLY_OPTIMAL)
output: 在 B 之前插一个 layout transition barrier
```

多 queue 时：

```
input:  pass A 写 image X 在 graphics queue (COLOR_ATTACHMENT_OPTIMAL)
        pass B 读 image X 在 compute queue (SHADER_READ_ONLY_OPTIMAL)
output: 在 A 之后插一个 release barrier (graphics queue family → compute queue family)
        在 B 之前插一个 acquire barrier (graphics queue family → compute queue family)
        + timeline 同步保证 release 在 acquire 之前完成
```

输入维度多了一个 *queue family*，输出动作复杂度大幅上升。

如果 frame graph 还没做单 queue 的 barrier 推导（REQ-038 未完成）就直接做跨 queue，等于一次性吃下两个风险点，不可控。

### 理由 3：拓扑排序需要扩展

Frame graph 的拓扑排序原本是 *单 queue 内的偏序*。多 queue 之后变成 *跨 queue 的偏序图*：

```
graphics queue:  G1 → G2 → G3 → G4
                  ↘            ↗
                   C1 → C2 → C3
compute queue:
```

`G2 → C1` 表示 graphics G2 之后才能开始 compute C1（资源依赖）。`C3 → G4` 同理。

拓扑排序算法本身（DFS / Kahn）不变，但：

- 节点的"队列归属"需要纳入排序约束
- 同 queue 内的 submit batch 需要识别（哪些连续 pass 可以一次 submit）
- 跨 queue 同步点需要识别（哪里需要 timeline signal/wait）

这些是在 REQ-036（frame graph 拓扑排序）之上的扩展，必须等 REQ-036 落地后再做。

## 5.2 时序约束

总结 async compute REQ 与 frame graph REQ 的时序约束：

| async compute REQ | 依赖的 frame graph REQ | 原因 |
|------|---------|------|
| REQ-A（compute queue + pipeline 基础设施） | 无 | 纯基础设施，独立可做 |
| REQ-B（queue ownership transfer） | 无 | 提供工具函数即可 |
| REQ-C（FramePass 队列亲和性字段） | REQ-035（FramePass 数据结构扩展） | 共改 FramePass |
| REQ-D（跨 queue 自动 barrier 推导） | **REQ-038（自动 barrier 推导）** | 单 queue barrier 是跨 queue 的前提 |
| REQ-E（实战 demo） | 上述 | 需要前置就绪 |
| REQ-F（硬件 fallback） | 无 | 独立可做 |

**关键时序**：

```
frame graph 演进                async compute 演进
─────────────                  ───────────────
REQ-035                         REQ-A
  ↓                               ↓
REQ-036                         REQ-B
  ↓                               ↓
REQ-037                         REQ-C ← 跟 REQ-035 共改 FramePass
  ↓                               ↓
REQ-038 ──────────────────────→ REQ-D ← 必须等 REQ-038
  ↓                               ↓
REQ-039 / 040 / 041             REQ-E / F
```

REQ-A / B / F 可以在任何时候做（独立基础设施）。REQ-C 需要 REQ-035。REQ-D 需要 REQ-038。

## 5.3 一种安全的推进策略

### 阶段 1（早期）：纯基础设施

只做 REQ-A + REQ-B + REQ-F。让 LX *能跑* compute shader、能 *手工* 跨 queue 同步、有 fallback。

不动 frame graph 集成。compute 工作通过 `IGpuTimeline` 直接调用，不进 graph。

**适合什么场景**：单点的 compute 实验（比如 "GPU 上算个 mandelbrot"），不进生产路径。

### 阶段 2（中期）：与 frame graph REQ-035 协同

REQ-035 进展到 *FramePass 加 inputs/outputs 字段* 时，REQ-C 顺势把 queue 字段也加进去。改 FramePass 改一次。

REQ-C 的实现可以先停在"按 queue 分流 submit"，barrier 仍然手工（或暂用全局 barrier）。

**适合什么场景**：有多个 compute pass，需要进 frame graph 调度，但跨 queue barrier 还是少量手工。

### 阶段 3（晚期）：跟 frame graph REQ-038 集成

REQ-038（自动 barrier 推导）落地后，立刻做 REQ-D 把跨 queue barrier 自动化。这一步是 *async compute 的核心收益*。

**适合什么场景**：进入大规模 async 使用（多 compute pass + 复杂 graphics-compute 互动），手工 barrier 不可维护。

## 5.4 反过来：frame graph 演进时要给 async compute 留接口

frame graph 演进到关键节点时，需要在设计中 *为 async compute 留接口*，避免后续要重构。

### REQ-035（FrameGraphResource 数据结构）

`FrameGraphResource.resource_info` 应该考虑：

- 资源是 storage buffer / storage image 还是 attachment / texture？
- 资源跨 queue 时，sharing mode 怎么声明？

这两点不需要立刻实现，但 *字段要预留*，避免 REQ-D 时大改。

### REQ-036（compile - 构边 + 拓扑排序）

拓扑排序算法实现时，思考 *节点是否要支持 queue 归属维度*。

- 第一版：所有节点默认 graphics queue
- 接口预留：`FramePass.queue: QueueKind = Graphics`

REQ-036 落地后只是把 `queue` 字段当装饰，不影响拓扑排序行为。但接口已经在，REQ-C 来加实现时不需要改 schema。

### REQ-038（自动 barrier 推导）

barrier 推导算法 *从一开始就考虑 queue family*。即便单 queue 时不需要 ownership transfer，算法路径要预留：

```python
def derive_barriers(src_use, dst_use):
    layout_transition = compute_layout(src_use, dst_use)
    access_mask_change = compute_access(src_use, dst_use)
    if src_use.queue_family != dst_use.queue_family:
        return ownership_transfer_barrier(layout, access, queues)
    else:
        return regular_barrier(layout, access)
```

第一版默认两个 use 都在 graphics family，走 `regular_barrier` 分支。REQ-D 时打开 `ownership_transfer_barrier` 分支即可。

## 5.5 一句话总结

**Async compute 的基础设施可以独立做，但与 frame graph 的深度集成必须跟着 frame graph 演进的节奏走。提前做会重构，太晚做会变 boilerplate 地狱。**

## 5.6 接下来读什么

- [06 演进路径](06-演进路径.md) — REQ-A..F 切分 + 触发条件 + 风险点
