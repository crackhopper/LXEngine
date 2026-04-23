# Multi-threading 技术调研

> 状态：**仅记录，暂不实施**
> 最后更新：2026-04-23
> 目的：记录游戏引擎多线程架构的主流做法、书上（*Mastering Graphics Programming with Vulkan*）介绍的 enkiTS + 异步 I/O 模式、LX Engine 当前的单线程现状，以及将来要不要上、怎么分步上。

## 这个目录是什么

游戏引擎多线程是个大话题。这组文档面向 **没系统做过引擎多线程的读者**，从"为什么要、选哪种模型"讲到"LX Engine 现在到哪一步、下一步怎么走"。

顺读约 30 分钟。

## 建议阅读顺序

| # | 文档 | 讲什么 | 谁适合读 |
|---|------|-------|---------|
| 01 | [概念与选型](01-概念与选型.md) | thread-per-subsystem / task-based / fiber-based 三种架构、为什么选 task、业界案例 | 所有人 |
| 02 | [enkiTS 与异步 I/O 模式](02-enkiTS与异步IO模式.md) | enkiTS 最小 API、pinned task 双层结构、书里的异步加载模式 | 打算真正做的人 |
| 03 | [LX Engine 当前实现](03-LX当前实现.md) | 真实状态：目前基本单线程，仅 `StringTable` 做了前置线程安全 | 工程决策层 |
| 04 | [演进路径](04-演进路径.md) | 触发条件、分阶段引入方案、和 bindless / frame-graph / AI agent 的交互 | 工程决策层 |
| 05 | [enkiTS 与 Vulkan queue 的分工](05-enkiTS与Vulkan-queue的分工.md) | 两层并行 + fence/semaphore 桥梁；书里异步加载的完整数据流；timeline semaphore 补充 | 想搞懂 CPU/GPU 分工本质的人 |
| 06 | [CPU-GPU 分层架构草案](06-CPU-GPU分层架构草案.md) | LX 未来的 `ITaskScheduler` / `IGpuJobOrchestrator` / `IGpuTimeline` 接口设计 + 和 FrameGraph 的关系 | 负责设计实施的工程师 |
| 07 | [命令录制并行](07-命令录制并行.md) | 录制并行的三个层级（帧 / pass / pass 内）、Primary vs Secondary、F×T pool 池化、并行录制≠并行提交、LX 渐进实施 | 做录制并行改造的工程师 |
| 08 | [Timeline Semaphore 与资源退休模型](08-Timeline与资源退休模型.md) | **LX 选定的 GPU 同步方案**：全面 timeline，去 fence + binary；`RetirePoint` / `completedValue` 模型；四种并行里 timeline 的作用；resource versioning | 做 GPU 同步设计的工程师 |
| 09 | [Frame-local 资源集](09-Frame-local资源集.md) | 三种资源生命周期（×N / 单份 / 版本化）；`FrameSlot` 抽象；常见错分；和 bindless / pipeline-cache 的共同语言 | 设计资源生命周期的工程师 |

## TL;DR · 关键结论

**关于选型**：

- 主流游戏引擎有三种架构：**thread-per-subsystem**（老）、**task-based**（主流）、**fiber-based**（id / Naughty Dog 这种榨性能的工作室）
- **task-based 是目前最平衡的选择**：平台无关、不可中断、库成熟、调试直接
- fiber 需要显式 yield / resume，写错容易出诡异 bug
- enkiTS 是一个成熟、轻量、跨平台的 task scheduler 开源库

**关于 enkiTS + 异步 I/O 模式**：

- `TaskScheduler` + `ITaskSet::ExecuteRange` + 可选 lambda 形式就是 hello-world
- **pinned task** 是钉死在某个 thread 上的 task，不会被调度器搬走
- 书里的"异步 I/O 线程"是 **双 pinned task 结构**：一个调度哨兵（`RunPinnedTaskLoopTask`），一个工作循环（`AsynchronousLoadTask`），都钉到最后一个 worker thread

**关于 LX Engine 现状**：

- **整个引擎基本单线程**，主线程跑渲染 / scene update / 资源加载
- 唯一例外：`StringTable` 用了 `shared_mutex + atomic`，是前置的并发安全保护，为将来留接口
- **没有 scheduler、没有 worker 池、没有 task 图**
- 目前场景小、资产少，单线程够用；但这是"**还没到需要的阶段**"，不是"**不需要**"

