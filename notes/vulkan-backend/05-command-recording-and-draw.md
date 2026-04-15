# Vulkan Backend 模块五：命令录制与提交

## `VulkanCommandBufferManager` 的责任

命令缓冲管理分成两类池：

- 每帧一个常驻 command pool，用于主渲染命令
- 一个 transient pool，用于 single-time commands，例如 texture upload

`beginFrame(frameIndex)` 会重置这一帧对应的 command pool，并清空该帧分配记录。这样主渲染路径可以重复利用命令缓冲对象。

## 主绘制路径

`VulkanRendererImpl::draw()` 的主流程可以概括为：

1. 计算 `currentFrameIndex`
2. acquire swapchain image
3. 如果 swapchain 过期，则 rebuild 并返回
4. `cmdBufferMgr->beginFrame(...)`
5. `descriptorManager.beginFrame(...)`
6. 分配主命令缓冲并 `begin()`
7. `beginRenderPass(...)`
8. `setViewport(...)` 和 `setScissor(...)`
9. 遍历所有 pass 和 item
10. `bindPipeline(...)`
11. `bindResources(...)`
12. `drawItem(...)`
13. 结束 render pass 和命令缓冲
14. `vkQueueSubmit(...)`
15. `present(...)`

这是一个很典型的“每帧一个主 command buffer”的模型。

## `bindPipeline(...)` 做了什么

除了 `vkCmdBindPipeline(...)`，它还把当前 pipeline 的：

- `VkPipelineLayout`
- push constant 的 stage mask / offset / size

缓存进 `VulkanCommandBuffer` 自身。后面的 `drawItem(...)` 和资源绑定就不需要再反查 pipeline。

## `bindResources(...)` 的实际工作

这一步做三类绑定：

1. 为 pipeline 每个 descriptor set 分组、分配和写入 descriptor
2. 绑定 vertex buffer / index buffer
3. 如果 item 有 `objectInfo`，写 push constants

descriptor 写入时根据反射 binding 类型区分：

- UBO / SSBO：查 `VulkanBuffer`
- `Texture2D` / `TextureCube`：查 `VulkanTexture`

这里没有单独的“材质绑定器”对象，命令缓冲录制阶段就是最终的绑定汇合点。

## `drawItem(...)` 为什么很薄

`drawItem(...)` 只做一件事：`vkCmdDrawIndexed(...)`。

前提条件都被前面的阶段准备好了：

- pipeline 已绑定
- descriptor set 已绑定
- vertex/index buffer 已绑定
- push constants 已写入

这说明当前实现把复杂度集中在 draw 前的“准备阶段”，实际 draw call 保持极薄。

## 同步与提交

提交时使用的是 swapchain 提供的每帧同步对象：

- acquire 等待 `imageAvailableSemaphore`
- queue submit 完成后 signal `renderFinishedSemaphore`
- present 等待 `renderFinishedSemaphore`
- in-flight fence 负责 CPU 侧限制同一 frame slot 的复用时机

当前提交目标只有 graphics queue。present 走 `presentQueue`，但命令执行和资源上传并没有专门的 transfer queue 分流。

## 当前实现的现实含义

- 优点：路径直接，调试容易，对理解 renderer 非常友好。
- 代价：descriptor 仍是逐 draw 分配/写入；texture 上传是同步等待；没有更细的队列并行与资源生命周期调度。

这也解释了为什么这套后端很适合作为当前项目的“实现说明对象”：结构完整，但还没有被性能优化层层包住。
