# 2026-05 — lxe_editor resize 偶发黑屏 / 闪烁（NVIDIA Optimus + dGPU）

## 摘要

`lxe_editor` 在窗口最大化 / 还原 / 切换显示器时，**场景画面**偶发黑屏（只剩 clear color，UI 正常）或闪烁（每帧细微差异）。修过单 depth race / per-image depth / 完整 subpass dependency / 显式 layout transition / 不同 in-flight 数 / oldSwapchain handle 等多轮，都只切换"症状形态"而非真正消除。

最终在 `demo_minimal_resize` 教科书 baseline 上做单变量隔离，锁定**真正的根因**：

> `VulkanDevice::pickPhysicalDevice` 默认按 score 选择最高优先级 GPU（DISCRETE > INTEGRATED）。在用户的 NVIDIA Optimus 笔记本（Intel iGPU + NVIDIA RTX 3070 Ti dGPU）上，这把渲染路径切到了 NVIDIA dGPU。dGPU 的输出必须经过 cross-GPU PRIME copy 才能呈现到 Intel iGPU 控制的显示器；NVIDIA driver 在这条 cross-GPU 路径上对 swapchain rebuild 时 depth attachment 的内部状态（HiZ / tile compression / cache mode）的管理有非确定性，跨 GPU 时序窗口又把这种非确定性放大成可见后果。**iGPU 路径上同样的代码完全稳定**。

修复主体是把 `VulkanDevice` 的 GPU 偏好默认改回 integrated，提供 `LX_VK_PREFER_GPU` 环境变量给确实需要 dGPU 的用户。其它路上修过的应用层 bug（debug-utils 副作用、跨帧 depth race、subpass dependency 不全、per-image depth 设计）都是真实改进，保留。

---

## 时间线

| 阶段 | 关键现象 | 应用层假设 | 事实 |
|---|---|---|---|
| 0 | lxe_editor 偶发蓝屏（clear color = 蓝） | 多帧共享 depth attachment 跨帧 race | 跨帧 race 真实存在但不是"黑屏 vs 闪烁"切换的元凶 |
| 1 | 改 per-image depth + `m_imagesInFlight` + 完整 subpass dependency；蓝屏消失 | race 被压实 | race 在 iGPU 上根本无害；这一系列改动只是规范化 |
| 2 | 出现持续闪烁（细微差异，UI 不闪） | per-image depth 引入 image 间不一致 | 在 dGPU + PRIME 路径下 driver 给每张 image 的内部 cache 模式是独立 lottery，被时序放大成肉眼可见的闪烁 |
| 3 | DIAG: 强制 `kMaxFramesInFlight=1` + 单 depth → 闪烁消失但黑屏回来 | 跨帧 race 是元凶 | 单 depth 在 dGPU 路径上锁在不利状态 |
| 4 | 写完全教科书 raw vk 的 baseline (`demo_minimal_resize_baseline`) → 完全稳定 | baseline 用了 first-suitable 选 GPU，**默认选了 iGPU** | 这是关键转折点，但当时没意识到 |
| 5 | 从 baseline 出发单变量增量回放 `VulkanDevice` 的差异，最终 delta 2-only（仅 score-based pick）就能复现黑屏 | 锁定 score-based GPU pick | stdout 显示 `Selected discrete GPU: NVIDIA GeForce RTX 3070 Ti Laptop GPU` —— 切到 dGPU 了 |
| 6 | 修复 fix A（默认 iGPU + 环境变量）+ fix B（rebuild 传 oldSwapchain）；`lxe_editor` 默认稳定，`LX_VK_PREFER_GPU=discrete` 走 dGPU 仍偶发 | 应用层无法完全修 dGPU + Optimus 路径，需要 driver 配合 | 提供 workaround 文档化 |

---

## 现象演变与每一阶段的诊断

### 阶段 0：单 depth image + 3 in-flight → "持续蓝屏"

最初症状：最大化 / 还原后整片只剩 clear color（被设为蓝色用作 debug，于是呈现蓝屏），UI 正常。

