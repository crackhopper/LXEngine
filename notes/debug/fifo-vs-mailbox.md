# Vulkan Present Mode：FIFO vs MAILBOX，以及为何 FIFO 在 NVIDIA Optimus 上 resize 黑屏

本文是 [2026-05 demo_scene_viewer resize 偶发黑屏复盘](2026-05-resize-blackscreen.md) 的衍生技术解释。聚焦"为什么把默认 present mode 从 `VK_PRESENT_MODE_FIFO_KHR` 切到 `VK_PRESENT_MODE_MAILBOX_KHR` 救了 dGPU 路径"——目的是让后续读者不用回头读完整复盘也能理解机理。

引擎当前默认行为（`VulkanSwapchain::createInternal`）：

> **MAILBOX 优先；查询 surface 不支持 MAILBOX 时 fallback 到 FIFO。**

---

## 1. 两种 present mode 在做什么

### `VK_PRESENT_MODE_FIFO_KHR`

**所有 Vulkan 实现必须支持。** 模型上是一个严格的 FIFO 队列：

```
应用                                    显示器 (vsync 节拍)
 │  acquire image i                       │
 │  render to image i                     │
 │  vkQueuePresentKHR(image i)            │
 │   └→ image i 入队 ─────────────────────┐│
 │  acquire image j                       ││
 │  render to image j                     ││
 │  vkQueuePresentKHR(image j)            ││
 │   └→ image j 入队 ─────────────────────┐│
 │  acquire image k                       ││
 │   └→ 队列满 → 阻塞等待               │
 │                                        │
 │                           vsync ──────→│ 取队首 image i 显示
 │   └→ image i 释放 → acquire 解锁       │
 │                           vsync ──────→│ 取队首 image j 显示
 │                                ...
```

关键性质：

- 显示器**严格按 vsync 节拍**消费队列里的下一帧
- 队列满（通常 = swapchainImageCount - 1）时应用 acquire 会**阻塞**
- **不撕裂**（vsync 同步）
- **不丢帧**（每个提交的帧都会被显示）
- 帧率上限 = 显示器刷新率（60Hz 屏 → ≤ 60fps）
- 输入延迟较高（队列堆积一帧 = 一次 vsync 间隔的延迟）

### `VK_PRESENT_MODE_MAILBOX_KHR`

**可选支持**——驱动可以不暴露。模型上是一个 1 槽位的"邮箱"，不是队列：

```
应用                                    显示器 (vsync 节拍)
 │  acquire image i                       │
 │  render to image i                     │
 │  vkQueuePresentKHR(image i)            │
 │   └→ 邮箱 = image i ───────────────────┐│
 │  acquire image j (不阻塞)              ││
 │  render to image j                     ││
 │  vkQueuePresentKHR(image j)            ││
 │   └→ 邮箱 = image j；image i 被丢弃 ──┐│
 │                            vsync ─────→│ 取邮箱里的 image j 显示
 │  acquire image k                       │
 │  render to image k                     │
 │  vkQueuePresentKHR(image k)            │
 │   └→ 邮箱 = image k                    │
 │                            vsync ─────→│ 取邮箱里的 image k 显示
```

关键性质：

- 显示器仍按 vsync 节拍消费，但取的是**邮箱里最新那一帧**
- 应用 `vkQueuePresentKHR` **不阻塞**——新帧到达时**替换**邮箱里旧的未显示帧（旧帧丢弃）
- **不撕裂**（仍 vsync 同步）
- **会丢帧**（被替换的旧帧被扔掉）
- 帧率**可以超过** vsync（应用持续渲染，driver 内部丢）
- 输入延迟较低（显示总是最新到达的）

---

## 2. 普通（单 GPU 桌面）场景下的取舍

| | FIFO | MAILBOX |
|---|---|---|
| 规范要求 | 必须支持 | 可选 |
| 帧率 | 锁 vsync | 可超 vsync |
| 撕裂 | 不会 | 不会 |
| 丢帧 | 不会 | 会 |
| 输入延迟 | 高一帧 | 低 |
| 耗电 | 低（按 vsync 节拍）| 略高（持续渲染、丢帧）|

普通桌面上两种都能稳定工作，纯粹是延迟/耗电权衡：游戏 / 编辑器 / 高交互应用偏好 MAILBOX；视频播放 / 后台渲染偏好 FIFO 或 FIFO_RELAXED。

