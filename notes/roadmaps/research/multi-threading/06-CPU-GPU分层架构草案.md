# 06 · CPU-GPU 分层架构草案

> 这一章是 LX Engine 未来多线程落地的 **具体设计草案**，把 [04 · 演进路径](04-演进路径.md) 的分阶段计划落到类、接口、分层。
> 前置阅读：[05 · enkiTS 与 Vulkan queue 的分工](05-enkiTS与Vulkan-queue的分工.md)（两层并行 + fence/semaphore 桥梁）。
>
> 状态：**设计草案，未实施**。

## 6.1 设计目标

1. **和 LX 现有分层一致**：`core/`（接口）→ `infra/`（CPU 侧实现，enkiTS）→ `backend/vulkan/`（GPU 侧实现）
2. **上层接口不依赖 enkiTS 或 Vulkan**：将来可以换 TaskFlow、切 timeline semaphore、甚至支持 D3D12 backend
3. **CPU 任务和 GPU 工作有独立但可协作的抽象** —— 不硬绑定
4. **和现有 `FrameGraph` 兼容**：FrameGraph 成为 GPU 工作的 **生产者之一**，不被替代
5. **按 timeline semaphore 的心智模型设计**，backend 先用 binary fence+semaphore 实现，将来无痛切换

## 6.2 原始思路的四处精化

用户（架构师）最初提出的思路基本方向对，但有四处值得精化：

### 精化 1：术语 —— "GPU subtask" 改为 `GpuJob`

"subtask" 暗示强 "父-子拆分"，但 CPU task 和 GPU 工作的关系其实是 **produce / submit / consume**，不是拆分。

| 候选名 | 优缺点 |
|-------|------|
| `GpuJob` ✅ | 清晰、通用、不预设层次 |
| `GpuSubmission` | 最贴合 `vkQueueSubmit`，但偏底层 |
| `RenderTask` | 容易和 CPU `Task` 混 |
| `GpuWorkItem` | 啰嗦 |

**选 `GpuJob`**：一个 `GpuJob` = "一次 `vkQueueSubmit` 提交到某个 queue 的工作 + 它的依赖"。

### 精化 2：CPU task 和 GpuJob 的关系是 produce/consume，不是拆分

```
一个 CPU 任务（enkiTS Task）在执行过程中可能：
  ├── 录制 GPU 命令（vkCmdCopy*、vkCmdDraw*）  ← 产生 GpuJob
  ├── 提交 GpuJob 到某个 queue                  ← 提交
  ├── 投递另一个 CPU Task                       ← fan-out
  └── 等待 fence 上的 GPU 结果                  ← 消费
```

一个 CPU task **可能** 产生多个 GpuJob，**可能** 根本不碰 GPU。反过来 GpuJob 也不和某个 CPU task 一对一绑定。接口设计上体现为：`TaskContext` 提供 `submitGpuJob(job)` / `waitForGpuJob(handle)` 两个方法，而不是把 `GpuJob` 作为 Task 的子字段。

### 精化 3：GPU 侧是 Orchestrator，不是 Scheduler

真正的 GPU 调度由驱动 / 硬件做。用户态代码能做的是：

- 管理 queue 句柄（VkQueue）
- 管理 fence / semaphore 的对象池
- 维护 GpuJob 的依赖图
- 决定 submit 顺序 + 正确的 wait/signal semaphore
- 回收已完成 GpuJob 的资源

所以取名 **`GpuJobOrchestrator`**（"编排器"），不叫 "scheduler"。

### 精化 4：Timeline semaphore 作为心智模型

不要把 fence 和 binary semaphore 当成两个对象来抽象。按 timeline 的 **单调 value** 抽象一次：

```cpp
class IGpuTimeline {
    virtual uint64_t nextSignalValue() = 0;             // 分配下一个 value
    virtual uint64_t completedValue() const = 0;        // 当前已完成的最大 value
    virtual void     wait(uint64_t v, uint64_t timeoutNs) = 0;
    virtual void     waitOnSubmit(uint64_t v) = 0;      // GpuJob 间 GPU↔GPU wait
};
```

> ⚠️ **方向更新（2026-04-23）**：LX 最新决定是 **第一版就直接全面使用 timeline semaphore**（Vulkan 1.2+ 已 core），**完全去 fence + binary semaphore**，不走"binary 过渡"方案。
>
> 详细论证、资源退休模型、GpuJobHandle 与 retireValue 的对应关系见 [08 · Timeline Semaphore 与资源退休模型](08-Timeline与资源退休模型.md)。
>
> 配套的"哪些资源要按帧复制 / 哪些单份 / 哪些版本化"见 [09 · Frame-local 资源集](09-Frame-local资源集.md)。

