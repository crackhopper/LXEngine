# 05 · enkiTS 与 Vulkan queue 的分工

> 这一章回答一个读完 [02 · enkiTS 与异步 I/O 模式](02-enkiTS与异步IO模式.md) 常见的疑问：
> **"Vulkan 的 queue、fence、semaphore 和前面讲的 enkiTS task 是什么关系？"**
>
> 答案：它们解决的是 **不同层面的并行**，缺一不可，中间靠 fence/semaphore 缝合。

## 5.1 核心关系：两层并行 + 一座桥

```
┌───────────────────────────────────┐     ┌───────────────────────────────────┐
│  CPU 并行（enkiTS 管）              │     │  GPU 并行（Vulkan queue 管）        │
│                                   │     │                                   │
│  线程 0 (主)   → 渲染 + 主循环       │     │  Graphics queue → vkCmdDraw       │
│  线程 1..N-1  → 普通 worker task   │     │  Transfer queue → vkCmdCopy*      │
│  线程 N       → I/O pinned task    │     │  Compute queue  → vkCmdDispatch   │
└───────────────┬───────────────────┘     └────────────────┬──────────────────┘
                │                                          │
                └──────────────┬───────────────────────────┘
                               ▼
                        Fence / Semaphore
                    （CPU 和 GPU 的同步桥梁）
```

| 层次 | 并行的是什么 | 单位 | 谁管 |
|------|-----------|-----|------|
| **上层** | CPU 线程间 | enkiTS task / pinned task | enkiTS |
| **下层** | GPU 硬件单元间（3D 引擎 / DMA / compute 引擎） | Vulkan queue | Vulkan |
| **桥梁** | 两层之间的完成通知 | Fence / Semaphore | Vulkan |

一句话：**enkiTS 让 CPU 不卡、Vulkan queue 让 GPU 并行、fence/semaphore 让两边互相知道进度**。

## 5.2 为什么都需要？缺一不可

### 假设只用 enkiTS、不用 transfer queue

- ✅ I/O 线程读盘、stbi 解码、memcpy 到 staging buffer 都能不卡主线程
- ❌ 但 `vkQueueSubmit` 把 copy 命令提交到了 **主 queue**
- ❌ GPU 上 copy 命令和 `vkCmdDraw` 就 **串行了** —— CPU 侧省下的好处被 GPU 侧拖平
- ❌ 而且 copy 走的是 graphics queue 对应的 3D 引擎，没利用上 DMA 专用硬件

### 假设只用 transfer queue、不用 enkiTS

- ✅ GPU 上 copy 和 render 可以并行
- ❌ 但 I/O 读盘、stbi 解码仍然阻塞主线程 —— 主线程几百 ms 冻帧
- ❌ `vkQueueSubmit` 也是主线程调的，堆积久了 submit 本身也有延迟

**两层都要，才是真·异步加载**。

## 5.3 Fence 和 Semaphore 的分工

这两个同步原语经常被混起来说，其实分工很清晰：

| 同步原语 | 方向 | 谁等谁 | 书里的例子 |
|---------|-----|-------|----------|
| **Fence** | **CPU ↔ GPU** | **CPU** 等 **GPU** 做完一批活 | `transfer_fence`：CPU 的 I/O 线程要知道"上一次 copy 做完了吗，我能复用 staging buffer 吗" |
| **Semaphore** | **GPU ↔ GPU** | **Graphics queue** 等 **Transfer queue** | `transfer_complete_semaphore`：Graphics queue 采样之前要等"transfer queue 的 copy 真的完成了" |

### 为什么两个都要？

- CPU 线程 **必须** 用 fence 才能知道 GPU 的进度（CPU 看不见 GPU 的执行状态）
- 两个 queue 之间用 fence 会低效（绕道 CPU），用 semaphore 是"GPU 内部握手"，不经过 CPU

### 现代补充：Timeline Semaphore

Vulkan 1.2+ 的 **timeline semaphore** 统一了 fence + binary semaphore：一个对象带单调递增的 value，CPU 和 GPU 都可以 wait/signal。

```cpp
VkSemaphore timelineSem;
vkSignalSemaphore(dev, {timelineSem, 42});             // GPU 做完就 signal 到 42
vkWaitSemaphoreKHR(dev, {timelineSem, 42}, timeout);   // CPU 等 value 达到 42
```

现代 AAA 引擎（Frostbite / UE5）都在往 timeline 迁。好处：

- 一个对象代替 fence + semaphore，简化对象池管理
- 一次 submit 可以 wait/signal 多个 value，依赖图表达更自然
- CPU 侧不用为"等哪个 fence"写分叉代码

LX 未来的设计应该 **按 timeline 的心智模型来抽象**（见 [06 · CPU-GPU 分层架构草案](06-CPU-GPU分层架构草案.md)）。

> ⚠️ **方向更新（2026-04-23）**：LX 最新决定是 **第一版就直接全面使用 timeline semaphore，完全去 fence + binary semaphore**。详见 [08 · Timeline Semaphore 与资源退休模型](08-Timeline与资源退休模型.md)。本节上面的 "Fence vs Semaphore 分工" 是书本（Vulkan 1.0）模型，仅作背景介绍；LX 不再走这条路。

## 5.4 完整数据流：两节合起来做的事

