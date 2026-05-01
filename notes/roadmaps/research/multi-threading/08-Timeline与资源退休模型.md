# 08 · Timeline Semaphore 与资源退休模型

> 这一章说明 LX Engine 选定的 GPU 同步方案：**全面使用 timeline semaphore，完全去除 fence 和 binary semaphore**。
>
> 这是对 [05 §5.3](05-enkiTS与Vulkan-queue的分工.md#53-fence-和-semaphore-的分工) "Fence vs Semaphore 分工" 的 **替代方案**，不是补充。05 章描述的是书本（Vulkan 1.0 时代）的做法；本章是 LX 的决定方向。
>
> 前置阅读：[05 · enkiTS 与 Vulkan queue 的分工](05-enkiTS与Vulkan-queue的分工.md)、[06 · CPU-GPU 分层架构草案](06-CPU-GPU分层架构草案.md)。

## 8.1 一句话定位

> **Timeline semaphore 绑定的是 queue / submit 进度线，不是资源。**
> **资源是否"还在被使用"，靠它自己记录的 `retireValue` 和对应 timeline 的 `completedValue` 比较。**

这是全章最该记住的一句话。下面所有内容都是它的展开。

## 8.2 为什么直接上 timeline，不走 binary 过渡

之前 [06 §6.2 精化 4](06-CPU-GPU分层架构草案.md#精化-4timeline-semaphore-作为心智模型) 的设想是："接口按 timeline 抽象，backend 先用 binary fence + binary semaphore 实现"。

**这个方案作废**。新决定：**第一版就直接用 timeline semaphore，完全去 fence + binary semaphore**。

理由：

| 维度 | Binary + Fence（书本 / 旧方案） | Timeline（新方案） |
|------|------------------------------|------------------|
| API 可用性 | Vulkan 1.0 | **Vulkan 1.2 已进 core，驱动普及** |
| 对象数 | 一对一映射资源时对象池爆炸 | 少数几条全局 timeline |
| CPU / GPU 统一 | Fence 给 CPU，semaphore 给 GPU —— 两种 API | **CPU 和 GPU 用同一接口 wait/signal** |
| 迁移成本 | 将来还要切 timeline，无法避免 | 零迁移 |
| 实现复杂度 | binary → timeline 兼容层要维护 | 直接用 |

**没有"binary 兼容层"的现实收益**，不如一次到位。

## 8.3 Timeline semaphore 的本质

一条 timeline semaphore = **一条单调递增的 GPU 进度线**。

```
graphicsTimeline:
    submit A  →  signal 100
    submit B  →  signal 101
    submit C  →  signal 102
    ...
```

它**不表示**"当前帧是谁"，**表示**的是："graphics queue 已经推进到哪个完成点"。

所以：

- ❌ 不需要每帧一套 semaphore
- ❌ 不需要按资源分配 semaphore
- ✅ 多帧在飞通过"**不同 frame slot 记录不同 retireValue**"表达
- ✅ 资源退休通过"**自己记 retireValue，查 timeline.completedValue**"表达

## 8.4 LX 的最小 timeline 集合

不是"每帧一条"，不是"每个资源一条"，而是 **按 queue / submit stream 组织**：

```cpp
struct GpuTimelines {
    IGpuTimeline transferTimeline;   // transfer queue 进度
    IGpuTimeline graphicsTimeline;   // graphics queue 进度
    IGpuTimeline presentTimeline;    // present 完成进度
    IGpuTimeline computeTimeline;    // compute queue 进度（预留，没硬件就不用）
};
```

全引擎 3~4 条 timeline，不随帧数 / 资源数增长。

### 为什么是按 queue / stream 组织

- Queue 之间没有隐式同步，一定要显式 timeline
- 同一个 queue 的 submit 天然按 signal 顺序串行，单调递增
- Present 虽不是通用 queue，但 swapchain 有自己的完成节奏

## 8.5 资源退休模型

任何"GPU 用完才能复用"的对象，都用同一个抽象：

```cpp
struct RetirePoint {
    QueueLine line;     // 挂哪条 timeline
    uint64_t  value;    // 退休值
};

class Resource /* or Allocation / Batch / FrameSlot */ {
    RetirePoint retireAt;   // 我"预约"在这条线到 value 时可回收
    // ...
};
```

### 判断资源是否可复用

```cpp
bool canReuse(const Resource& r, const GpuTimelines& tl) {
    auto& line = tl.get(r.retireAt.line);
    return line.completedValue() >= r.retireAt.value;
}
```

**完全不需要 barrier / 锁位 / per-resource semaphore**。只是一次比较。

### 适用对象（所有需要"等 GPU 用完"的东西）

- frame slot
- command pool
- descriptor arena
- staging block
- upload batch
- transient allocation
- swapchain image 某次使用
- 某资源的某个版本

**统一模型**：记一个 `RetirePoint`，查 timeline。

### 如果资源跨多条 timeline

例如 graphics 读过、transfer 又写过的 image：

```cpp
struct RetirePoints {
    RetirePoint graphicsUse;
    RetirePoint transferUse;
};

bool canReuse(const Resource& r, const GpuTimelines& tl) {
    return tl.graphics.completedValue() >= r.retires.graphicsUse.value
        && tl.transfer.completedValue() >= r.retires.transferUse.value;
}
```

但**工程上应尽量避免一个复用单元同时依赖多条 timeline**。实践里：

- frame-local 资源 → 主要挂 `graphicsTimeline`
- upload / staging 资源 → 主要挂 `transferTimeline`

## 8.6 `maxFramesInFlight = 3` 的正确解读

这个值在老模型里常被误解成"要有 3 套 semaphore"。**错**。正确解读：

```
maxFramesInFlight = 3

↓

3 个 FrameSlot（资源包）

↓

每个 slot 记 graphicsRetireValue / presentRetireValue（value 而已，不是对象）

↓

timeline semaphore 仍然是全局 少数几条
```

具体例子：

```cpp
frameSlot[0].graphicsRetireValue = 101;
frameSlot[1].graphicsRetireValue = 102;
frameSlot[2].graphicsRetireValue = 103;

// CPU 想复用 frameSlot[0] 时：
if (graphicsTimeline.completedValue() >= frameSlot[0].graphicsRetireValue) {
    // slot 0 所有 frame-local 资源都可以 reset / 重用
}
```

**Frame slot 是资源复用单位，timeline 是进度表达方式**，两者正交。

关于哪些资源要 ×3、哪些不要，见 [09 · Frame-local 资源集](09-Frame-local资源集.md)。

## 8.7 四种并行里 timeline 的作用

把 [05](05-enkiTS与Vulkan-queue的分工.md) 和 [07](07-命令录制并行.md) 的四种并行整合看，timeline 在每一种里扮演的角色：

### ① 资源上传（Loading）并行

- **CPU 侧**：读文件、解码、准备 upload batch —— 不需要 semaphore（CPU 内部）
- **GPU 侧**：`transfer submit signal transferTimeline = T`
- **消费侧**：graphics submit 若要采样上传结果，`wait transferTimeline >= T`
- **Staging buffer 退休**：记 `transferTimeline@T`，`T` 被覆盖后可以复用

### ② 多 Frame 并行

- `graphicsTimeline` + `presentTimeline`
- 3 个 frame slot 轮转，各自记自己的 `graphicsRetireValue`
- **开始新帧不是"切下一套 semaphore"**，而是**找一个已退休的 slot**

### ③ 多 Pass 并行

- 多 pass 录制是**纯 CPU 并行**，不靠 semaphore
- semaphore 只在 **跨 queue / 跨 submit** 处起作用：
  - Transfer → Graphics（上传后要用）
  - Compute → Graphics（compute 结果 draw 要读）
  - Graphics pass A → Graphics pass B（如果拆成多次 submit）

### ④ Pass 内并行

- 多个 secondary command buffer 并行录制 —— **纯 CPU 并行**
- 录制好后汇总进 primary，最终一次 graphics submit signal 一个新 `graphicsTimeline` 值
- Timeline 管的是"**最终 submit 的退休点**"，不是"录制线程的同步"

**总结**：timeline 只在"submit 与 submit 之间"起作用，不参与 CPU 任务调度。

## 8.8 Timeline 的粒度：submit，不是资源指令级

**这是最容易踩的坑**。

timeline 表达的是：
- ✅ **某次 `vkQueueSubmit` 对应的完成点**

不是：
- ❌ queue 内某条具体命令的完成点
- ❌ 某资源在一次 submit 内部"前半段已用完"的细粒度时间点

### 踩坑例子

一个大 submit 里：

```
submit X:
  vkCmdCopyBufferToImage(textureA, ...)  ← 用了 Texture A
  vkCmdDraw(...)                          ← 大量无关 draw
  vkCmdDraw(...)
  ... (几百条后续命令)
signal graphicsTimeline = 500
```

如果把 Texture A 的退休点挂成 `graphicsTimeline@500`，那更新 A 要等 **整个大 submit 完成**。

### 这不是 timeline 的 bug

**是 submit 粒度太粗的问题**。两种解法：

1. **切细 submit** —— 让 A 相关的命令单独 submit，signal 一个更早的 value
2. **资源版本化** —— 别原地更新 A，写一个新版本（见 [8.9](#89-资源版本化长期-reader--异步更新的标准解)）

实际工程里通常选 2，因为 submit 本身有固定开销，不能无限切细。

## 8.9 资源版本化：长期 reader + 异步更新的标准解

场景：同一张纹理 **多帧持续被 graphics 采样**，同时 transfer 想异步**覆盖写**它。

直觉错误做法：

```
transfer 想改 Texture A → 等 graphicsTimeline 推到"最后一个 A 的 reader submit"的 value
→ 问题：如果 graphics 每帧都读 A，这个值永远往后推，transfer 永远等不到
```

这不是同步设计失败，是**真实的数据依赖**：正在读的资源不能被同时原地重写。

正确做法 —— **resource versioning**：

```cpp
struct VersionedTexture {
    std::vector<TextureHandle> versions;   // 不同时间的多个版本
    // ...
};

// Transfer 写新版本：
transferQueue.submit(writeToVersion(v2));
vt.versions.push_back({v2, transferTimeline@T});

// Graphics 下一帧切到新版本：
descriptorSet.bind(vt.latestReady(graphicsTimeline));

// 老版本挂 retireValue，完全退休后回收：
for (auto& v : vt.versions) {
    if (canReuse(v, timelines)) freeVersion(v);
}
```

相当于 GPU 资源的 **copy-on-write / double buffering**。

适用场景：
- 异步 streaming 的大纹理
- 动态更新的大 buffer
- Hot reload 的 material
- 任何"写入者 vs 长期读取者"冲突的资源

## 8.10 Timeline value 的两个保证

### 单调递增，不会变小

**在同一条 queue 上**：

- submit 顺序有序
- signal value 单调递增
- `completedValue` 只会单调递增

**不可能**出现：后一帧先完成 signal 101、前一帧后完成 signal 100，然后 `completedValue` 从 101 退回 100。

不同 queue 之间可以不同步推进（`transferTimeline = 50` 同时 `graphicsTimeline = 20`），这是正常的。

### 不会溢出

`VkSemaphore` timeline value 是 `uint64_t`。

- 100 万帧 / 秒 × 每帧 1000 次 signal × 运行 1 年 ≈ 3 × 10^13
- `uint64_t` 上限 ≈ 1.8 × 10^19

差 6 个数量级。工程上 **认为不会溢出**。

第一版实现直接：

```cpp
uint64_t nextSignalValue = 1;  // 单调递增，不重置
```

## 8.11 五个容易混淆的点

### 8.11.1 Semaphore 不是资源锁

❌ `Texture → semaphore`、`Framebuffer → semaphore`
✅ `queue / submit stream → timeline semaphore`、`resource → retireValue`

### 8.11.2 Timeline 不是每帧一套

❌ `frame0.graphicsSemaphore / frame1.graphicsSemaphore / frame2.graphicsSemaphore`
✅ 一条全局 `graphicsTimeline` + 三个 frame slot 各自记不同 value

### 8.11.3 Pass 内并行不靠 semaphore

Secondary command buffer 的并行录制是 **CPU 任务并行**，timeline 只管最终 submit 的完成点。

### 8.11.4 Timeline 不替代 barrier

| 角色 | 管什么 |
|------|------|
| Timeline | **什么时候**（when）—— submit 完成点 |
| Pipeline barrier / image barrier | **怎么合法访问**（how）—— memory visibility、layout transition、queue ownership |

两者正交，**缺一不可**。Timeline 告诉你"GPU 推到哪了"，barrier 告诉 GPU "现在这块内存可以被什么方式读"。

### 8.11.5 Submit 粒度问题不是 timeline 的问题

Timeline 粒度天然是 submit。想更早释放资源，要么切细 submit，要么资源版本化。不要怪 timeline。

## 8.12 和 CPU task system 的互补关系

[05 §5.1](05-enkiTS与Vulkan-queue的分工.md#51-核心关系两层并行--一座桥) 里画过两层并行的图。现在可以把 timeline 的角色明确进去：

```
enkiTS task scheduler
└─ 谁在 CPU 上并行准备工作
    · file load / decode
    · upload batch 准备
    · pass recording task
    · secondary command recording task

Timeline semaphore
└─ GPU 提交推进到哪里、哪些资源现在可复用
    · queue 之间依赖
    · frame-local 资源复用时机
    · upload batch / staging / pass submit 的退休点
```

**两者互补，不是替代**：

- `enkiTS` 管 **CPU 什么时候开干、谁来干**
- `Timeline` 管 **GPU 干到哪了、什么可以动**

> ⚠️ **容易让人误以为"上了 scheduler 就有并行 renderer"** —— 这是错的。
> CPU task system 只能并行 CPU 工作；Vulkan queue / timeline / barrier 仍然需要单独设计。
> 两层都要。

## 8.13 对 LX 设计的具体影响

### 更新 `IGpuTimeline` 接口（对应 [06 §6.5](06-CPU-GPU分层架构草案.md#65-最小接口草案)）

```cpp
// core/gpu/gpu_timeline.hpp
class IGpuTimeline {
public:
    /// Signal 的下一个 value（提交到 queue 时用）。
    /// 不阻塞，只是分配一个单调 value。
    virtual uint64_t nextSignalValue() = 0;

    /// 当前已完成的最大 value。非阻塞。
    virtual uint64_t completedValue() const = 0;

    /// CPU 阻塞等待 value 到达。
    virtual void wait(uint64_t value, uint64_t timeoutNs) = 0;

    /// GpuJob 之间 wait：把这条 timeline 的 value 作为依赖加入 submit。
    /// 实现侧翻译为 VkTimelineSemaphoreSubmitInfo。
    virtual void waitOnSubmit(uint64_t value) = 0;

    virtual ~IGpuTimeline() = default;
};
```

### `GpuJob` 里加 `retirePoint` 输出

```cpp
struct GpuJob {
    QueueKind                    queue;
    CommandBufferRecordFn        record;
    std::vector<GpuJobHandle>    waitOn;
    // (去掉 cpuObservable 字段 —— 所有 submit 天然可观察)
};

struct GpuJobHandle {
    uint64_t retireValue;   // 该 submit 在其 queue timeline 上的 signal value
    QueueKind queue;        // 对应哪条 timeline
};

// 查询：
bool orchestrator.isComplete(handle) {
    return timelines.get(handle.queue).completedValue() >= handle.retireValue;
}
```

### Backend 直接用 VK_SEMAPHORE_TYPE_TIMELINE

```cpp
// backend/vulkan/vulkan_timeline.hpp
class VulkanTimeline : public IGpuTimeline {
    VkSemaphore m_sem;   // type = VK_SEMAPHORE_TYPE_TIMELINE
    std::atomic<uint64_t> m_nextValue{1};
};
```

**不再有 VkFence 对象**（除了 swapchain 交互可能必需的少数例外）。**不再有 binary VkSemaphore**。

### 资源生命周期统一

所有 per-frame / per-submit / per-version 的资源都带 `RetirePoint`。回收时查对应 timeline，不查 fence。

## 8.14 一句话

**Timeline 绑定 queue / submit 进度线，资源记自己的 `retireValue`；`completedValue >= retireValue` 即可复用。`maxFramesInFlight = 3` 是 3 个 frame slot，不是 3 套 semaphore。CPU 并行靠 enkiTS，GPU 进度靠 timeline，两者互补。Pipeline barrier 管"怎么访问"，timeline 管"什么时候"，正交。**

## 下一步

- 哪些资源要 ×N / 哪些单份 / 哪些版本化 → [09 · Frame-local 资源集](09-Frame-local资源集.md)
- 回到分层架构看接口如何落地 → [06 · CPU-GPU 分层架构草案](06-CPU-GPU分层架构草案.md)
- 回到整体演进路径 → [04 · 演进路径](04-演进路径.md)
- `computeTimeline` 在 async compute 路径上的具体使用 → [async-compute 调研](../async-compute/README.md)（多 queue 机制、跨 queue ownership transfer、与 frame graph 的耦合时序）
