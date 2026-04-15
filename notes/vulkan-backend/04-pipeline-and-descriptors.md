# Vulkan Backend 模块四：Pipeline 与 Descriptor

## 入口关系

pipeline 相关逻辑分成三层：

- `PipelineCache`：按 `PipelineKey` 缓存 `VulkanPipeline`
- `VulkanPipeline`：把 `PipelineBuildDesc` 变成真正的 `VkPipeline`
- `VulkanDescriptorManager`：缓存 `VkDescriptorSetLayout`，并负责每帧 descriptor set 分配与复用

`VulkanResourceManager` 在这里主要是一个上层协调者：它持有 `PipelineCache`，对 renderer 暴露 `getOrCreateRenderPipeline(item)` 和 `preloadPipelines(...)`。

## `PipelineBuildDesc` 如何落到 Vulkan

`VulkanPipeline` 构造时会把 `PipelineBuildDesc` 内容拆到自己的成员里：

- `stages`
- `bindings`
- `vertexLayout`
- `renderState`
- `topology`
- `pushConstant`

之后 pipeline 创建过程基本是直接翻译：

- shader bytecode -> `VkShaderModule`
- `ShaderResourceBinding` -> descriptor set layouts
- `VertexLayout` -> vertex binding / attribute descriptions
- `RenderState` -> rasterization / depth / blend state
- `PrimitiveTopology` -> input assembly topology
- `PushConstantRange` -> `VkPushConstantRange`

这也是当前后端“反射驱动”的核心：没有硬编码 shader 名到 layout 的映射表。

## Descriptor Layout 的缓存策略

`VulkanDescriptorManager::getOrCreateLayout(...)` 的 key 来自 `DescriptorLayoutKey`。这个 key 故意忽略 `ShaderResourceBinding.name`，只看：

- `set`
- `binding`
- `type`
- `stageFlags`
- `descriptorCount`

这样做的含义是：只要 descriptor 形状一致，就复用同一个 `VkDescriptorSetLayout`，哪怕绑定名不同。

## 为什么绑定资源时按名字路由

`VulkanCommandBuffer::bindResources(...)` 先从 `item.descriptorResources` 构建一张：

- `binding name -> IRenderResource`

然后遍历 pipeline 的反射绑定列表，用 `ShaderResourceBinding.name` 去匹配资源上的 `getBindingName()`。

这个设计带来两个约束：

- shader block / sampler 名称必须和 CPU 资源暴露的 binding name 一致
- backend 不再依赖 slot enum 或手写绑定表

这也是当前材质系统、scene-level 资源和后端之间能解耦的关键。

## 每帧 descriptor set 的分配模型

`VulkanDescriptorManager` 维护 `maxFramesInFlight` 个 frame context。每个 context 有：

- 一个 descriptor pool
- `freeSets`
- `pendingReturn`

工作方式是：

1. 这一帧需要绑定资源时，按 layout 分配 descriptor set
2. draw 结束后，RAII `DescriptorSet` 析构，把 set 放进当前 frame context 的 `pendingReturn`
3. 下次再次轮到这个 frame index 时，在 `beginFrame(...)` 里把 `pendingReturn` 转回 `freeSets`

这是一种“按帧复用”的轻量模型，避免每次绑定都重新建 layout 或频繁 free Vulkan descriptor set。

## Pipeline Cache 的行为

`PipelineCache` 提供三个入口：

- `find(key)`：只查不建
- `getOrCreate(info, renderPass)`：miss 时构建
- `preload(infos, renderPass)`：批量预构建

renderer 在 `initScene()` 之后会先 `preloadPipelines(...)`。这样 `draw()` 时理论上大多数 item 都已经命中缓存；如果仍然 miss，cache 会打印告警并现场创建。

这套模式把“场景初始化期的预热”和“运行时的兜底懒构建”同时保留了。