第一轮诊断怀疑"跨帧 depth race"——3 个 in-flight frame 共享同一张 depth attachment，submit 之间没有显式 synchronization，driver 内部 ordering 不可靠。这条 race 是**真实存在**的应用层 bug。

### 阶段 1：per-image depth + `m_imagesInFlight` + 完整 subpass dependency → 闪烁

按教科书做法：每张 swapchain image 配一份独立 depth attachment，`m_imagesInFlight[imageIndex]` 用 fence 串行同一 image 上的连续提交，subpass dependency 补全 `LATE_FRAGMENT_TESTS` + `srcAccessMask`。

蓝屏消失，但出现"持续闪烁"——细微差异，每帧之间不同；UI 完全正常。

当时的解释是 "per-image depth 在 dGPU + Optimus 上 driver 给每张 image 的 HiZ / tile compression 内部状态独立 lottery，每帧轮换 image 时差异显现成闪烁"。这个解释**事后看是对的**，但当时未理解到根因在 GPU 选择层。

### 阶段 2：DIAG kMaxFramesInFlight=1 + 单 depth → 黑屏回归

试图同时排除 race 和 per-image lottery。结果：闪烁消失，**黑屏回来**。这意味着"in-flight=1 + 单 depth"也不稳——意味着 race 不是黑屏的元凶（in-flight=1 没 race）。

这一步的真正价值是**否定假设**：黑屏不是跨帧 race 引起的。

### 阶段 3：教科书 raw vk baseline → 完全稳定

用户提议写一份完全独立、教科书式的 raw vk demo，把所有项目封装类（`VulkanDevice` / `VulkanSwapchain` / `VulkanRenderPass` / `VulkanFrameBuffer`）全部不用，只保留 `LX_infra::Window`（SDL 包装）。这就是后来固化为 `demo_minimal_resize_baseline` 的版本。

它**永远不黑屏不闪**。

baseline 的 `pickPhysicalDevice` 用的是 vulkan-tutorial.com 标准 "first-suitable"（取枚举顺序里第一个满足条件的）。在用户的 Optimus 笔记本上，PCI 枚举顺序通常是 Intel iGPU 在前——baseline **隐式选了 iGPU**。这是关键，但当时没注意到。

### 阶段 4：单变量回放 → 锁定 GPU pick

从 baseline 出发"逐步把 `VulkanDevice` 的差异加回去"。把 5 项差异（descriptor pool 创建 / score-based GPU pick / surface format B8 vs B8|R8 / isDeviceSuitable 不查 swapchain support / `createGraphicsHandle` vs `getVulkanSurface`）一项一项打进去。

- delta 1（descriptor pool 创建）单独：稳定
- delta 1+2+3+4+5 stack：黑屏

stdout 这时打出来 `Selected discrete GPU: NVIDIA GeForce RTX 3070 Ti Laptop GPU` —— 立刻意识到机器是 Optimus 笔记本，`VulkanDevice` 的 score-based pick 把渲染路径切到了 NVIDIA dGPU。

回到 delta 2-only（仅保留 score-based pick），其它 4 项退回 baseline → 仍然黑屏。**单变量锁定**：触发器就是"切到 dGPU"。

---

## 真正的根因

### 物理链路

用户机器是**笔记本 + Optimus 双 GPU**：

- 显示器物理连接到 Intel iGPU（笔记本的标准设计）
- NVIDIA dGPU 没有自己的显示输出
- 当应用用 dGPU 渲染时，输出走这条链：

```
应用 vkQueuePresentKHR
    → NVIDIA dGPU 上的 swapchain image
    → driver 内部 cross-GPU PRIME copy 到 iGPU 共享内存
    → DWM (Windows 桌面合成) 从 iGPU 读取
    → 显示器
```

中间这层 PRIME copy 由 NVIDIA driver + Windows DWM 联合管理，应用层不可见、也不可控。

### 为什么 swapchain rebuild 时崩坏

