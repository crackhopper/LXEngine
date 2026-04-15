# Vulkan Backend 模块二：设备、RenderPass 与 Swapchain

## `VulkanDevice` 负责什么

`VulkanDevice` 是后端最底层的基础设施对象，封装：

- `VkInstance`
- `VkSurfaceKHR`
- `VkPhysicalDevice`
- `VkDevice`
- graphics / present queue
- surface format / depth format
- `VulkanDescriptorManager`

它的 `initialize(...)` 内部依次做：

1. 从 window 取 Vulkan 所需 instance extensions
2. 创建 instance，并在支持时打开 validation layer 和 debug messenger
3. 通过 window 创建 `VkSurfaceKHR`
4. 挑选 physical device
5. 选择 surface format 与 depth format
6. 创建 logical device 和 queue

## 设备选择逻辑

核心检查点在 `pickPhysicalDevice()` / `findQueueFamilies()` / `checkDeviceExtensionSupport()`：

- 必须支持 graphics queue
- 必须支持 present queue
- 必须支持 `VK_KHR_SWAPCHAIN_EXTENSION_NAME`

选中物理设备后，`findSurfaceDepthFormat()` 会记录两件后面会被频繁使用的元数据：

- `m_surfaceFormat`
- `m_depthFormat`

这就是 renderer 在 `makeSwapchainTarget()` 里回填 core `RenderTarget` 的来源。

## `VulkanRenderPass` 的角色

当前实现仍使用传统 render pass：

- 1 个 color attachment，最终 layout 是 `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`
- 1 个 depth attachment，最终 layout 是 `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`
- subpass 数量为 1
- clear value 固定为 color + depth 两项

这说明当前后端假设的主路径非常直接：一个 swapchain color attachment，加一个 depth attachment，然后直接 present。

## `VulkanSwapchain` 管什么

`VulkanSwapchain` 不只是 `VkSwapchainKHR` 包装，它还把“直接可渲染的一组目标”都放在一起管理：

- swapchain images
- 每张 image 对应的 image view
- 一张共享的 depth image / depth image view
- 每张 swapchain image 对应的 `VulkanFrameBuffer`
- 每帧的 acquire / render-finished semaphore 与 in-flight fence

所以对 renderer 来说，swapchain 已经是“一套可直接开始 render pass 的帧目标”。

## 初始化与重建

`initialize(renderPass)` 内部做：

1. 从 device 读取 surface/depth format
2. 创建 swapchain
3. 为 swapchain image 创建 image view
4. 创建 depth image / memory / image view
5. 创建同步对象
6. 基于 render pass 和 attachments 创建 framebuffer

`rebuild(renderPass)` 则是：

1. `waitIdle()`
2. 销毁旧的 swapchain 相关对象
3. 用当前窗口大小重新创建 swapchain
4. 重建 image views / depth resources / sync objects / framebuffers

这套流程意味着 resize 恢复点被压在 swapchain 层，而不是让 renderer 自己手工重建每个子对象。

## 一个关键的跨层连接点

`VulkanRendererImpl::makeSwapchainTarget()` 会把 Vulkan 的 surface/depth format 反向映射成 core 的 `ImageFormat`。这很重要，因为：

- core 层和 `FrameGraph` 只能看到抽象 `RenderTarget`
- backend 需要保证这个 target 真正反映当前 driver 选出来的交换链格式

这样 `Scene::getSceneLevelResources(pass, target)` 在过滤 camera target 时，看到的是和真实 swapchain 一致的 target，而不是一个写死的默认值。
