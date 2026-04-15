# Vulkan Backend 文档索引

> 这一组文档解释 `src/backend/vulkan/` 当前实现是如何把 core 层的 `Scene` / `FrameGraph` / `RenderingItem` 落到 Vulkan API 上的。

## 阅读顺序

1. [总体结构与生命周期](01-overview-and-lifecycle.md)：先理解谁负责 orchestration，谁负责具体 Vulkan 对象。
2. [设备、RenderPass 与 Swapchain](02-device-and-swapchain.md)：看初始化时怎样拿到 instance/device/surface/swapchain/framebuffer。
3. [资源同步](03-resource-sync.md)：看 CPU 资源如何映射成 GPU 资源，以及 dirty 数据怎么上传。
4. [Pipeline 与 Descriptor](04-pipeline-and-descriptors.md)：看反射结果怎样变成 `VkDescriptorSetLayout`、`VkPipelineLayout`、`VkPipeline`。
5. [命令录制与提交](05-command-recording-and-draw.md)：看一帧里 acquire/record/submit/present 的完整路径。

## 代码地图

- 顶层入口：`src/backend/vulkan/vulkan_renderer.cpp`
- 设备与基础设施：`src/backend/vulkan/details/device.*`
- 交换链与渲染目标：`src/backend/vulkan/details/render_objects/*`
- 资源同步：`src/backend/vulkan/details/resource_manager.*`
- GPU 资源：`src/backend/vulkan/details/device_resources/*`
- pipeline：`src/backend/vulkan/details/pipelines/*`
- descriptor：`src/backend/vulkan/details/descriptors/*`
- 命令缓冲：`src/backend/vulkan/details/commands/*`

## 后端的核心职责

- 接住 core 层已经整理好的 `FrameGraph` 和 `RenderingItem`。
- 把 CPU 资源转换为 Vulkan buffer / image / sampler。
- 按 `PipelineBuildDesc` 预构建或懒构建 graphics pipeline。
- 在每帧中完成 acquire、命令录制、queue submit、present。

## 它不负责什么

- 不负责引擎主循环与 update hook；这些职责在 `EngineLoop`。
- 不负责 scene-level 资源拼装；这些资源在 `RenderQueue::buildFromScene(...)` 阶段已经并入 `RenderingItem::descriptorResources`。
- 不靠硬编码 slot 枚举绑定资源；descriptor 路由依赖 shader reflection 的 `ShaderResourceBinding` 和资源上的 `getBindingName()`。

## 关联文档

- `notes/subsystems/vulkan-backend.md`
- `notes/subsystems/frame-graph.md`
- `notes/subsystems/pipeline-cache.md`
- `notes/subsystems/material-system.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`
