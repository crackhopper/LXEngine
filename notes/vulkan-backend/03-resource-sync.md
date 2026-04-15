# Vulkan Backend 模块三：资源同步

## 总体思路

`VulkanResourceManager` 维护一张 CPU 资源句柄到 GPU 资源对象的映射：

- key：`IRenderResource::getResourceHandle()`
- value：`VulkanAnyResource`

`VulkanAnyResource` 目前是三类对象的变体：

- `VulkanBuffer`
- `VulkanTexture`
- `VulkanShader`

但当前 `syncResource(...)` 实际支持的 GPU 镜像类型主要是 buffer 和 texture；`ResourceType::Shader` 会直接报错，因为 shader bytecode 的来源是 `PipelineBuildDesc.stages`，不是这里单独上传。

## `syncResource(...)` 的判定逻辑

每次同步时会先看资源类型：

- `PushConstant`：直接跳过，因为 push constant 不需要持久 GPU 对象
- 新资源：创建 GPU 对象，再立即上传一次数据
- 已存在且 dirty：只做更新

同步过的 handle 会被放进 `m_activeHandles`。一轮场景或一帧同步完成后，`collectGarbage()` 会把这轮没碰到的 GPU 资源回收掉。

这意味着资源管理是“按活跃引用集保留”的，而不是永远缓存。

## Buffer 路径

`createGpuResource(...)` 对 buffer 的策略是：

- vertex buffer：`VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT`
- index buffer：`VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT`
- uniform buffer：`VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT`

当前内存属性统一偏向 host visible/coherent，因此 `VulkanBuffer::uploadData(...)` 直接走 `map -> memcpy -> unmap`。

这说明当前实现优先简单和正确，没有把 vertex/index buffer 做成典型的 staging-to-device-local 双阶段上传。

## Texture 路径

texture 同步比 buffer 多一步 staging：

1. 创建目标 `VulkanTexture`
2. 创建一个 host-visible staging `VulkanBuffer`
3. 把像素数据写进 staging buffer
4. 申请 single-time command buffer
5. layout 从当前状态切到 `TRANSFER_DST_OPTIMAL`
6. `copyBufferToImage`
7. 再切到 `SHADER_READ_ONLY_OPTIMAL`
8. 提交并等待队列完成

也就是说，texture 上传是立即提交、立即等待的同步路径。它简单，但也意味着当前实现没有把资源上传和 draw 提交并行化。

## 资源同步发生在哪些时机

有两个固定入口：

- `initScene()`：首次把 `FrameGraph` 中所有 item 的资源都同步一遍
- `uploadData()`：每帧重新遍历 graph，把 dirty 资源补上传

后端并不自己追踪“哪些资源属于哪个 pass”，而是跟着 `FrameGraph` 中已经整理好的 `RenderingItem` 走。

## `VulkanBuffer` / `VulkanTexture` 的定位

这两个类都很薄，重点是 RAII 和最小功能集：

- `VulkanBuffer`：创建 buffer + memory，支持 map/unmap/upload
- `VulkanTexture`：创建 image + memory + image view + sampler，支持 layout transition 和 buffer-to-image copy

资源生命周期的上层拥有者不是它们自己，而是 `VulkanResourceManager` 的句柄映射表。
