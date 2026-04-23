# Vulkan Backend

> Vulkan backend 是 core 抽象到 Vulkan API 的落地点。它不决定“什么时候开始一帧”，也不负责业务层 update hook；这些编排职责已经上移到 `EngineLoop`。backend 负责把 `FrameGraph` 和 `RenderingItem` 真实提交到 GPU。
>
> 权威 spec: `openspec/specs/renderer-backend-vulkan/spec.md`

## 现在怎么读

原来的单页说明已经拆成目录化文档，入口在 [notes/vulkan-backend/index.md](../vulkan-backend/index.md)。

建议顺序：

1. [总体结构与生命周期](../vulkan-backend/01-overview-and-lifecycle.md)
2. [设备、RenderPass 与 Swapchain](../vulkan-backend/02-device-and-swapchain.md)
3. [资源同步](../vulkan-backend/03-resource-sync.md)
4. [Pipeline 与 Descriptor](../vulkan-backend/04-pipeline-and-descriptors.md)
5. [命令录制与提交](../vulkan-backend/05-command-recording-and-draw.md)

## 一页版总结

- `VulkanRenderer` 负责 orchestration：初始化、`initScene()`、`uploadData()`、`draw()`
- `VulkanDevice` 负责 instance/device/queue/surface format/depth format
- `VulkanSwapchain` 负责 swapchain image、depth、framebuffer、同步对象
- `VulkanResourceManager` 负责 CPU 资源镜像与 pipeline cache
- `VulkanCommandBuffer` 在 draw 阶段汇合 pipeline、descriptor、vertex/index buffer 和 push constants

## 当前实现最重要的约束

- 所有可能触发 Vulkan loader 初始化的可执行程序，都必须在 `main()` 一开始调用 `LX_core::expSetEnvVK()`
- 这不是“可选清理项”，而是为了抑制 implicit validation layer 自动加载时额外产生的 `.log` 文件；调用必须早于 window / renderer / Vulkan instance 初始化
- descriptor 路由按 binding name，不按硬编码 slot 枚举
- scene-level UBO 已经在 queue 构建阶段合并好，backend 不再补注入
- `VulkanResourceManager` 不直接持有旧式 pipeline map，而是委托给 `PipelineCache`
- `FrameGraph` 当前只接了 `Pass_Forward`，但 renderer 的遍历方式已经是按多 pass 组织

## 从哪里进入源码

- 顶层：`src/backend/vulkan/vulkan_renderer.cpp`
- 资源：`src/backend/vulkan/details/resource_manager.cpp`
- pipeline：`src/backend/vulkan/details/pipelines/`
- descriptor：`src/backend/vulkan/details/descriptors/`
- draw 命令：`src/backend/vulkan/details/commands/`
