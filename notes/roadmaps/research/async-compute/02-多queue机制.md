# 02 · 多 Queue 机制

> 阅读前提：[01](01-async-compute-是什么.md) 已经讲清 async compute 概念。本文展开 Vulkan 多 queue 的具体机制。
>
> **重要**：同步原语模型（fence / semaphore / timeline）已由
> [`multi-threading/08-Timeline与资源退休模型.md`](../multi-threading/08-Timeline与资源退休模型.md) 完整收口。
> 本文 *不重写* timeline 概念，只补 *多 queue 特有* 的部分。

## 2.1 Queue 是什么

Vulkan 把 GPU 的不同硬件引擎抽象成 *queue*：

| Queue 类型 | 能跑 | 对应硬件 |
|----------|------|---------|
| Graphics queue | 所有（graphics + compute + transfer） | 主图形引擎 |
| Compute queue | compute + transfer | 独立 compute 引擎（异步） |
| Transfer queue | 只 memory copy | DMA 引擎 |

**关键事实**：不同 queue 上的工作 *硬件层面* 可以并行执行。`async compute` 的 "async" 就是来自这一点。

NVIDIA / AMD / Intel 现代独显通常都至少有：1 个 graphics queue + N 个 compute queue + 1-2 个 transfer queue。

## 2.2 Queue Family 的查询与选择

Vulkan 把 queue 按 *family* 分组。每个 family 暴露能力位掩码：

```cpp
VkQueueFamilyProperties props;
props.queueFlags & VK_QUEUE_GRAPHICS_BIT
props.queueFlags & VK_QUEUE_COMPUTE_BIT
props.queueFlags & VK_QUEUE_TRANSFER_BIT
```

策略上要区分两类 family：

| 类型 | 含义 | 对 async 的意义 |
|------|------|---------------|
| 通用 graphics family | `GRAPHICS \| COMPUTE \| TRANSFER` | 能跑 compute 但 *不算 async*（同硬件） |
| **独立 compute family** | 只有 `COMPUTE \| TRANSFER`，不带 graphics | **真正的 async 通道** |
| 独立 transfer family | 只有 `TRANSFER`，不带 graphics/compute | 资源上传专用，不在本调研范围 |

**LX 当前选择策略**：只查 graphics-capable family，没有区分独立 compute family。REQ-A 需要扩展这个选择策略。

### Fallback 策略

不是所有硬件都有独立 compute family（移动 GPU、老集显常见）。fallback 设计：

1. 优先：找独立 compute family（`COMPUTE` but not `GRAPHICS`）
2. 次选：在 graphics family 上额外开一个 queue 实例（同硬件不同 stream）
3. 兜底：跟 graphics 共用同一个 queue（失去并行收益但功能不破）

通过 `IGpuTimeline` 抽象，调用方不应该感知这个 fallback —— 接口上仍然有"computeTimeline"，只是底层可能跟 graphicsTimeline 是同一个 VkSemaphore。

## 2.3 多 Queue 提交的代码模式

### Compute 提交

```cpp
// 1. 单独的 command buffer（不能跨 queue 复用）
CommandBuffer* cb = gpu.get_command_buffer(...);

// 2. 录制 compute 命令
cb->bind_pipeline(compute_pipeline);
cb->bind_descriptor_set(...);
cb->dispatch(group_x, group_y, group_z);

// 3. 单独 submit 到 compute queue，带 timeline
queue_submit2(vulkan_compute_queue, ...);
```

跟 graphics submit 主要区别：

- 命令是 `vkCmdDispatch` 而不是 `vkCmdDraw*`
- 提交到 `vulkan_compute_queue` 而不是 `vulkan_main_queue`
- command buffer 必须从 *compute queue family* 的 command pool 分配

### 跨 Queue 同步

书里布料模拟的例子：

```
compute queue:  [模拟布料顶点]  signal computeTimeline = 5
                       ↓
                       ↓ (graphics 必须等 compute 完成才能用新顶点)
                       ↓
graphics queue: [draw 布料 mesh]  wait computeTimeline ≥ 5
```

