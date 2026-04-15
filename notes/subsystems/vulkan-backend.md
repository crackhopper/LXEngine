# Vulkan Backend

> Vulkan backend 是 core 抽象到 Vulkan API 的落地点。它不决定“什么时候开始一帧”，也不负责业务层 update hook；这些编排职责已经上移到 `EngineLoop`。backend 只负责把 `RenderingItem` 真正提交给 GPU。
>
> 权威 spec: `openspec/specs/renderer-backend-vulkan/spec.md`

## 它解决什么问题

- 管理 device、swapchain、render pass、framebuffer、command buffer。
- 把 core 资源同步到 Vulkan 资源。
- 根据 `PipelineBuildDesc` 创建和复用 Vulkan pipeline。

## 核心对象

- `VulkanRenderer`：顶层入口，持有 `FrameGraph`。
- `VulkanDevice`：device、queue、swapchain 基础设施。
- `VulkanResourceManager`：资源同步和 pipeline 获取。
- `VulkanCommandBuffer`：录制 bind/draw。
- `PipelineCache`：pipeline 缓存层。

## 典型数据流

通常由 `EngineLoop` 驱动：`startScene()` 触发 `initScene()`，`tickFrame()` 触发 `uploadData()` + `draw()`。

1. `initScene(scene)` 构建 frame graph 并 preload pipelines。
2. `uploadData()` 同步所有 dirty 资源。
3. `draw()` 遍历 `passes × items`。
4. `bindPipeline`。
5. `bindResources`。
6. `drawItem`。

## 关键约束

- descriptor 路由按 binding name，不按硬编码 slot 枚举。
- `VulkanResourceManager` 只是 pipeline cache 的 thin forwarder。
- scene-level UBO 已经在 queue 构建阶段合并好，backend 不再补注入。
- `getBindingName()` 和 shader block 名必须一致。

## 从哪里改

- 想改资源上传：看 `VulkanResourceManager::syncResource(...)`。
- 想改 descriptor 绑定：看 `VulkanCommandBuffer::bindResources(...)`。
- 想改 pipeline 创建：看 `VulkanPipeline` 和 `PipelineCache`。
- 想改引擎级运行顺序：先看 `notes/subsystems/engine-loop.md` 和 `src/core/gpu/engine_loop.cpp`
- 想改 backend 内部 render loop：看 `VulkanRenderer::initScene()` / `draw()`。

## 关联文档

- `openspec/specs/renderer-backend-vulkan/spec.md`
- `notes/subsystems/pipeline-cache.md`
- `notes/subsystems/engine-loop.md`
- `notes/subsystems/frame-graph.md`