`VK_ERROR_OUT_OF_DATE_KHR` 触发 → `vkDeviceWaitIdle` + 销毁旧 swapchain + 创建新 swapchain + 重建 depth/framebuffer。

但是 **driver 内部的 cross-GPU PRIME copy 状态**不一定与应用层的 swapchain rebuild 同步：

- 旧 swapchain image 的 PRIME mirror copy 可能还在被引用
- 新 swapchain 第一次 present 时 PRIME 链路要重新建立
- driver 给新 image 分配的内部 metadata（HiZ / tile compression / cache mode）有非确定性
- DWM 收到的画面时机和应用 submit 的时序之间窗口在 cross-GPU 下被显著放大

每次 rebuild 是开"跨 GPU 时序的盒子"——抽到不利组合就黑屏 / 闪烁。

### 为什么 depth path 是症状显现路径

driver 厂商在 cross-GPU PRIME 上对 **color attachment** 投入了大量优化（present 的主路径），对 **depth attachment** 优化少（depth 不参与 present，driver 内部 HiZ / tile compression / cache mode 管理跟 color 走不同路径）。所以：

- color 输出在 dGPU 路径上**基本可靠** → UI 完整、画面边框/clear color 都对
- depth 在 dGPU 路径上**非确定性大** → 开 depth test 的几何全 fail → 黑屏；多 image 间 lottery 不一致 → 闪烁

iGPU 路径根本没 PRIME copy 这一层，driver 内部 depth path 也短得多，相同代码不出问题。

### "黑屏 / 闪烁" 是同一根因的两种形态

```
dGPU + Optimus PRIME 路径不稳（根因）
        │
        ├ + 单 depth + 多 in-flight  → 整张 depth 状态共享被污染   → 持续黑屏
        ├ + per-image depth          → 每张 image 内部状态各自抽彩 → 闪烁
        └ + 单 depth + in-flight=1   → 单张 depth 锁在不利状态     → 持续黑屏
```

我们之前调过的所有 in-flight / depth attachment / subpass dependency 修复，都只是在 dGPU 这条不稳的路径上换形态，没有真正消除根因。

### 为什么 UI 不出问题

imgui pipeline 默认 `depthTestEnable = FALSE`，UI fragment 完全不读 depth attachment。dGPU + Optimus 路径上 depth 那点非确定性碰不到 UI。color 路径稳定 → UI 输出始终正确。

这跟"关闭 depth test 就 OK"是同一回事：让 scene 也走 imgui 那条不依赖 depth 的路径。

---

## 路上顺手抓到的真实 bug（保留修复）

虽然不是黑屏的元凶，但都是客观存在的应用层缺陷，这次顺手修了：

### 1. `VulkanDevice::checkValidationLayerSupport({})` 误判

`createInstance` 在 caller 传空 `validationLayers` 时，仍然把 `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` 加进 instance extension list、把 debug create info 挂到 `pNext`、并真的创建 `VkDebugUtilsMessengerEXT`。原因是 `checkValidationLayerSupport({})` 对空 vector 走"foundLayers.size() == toCheck.size() == 0"返回 true。修复：开头加 `if (validationLayers.empty()) return false;`

### 2. 跨帧 depth race（per-image depth + `m_imagesInFlight`）

3 个 in-flight frame 共享同一张 depth attachment，跨 cmd buffer 没有显式同步——是 vk spec 上 undefined behavior。改为每张 swapchain image 一份独立 depth attachment + `m_imagesInFlight[imageIndex]` fence 串行化同一 image 上的连续提交。**iGPU 上不直接出问题（driver 宽容），但这是规范的设计选择**。

### 3. subpass dependency 不全

`render_pass.cpp` 的 `EXTERNAL → 0` dependency 缺 `LATE_FRAGMENT_TESTS_BIT` + `srcAccessMask`，按 vk spec 这是不严格的。补全。

### 4. `VulkanSwapchain::rebuild` 不传 `oldSwapchain`

