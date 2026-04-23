# 02 · enkiTS 与异步 I/O 模式

> 这一章展示 enkiTS 的最小 API、pinned task 机制，以及书里的"异步 I/O 线程"模式。
> 代码引自 *Mastering Graphics Programming with Vulkan*（第 3 章 Unlocking Multi-Threading）。

## 2.1 最小 API 面

### 初始化 scheduler

```cpp
enki::TaskSchedulerConfig config;
config.numTaskThreadsToCreate = 4;              // 额外创建 4 个 worker 线程

enki::TaskScheduler task_scheduler;
task_scheduler.Initialize(config);
```

一句话：**给我 4 个 worker 线程，加上主线程（0 号）共 5 个执行上下文**。

### 定义 task：继承式

```cpp
struct ParallelTaskSet : enki::ITaskSet {
    void ExecuteRange(enki::TaskSetPartition range_,
                      uint32_t threadnum_) override {
        // 在 threadnum_ 这个线程上执行工作
        // range_ 是 [start, end)，用于把一大块工作切片并行跑
    }
};

int main() {
    enki::TaskScheduler scheduler;
    scheduler.Initialize(config);

    ParallelTaskSet task;
    scheduler.AddTaskSetToPipe(&task);   // 投递到队列
    scheduler.WaitforTask(&task);        // 阻塞等它完成（可能在本线程协助执行）
}
```

### 定义 task：lambda 式

```cpp
enki::TaskSet task(1, [](enki::TaskSetPartition range_, uint32_t threadnum_) {
    // 同样的 body，写起来更紧凑
});
scheduler.AddTaskSetToPipe(&task);
```

两种写法 **功能完全等价**。继承式适合 task 要持有状态的场景，lambda 式适合一次性用。

### 常用能力

