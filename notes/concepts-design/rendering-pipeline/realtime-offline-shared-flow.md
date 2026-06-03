# Realtime 与 Offline：同一条 RenderWork 流水线

Realtime viewport 和 offline renderer 可以先想成同一座工厂里的两种工位：实时工位接着窗口和 swapchain，追求每帧快速交付；离线工位接着 headless device 和 readback buffer，追求可复现输出。两者加工的工单格式一致，都是 `FrameGraph` 里的 `RenderWorkQueue` 和 `RenderWorkItem`。

这页描述当前代码已经收敛后的共同路径。我们不把 offline 当成绕过 realtime 的轻薄封装，也不让 realtime 独占 FrameGraph；两者都先把场景事实变成 pass 内的 work item，再交给 Vulkan backend 的统一 pipeline / descriptor / command buffer 路径执行。

## 共同的工单从 RenderWorkBuildContext 开始

`RenderWorkBuildContext` 是这条流水线的入口参数。它像一张工单封面：告诉 `FrameGraph` 这次构建属于哪个 domain，并携带该 domain 需要的源数据。

| 对象 | 当前职责 | 工厂类比 |
|---|---|---|
| `RenderWorkBuildContext` | 区分 `Realtime` / `Offline`，携带 scene 或 offline job | 工单封面 |
| `FrameGraph` | 按 `FramePass` 顺序调用每个 queue 的 `build`，并校验 read/write | 排程表 |
| `RenderWorkQueue` | 在单个 pass 内生成、排序、去重 `RenderWorkItem` | 工位任务箱 |
| `RenderWorkItem` | 一次 pipeline work 的最小稳定记录 | 可执行工单 |
| `PipelineBuildDesc` | 从 work item 派生 backend 创建 pipeline 所需输入 | 机器配置单 |
| `RenderUploadPlan` | 从 queue 收集本 pass 需要同步的 `IGpuResource` | 备料清单 |

当前共同调用形状是：

```text
FrameGraph graph = ...;
graph.build(RenderWorkBuildContext::realtime(scene));
graph.compile();
resourceManager.preloadPipelines(graph.collectAllPipelineBuildDescs());

// offline 使用同一组调用，只是 context 换成 offline job + compute shader
graph.build(RenderWorkBuildContext::offline(job, createOfflinePrimaryRayShader()));
graph.compile();
resourceManager.preloadPipelines(graph.collectAllPipelineBuildDescs());
```

这个形状让我们读代码时先看共同主干，再看 domain 特化。新增 pass、target-aware pipeline identity、pipeline preload 这些能力只要接在 `FrameGraph` / `RenderWorkQueue` / `PipelineBuildDesc` 上，就会同时被两个入口看见。

## RenderWorkItem 表达一次 pipeline work

`RenderWorkItem` 不限定为“一个物体的一次 draw”。它表达的是“一次 pipeline 执行所需的上下文”。在传统实时路径里，当前实现通常还是一个 renderable 产出一个 raster item；在 offline compute 路径里，一个 item 可以代表整个离线场景的一次 compute dispatch。

| 字段组 | Realtime raster | Offline compute |
|---|---|---|
| `domain` | `RenderDomain::Realtime` | `RenderDomain::Offline` |
| `kind` | `RasterDraw` 或 `RasterBatch` | `ComputeDispatch` |
| `shaderInfo` | 材质 pass 的 graphics shader | `offline_primary_ray.comp` 包装成的 `IShader` |
| `descriptorResources` | material resources + scene-level resources + IBL resources | `SceneResourceTable` 上传视图导出的 SSBO + output buffer |
| `pipelineKey` | object signature + material signature + target signature | offline scene GPU data + offline compute shader + target signature |
| 特化 payload | `raster.vertexBuffer`、`indexBuffer`、`drawData` | `compute.groupCountX/Y/Z` |

这也是 `PipelineBuildDesc::fromRenderWorkItem(item)` 能同时处理 graphics 和 compute 的原因：它先看 `item.kind`，再填充 `PipelineBuildType::Graphics` 或 `PipelineBuildType::Compute`。

## Realtime 在 queue 内筛选可见对象

实时路径的 `RenderWorkBuildContext::realtime(scene)` 让 `RenderWorkQueue::build` 从 `Scene` 取数据。每个 pass 都按同一套规则收口：

| 步骤 | 当前代码事实 |
|---|---|
| 取 scene-level resources | `Scene::getSceneLevelResources(pass, target)` 按 pass 和 target 过滤 camera/light |
| 取可见性掩码 | `Scene::getCombinedCameraCullingMask(target)`，shadow pass 空掩码时使用全可见兜底 |
| 过滤 renderable | `supportsPass(pass)`、visibility mask、`getValidatedPassData(pass)` |
| 生成 work item | 从 `ValidatedRenderablePassData` 拷贝 shader、材质资源、几何资源和结构签名 |
| 拼接资源 | material resources 在前，scene-level resources 和 IBL resources 追加在后 |
| 排序 | 按 `pipelineKey` stable sort，减少同 pass 内 pipeline 切换 |

