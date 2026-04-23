# 09 · Frame-local 资源集

> 这一章回答一个看起来简单但实际非常容易搞错的问题：
> **哪些资源需要按 `maxFramesInFlight` 复制 N 份？哪些单份就够？哪些需要按版本复制？**
>
> 前置阅读：[08 · Timeline Semaphore 与资源退休模型](08-Timeline与资源退休模型.md)（`RetirePoint` / `completedValue` 模型）。

## 9.1 为什么需要 Frame-local

核心矛盾：**CPU 想尽快录下一帧，GPU 可能还在用上一帧的资源**。

```
Frame N-2 : GPU 正在执行
Frame N-1 : 已提交，等 present
Frame N   : CPU 正在录制     ← 想写入某些资源
Frame N+1 : CPU 在做准备      ← 想读某些资源
```

如果 CPU 写入的资源 GPU 还在读，就会出现 **读写冲突 / data race**。

解法有三类，对应三种资源生命周期。

## 9.2 三种资源生命周期

不是所有资源都一样对待。按"**是否需要每帧独立副本**"分成三类：

| 类别 | 特征 | 处理方式 | 示例 |
|------|------|---------|------|
| **A. Per-frame ×N** | CPU 每帧重写、GPU 可能还在读上一帧版本 | 复制 N 份（N = `maxFramesInFlight`），轮转使用 | command pool、descriptor arena、transient UBO |
| **B. 单份长期持有** | 写入一次后长期只读 | 一份即可，无并发冲突 | 静态纹理、mesh、pipeline、shader |
| **C. 按版本多版本化** | 长期被读，又需要异步更新 | resource versioning（不按帧数，按写入事件） | streaming 纹理、hot reload material、动态 buffer |

**误区**：以为 "有 3 帧在飞，所有资源都要 ×3"。错。大部分资源根本不需要。

## 9.3 A 类：per-frame ×N（真正的 frame-local）

这些资源 **每帧都要被 CPU 重写**，且上一帧的版本 **可能还在 GPU 上被读**。必须按帧数复制 N 份：

| 资源 | 为什么要 ×N |
|------|-----------|
| **Command pool / command buffer** | 下一帧要重新录制，上一帧的 cb 可能还在 GPU 执行 |
| **Descriptor allocator / arena** | 下一帧要 alloc 新 descriptor，上一帧的还被绑定着 |
| **Transient uniform / per-frame UBO** | 相机矩阵、视图参数每帧变，shader 可能还在读上一帧 |
| **Upload / staging 内存池** | 连续往 GPU 传东西，staging 块不能立刻复用 |
| **Submit metadata** | 记录本帧 submit 了什么、signal 了哪些 value |
| **Pass packet / draw packet 容器** | 本帧的 draw list / pass config 缓冲 |
| **Swapchain 同步对象**（imageAvailable） | 严格跟随 swapchain image 轮转 |

### 判断标准

符合 **全部三条** 的就是 A 类：

1. CPU 在本帧开始时**会覆盖写**这个资源
2. GPU 对这个资源的读/写 **跨帧在飞**
3. 没有自己的 "版本链" 结构

反例：

- 静态纹理虽然 GPU 在读，但 CPU 没在写，**不是 A 类**（B 类）
- 动态更新的 VAR 纹理，有自己的 `VersionedTexture`，**不是 A 类**（C 类）

## 9.4 B 类：单份长期持有

一次创建、长期只读 —— 没有并发写冲突，单份就够：