**关于演进**：

- 不紧迫，但**值得提前规划数据结构**（尤其是共享状态的所有权）
- 入门点是**异步资源加载**（正好对接 Phase 3 的 asset pipeline）
- 渲染多线程（command buffer 并行录制）放更后面
- 早做的成本最低：现在所有状态都默认单线程访问，等到处是隐式的"主线程独占"假设就不好改了

## 按问题导航

| 我想搞懂 | 看哪 |
|---------|------|
| task-based 和 fiber 有啥本质区别？ | [01 §1.3](01-概念与选型.md#13-三种架构的本质区别) |
| Destiny / Naughty Dog / Unreal 怎么做？ | [01 §1.5](01-概念与选型.md#15-业界案例) |
| enkiTS 最少要学什么 API？ | [02 §2.1](02-enkiTS与异步IO模式.md#21-最小-api-面) |
| pinned task 是啥？为什么异步 I/O 要用它？ | [02 §2.3](02-enkiTS与异步IO模式.md#23-pinned-task异步-io-专用线程) |
| LX 现在能不能跑多线程？ | [03](03-LX当前实现.md) |
| 我们什么时候该上？怎么上？ | [04](04-演进路径.md) |
| enkiTS 的 task 和 Vulkan queue 是什么关系？ | [05](05-enkiTS与Vulkan-queue的分工.md) |
| Fence 和 Semaphore 怎么分工？ | [05 §5.3](05-enkiTS与Vulkan-queue的分工.md#53-fence-和-semaphore-的分工) |
| 我们将来的 task / GPU job 接口长什么样？ | [06](06-CPU-GPU分层架构草案.md) |
| GpuJob 和 FrameGraph 什么关系？ | [06 §6.4](06-CPU-GPU分层架构草案.md#64-和现有-framegraph-的关系) |
| 命令录制并行有几个层级？ | [07 §7.6](07-命令录制并行.md#76--录制并行的三个层级) |
| Primary 和 Secondary command buffer 怎么选？ | [07 §7.5](07-命令录制并行.md#75-primary-vs-secondary-command-buffer) |
| 为什么要 F×T 个 command pool？ | [07 §7.3](07-命令录制并行.md#73-为什么要-f--t-个-pool) |
| 并行录制和并行提交是一回事吗？ | [07 §7.12](07-命令录制并行.md#712-并行录制--并行提交) |
| 为什么 LX 直接用 timeline，不走 binary 过渡？ | [08 §8.2](08-Timeline与资源退休模型.md#82-为什么直接上-timeline不走-binary-过渡) |
| 资源怎么知道自己能不能复用？ | [08 §8.5](08-Timeline与资源退休模型.md#85-资源退休模型) |
| `maxFramesInFlight = 3` 到底意味着什么？ | [08 §8.6](08-Timeline与资源退休模型.md#86-maxframesinflight--3-的正确解读) |
| 同一张纹理被多帧读，想异步更新怎么办？ | [08 §8.9](08-Timeline与资源退休模型.md#89-资源版本化长期-reader--异步更新的标准解) |
| 哪些资源要 ×N / 哪些单份 / 哪些版本化？ | [09 §9.2](09-Frame-local资源集.md#92-三种资源生命周期) |
| FrameSlot 里该放什么？ | [09 §9.7](09-Frame-local资源集.md#97-frameslot-抽象) |
| 新资源怎么判断归类？ | [09 §9.11](09-Frame-local资源集.md#911-设计检查清单) |

## 参考

- *Mastering Graphics Programming with Vulkan* · Chapter 3 · "Unlocking Multi-Threading"
- [Destiny's Multithreaded Rendering — GDC 2015](https://www.gdcvault.com/play/1021926/Destiny-s-Multithreaded-Rendering)
- [Parallelizing the Naughty Dog Engine Using Fibers — GDC 2015](https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine)
- [enkiTS · GitHub](https://github.com/dougbinks/enkiTS)
- LX Engine 相关 spec（未来可能新增）：`openspec/specs/multi-threading/`（目前不存在）