**dGPU + Optimus 笔记本不是普通场景。** 下一节展开。

---

## 3. NVIDIA Optimus 笔记本的 PRIME 路径为什么改变了游戏规则

### 3.1 物理链路

笔记本的物理显示器**只接在 Intel iGPU 上**——这是 Optimus 笔记本的标准设计，dGPU 没有自己的显示输出。所以"用 dGPU 渲染、显示在屏幕上"实际经过这条链：

```
应用 vkQueuePresentKHR
    │
    ↓ NVIDIA dGPU 上的 swapchain image
    │
    ↓ NVIDIA driver 内部 cross-GPU PRIME copy
    │   └─ 把 dGPU 的 image 内容复制到 iGPU 共享内存
    │
    ↓ Intel iGPU 共享内存
    │
    ↓ Windows DWM (桌面合成器) 从 iGPU 读取
    │
    ↓ 显示器
```

中间这一层 **PRIME copy 完全在 driver 内部**，应用层不可见、不可控、也没有 Vulkan API 能直接同步它。它是 Optimus 这条 dGPU 路径的**核心成本**——多了一次跨 GPU 的内存搬运 + 跨 GPU 同步。

### 3.2 FIFO 的硬节拍如何挤压 PRIME 时序窗口

FIFO 模式下，每次 vsync 显示器消费队列里下一帧。但是这一帧要真的能"被显示"，必须在 vsync 触发**之前**完成全部 PRIME copy：

```
                 1 vsync interval (16.67ms @ 60Hz)
                 ↓
    ├─────────────────────────────────────┤
    应用 submit 完成 ────► dGPU 渲染完成 ────► PRIME copy 完成 ────► DWM 拿到 ────► 显示器接收
                                      ↑
                                这中间所有动作必须挤进
                                这一个 vsync 间隔
```

正常情况下没问题。但有几个独立的不确定性同时存在：

1. dGPU 渲染时间不定（依赖场景复杂度）
2. PRIME copy 时间不定（依赖 image 大小、PCIe 带宽、driver 调度）
3. driver 内部资源（PRIME copy 的 mirror image / metadata）有自己的生命周期管理

FIFO 严格"1 frame ↔ 1 vsync"的合同要求每次都能挤进去，**没有 slack**。

### 3.3 swapchain rebuild 让时序窗口进一步崩溃

resize / 最大化 / 切屏触发 `VK_ERROR_OUT_OF_DATE_KHR` → 应用做：

```
vkDeviceWaitIdle
    │
    ↓ 销毁旧 swapchain
    ↓ 销毁旧 image views / framebuffers / depth attachment
    │
    ↓ 创建新 swapchain
    ↓ 创建新 image views / framebuffers / depth attachment
    │
    ↓ 重新进入 acquire/present 循环
```

但 **driver 内部的 PRIME copy 状态不一定与应用层 swapchain rebuild 同步**：

- 旧 swapchain image 的 PRIME mirror copy 可能还被 driver 内部引用（DWM 上一帧还没真正交付到屏幕）
- 新 swapchain 的第一次 present 需要**重新建立** PRIME copy 链路（资源分配、metadata 设置、PCIe 通道协商）
- driver 给新 image 分配的内部 metadata（HiZ buffer / tile compression / cache mode）有非确定性
- DWM 的接收节奏跟应用 submit 时序之间，在 cross-GPU 下窗口被显著放大

每次 rebuild 是开"跨 GPU 时序的盒子"。FIFO 的硬节拍下，碰上不利时序就**整帧 PRIME copy 失败**——driver 把空白 image / 旧 image / 错位 image 喂给 DWM。在 depth path 上的具体后果是 fragment 全部 fail depth test → 黑屏；color attachment 路径相对鲁棒（driver 厂商优化最多）所以 UI 区域照样能看到。