| 资源 | 说明 |
|------|------|
| **静态纹理** | 加载一次，全生命周期被采样 |
| **静态 mesh buffer（vertex / index）** | 场景加载完不变 |
| **Shader module / pipeline** | 编译一次，反复使用 |
| **长生命周期 material 资源** | 游戏运行期内不变 |
| **长生命周期 render target 定义** | 分辨率不变就不用动 |
| **Bindless descriptor set 本身** | 设置表的 **对象本身** 是单份；里面 **slot 的更新** 另说（见 [9.6](#96-和-bindless-的交互)）|

这些资源**完全不走 frame slot 机制**。初始化一次，退出时销毁。

### 注意：bindless descriptor **set** 是单份，但 **slot 更新** 需要 RetirePoint

Bindless 的大 descriptor set 是 B 类。但 "我要往 slot 100 写新纹理"、"我要释放 slot 50" 这些 **slot 级更新** 仍然要按 [08 §8.5](08-Timeline与资源退休模型.md#85-资源退休模型) 的 `RetirePoint` 模型，见 bindless-texture 研究的同步策略。

## 9.5 C 类：按版本多版本化

这是 A 类和 B 类之外的"**长期被读取、又需要异步更新**"的情况。

### 为什么不能用 A 类（×N）？

因为 N 不够。你可能：

- 静态读几百帧，突然加载完新版本
- 按内容哈希而不是帧号识别"哪个版本"
- 老版本可能还要保留几帧供回退

### 为什么不能用 B 类（单份）？

因为要写入。原地覆盖会和读者冲突。

### 解法：资源版本化（resource versioning）

```cpp
struct VersionedTexture {
    struct Version {
        TextureHandle handle;
        RetirePoint   retireAt;   // 这个版本何时可被释放
    };
    std::vector<Version> versions;     // 历史版本
    size_t               latestReady;  // 当前能被读的最新版本

    TextureHandle currentForRead(const GpuTimelines& tl) const;
    void          publishNew(TextureHandle newH, RetirePoint writeFinish);
    void          reclaim(const GpuTimelines& tl);   // 清退老版本
};
```

典型流程：

```
1. Transfer queue 写 v2（新版本）→ signal transferTimeline@T
2. v2 的 retireAt = { transferTimeline, T }
3. Graphics 在下一帧 compile 时检查：v2 是否 completedValue >= T？
4. 是 → latestReady = v2，新帧开始采样 v2
5. 老版本 v1：等 graphicsTimeline 不再用它，回收
```

### 适用场景

| 场景 | 为什么 C 类 |
|------|----------|
| 异步 streaming 纹理 | 加载频率低，但加载时机不定 |
| Hot reload material / texture | 开发期，不按帧触发 |
| 动态 buffer（大场景的 scene buffer） | 内容变化节奏远慢于帧 |
| LOD 切换 | 不同分辨率版本需要共存 |

## 9.6 和 bindless 的交互

Bindless 引入后，A 类和 C 类会跟 bindless 的 slot 管理深度交互：

```
bindless descriptor set        (B 类：单份长期持有)
│
└── 内部 slot 数组 65536
    ├── slot 0..99     (持续使用)
    ├── slot 100..200  (上一帧写的，挂 graphicsTimeline@101)
    ├── slot 201..300  (这一帧写的，挂 graphicsTimeline@102)
    └── slot 301+      (空)
```

Slot 本身不属于 A / B / C 任何一类，它是一个 **"基于 RetirePoint 的 per-slot 生命周期"**。Bindless 的 "append-only + 延迟回收" 策略（参见 `../bindless-texture/02-实现细节.md`）实质上就是这一章的 `RetirePoint` 模型，只是粒度到单个 slot。

## 9.7 FrameSlot 抽象

A 类资源全部聚在一个 `FrameSlot` 里：

```cpp
// core/gpu/frame_slot.hpp
struct FrameSlot {
    // === A 类资源集合 ===
    std::vector<VkCommandPool>       cmdPoolsPerThread;  // F × T 的 T 维度
    DescriptorArena                  descriptorArena;
    TransientBufferPool              uniformArena;
    StagingPool                      stagingArena;
    std::vector<SubmissionMeta>      submissions;        // 本 slot 本轮 submit 记录
    PassPacketPool                   passPackets;
    DrawPacketPool                   drawPackets;

    // === 退休标记 ===
    RetirePoint   graphicsRetireAt;   // 本 slot 上一轮 graphics submit 的 value
    RetirePoint   presentRetireAt;    // present 完成点
    // transfer 不单独挂，因为 transfer 的 staging 通常有自己的独立 slot / ring

    // === 状态 ===
    enum State { Free, Recording, Submitted, Presented } state;
};
```

### 新帧开始的流程

```cpp
FrameSlot* FrameSlotManager::acquireNextSlot(GpuTimelines& tl) {
    // 轮转找一个已退休的 slot
    for (auto& slot : m_slots) {
        if (slot.state == FrameSlot::Free) return &slot;
        if (tl.graphics.completedValue() >= slot.graphicsRetireAt.value
         && tl.present.completedValue()  >= slot.presentRetireAt.value) {
            slot.reset();                 // reset pool / arena / packets
            slot.state = FrameSlot::Recording;
            return &slot;
        }
    }
    // 所有 slot 都在飞：CPU 必须等
    waitForAnySlot(tl);
    return acquireNextSlot(tl);
}
```

### 一帧结束时

```cpp
void endFrame(FrameSlot& slot, GpuTimelines& tl) {
    uint64_t graphicsValue = tl.graphics.nextSignalValue();
    submitAllPendingJobs(slot, graphicsValue);
    slot.graphicsRetireAt = { Graphics, graphicsValue };

    uint64_t presentValue = tl.present.nextSignalValue();
    present(slot, presentValue);
    slot.presentRetireAt = { Present, presentValue };

    slot.state = FrameSlot::Submitted;
}
```

`reset()` 的操作是 A 类资源统一的：

```cpp
void FrameSlot::reset() {
    for (auto& pool : cmdPoolsPerThread)
        vkResetCommandPool(device, pool, 0);
    descriptorArena.reset();
    uniformArena.reset();
    stagingArena.reset();
    submissions.clear();
    passPackets.clear();
    drawPackets.clear();
    state = Recording;
}
```

**reset 比 destroy/recreate 快 10~100 倍** —— 驱动和 allocator 都能复用底层内存。

## 9.8 典型容量参数

```
maxFramesInFlight = 3              // FrameSlot 数量
maxRecordingThreads = hardware_concurrency() - 2
cmdPoolsPerThread = T              // F × T 的 T 维度

descriptorArena 单 slot = 4 MB     // 够一帧的 draw call 用
uniformArena 单 slot = 16 MB
stagingArena 单 slot = 64 MB       // 够异步加载塞
passPackets / drawPackets = 按场景估

swapchain images = 通常 = maxFramesInFlight，某些场景设成 +1
```

**总内存开销**： `3 × (4 + 16 + 64) ≈ 252 MB` 每 slot 类型，桌面端完全能接受；移动端要权衡。

## 9.9 什么东西容易被错分

实战里最容易搞错的几个：

| 资源 | 容易被错分到 | 实际应该 |
|------|-----------|--------|
| 静态 scene buffer | A 类（以为要 ×N） | **B 类**，写一次就行 |
| 动态 scene buffer（大场景） | B 类（以为单份够） | **C 类**，版本化 |
| Swapchain image | A 类 | **由 swapchain 独立管**，和 frame slot 数可能不等 |
| 大纹理的 mip 级 streaming | A 类 | **C 类**，按 mip 粒度版本化 |
| Bindless descriptor set 整体 | A 类 | **B 类**，set 本身一份；slot 是独立生命周期 |
| Pipeline cache（见 `../pipeline-cache/`）| A 类 | **B 类**，持续累积的查表，不按帧复制 |

## 9.10 和 bindless / pipeline-cache 的呼应

三份 research 在资源复用上形成统一模型：

| Research | 资源类型 | 复用策略 |
|---------|---------|--------|
| [bindless-texture](../bindless-texture/) | Bindless descriptor set 的 slot | append-only + RetirePoint 回收（等效 C 类） |
| [pipeline-cache](../pipeline-cache/) | VulkanPipeline 对象 | B 类（长期持有，按 key 查表） |
| [multi-threading](README.md) | Frame-local 资源 | A 类（per-frame ×N） |
| 所有 | 资源版本化场景 | C 类（versioning） |

**共同语言**：`RetirePoint { queue, value }` + `completedValue >= retireValue`（见 [08](08-Timeline与资源退休模型.md)）。

## 9.11 设计检查清单

新增任何需要"跨帧生命周期"的资源时，按这个顺序判断：

```
Q1: 这个资源 CPU 会每帧覆盖写吗？
    ├── 否 → 不是 A 类
    └── 是 → Q2

Q2: GPU 读它的节奏跟得上 CPU 写的节奏吗？
    ├── 是（一帧内都完成）→ 不需要 ×N，设单份 + 合理 barrier
    └── 否（跨帧在飞）→ A 类，放进 FrameSlot ×N

Q1 否的情况：
    Q3: 它会被异步更新吗（不按帧触发）？
        ├── 否 → B 类，单份
        └── 是 → C 类，resource versioning

Q2 是但 Q1 仍否：
    → 不需要 frame-local 机制，用普通资源生命周期
```

## 9.12 对当前 LX Engine 的现状对照

LX 当前渲染单线程，所有资源实际上 **都是 B 类单份** —— 包括 `VulkanResourceManager`、`PipelineCache`、`CommandBufferManager`。

将来演进时：

| 现有对象 | 将来的归类 |
|---------|---------|
| `VulkanResourceManager` | B 类（注册表）+ slot 生命周期 |
| `CommandBufferManager`（当前按 frame 分 pool） | **扩成 FrameSlot 的一部分**，按 F × T 分 pool |
| `PipelineCache` | B 类（对象缓存），不动 |
| `MaterialInstance` | 看用法 —— 静态材质是 B，动态材质可能 C |
| 未来的 transient UBO / staging | A 类，进 FrameSlot |

改造的重点是 **把 `CommandBufferManager` 扩展为 `FrameSlot`**，其他资源按新增需求按类别归位。

## 9.13 一句话

**按"CPU 每帧是否会覆盖写"+ "GPU 是否跨帧还在用"判断：都是 → A 类 per-frame ×N；只有 GPU 长期读 → B 类单份；长期读 + 异步更新 → C 类版本化。`FrameSlot` 是 A 类资源的统一容器，用 `RetirePoint` 而不是 fence 观察何时可复用。**

## 下一步

- 回到 timeline 模型看 RetirePoint 怎么工作 → [08 · Timeline Semaphore 与资源退休模型](08-Timeline与资源退休模型.md)
- 回到分层架构看 FrameSlot 在哪一层 → [06 · CPU-GPU 分层架构草案](06-CPU-GPU分层架构草案.md)
- 回到演进路径 → [04 · 演进路径](04-演进路径.md)
