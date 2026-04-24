# 资源上传

> 这一页解释“CPU 侧资源什么时候真正变成 Vulkan 资源”，以及当前实现里不同资源类型的上传时机和路径。

## 它解决什么问题

- 把 `IGpuResource` 这层抽象，接到实际的 backend 上传流程上。
- 解释首帧初始化上传和逐帧增量上传的边界。
- 说明当前 buffer / texture 两条上传路径为什么不完全一样。

如果你刚看完 [IGpuResource：core 层的 GPU 资源统一契约](../source_analysis/src/core/rhi/gpu_resource.md)，这页就是它在后端的落点。

## 先记住三层角色

- `IGpuResource`：CPU 侧资源适配接口，提供类型、字节、大小、binding 名和 dirty 状态。
- `VulkanRenderer`：决定“这一轮要同步哪些资源”，遍历 `FrameGraph` 中的 `RenderingItem`。
- `VulkanResourceManager`：把 CPU 资源句柄映射成实际的 Vulkan GPU 对象，并负责创建、更新和回收。

所以“上传”不是由某个资源自己主动完成，而是 renderer 在固定时机遍历场景后，由 resource manager 统一执行。

## 资源上传发生在哪些时机

当前有两个固定入口：

1. `initScene()`
2. `uploadData()`

对应代码路径：

- `EngineLoop::startScene()` 调 `renderer->initScene(scene)`，做首轮全量同步
- `EngineLoop::tickFrame()` 里在 update hook 之后调 `renderer->uploadData()`，做逐帧增量同步

也就是说，时序上是：

1. scene / material / camera / skeleton 等 CPU 对象先更新
2. 这些对象通过各自的 `IGpuResource` 或包装对象标记 dirty
3. renderer 在固定入口统一遍历并调用 `syncResource(...)`
4. backend 决定是否创建 GPU 镜像、是否真正执行上传

## 首次上传和增量上传的区别

`VulkanResourceManager::syncResource(...)` 的判断逻辑很简单：

- 资源第一次出现：创建 GPU 资源，并强制上传一次
- 资源已经存在且 `isDirty()`：只做更新
- 资源已经存在且不 dirty：跳过

这意味着当前实现不是“每帧盲目全量上传”，而是“renderer 每帧全量遍历，resource manager 按 dirty 判定是否真正更新”。

这样做的好处是：

- 场景遍历逻辑保持统一
- 资源是否需要上传由资源自身状态决定
- backend 缓存可以稳定按 handle 复用

## 资源是怎么被识别和缓存的

`VulkanResourceManager` 用 `IGpuResource::getResourceHandle()` 当 key，维护一张 CPU 资源到 GPU 资源的映射。

- key：CPU 资源对象地址
- value：`VulkanAnyResource`

当前主要包含：

- `VulkanBuffer`
- `VulkanTexture`

这张表不是永久缓存。每轮同步时碰到的 handle 会进入 `m_activeHandles`，之后 `collectGarbage()` 会把本轮没再出现的 GPU 资源回收掉。

所以当前策略更接近“按当前活跃引用集保留 GPU 镜像”，不是无限期保留历史资源。

## Renderer 怎么把资源送进上传路径

`VulkanRenderer` 在 `initScene()` 和 `uploadData()` 里都会遍历 `FrameGraph` 的 pass 和 item，然后同步三类资源：

- `item.vertexBuffer`
- `item.indexBuffer`
- `item.descriptorResources`

这件事很重要，因为它说明 backend 并不自己重新理解 scene / material / camera 的业务结构；它只消费 `RenderingItem` 已经整理好的资源集合。

也就是说，资源上传的“输入边界”就是 `RenderingItem`。

## Buffer 上传路径

当资源类型是 buffer 时，`createGpuResource(...)` 会根据 `ResourceType` 创建 `VulkanBuffer`：

- `VertexBuffer`：`VERTEX_BUFFER_BIT | TRANSFER_DST_BIT`
- `IndexBuffer`：`INDEX_BUFFER_BIT | TRANSFER_DST_BIT`
- `UniformBuffer`：`UNIFORM_BUFFER_BIT`

而在更新阶段，当前实现统一走 `VulkanBuffer::uploadData(...)`。

这代表当前 buffer 上传策略的特点是：

- 设计上允许 vertex/index buffer 以后走更复杂的 copy 路径
- 但当前实现仍偏向简单直接
- host-visible / coherent 的内存策略优先保证正确性和低复杂度

所以现在不要把它理解成“典型的 device-local + staging 双阶段 Vulkan 最优路径”；它更像一个先把统一上传通路跑通的基础实现。

## Texture 上传路径

texture 路径比 buffer 多了一层 staging，流程是：

1. 创建目标 `VulkanTexture`
2. 创建 host-visible staging buffer
3. 把像素字节写进 staging buffer
4. 申请 single-time command buffer
5. 把 image layout 切到 `TRANSFER_DST_OPTIMAL`
6. 执行 `copyBufferToImage`
7. 再切到 `SHADER_READ_ONLY_OPTIMAL`
8. 立即提交并等待完成

所以 texture 上传当前是“同步、立即完成”的路径，而不是异步队列化上传。

这样做的好处是简单，坏处也很明确：

- 上传与 draw 不能很好重叠
- 大纹理上传会直接拉长这一帧的同步成本

## Dirty 标记在这里扮演什么角色

`IGpuResource` 基类自带 dirty 状态，但不同资源设置 dirty 的时机不同：

- `VertexBuffer` / `IndexBuffer`：CPU 侧几何数据更新时直接 `setDirty()`
- `CombinedTextureSampler`：`update(...)` 时 `setDirty()`
- `CameraData` / `SkeletonData`：更新 packed UBO 数据后 `setDirty()`
- `ParameterBuffer`：先记录内部 pending-sync，`MaterialInstance::syncGpuData()` 再统一转成 `IGpuResource::setDirty()`

这说明 dirty 协议在项目里是统一的，但并不要求每种资源都用完全相同的写入时机。

## 一次完整的上传链路

拿“材质参数被修改”举例：

1. 调用 `MaterialInstance::setParameter(...)`
2. 对应的 `ParameterBuffer::buffer` 被写入，并记内部 dirty
3. `MaterialInstance::syncGpuData()` 把这些槽位转成 `IGpuResource::setDirty()`
4. 该资源通过 `SceneNode` / `RenderingItem` 进入 `descriptorResources`
5. `VulkanRenderer::uploadData()` 遍历到它，并调用 `resourceManager->syncResource(...)`
6. `VulkanResourceManager` 发现它已存在且 dirty，于是更新对应 `VulkanBuffer`

相机、骨骼、纹理、几何数据虽然来源不同，但进入 backend 后都在这条统一协议上汇合。

## 当前实现的边界

- 资源同步是 renderer 驱动，不是后台线程驱动。
- buffer 路径优先简单正确，没有全面上 device-local + staging 优化。
- texture 路径已经使用 staging，但仍是立即提交、立即等待。
- GPU 资源缓存按“本轮活跃句柄”保留，不做长期资源驻留策略。
- resource manager 不理解高层业务语义，它只处理 `IGpuResource` 和 `RenderingItem` 给出的集合。

## 从哪里继续读

- [IGpuResource：core 层的 GPU 资源统一契约](../source_analysis/src/core/rhi/gpu_resource.md)
- [Vulkan Backend 模块三：资源同步](../vulkan-backend/03-resource-sync.md)
- [Vulkan Backend](vulkan-backend.md)