## 6.3 完整分层图

```
┌────────────────────────────────────────────────────────────────────────┐
│  core/  (接口层，不依赖具体库 / API)                                    │
│                                                                        │
│    ITaskScheduler       — 投递 / 等待 / 依赖                            │
│    TaskHandle           — task 引用                                     │
│    TaskContext          — task body 里可用的接口                        │
│       └── submitGpuJob / waitForGpuJob / addChildTask                  │
│                                                                        │
│    IGpuJobOrchestrator  — 创建 / 提交 / 等 GpuJob 完成                  │
│    GpuJob               — queue kind + record fn + deps                │
│    GpuJobHandle         — 不透明引用 + timeline value                   │
│    IGpuTimeline         — CPU/GPU 共用的单调同步原语                    │
│                                                                        │
└────────────┬──────────────────────────────────┬────────────────────────┘
             ▼                                  ▼
┌────────────┴────────────────────┐  ┌──────────┴─────────────────────────┐
│  infra/                         │  │  backend/vulkan/                   │
│  (CPU 侧具体实现)                │  │  (GPU 侧具体实现)                   │
│                                 │  │                                    │
│  EnkiTsTaskScheduler            │  │  VulkanGpuJobOrchestrator          │
│    └→ TaskScheduler             │  │    └→ 管 VkQueue / VkSemaphore      │
│       + pinned task +           │  │       + fence pool + cmd pool      │
│       work-stealing             │  │       + 跨 queue ownership barrier │
│                                 │  │                                    │
│                                 │  │  VulkanTimeline                    │
│                                 │  │    阶段 1：binary fence + counter  │
│                                 │  │    阶段 2：真 timeline semaphore   │
└─────────────────────────────────┘  └────────────────────────────────────┘
```

数据流：

```
应用代码 / FrameGraph
    ↓ scheduler.addTask(body)
ITaskScheduler  (core 接口)
    ↓ EnkiTsTaskScheduler (infra 实现)
线程池 (enkiTS worker)
    ↓ 任务 body 里调
TaskContext::submitGpuJob(job)
    ↓ 转发给
IGpuJobOrchestrator  (core 接口)
    ↓ VulkanGpuJobOrchestrator (backend 实现)
VkQueueSubmit(..., wait=semA@N, signal=semB@M, fence=F)
    ↓
GPU 执行，完成后 semB@M 达到 → 下游 GpuJob 可启动
                        fence F signal → CPU 侧可观察
```

## 6.4 和现有 FrameGraph 的关系

LX 已有 `FrameGraph` / `RenderQueue` / `RenderTarget`（见 `openspec/specs/frame-graph/spec.md`）。新增的 GpuJob 层**不替代 FrameGraph，在其下**：

```
应用 / 场景
    ↓
FrameGraph   （声明式 pass 图，已有）
    ↓ compile 阶段产出
一批 GpuJob  （每个 pass 或 pass group → 1~N 个 GpuJob）
    ↓
GpuJobOrchestrator   （新增，统一编排）
    ↓
VkQueueSubmit
```

这样：

- **FrameGraph 是 GpuJob 的生产者之一**，不是替代
- **异步加载**（来自 pinned I/O task）、**后台 compute**、**streaming** 都是 GpuJob 的其他生产者
- 所有 GpuJob 走同一套 orchestrator，共享同一个 fence/semaphore 对象池
- 异步加载和渲染之间的依赖（"新纹理必须在采样它的 pass 之前 ready"）天然用 GpuJob 的 `waitOn` 字段表达

## 6.5 最小接口草案

只是给个 **方向感**，真实施时要 review。

### core/task

```cpp
// core/task/task_handle.hpp
using TaskHandle = uint64_t;            // opaque
using GpuJobHandle = uint64_t;          // opaque

// core/task/task_scheduler.hpp
class TaskContext;

class ITaskScheduler {
public:
    virtual TaskHandle addTask(std::function<void(TaskContext&)> body) = 0;
    virtual TaskHandle addPinnedTask(uint32_t threadId,
                                     std::function<void(TaskContext&)> body) = 0;
    virtual void       addDependency(TaskHandle parent, TaskHandle child) = 0;
    virtual void       waitForTask(TaskHandle) = 0;
    virtual ~ITaskScheduler() = default;
};

// core/task/task_context.hpp
class IGpuJobOrchestrator;
struct GpuJob;

class TaskContext {
public:
    virtual uint32_t             threadId() const = 0;
    virtual GpuJobHandle         submitGpuJob(const GpuJob&) = 0;
    virtual void                 waitForGpuJob(GpuJobHandle, uint64_t timeoutNs) = 0;
    virtual TaskHandle           addChildTask(std::function<void(TaskContext&)>) = 0;
    virtual ~TaskContext() = default;
};
```