> 详细的"为什么是 depth 而不是 color"机理见 [主复盘文档](2026-05-resize-blackscreen.md#真正的根因)。

### 3.4 为什么 MAILBOX 解决

MAILBOX 把"严格 1:1 节拍"换成"邮箱抢占"。对 PRIME 链路的影响：

```
            FIFO                          MAILBOX
            ────                          ───────
       每个 vsync 必须有                每个 vsync 取邮箱里
       一帧 PRIME copy 完成              已完成的最新一帧
       否则上一帧"赖在屏幕上"            （旧帧来不及就直接丢）
       严格、无 slack                    宽松、driver 可丢帧

       PRIME 偶发延误 → 整帧失败          PRIME 偶发延误 → 该帧被丢，
       → 黑屏 / 错帧                      下一帧自然替换上来
                                        → 用户察觉不到
```

具体几条：

- 应用持续提交不阻塞 → driver 总有 1-2 帧的 in-flight buffer 在 PRIME 链路里
- 如果某一帧 PRIME copy 时序撞上 driver 内部资源竞态，driver 可以**丢这一帧**而不是"必须显示"
- 邮箱替换给 PRIME copy 链路一个隐含的 retry 机会
- driver 内部的 PRIME 调度器在 MAILBOX 下走的是另一条更宽松的路径（dxvk / vkd3d-proton 的实测经验）

实测：用户在 NVIDIA RTX 3070 Ti Laptop 上把 `demo_minimal_resize` 切到 MAILBOX 后，最大化/还原/切屏的 dGPU 路径**完全稳定**。

---

## 4. 我们的引擎选择

`VulkanSwapchain::createInternal` 用 anonymous namespace helper `chooseSwapPresentMode(VkPhysicalDevice, VkSurfaceKHR)`：

1. `vkGetPhysicalDeviceSurfacePresentModesKHR` 查 surface 支持列表
2. 若包含 `VK_PRESENT_MODE_MAILBOX_KHR` → 返回 MAILBOX
3. 否则 fallback `VK_PRESENT_MODE_FIFO_KHR`（规范保证支持）
4. 启动 `std::cout` 打印实际选中的 mode

启动时能看到这一行：

```
[VulkanSwapchain] present mode: VK_PRESENT_MODE_MAILBOX_KHR
```

或：

```
[VulkanSwapchain] present mode: VK_PRESENT_MODE_FIFO_KHR (MAILBOX not advertised by driver)
```

---

## 5. 取舍代价（接受的代价）

切到 MAILBOX-first 默认意味着：

### 5.1 略高耗电

MAILBOX 不阻塞 → 应用渲染速率不再受 vsync 限制；渲染了又被丢的帧消耗了 GPU/CPU 周期。在编辑器 / UI 类负载上 GPU 远没满载，差距很难察觉；游戏类满载 GPU 应用差距会显著（功耗 / 噪音 / 笔记本续航）。

如果有"持续高 GPU 负载 + 笔记本续航敏感"的场景，可以在那条路径上单独走 FIFO。

### 5.2 会丢帧（无副作用，但存在）

被替换的旧帧被扔掉。对**视觉连续性**没影响（vsync 同步不撕裂、显示总是最新），但对**严格按帧编号驱动的逻辑**（视频帧精确对齐音轨、固定 step 物理仿真）可能影响——这种场景应该自己用 timeline / explicit pacing 而不是依赖 present 节拍，所以实际上无影响。

### 5.3 输入延迟降低（好处，不是代价）

显示总是最新到达的帧 → 用户输入到屏幕反馈的延迟低 1 帧。

---

## 6. 何时仍需要 FIFO

引擎默认 MAILBOX-first，但下面几种情境应该回退或显式锁定 FIFO：

1. **MAILBOX 不可用**：Wayland 上一些古旧驱动、嵌入式平台、某些虚拟化环境。fallback 自动处理
2. **严格节拍渲染**：视频播放器（音视频帧锁同步）、benchmark（需要稳定的 vsync 帧间隔做时序测量）
3. **省电场景**：明确想被 vsync 限速以省电的场景（e.g. 笔记本电池模式 idle 屏幕）

引擎层目前不暴露 per-swapchain 的覆盖接口，需要时再加。`LX_VK_PREFER_GPU=integrated` 是一个相邻 workaround——把渲染路径压回 iGPU，绕开整个 PRIME 链路；MAILBOX 不可用时也能用这个保兜底。

---

## 7. 参考

- Vulkan 规范 §29.5 Swapchains → presentMode 语义
- dxvk / vkd3d-proton 的 swapchain present mode 选择（在 Optimus 上能跑稳的关键之一）
- 主复盘：[2026-05 — demo_scene_viewer resize 偶发黑屏 / 闪烁](2026-05-resize-blackscreen.md)
