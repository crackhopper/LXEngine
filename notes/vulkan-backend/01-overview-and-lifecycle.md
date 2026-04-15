# Vulkan Backend 总体结构与生命周期

## 一句话模型

当前实现里，`VulkanRenderer` 是后端总调度器；它自己不做底层 Vulkan 细节，而是串起一组更小的对象：

- `VulkanDevice`：instance / surface / physical device / logical device / queue / descriptor manager
- `VulkanCommandBufferManager`：每帧 command pool 与 transient command pool
- `VulkanResourceManager`：CPU 资源到 GPU 资源的镜像管理，以及 pipeline cache
- `VulkanSwapchain`：swapchain image、depth image、framebuffer、同步原语

## 初始化顺序

`VulkanRendererImpl::initialize(...)` 的顺序很固定：

1. 创建 `VulkanDevice`
2. 创建 `VulkanCommandBufferManager`
3. 创建 `VulkanResourceManager`
4. 用 device 的 surface/depth format 初始化 `VulkanRenderPass`
5. 创建并初始化 `VulkanSwapchain`

这个顺序反映了依赖方向：

- command pool 依赖 device 和 graphics queue family
- resource manager 依赖 device，render pass 又依赖 surface/depth format
- swapchain 初始化时需要已经可用的 `VulkanRenderPass` 来创建 framebuffer

## 场景绑定顺序

`initScene(scene)` 做的是“把 scene 解释成后端可执行的数据”：

1. 计算 swapchain 对应的 `RenderTarget`
2. 给 target 为空的 camera 回填这个 target
3. 重建 `FrameGraph`，当前只加入 `Pass_Forward`
4. 调用 `m_frameGraph.buildFromScene(*scene)`
5. 遍历每个 `RenderingItem`，同步 vertex/index/descriptor 资源
6. 预热所有需要的 pipeline

这里最关键的一点是：后端不再自己拼 camera/light UBO。scene-level 资源必须在 queue 构建期就已经出现在 `RenderingItem::descriptorResources` 中。

## 每帧执行顺序

`draw()` 的帧路径是：

1. `swapchain->acquireNextImage(...)`
2. `cmdBufferMgr->beginFrame(...)`
3. `descriptorManager.beginFrame(...)`
4. 分配主命令缓冲并开始录制
5. 开始 render pass，设置 viewport/scissor
6. 遍历 `pass × item`，逐个 bind pipeline / bind resources / draw
7. 结束命令缓冲，提交 graphics queue
8. `swapchain->present(...)`

如果 acquire 或 present 返回 `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR`，当前策略是直接 rebuild swapchain，然后跳过这一帧。

## 为什么 `FrameGraph` 在后端内部持有

`VulkanRendererImpl` 里直接持有 `m_frameGraph`，意义是把“当前 scene 已解析出的渲染计划”留在 renderer 内部：

- `initScene()` 负责构建一次
- `uploadData()` 负责按这个 graph 更新 dirty 资源
- `draw()` 负责按这个 graph 提交 draw

所以后端并不是每帧重新从 scene 拼 draw list，而是把 scene 编译成一份更接近 GPU 提交的数据结构。

## 当前实现的边界

- 当前只走 graphics queue，没有 compute/transfer queue 分工。
- 当前 render path 还是经典 `RenderPass + Framebuffer` 模型，代码里也明确提到未来可迁移到 dynamic rendering。
- 当前 `FrameGraph` 只塞了一个 `Pass_Forward`，但 renderer 的遍历方式已经是按多 pass 设计的。