### core/gpu

```cpp
// core/gpu/queue_kind.hpp
enum class QueueKind : uint8_t { Graphics, Compute, Transfer };

// core/gpu/gpu_job.hpp
using CommandBufferRecordFn = std::function<void(class ICommandBuffer&)>;

struct GpuJob {
    QueueKind                    queue;
    CommandBufferRecordFn        record;         // 用户提供的录制 lambda
    std::vector<GpuJobHandle>    waitOn;         // 依赖哪些 GpuJob
    bool                         cpuObservable = false;  // 是否需要 fence
};

// core/gpu/gpu_timeline.hpp
class IGpuTimeline {
public:
    virtual uint64_t signalNext() = 0;                          // GPU 完成后单调递增
    virtual bool     isReached(uint64_t value) const = 0;       // 非阻塞 query
    virtual void     wait(uint64_t value, uint64_t timeoutNs) = 0; // CPU 阻塞等
    virtual ~IGpuTimeline() = default;
};

// core/gpu/gpu_job_orchestrator.hpp
class IGpuJobOrchestrator {
public:
    virtual GpuJobHandle submit(const GpuJob&) = 0;
    virtual bool         isComplete(GpuJobHandle) const = 0;
    virtual void         wait(GpuJobHandle, uint64_t timeoutNs) = 0;
    virtual void         reclaimFinished() = 0;     // 帧起始清理
    virtual ~IGpuJobOrchestrator() = default;
};
```

### infra / backend 实现骨架

```cpp
// infra/task/enki_task_scheduler.hpp
class EnkiTsTaskScheduler : public ITaskScheduler {
    enki::TaskScheduler m_scheduler;
    // ... lambda → ITaskSet 适配器
};

// backend/vulkan/gpu_job_orchestrator.hpp
class VulkanGpuJobOrchestrator : public IGpuJobOrchestrator {
    VkQueue                                        m_graphicsQueue;
    VkQueue                                        m_transferQueue;   // 可能 == main
    std::vector<VkCommandPool>                     m_cmdPoolsPerThread;
    std::unordered_map<GpuJobHandle, PendingJob>   m_inFlight;
    VulkanTimeline                                 m_timeline;
    // ...
};
```

## 6.6 同步模型的统一

所有同步用 **一个 `IGpuTimeline` 对象** 表达：

- CPU task 想等 GPU：`orchestrator.wait(handle, timeout)`
- 下游 GpuJob 等上游：`GpuJob::waitOn` 包含上游 handle
- 主线程想观察：`orchestrator.isComplete(handle)`（非阻塞）

backend 负责把抽象 handle 翻译成：
- 阶段 1（binary）：每个 handle 映射到一对 `(VkFence, VkSemaphore)`
- 阶段 2（timeline）：handle 的 value 直接就是 timeline semaphore 的 value

用户层**从不直接碰 VkFence / VkSemaphore**。这是整个设计的关键收益。

## 6.7 典型工作流

### 场景 A：异步加载纹理（对应 [05 §5.4](05-enkiTS与Vulkan-queue的分工.md#54-完整数据流两节合起来做的事)）

```cpp
// 主线程
scheduler.addPinnedTask(ioThreadId, [=](TaskContext& ctx) {
    auto bytes = stbi_load(path);
    memcpy(staging->mappedPtr, bytes, size);
    free(bytes);

    GpuJob upload{
        .queue = QueueKind::Transfer,
        .record = [=](ICommandBuffer& cb) {
            cb.copyBufferToImage(staging, textureHandle);
            cb.queueReleaseBarrier(textureHandle, Transfer, Graphics);
        },
        .cpuObservable = true,
    };
    auto handle = ctx.submitGpuJob(upload);

    // 通知主线程（通过完成队列）
    completedTextures.push({textureHandle, handle});
});
```

### 场景 B：FrameGraph 的一帧渲染