```
主线程                I/O pinned task（enkiTS）          Transfer queue (GPU)          Graphics queue (GPU)
─────────────────     ──────────────────────────        ────────────────────────      ────────────────────────
投递加载请求 ─────>   从队列取一个请求
                      vkGetFenceStatus(transfer_fence)
                          └ 上一轮没完？→ return
                      vkResetFences(transfer_fence)
                      stbi_load(path)  ← 阻塞 I/O
                      memcpy → staging_buffer
                      cb->begin()
                      vkCmdCopyBufferToImage
                      vkCmdPipelineBarrier（layout transition）
                      vkCmdPipelineBarrier（queue ownership release）
                      cb->end()
                      vkQueueSubmit(transfer_queue,
                         signal=transfer_complete_semaphore,
                         fence=transfer_fence)  ─────────> DMA 引擎在这跑 copy
                                                          完成时：
                                                          - 给 transfer_fence 打 signal（给 CPU 看）
                                                          - 给 semaphore 打 signal（给 graphics queue 看）
                      把纹理 handle 加到"完成队列"
                      （mutex 保护的 vector）

下一帧开始：
从"完成队列"
pop 已完成纹理
插入 queue ownership
acquire barrier +
layout 改成 SHADER_READ
vkQueueSubmit(graphics_queue,
   wait=transfer_complete_semaphore)  ────────────────────────────────────────> 采样新纹理
```

看清楚这张图就能体会到：**书里两节讲的不是两件事，是一件事**。

1. **enkiTS** 提供"不卡主线程的 CPU 执行上下文"
2. **Transfer queue** 提供"不卡主渲染的 GPU 执行上下文"
3. **Fence / Semaphore** 把两个上下文缝合起来

## 5.5 书里没明说、但很关键的四个细节

### (a) `vkQueueSubmit` 是"单 queue 单线程"约束的

Vulkan 规范要求：**同一个 `VkQueue` 不能被多个线程同时提交**。所以：

- 如果 I/O 线程独占 transfer queue，它 submit 不用加锁
- 如果 I/O 线程和主线程都想提交到同一个 graphics queue（没 transfer queue 的 GPU），就得加 mutex

这是 pinned task 为什么要钉住线程的另一个理由 —— 不光是 I/O 阻塞不能挪动，**它手里的 queue 也不能跨线程乱用**。

### (b) Command pool 必须"一线程一 pool"

书里给 transfer queue 专门创建了 command pool。但 Vulkan 的更硬约束是：**一个 command pool 同时只能被一个线程用**。

所以 I/O 线程必须有 **自己的** command pool，和主线程的 command pool 彻底分开。这是"**每线程一 pool**" 模式的起点，将来上 command buffer 并行录制（[04 · 演进路径](04-演进路径.md) 的阶段 3），每个 worker 线程都要有自己的 pool。

### (c) Queue family ownership transfer —— Vulkan 独特的一道坎

书里 `srcQueueFamilyIndex = transferQueue` / `dstQueueFamilyIndex = graphicsQueue` 那段 barrier 是 Vulkan 独特的一道坎：不同 queue family 背后可能是不同的硬件缓存 / 内存区域。image 在 transfer queue 写完，GPU 里 **另一块缓存** 可能还没同步。

显式做 ownership transfer 告诉驱动"请确保这个 image 在 graphics queue 能正确读到"。

这和 CPU 线程的 `memory_order_*` 是同一思路 —— 两个执行上下文共享数据时，必须显式同步缓存 / 可见性。

### (d) Staging buffer 为什么一定要在这个架构里

`VK_BUFFER_USAGE_TRANSFER_SRC_BIT` + `VMA_ALLOCATION_CREATE_MAPPED_BIT` 的 64 MB staging buffer 是整个异步加载的 **关键中转站**：

- **CPU 侧**：`memcpy` 把解码后的像素写进去（走 CPU，到 host memory）
- **GPU 侧**：transfer queue 的 DMA 引擎从这里 copy 到 device local 的 `VkImage`（走 GPU）

没有 staging buffer，你就得用 `HOST_VISIBLE` 的 image（性能差）或者让 CPU 直接写 device local（做不到）。所以 staging buffer 是 **"让 CPU 和 GPU 各做自己擅长的一半"** 的前提。

## 5.6 关系速查表

| 问题 | 答案 |
|------|------|
| enkiTS task 的 worker thread 能直接调 Vulkan API 吗？ | 能，但要守 Vulkan 的线程规则（见 5.5） |
| 一个 task 能提交到多个 queue 吗？ | 能，但每个 queue 一次 submit，且要自己管 semaphore |
| 不同 queue 的 submit 谁先谁后？ | 由 semaphore 依赖表达，不由 submit 顺序决定 |
| Fence 和 Semaphore 能互换吗？ | 不能（传统模型）；能（timeline semaphore） |
| CPU task 完成 ≠ GPU 工作完成吧？ | 对！CPU task submit 完就结束，GPU 工作靠 fence 观察 |
| 没有 transfer queue 的硬件怎么办？ | 用 graphics queue 做 copy（要加锁），或接受异步上限降低 |

## 5.7 心智模型

读完这章后最该记住的三条：

1. **CPU task 和 GPU submission 不是一个东西** —— CPU task body 结束 ≠ GPU 做完了，fence 才能告诉 CPU "真完了"
2. **Vulkan queue 是 GPU 的"执行上下文"，enkiTS thread 是 CPU 的"执行上下文"** —— 对称但独立
3. **Fence/Semaphore 是两种上下文之间的唯一通信渠道** —— 不通过它们，两边互不知道对方进度

这套心智模型也是下一篇 [06 · CPU-GPU 分层架构草案](06-CPU-GPU分层架构草案.md) 的起点。

## 下一步

- 看具体的分层架构设计 → [06 · CPU-GPU 分层架构草案](06-CPU-GPU分层架构草案.md)
- 回到演进计划 → [04 · 演进路径](04-演进路径.md)
