# Vulkan Backend

> 渲染后端层，位于 `src/backend/vulkan/`，命名空间 `LX_core::backend`。它是 core 接口（`IRenderResource` / `IShader` / `IMaterial` / `IRenderable`）到 Vulkan API 的适配层。其他 backend（Metal / DX12 / ...）按同样的方式可以挂到同一套 core 下，目前只有 Vulkan 一个实现。
>
> 权威 spec: `openspec/specs/renderer-backend-vulkan/spec.md`

## 核心类一览

### 顶层 (`src/backend/vulkan/`)

- **`VulkanRenderer`** (`vk_renderer.hpp:8`) — 继承 `gpu::Renderer`，对外的主入口
  - `initScene(scenePtr)` — 构建 `FrameGraph`、调 `pipelineCache.preload(...)`、注入 camera/light UBO
  - `draw(item)` — 单次 draw 的封装
- **`VulkanDevice`** (`details/vk_device.hpp:20`) — 物理/逻辑 device、queue、swapchain 的持有者
  - 构造参数：`IWindowPtr window`
  - 子 managers：`VulkanDescriptorManager` / `VulkanCommandBufferManager`
- **`VulkanResourceManager`** (`details/vk_resource_manager.hpp`) — CPU 资源 → GPU 资源的映射管理器
  - `syncResource(cmdBufferMgr, resourcePtr)` — 把 dirty 的 `IRenderResource` 推到 GPU
  - `getOrCreateRenderPipeline(item)` — **thin shim**，转发到 `PipelineCache`
  - 持有 `VulkanRenderPass` + `PipelineCache`
  - **不再**持有 `unordered_map<PipelineKey, VulkanPipelinePtr>`（REQ-003b 起迁到 `PipelineCache`）

### Pipeline (`details/pipelines/`)

- **`VulkanPipeline`** — 封装一个 `VkPipeline` + `VkPipelineLayout` + `VkDescriptorSetLayout[]`
  - 从 `PipelineBuildInfo` 构建
- **`PipelineCache`** — `PipelineKey → VulkanPipelinePtr` 映射 + `preload` 接口（见 `notes/subsystems/pipeline-cache.md`）

### Commands (`details/commands/`)

- **`VulkanCommandBufferManager`** — 每 frame-in-flight 一个 pool + buffer
- **`VulkanCommandBuffer`** (`vkc_cmdbuffer.hpp`) — 录制接口
  - `beginRenderPass / endRenderPass`
  - `bindPipeline(pipeline)`
  - `bindResources(resourceManager, pipeline, item)` — 核心路径，**按 binding name 匹配反射** + `IRenderResource::getBindingName()`
  - `drawItem(item)` — 最终提交 draw command

### Descriptors (`details/descriptors/`)

- **`VulkanDescriptorManager`** (`vkd_descriptor_manager.hpp:64`) — pool + layout cache
  - `DescriptorSet` / `DescriptorLayoutKey` / `DescriptorLayoutHasher`
- **`DescriptorUpdateInfo`** (`:17`) — `bindResources` 路径上构造的一条更新

### Render Objects (`details/render_objects/`)

- **`VulkanRenderPass`** (`vkr_renderpass.hpp:19`) — `VkRenderPass` 包装
- **`VulkanFrameBuffer`** (`vkr_framebuffer.hpp:13`) — `VkFramebuffer` 包装
- **`VulkanSwapchain`** (`vkr_swapchain.hpp:18`) — swapchain + acquire/present
- **`VulkanRenderContext`** (`vkr_rendercontext.hpp:12`) — pass + framebuffer 组合

### Resources (`details/resources/`)

- **`VulkanBuffer`** (`vkr_buffer.hpp:19`) — UBO / VBO / IBO 的 `VkBuffer` 包装
- **`VulkanTexture`** (`vkr_texture.hpp`) — `VkImage` + `VkImageView` + `VkSampler`
- **`VulkanShader`** — SPIR-V 字节码 → `VkShaderModule`

## 典型的一帧路径

```
VulkanRenderer::draw(item)
  │
  ├── resourceManager.syncResource(cmdBufferMgr, resource)
  │     │ for each IRenderResource in item.descriptorResources:
  │     │   if (resource->isDirty()):
  │     │     上传 CPU 数据到 VulkanBuffer / VulkanTexture
  │     │     resource->clearDirty()
  │
  ├── pipeline = pipelineCache.find(item.pipelineKey)  // 预构建期已 preload
  │
  ├── cmdBuffer.bindPipeline(*pipeline)
  │
  ├── cmdBuffer.bindResources(resourceManager, *pipeline, item)
  │     │ for each ShaderResourceBinding in pipeline reflection:
  │     │   name = binding.name
  │     │   在 item.descriptorResources 里找
  │     │     IRenderResource::getBindingName() == StringID(name) 的那个
  │     │   更新 descriptor set (VkWriteDescriptorSet)
  │
  └── cmdBuffer.drawItem(item)
        │ pushConstant(item.objectInfo)
        │ vkCmdDrawIndexed(...)
```

## 描述符路由（REQ-003b 之后）

以前的路径靠硬编码的 `PipelineSlotId` 枚举（`CameraUBO` / `MaterialUBO` / `AlbedoTexture` / ...）在 resource 和 pipeline slot 之间建映射。REQ-003b 之后：