```cpp
// FrameGraph compile 阶段
std::vector<GpuJob> jobs;
for (auto& pass : frameGraph.passes) {
    GpuJob job{
        .queue = QueueKind::Graphics,
        .record = [&pass](ICommandBuffer& cb) { pass.record(cb); },
        .waitOn = pass.dependencies(),  // 从依赖 pass 的 GpuJobHandle
    };
    jobs.push_back(job);
}

// 渲染线程（可能是主线程，也可能是某个 task）
for (auto& job : jobs) orchestrator.submit(job);
```

### 场景 C：异步加载 → 本帧采样

异步 upload 的 GpuJob 完成信号，被引用它的 render pass 加入 `waitOn`：

```cpp
// 帧起始 flush：完成队列里取出新纹理
for (auto& [texHandle, uploadHandle] : completedTextures) {
    bindlessSet.update(texHandle);               // 更新 bindless slot
    // 本帧采样它的 pass 在 compile 时把 uploadHandle 加到 waitOn
}
```

## 6.8 与 LX 现有 spec 的兼容性

| 现有 spec | 和新设计的关系 |
|----------|------------|
| `frame-graph/spec.md` | FrameGraph 成为 GpuJob 的生产者，compile 阶段产出 `std::vector<GpuJob>` |
| `pipeline-cache/spec.md` | 不变，仍然 preload + getOrCreate；阶段 2 的并行创建是调用模式变化，接口不改 |
| `pipeline-key/spec.md` | 不变 |
| `render-signature/spec.md` | 不变 |
| `renderer-backend-vulkan/spec.md` | 会扩展：新增 `VulkanGpuJobOrchestrator` 章节 |
| 新增 spec | `openspec/specs/task-scheduler/spec.md`、`openspec/specs/gpu-job/spec.md` |

## 6.9 实施路径（和 04 演进路径映射）

| 阶段 | 04 的步骤 | 本草案对应的工作 |
|------|---------|--------------|
| 阶段 0（零成本预留） | 新子系统接口不假设单线程 | 此阶段**不引入**新接口，只在新代码里避免单线程硬假设 |
| 阶段 1（enkiTS + 异步加载） | Phase 3 触发 | 定义 `ITaskScheduler` / `IGpuJobOrchestrator`；infra 放 enkiTS；backend 实现 binary fence+semaphore 版 `VulkanGpuJobOrchestrator` |
| 阶段 2（pipeline / shader 并行） | 与 bindless 同期 | `PipelineCache::preload` 内部调度 `addTask` 并行；沿用已有的 orchestrator |
| 阶段 3（cmd buffer 并行录制） | 性能触发 | FrameGraph compile 产出多个 `GpuJob`，每个走自己的 cmd pool |
| 阶段 4（可选） | scene / culling 并行化 | parallel-for 封装在 `ITaskScheduler` 之上 |

## 6.10 风险与开放问题

| 问题 | 说明 | 倾向 |
|-----|------|-----|
| `GpuJob` 是否要显式声明读写的资源 | 像 FrameGraph 那样的 Read/Write declarations 可以自动推依赖 | 第一版不做，让用户手写 waitOn；后续看 FrameGraph compile 能否产生 |
| secondary command buffer 要不要 | Vulkan 1.3 有 dynamic rendering，secondary 用途缩小 | 阶段 3 再评估 |
| D3D12 backend 支持 | 现在只写 Vulkan | 接口抽象时只定 `QueueKind::Graphics/Compute/Transfer`，不暴露 Vulkan 类型，留口 |
| AI agent 任务的优先级 | Phase 10 agent 推理不应抢渲染线程 | pinned task 到"非主非 I/O"的 worker，或 Task priority 机制 |
| enkiTS 将来要换 | `ITaskScheduler` 包一层接口即可 | 设计本身支持，不需额外动作 |

## 6.11 一句话

**CPU 侧 `ITaskScheduler` + GPU 侧 `IGpuJobOrchestrator`，中间用 `IGpuTimeline` 做同步；infra 放 enkiTS、backend 放 Vulkan；FrameGraph 是 GpuJob 的生产者，不是替代**。接口按 timeline semaphore 的心智模型设计，backend 先 binary 实现将来无痛切换。

## 下一步

- 回到演进计划看什么时候真做 → [04 · 演进路径](04-演进路径.md)
- 理解 CPU-GPU 分工的底层原理 → [05 · enkiTS 与 Vulkan queue 的分工](05-enkiTS与Vulkan-queue的分工.md)