FrameGraph 不重新理解材质，也不替 SceneNode 做 validation。进入 queue 的 realtime 数据已经是 pass-level validation 之后的结构事实。

## Offline 也走 FrameGraph，只是产出 compute work

离线路径使用 `RenderWorkBuildContext::offline(job, shader)`。当 pass 是 `Pass_OfflineRayTrace` 时，`RenderWorkQueue` 会创建一个 `ComputeDispatch` item：

| 输入 | 如何进入 work item |
|---|---|
| `OfflineRenderJob` | 提供输出尺寸、scene resource table 上传视图、软件 BVH 等离线任务数据 |
| `createOfflinePrimaryRayShader()` | 把 `offline_primary_ray.comp` 包装成 `IShader`，并校验 SSBO descriptor 合同 |
| `offline::buildOfflineSceneStorageResources(job)` | 生成 `SceneVertices`、`SceneIndices`、`SceneMeshes`、`ScenePrimitives`、`SceneObjects`、`SceneMaterials`、`SceneBvhNodes`、`SceneFrameParams`、`OutputPixels` |
| output size | 转成 8x8 local size 对应的 dispatch group count |

这里的 BVH 仍然是当前 software-compute integrator 使用的自建 BVH。它不是重复数据路径，而是从统一 scene GPU 数据派生出的加速结构；后续硬件 RT 可以复用 scene GPU 记录，再在 backend 内构建 BLAS/TLAS/SBT。

## Backend 用同一套 pipeline 和命令入口执行

Vulkan backend 收到 work item 后，不再分成两套 pipeline cache。共同路径是：

```text
PipelineBuildDesc::fromRenderWorkItem(item)
  -> VulkanResourceManager::preloadPipelines(descs)
  -> PipelineCache::getOrCreatePipeline(desc, renderPass)
  -> VulkanPipelineRef
  -> VulkanCommandBuffer::bindPipeline(ref)
  -> VulkanCommandBuffer::bindResources(resourceManager, ref, item)
  -> VulkanCommandBuffer::executeWorkItem(item)
```

| Vulkan 类型 | 当前职责 |
|---|---|
| `VulkanGraphicsPipeline` | graphics pipeline 的 Vulkan 对象和 layout |
| `VulkanComputePipeline` | compute pipeline 的 Vulkan 对象和 layout |
| `VulkanPipelineRef` | graphics / compute pipeline 的统一非拥有引用 |
| `PipelineCache` | 按 `PipelineKey` 分别缓存 graphics 和 compute pipeline |
| `VulkanResourceManager::getOrCreatePipeline(item)` | 从 work item 派生 build desc，并返回 `VulkanPipelineRef` |
| `VulkanCommandBuffer` | 按 ref 的实际类型选择 bind point，再按 binding name 绑定 descriptor |

`VulkanPipelineRef` 只是一层类型安全的引用并集。它不拥有 pipeline，也不隐藏 graphics/compute 的底层差异；它让执行层可以先共享流程，再在 `std::visit` 内做必要特化。

## 两条入口的特化边界

共享主干不意味着两条入口完全相同。差异保留在真正有差异的地方：

| 维度 | Realtime | Offline |
|---|---|---|
| 输出 | swapchain / frame graph attachment / post process | storage buffer readback |
| pass 内容 | shadow、forward、post、debug overlay 等 raster pass | 当前 `Pass_OfflineRayTrace` compute pass |
| 上传节奏 | 每帧从 pass queue 构建 upload plan，并结合 dirty resource 同步 | 每个 offline pass 显式同步 queue 的 upload plan |
| 命令范围 | render pass / framebuffer / viewport / scissor / draw indexed | compute dispatch，最后增加 shader-write 到 host-read barrier |
| 资源形态 | UBO、texture、frame graph sampled attachment、vertex/index buffer | SSBO scene tables、software BVH、output pixel buffer |
| 交付 | queue submit 后 present | queue submit 后 host readback |

这个边界让我们可以同时保持两点：共同概念一致，底层实现不假装相同。

## 当前边界

| 已实现 | 当前边界 |
|---|---|
| Realtime/offline 都通过 `FrameGraph::build(context)` 生成 work queue | FrameGraph 仍按显式 pass 顺序，不做自动拓扑排序 |
| `RenderWorkItem` 支持 raster / compute / 后续 RT work kind | 硬件 ray tracing pipeline 还没有实现 |
| `PipelineCache` 同时缓存 graphics 和 compute pipeline | pipeline eviction / LRU 没有实现 |
| Offline software-compute 使用统一 scene GPU 数据和自建 BVH | offline 多 pass 的更多材质/lighting pass 还需要继续扩展 |
| backend 统一用 `VulkanPipelineRef` 绑定 pipeline 和 descriptor | realtime render pass / framebuffer 生命周期仍是 graphics 特化 |

## 继续阅读

- [FrameGraph：一帧的 Pass 排程表](framegraph.md)
- [RenderWorkQueue：把 Scene 收敛成 Work 列表](render-queue.md)
- [什么是 Pipeline](../../concepts/material/what-is-pipeline.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