- 每个 `IRenderResource` 实现 `getBindingName() → StringID`
- `VulkanCommandBuffer::bindResources` 从 pipeline 的 `ShaderResourceBinding` 列表取每个 binding 的 `name`
- 用 `StringID(binding.name)` 在 `item.descriptorResources` 里查找
- 没有硬编码的 enum lookup table

这意味着：
- 加一个新 UBO 只需要 (a) 实现一个 `IRenderResource` 子类 (b) shader 里声明正确的 block 名 — **不用改 backend 代码**
- 不同 shader 可以共存，各自有完全不同的 binding 集

## 和 core 的绑定点

| Core 抽象 | Backend 实现 / 消费者 |
|-----------|---------------------|
| `IRenderResource::setDirty/clearDirty` | `VulkanResourceManager::syncResource` 读 dirty 位 |
| `IRenderResource::getBindingName()` | `VulkanCommandBuffer::bindResources` 路由 descriptor |
| `IShader::getReflectionBindings()` | `VulkanPipeline::build` 生成 `VkDescriptorSetLayoutBinding[]` |
| `IShader::getAllStages()` | `VulkanPipeline::build` 创建 `VkShaderModule` |
| `VertexLayout` | `VulkanPipeline::build` 生成 `VkVertexInputAttributeDescription[]` |
| `RenderState` | `VulkanPipeline::build` 填充 rasterizer / depth-stencil / blend |
| `PrimitiveTopology` | `VulkanPipeline::build` 设置 `VkPipelineInputAssemblyStateCreateInfo` |
| `ImageFormat` | `backend::toVkFormat()` |
| `PipelineKey` | `PipelineCache` 的 map key |
| `PipelineBuildInfo` | `PipelineCache::preload/getOrCreate` 的输入 |

## 注意事项

- **`LX_core::backend` 是嵌套 namespace**: 不是独立的 `LX_backend`。意图是强调 backend 依附于 core 的契约。
- **`VulkanResourceManager` 是 thin forwarder（REQ-003b 后）**: `getOrCreateRenderPipeline(item)` 只做 `pipelineCache.getOrCreate(fromRenderingItem(item), renderPass)`；内部不再有 pipeline 映射。如果看到它自己持 `unordered_map<PipelineKey, ...>`，那是 pre-REQ-003b 的老代码。
- **Descriptor binding 名必须在 shader 声明里与 core 类的 `getBindingName()` 一致**: 这是唯一的耦合点。`CameraUBO` 的 shader 名必须是 `"CameraUBO"`；`SkeletonUBO` 的 shader 块必须叫 `Bones`（因为 `SkeletonUBO::getBindingName()` 返回 `"Bones"`）。这条契约由人工维护，没有编译期检查 —— 如果名字不一致，backend 会在 `bindResources` 时找不到，descriptor set 空着，GPU 读 0。
- **`VulkanRenderer::initScene` 负责 scene UBO 注入**: `RenderableSubMesh::getDescriptorResources` **不**返回 scene 级 UBO（camera / light），这些由 `initScene` 从 `scene->camera->getUBO()` / `scene->directionalLight->getUBO()` 拿到后注入到每个 `RenderingItem::descriptorResources`。
- **Push constant 由 `ObjectPC` 承载**: 通过 `RenderingItem::objectInfo` 传递。`PC_Draw` (`src/core/gpu/render_resource.hpp:83`) 是 std140 layout 的 128 字节结构（`mat4 model + enableLighting + enableSkinning + padding[2]`）。
- **Vulkan 版本依赖**: 用 `VK_API_VERSION_1_3`，依赖 `VK_KHR_dynamic_rendering` / `VK_EXT_descriptor_indexing` 等现代扩展（具体见 `vk_device.cpp`）。

## 测试

所有 `test_vulkan_*.cpp` 都需要一个 Vulkan device，无 GPU 时 CI 会跳过：

- `src/test/integration/test_vulkan_buffer.cpp` — VBO/IBO/UBO 上传
- `src/test/integration/test_vulkan_texture.cpp` — 纹理上传 + sampling
- `src/test/integration/test_vulkan_shader.cpp` — SPIR-V → `VkShaderModule`
- `src/test/integration/test_vulkan_pipeline.cpp` — pipeline 构建 + `PipelineBuildInfo` 消费
- `src/test/integration/test_vulkan_command_buffer.cpp` — 端到端：scene → RenderingItem → bindResources → drawItem
- `src/test/integration/test_vulkan_resource_manager.cpp` — ResourceManager 映射表
- `src/test/integration/test_vulkan_swapchain.cpp` — 窗口 + present
- `src/test/integration/test_vulkan_framebuffer.cpp` — FBO 创建
- `src/test/test_render_triangle.cpp` — 完整三角形渲染烟雾测试

## 延伸阅读

- `openspec/specs/renderer-backend-vulkan/spec.md` — Vulkan backend 的完整 normative 要求
- `notes/subsystems/pipeline-cache.md` — `PipelineCache` 的独立契约
- `notes/subsystems/pipeline-identity.md` — `PipelineKey` + `PipelineBuildInfo` 的 core 侧
- 归档: `openspec/changes/archive/2026-03-23-implement-renderer-framework/` — 最初 backend 框架
- 归档: `openspec/changes/archive/2026-03-24-refactor-vulkan-device-fix/` — device 构造整理
- 归档: `openspec/changes/archive/2026-04-13-pipeline-prebuilding/` — 引入 `PipelineCache` / `PipelineBuildInfo` / descriptor-by-name