这套模式在 [`multi-threading/08`](../multi-threading/08-Timeline与资源退休模型.md) 里已经设计过。LX 的 `IGpuTimeline.waitOnSubmit(value)` 接口直接对接这个用法。

### Wait Stage 的细节

跨 queue 同步时 wait stage 决定 GPU 哪一阶段需要这个 semaphore 满足：

```cpp
{ ..., compute_semaphore, value=5,
  VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR }
```

意思："graphics pipeline 走到 *vertex attribute input* 阶段时才需要 compute 完成"。在那之前的 stage（比如 indirect command read）可以提前并行跑。

**这是 async compute 的性能优化点**：

| Compute 写的内容 | 推荐 wait stage |
|----------------|----------------|
| Vertex 数据（布料 / 动画） | `VERTEX_ATTRIBUTE_INPUT` |
| Texture（被 fragment 采样） | `FRAGMENT_SHADER` |
| Indirect draw count buffer | `DRAW_INDIRECT` |
| Descriptor 数据 | `*_SHADER`（取决于哪 stage 用） |

wait stage 选得越晚，越多 graphics 工作可以提前开始。

## 2.4 Queue Family Ownership Transfer

这是 *async compute 特有* 的机制，[`multi-threading/08`](../multi-threading/08-Timeline与资源退休模型.md) 没覆盖。

### 为什么需要

Vulkan 资源（buffer / image）创建时声明 sharing mode：

| Sharing mode | 含义 | 性能 |
|--------------|------|------|
| `VK_SHARING_MODE_EXCLUSIVE`（默认） | 同一时刻只能一个 queue family 拥有 | 最优 |
| `VK_SHARING_MODE_CONCURRENT` | 多 queue family 可同时访问 | 有 penalty |

绝大多数引擎选 EXCLUSIVE，但意味着 *跨 queue family 使用资源时要做 ownership transfer*。

### Transfer 的形态

跨 queue 用资源时要发两次 barrier：

```cpp
// 1. 释放 ownership（在 srcQueue 上）
release_barrier.srcQueueFamilyIndex = src_family;
release_barrier.dstQueueFamilyIndex = dst_family;
// ... record on src queue's command buffer

// 2. 获取 ownership（在 dstQueue 上）
acquire_barrier.srcQueueFamilyIndex = src_family;
acquire_barrier.dstQueueFamilyIndex = dst_family;
// ... record on dst queue's command buffer

// 加上 timeline 同步保证 release 在 acquire 之前完成
```

这两次 barrier 的内容必须完全镜像（layout / access mask），任一不一致 → validation error / undefined behavior。

### 何时不需要 Transfer

- 同一 queue family 内（同 family 多 queue 实例）
- 资源是 `VK_SHARING_MODE_CONCURRENT`
- 资源是 swapchain image 配合 swapchain 的 sharing mode

### LX 当前状态

LX 所有资源都是单 queue 用，不需要 transfer。REQ-B 才会引入这个机制。

## 2.5 与 multi-threading/08 的引用边界

| 主题 | 归属 |
|------|------|
| Timeline semaphore 的本质 / API / wait/signal 模式 | [`multi-threading/08 §8.3-8.5`](../multi-threading/08-Timeline与资源退休模型.md#83-timeline-semaphore-的本质) |
| LX 的最小 timeline 集合（含 `computeTimeline`） | [`multi-threading/08 §8.4`](../multi-threading/08-Timeline与资源退休模型.md#84-lx-的最小-timeline-集合) |
| 资源退休模型（RetirePoint） | [`multi-threading/08 §8.5`](../multi-threading/08-Timeline与资源退休模型.md#85-资源退休模型) |
| `IGpuTimeline` 接口设计 | [`multi-threading/08 §8.13`](../multi-threading/08-Timeline与资源退休模型.md#813-对-lx-设计的具体影响) |
| Queue family 选择策略 | **本调研** |
| Queue family ownership transfer | **本调研** |
| 多 queue submission 的代码模式 | **本调研** |
| Wait stage 优化 | **本调研** |

## 2.6 接下来读什么

- [03 compute shader 基础](03-compute-shader基础.md) — SIMT / dispatch / GLSL 陷阱