- **`AddTaskSetToPipe`**：投递任务到全局队列，由 scheduler 派发给空闲 worker
- **`WaitforTask`**：阻塞等待某个 task 完成，同时本线程可能协助执行其他 task
- **Task 依赖**：通过 `AddDependency(&parent, &child)` 声明"child 必须在 parent 后跑"
- **子 task 生成**：运行中的 task 可以再调 `AddTaskSetToPipe` 投更多任务 —— 这些子 task 进当前 worker 的**本地队列**，其他 worker 可以偷（即 work-stealing queue，见 [01 §1.6](01-概念与选型.md#16-work-stealing-queue--task-调度的关键机制)）

### 关键约束

- **TaskSet 对象必须在完成前保持存活**（scheduler 只持有指针）
- **Task body 里不能抛异常出 task 边界**（enkiTS 不捕获）
- **WaitforTask 是协作式的**：调用线程会协助跑其他 task，而不是 OS 层面的 sleep

## 2.2 异步 I/O 为什么不能用普通 task

回顾 [01 §1.3](01-概念与选型.md#13-三种架构的本质区别) 的关键结论：**task 不可中断**。

这意味着如果你写：

```cpp
enki::TaskSet loadTask(1, [](auto range, uint32_t thread) {
    auto bytes = readFileFromDisk("big_texture.ktx");  // ← 阻塞几毫秒到几百毫秒
    uploadToGPU(bytes);
});
```

这个 task **会霸占一个 worker thread** 直到 I/O 完成 —— worker 被钉住、work-stealing 也救不了它，其他 CPU 密集型 task 无法插队。如果同时有 N 个 I/O 任务，可能**把所有 worker 都堵死**，主线程 render 也挂了。

fiber-based 系统遇到这个问题可以 yield；task-based 没法 yield，所以必须走另一条路：**把 I/O 任务钉到"不会被普通 task 用的专用线程"上**。

这就是 **pinned task** 的用武之地。

## 2.3 Pinned task：异步 I/O 专用线程

### Pinned task 是什么

- **普通 task**：调度器自由决定在哪个 worker 上跑
- **Pinned task**：**钉死在特定 threadNum 上**，调度器不会把它挪走，也不会把别的普通 task 挪到这个线程

通过 `threadNum` 字段声明：

```cpp
struct MyPinnedTask : enki::IPinnedTask {
    void Execute() override { /* ... */ }
};

MyPinnedTask task;
task.threadNum = scheduler.GetNumTaskThreads() - 1;  // 钉到最后一个 worker
scheduler.AddPinnedTask(&task);
```

### 书里的"双 pinned task" 结构

书里做异步加载用了一个精巧的双层结构：

```cpp
// 层 1：调度哨兵 —— 常驻循环，等新 pinned task 并执行
struct RunPinnedTaskLoopTask : enki::IPinnedTask {
    void Execute() override {
        while (task_scheduler->GetIsRunning() && execute) {
            task_scheduler->WaitForNewPinnedTasks();  // 阻塞等 pinned 任务
            task_scheduler->RunPinnedTasks();         // 执行它们
        }
    }
    enki::TaskScheduler* task_scheduler;
    bool execute = true;
};

// 层 2：实际工作 —— 一个死循环，持续处理 I/O 请求队列
struct AsynchronousLoadTask : enki::IPinnedTask {
    void Execute() override {
        while (execute) {
            async_loader->update();  // 检查加载队列、上传 staging、通知完成
        }
    }
    AsynchronousLoader* async_loader;
    bool execute = true;
};
```

挂接方式：两个 task 都钉到**同一个线程**（最后一个 worker）：

```cpp
// 先挂调度哨兵
RunPinnedTaskLoopTask run_pinned_task;
run_pinned_task.threadNum = task_scheduler.GetNumTaskThreads() - 1;
task_scheduler.AddPinnedTask(&run_pinned_task);

// 再挂工作循环（钉到同一个线程）
AsynchronousLoadTask async_load_task;
async_load_task.threadNum = run_pinned_task.threadNum;
task_scheduler.AddPinnedTask(&async_load_task);
```

### 为什么要两层

单看有点冗余，其实两个 task 分工不同：

| 角色 | 职责 | 生命周期 |
|------|------|---------|
| `RunPinnedTaskLoopTask` | **调度哨兵**：`WaitForNewPinnedTasks` + `RunPinnedTasks`，保证这条线程处理所有投递进来的 pinned 任务 | 应用启动到关闭 |
| `AsynchronousLoadTask` | **工作循环**：死循环检查自定义加载请求队列 | 应用启动到关闭（通过 `execute = false` 优雅退出） |

好处：

1. **哨兵让这条线程"听得见"**：将来可以往同一线程投递其他一次性 pinned task（比如 shader 编译、mesh 预处理），不只是加载
2. **工作循环让这条线程"停不下来"**：I/O 队列空了也不睡死，一旦有请求立刻响应
3. **优雅停机点清晰**：`execute = false` 让循环退出，scheduler `WaitforAll` 清场

## 2.4 为什么不直接用 `std::thread`

这是读完这个模式后很自然的疑问：**既然是个专用线程，写个 `std::thread` 不就行了？**

差异在于**与 task 图的统一管理**：

| 维度 | 裸 `std::thread` | enkiTS pinned task |
|------|----------------|-------------------|
| 线程生命周期管理 | 手写 `join` / condition variable | scheduler 统一管 |
| 和其他 task 表达依赖 | 要自己搞 promise/future 桥接 | 直接用 task dependency |
| 关闭流程 | 手写原子标志 + join | `execute = false` + `WaitforAll` |
| 可观测性 | 要自己加 profile 探针 | scheduler 已有 hook |
| 将来扩展 | 每个新线程都要重新写一套 | 同一套 pinned task 模式 |

裸线程做得出来，但**和引擎整体调度分裂**。pinned task 是"I/O 线程是 task 图里的一个正常节点"，这是工程上的价值。

## 2.5 书里没讲、但真正做会需要的

这一节就讲到这里结束，留了一个 cliffhanger 给 Vulkan queue。但真正把异步加载做出来还需要：

1. **独立 transfer queue**：Vulkan 的 `VK_QUEUE_TRANSFER_BIT` queue 可以和 graphics queue 并行做 `vkCmdCopyBufferToImage`，不占图形队列
2. **双缓冲 staging buffer**：CPU 往一块写时 GPU 在另一块读
3. **完成信号**：`VkSemaphore` / `VkFence` 告诉 main 线程"第 N 张贴图好了"
4. **资源可见时机**：texture 上传完 ≠ shader 可以采 —— 还要 memory barrier 做 layout transition；如果用 bindless，还要配合 `UPDATE_AFTER_BIND` + 帧起始 flush 策略（参考 [bindless-texture/02 §2.7](../bindless-texture/02-实现细节.md#27-同步与更新策略)）
5. **跨线程生命周期**：被加载的纹理不能在完成通知回 main 线程之前就被引用，否则引用 ID 悬空

这些都是 **enkiTS 不解决**的部分 —— enkiTS 只给你一条专用线程，具体怎么用好它是引擎自己的事。

## 2.6 Worker 线程的一般约束

除了 I/O 专用线程，普通 worker 线程还有一些必须遵守的规矩：

- **不能碰 OS 事件**：SDL 事件循环必须在主线程
- **不能直接提交到 swapchain**：`vkQueuePresentKHR` 通常限定在主线程
- **Vulkan queue 的外部同步**：一个 queue 不能被多个线程并发提交，要么加锁要么限定主线程
- **共享数据的所有权要明确**：task A 读某个对象、task B 同时写，没有锁就是 data race

这些是 task 架构之外的约束，enkiTS 不管，引擎要自己守。

## 下一步

- 看 LX 现状 → [03 · LX Engine 当前实现](03-LX当前实现.md)
- 看演进路径 → [04 · 演进路径](04-演进路径.md)