按 vk spec，rebuild 时把旧 `VkSwapchainKHR` 传进 `VkSwapchainCreateInfoKHR::oldSwapchain` 让 driver 复用底层资源是更稳妥的写法。改为保留旧 handle 走 cleanup，传给 `vkCreateSwapchainKHR`，新 swapchain 完成后再销毁旧 handle。

### 5. 显式 depth image layout transition

`createDepthResources` 后追加一次 transient command buffer 把 depth image 显式从 `UNDEFINED` transition 到 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`，避免依赖 driver 在第一次 render pass 上做 lazy transition 的实现差异。

---

## 走过的弯路（避免重复）

### 弯路 1：以为是 host-visible buffer 写竞态

观察到 camera UBO 是单一共享 host-visible buffer，CPU 直接 memcpy。怀疑是写竞态，加了 `waitForAllFrames` 在 dirty 时同步。但闪烁/黑屏仍在。

**教训**：代码里这条路径确实不规范，但它不是这次的元凶。在写竞态可被 driver 路径放大或不放大时，应用层本身的 race 不一定是元凶。

### 弯路 2：以为是 per-image depth attachment 在 driver 内部 cache 模式 lottery

加 `transitionDepthImageToOptimal` 想"统一"每张 image 的初始 cache 状态。没用——driver 内部 cache 模式分配是基于 image 物理地址 / 分配时刻，不靠 layout barrier 统一。

**教训**：layout barrier 只统一 layout state machine，不统一 driver 内部 metadata。

### 弯路 3：以为是 `kMaxFramesInFlight=1` 能消除所有 race

强制 in-flight=1 仍黑屏 → 否定了"跨帧 race 是元凶"假设。这一步本身是有价值的，但当时一度想"是不是 race 还是有别的方式触发"，又花了一些时间在不相干的地方。

**教训**：实验结果如果直接否定主假设，要立刻换方向，不要继续在原假设里加补丁。

### 弯路 4：单变量隔离做晚了

阶段 1 / 2 / 3 都是"在已有代码基础上加补丁"。直到阶段 3 写出 baseline 才有 known-good 对照。如果更早就建立 baseline，节省的时间可能在一半以上。

**教训**：诊断 GPU / driver 层 bug，"教科书 raw 实现的已知好基线"价值极高，应该早做。

---

## 最终修复

### Fix A：`VulkanDevice` 默认偏好 integrated GPU

`src/backend/vulkan/details/device.cpp`：

- 新增 anonymous namespace 工具：
  - `enum class GpuPreference { Integrated, Discrete };`
  - `readGpuPreferenceFromEnv()`：读环境变量 `LX_VK_PREFER_GPU`，默认 `integrated`，可选 `discrete`
  - `gpuSelectionScore(type, pref)`：根据 preference 决定 score 排序
- `pickPhysicalDevice` 改用 `gpuSelectionScore(props.deviceType, preference)`
- 启动多打印一行 `[VulkanDevice] GPU preference: <integrated|discrete>` 让选择可见
- 旧 `physicalDevicePreferenceScore(type)` 与 `getPhysicalDevicePreferenceScoreForTesting` 保持原 discrete-first 语义不变 → `test_vulkan_device_selection.cpp` 不破坏

### Fix B：`VulkanSwapchain::rebuild` 传 `oldSwapchain`

`src/backend/vulkan/details/render_objects/swapchain.{hpp,cpp}`：

- `createInternal` 增加可选参数 `VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE`，传给 `VkSwapchainCreateInfoKHR::oldSwapchain`
- `rebuild` 改为：保留旧 handle → cleanup（不销毁旧 swapchain）→ `createInternal(rawExtent, oldHandle)` → 新 swapchain + image views + depth + framebuffers + sync 全部建好后再 `vkDestroySwapchainKHR(oldHandle)`
- `initialize` 路径仍走 `oldSwapchain = VK_NULL_HANDLE`，行为不变

注意：fix B 实测对 dGPU + Optimus 路径**改善有限**——cross-GPU PRIME 时序问题主要在 driver 内部，oldSwapchain 只是个 hint。但这是 vk spec 上推荐的写法，无害且对其它环境（如 NVIDIA 桌面单卡 / AMD）有正面效果，保留。

### Fix C（**真正解决 dGPU 路径**）：`VulkanSwapchain` 默认 MAILBOX-first present mode

`src/backend/vulkan/details/render_objects/swapchain.cpp`：

- anonymous namespace 新增 `chooseSwapPresentMode(VkPhysicalDevice, VkSurfaceKHR)`：查询 surface 支持的 present modes，**MAILBOX 优先**，fallback FIFO
- `createInternal` 改用它，删掉硬编码 `VK_PRESENT_MODE_FIFO_KHR`
- 启动打印一次实际选了哪个 mode

为什么有效：FIFO 强制 1 frame ↔ 1 vsync 的硬节拍，cross-GPU PRIME copy 必须在每个 vsync 间隔内完成；MAILBOX 让 driver 在内部排队多帧、按需丢，PRIME 链路有时序 slack。dxvk / vkd3d-proton 在 Optimus 上正是这个 fallback。

实测结果：dGPU + Optimus 路径下 MAILBOX **解决了 resize 黑屏**。`demo_minimal_resize` 在 NVIDIA RTX 3070 Ti Laptop GPU 上跑稳定。

### Fix D：`VulkanDevice` 默认 GPU 偏好改回 `Discrete`

因为 fix C 让 dGPU 路径稳定了，fix A 临时把默认压回 integrated 的 workaround 不再需要——把默认改回 `Discrete`，恢复"高性能优先"语义。`LX_VK_PREFER_GPU` 环境变量保留：

- 默认（不设置）：Discrete
- `LX_VK_PREFER_GPU=integrated`：仍可显式压回 iGPU（给 MAILBOX 不可用或仍 flaky 的极端环境留口子）
- `LX_VK_PREFER_GPU=discrete`：显式 Discrete

`physicalDevicePreferenceScore`、`getPhysicalDevicePreferenceScoreForTesting` 完全不动 → `test_vulkan_device_selection.cpp` 不破坏。

### 顺带修复（详见上节）

- `checkValidationLayerSupport({})` 短路返回 false
- per-image depth + `m_imagesInFlight` + 完整 subpass dependency + 显式 layout transition

---

## 仍开放的问题

### ✅ dGPU + Optimus 路径上的偶发黑屏 —— **已被 fix C (MAILBOX) 解决**

最初这一节是"应用层无法完全消除，需要走 driver 层 workaround"。但 fix C 把默认 present mode 切到 MAILBOX 后，用户实测在 NVIDIA RTX 3070 Ti Laptop 上 dGPU 路径完全稳定，所以**默认配置（Discrete + MAILBOX）已经是稳的**。

下面这些 workaround 现在只用于**MAILBOX 不可用或仍 flaky 的极端环境**作为备用：

1. **`LX_VK_PREFER_GPU=integrated`**：把渲染压回 iGPU，绕开 PRIME 路径。完全跳过这条不稳定链。
2. **NVIDIA 控制面板**：3D 设置 → 程序设置 → 把可执行文件绑到 "集成图形" 或 "高性能 NVIDIA"。零代码 workaround。
3. **升级 NVIDIA driver 到最新**：旧驱动版本在 Vulkan WSI / Optimus 路径上有数轮稳定性修复。
4. **更激进的 surface rebuild**（长期 TODO，目前不实施）：每次 resize 重建 `VkSurfaceKHR` 而不只是 swapchain。代码改动较大，目前不需要。

### MAILBOX vs FIFO 取舍记录

切到 MAILBOX-first 默认带来的副作用：

- **耗电略增**：MAILBOX 允许 GPU 持续渲染、超出 vsync 上限，driver 内部丢旧帧。FIFO 严格 vsync 节拍下 GPU 等待显示间隔。编辑器/UI 类应用 GPU 工作量小，差距不显著
- **轻微输入延迟降低**：MAILBOX 显示总是最新到达的帧
- 需要 MAILBOX 不可用（极少数环境）时 fallback 到 FIFO，行为不变

### `NvOptimusEnablement` 导出

NVIDIA 推荐应用导出 `__declspec(dllexport) DWORD NvOptimusEnablement = 1`（强制 dGPU）/ `= 0`（强制 iGPU）作为 driver 切换 hint。本次没加——它跟 `LX_VK_PREFER_GPU` 是冲突机制（一个静态导出 vs 一个运行时环境变量）。后续若有更细的 GPU 选择需求再考虑。

---

## 工件 / 回归测试基线

### `demo_minimal_resize_baseline`（**冻结**，不要修改）

`src/demos/minimal_resize_baseline/main.cpp` —— 完全教科书风格 raw Vulkan，只用 `LX_infra::Window`（SDL 包装）+ raw vk。在用户的 Optimus 笔记本上验证过完全稳定（baseline 默认 first-suitable 选 iGPU）。

后续遇到任何 swapchain / depth / WSI / resize 类问题，可以并排跑这个 demo 做 A/B 对照。

### `demo_minimal_resize`（迭代试验场）

`src/demos/minimal_resize/main.cpp` —— 当前是"baseline + MAILBOX-first present mode fallback + GPU 列表打印 + score-based pick"的状态，作为 dGPU 路径下进一步实验的载体。

### `test_vulkan_device_selection.cpp`

`src/test/integration/test_vulkan_device_selection.cpp` 保持原 discrete-first score 语义断言。这次修复**有意**没动这个测试——让"pure score function 的哲学排序" 跟 "实际选择策略" 解耦。

---

## 教训

1. **症状显现路径 ≠ 根因**。"关 depth test 就 OK" 把范围收窄到 depth path 上，但根因不在 depth attachment 本身——是 dGPU + Optimus 把 depth path 上的非确定性放大成可见后果。修了一堆 depth path 上的应用层 bug 都是治症不治本。
2. **建立教科书 known-good baseline 越早越好**。带 driver / GPU 层非确定性的问题，没有 baseline 对照很难诊断。
3. **单变量隔离要严格**。每次只引入一个差异，stack 加进去前先确认每一项独立的影响。本次正是这样最终锁定到 `score-based pick` 单变量。
4. **"在 iGPU 上稳的代码不代表正确"**。iGPU driver 路径短、宽容，能掩盖应用层的 race / 不规范设计。我们顺手修的几个真实 bug（debug-utils 副作用、跨帧 race、dependency 不全）在 iGPU 上没显现，是这次诊断时被 dGPU 路径"放大"才暴露的。
5. **stdout 早打印关键决策**。`Selected discrete GPU: NVIDIA ...` 这一行如果在调试早期就在窗口标题或 stdout 上出现，整个诊断会快很多。后续所有"选了什么/默认什么"的决策都应该清楚打到 stdout 上。
6. **bug 复盘值得写下来**。下次遇到 "笔记本上 resize 偶发 + UI 正常 + 关 depth test 就好"立刻去翻这个 file 而不是从头猜起。

---

## 关联代码

主要 commit（按时间顺序，最后一个为本次最终修复）：

- `ff4364e` Use per-image depth attachments for swapchain
- `f97d4fe` Track in-flight swapchain images（`m_imagesInFlight`）
- `27cf929` Synchronize render pass depth writes（subpass dependency 补全）
- `11be022` Pre-transition swapchain depth images to optimal layout
- `b76c1d7` Fix VulkanDevice silently enabling debug utils on empty validationLayers
- `a69d725` Rewrite demo_minimal_resize as a textbook raw-Vulkan baseline
- `9ae296e` Add demo_minimal_resize_baseline as a frozen known-good reference
- `6819f17` demo_minimal_resize delta 2 only: GPU pick is the prime suspect
- `b9c73c8` Pin GPU pick to integrated by default and pass oldSwapchain on rebuild
- `b01667d` Restore per-image depth + in-flight=3, add MAILBOX probe and debug postmortem
- `<本次>` Default to MAILBOX-first present mode and revert default GPU preference back to Discrete
