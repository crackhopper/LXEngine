# Offline Renderer 实现结构：从 Scene 到 Offline FrameGraph

Offline renderer 像一条离线实验流水线：editor 产出场景说明书，loader 把说明书整理进 `SceneResourceTable`，offline `FrameGraph` 把样品变成 compute 工单，Vulkan backend 按统一 pipeline 路径执行，image writer 把结果保存成可复现记录。

## 总体流水线

| 阶段 | 代码入口 | 输入 | 输出 |
|---|---|---|---|
| CLI 参数 | `src/tools/lxe_offline_render/offline_render_cli.*` | `--scene` / `--profile` / overrides | `OfflineRenderCliOptions` |
| Scene 读取 | `src/infra/scene_io/scene_document.*` | `.scene.yaml` | `SceneDocument` |
| Profile 选择 | `src/core/offline/offline_render_profile.*` | `scene.outputProfiles` + `scene.offlineRender` + CLI overrides | `ResolvedRenderProfile` |
| Scene 加载 | `src/infra/offline/offline_scene_loader.*` | `SceneDocument` | `SceneResourceTable` |
| Offline pass 创建 | `src/core/offline/offline_render_work_graph.*` | `OutputProfile` | `FrameGraph` + `Pass_OfflineRayTrace` |
| Work item 构建 | `src/core/frame_graph/render_queue.*` | `RenderWorkBuildContext::offline(job, shader)` | offline `ComputeDispatch` `RenderWorkItem` |
| Storage resources | `src/core/offline/offline_scene_storage_resources.*` | `SceneResourceTable` + output settings | Scene SSBO、BVH、frame params、output buffer |
| Compute 执行 | `src/backend/vulkan/offline/offline_render_graph_executor.*` | compiled graph + upload plan + pipeline cache | `OfflineReadbackImage` |
| 文件写出 | `src/infra/offline/offline_image_writer.*` | linear RGBA float | EXR / PNG / JSON / raw |

这条流水线的关键是层次分离：`core` 定义 `SceneResourceTable`、profile、offline pass、storage resources 和 `RenderWorkItem`；`infra` 负责 scene/asset/output 这些文件系统边界；`backend/vulkan/offline` 只负责 headless Vulkan 运行时、graph executor、readback 和 shader 包装。

## CLI 只编排，不渲染

`src/tools/lxe_offline_render/main.cpp` 做四件事：

1. 解析命令行参数。
2. 读取 scene 并解析 profile。
3. 调用 loader 和 renderer。
4. 调用 `writeOfflineImageOutputs()` 写文件。

它不直接写 EXR，也不理解 TinyEXR；这些细节封在 `OfflineImageWriter` 里。这个边界让后续 GUI、批处理或测试入口可以复用同一套 writer，而不用复制 CLI 的文件写出逻辑。

## SceneDocument 到 SceneResourceTable

`OfflineSceneLoader` 的任务是“裁剪”：实时 scene 文档包含 editor camera、节点层级、可见性、mesh/material URI、light 和 environment。离线 renderer 不需要 editor 操作状态，也不需要 realtime draw item，所以 loader 使用被选中 `OutputProfile.cameraPath` 找到离线相机，并把可离线计算的字段注册进 `SceneResourceTable`。

| Scene 文档信息 | `SceneResourceTable` 入口 |
|---|---|
| selected `OutputProfile.cameraPath` | `CameraResource` |
| mesh node + transform | `MeshResource` + `ObjectResource` |
| material URI / fallback parameters | `MaterialInstance` |
| directional light | `LightResource` |
| environment HDR URI | deferred scene/environment record |
| loader / resolver warning | `OfflineSceneLoadResult.warnings` |

资产路径统一通过 `OfflineAssetResolver` 处理。内置资产、项目相对路径和 `cache://` URI 都在这一层收敛为本地文件路径。

## Offline FrameGraph 生成 compute 工单

`software-compute` integrator 现在不直接手写一套独立 dispatch 流程，而是和 realtime 一样先走 `FrameGraph` / `RenderWorkQueue` 主干。区别在于 offline context 只生成一个 compute work item。

| 步骤 | 代码入口 | 当前结果 |
|---|---|---|
| 创建离线 graph | `createOfflineRenderFrameGraph(job.output)` | 一个 `Pass_OfflineRayTrace`，target 是 offscreen RGBA16Float |
| 包装 shader | `createOfflinePrimaryRayShader()` | `offline_primary_ray.comp.spv` 被包装成 `IShader`，并校验 9 个 SSBO binding |
| build queue | `FrameGraph::build(RenderWorkBuildContext::offline(job, shader))` | `RenderWorkQueue` 调用 `makeOfflineComputeItem()` |
| 派生资源 | `buildOfflineSceneStorageResources(job)` | descriptor resources 和 `OutputPixels` storage buffer |
| 派生 pipeline | `PipelineBuildDesc::fromRenderWorkItem(item)` | compute pipeline build desc |
| 执行 graph | `OfflineRenderGraphExecutor::execute()` | sync resources、bind pipeline/resources、dispatch、host-read barrier |

这个结构和 [Realtime / Offline 共享流程](../../concepts-design/rendering-pipeline/realtime-offline-shared-flow.md) 对齐：两条入口共享 `RenderWorkItem`、pipeline cache、descriptor 绑定和 command buffer 执行形态，只在输出目标、work kind 和资源形态上分开。

## Ray Buffer 合同

`buildOfflineSceneStorageResources()` 会调用 `SceneResourceTable::buildUploadView()`，再从 upload view 派生 BVH 和 shader frame params。这里最重要的是 C++ 与 GLSL 的布局一致，同时保留 vertex/index/object/material 的索引关系。

| C++ struct | GLSL struct | 用途 |
|---|---|---|
| `SceneGpuVertexRecord` | `lxSceneVertexRecord` | position、normal、uv、tangent |
| `SceneGpuMeshRecord` | `lxSceneMeshRecord` | vertex/index offset 与 geometry index |
| `SceneGpuPrimitiveRecord` | `lxScenePrimitiveRecord` | index offset、mesh/material/object index |
| `SceneGpuObjectRecord` | `lxSceneObjectRecord` | object/world transform、bounds、visibility |
| `SceneGpuMaterialRecord` | `lxSceneMaterialRecord` | baseColor、PBR 参数、emissive、texture flags |
| `SceneSoftwareBvhNode` | `lxSceneBvhNode` | bounds 和 child/primitive range |
| `SceneGpuFrameParams` | `SceneFrameParams` | 相机 basis、尺寸、samples、light、environment |

每次我们新增材质字段、光源 buffer、纹理索引或 AOV，都必须同步修改：

| 必改位置 | 原因 |
|---|---|
| `SceneResourceTable` | CPU 侧表达能力 |
| `OfflineSceneLoader` | 从 scene/material 文件采集数据 |
| `SceneResourceTable::buildUploadView()` | 打包成 indexed GPU records |
| `buildOfflineSceneStorageResources()` | 生成 SSBO、frame params 和 output buffer |
| `SceneSoftwareBvh` | 构建派生 BVH 节点和 primitive 重排引用 |
| GLSL compute shader | 按同一 layout 读取 |
| `test_offline_gpu_scene` | 固定 layout 和基础数据 |

## VulkanOfflineRenderer 的职责边界

`VulkanOfflineRenderer` 是 offline integrator 协调入口。当前只支持 `software-compute`，它初始化 headless `VulkanDevice`，创建 offline `FrameGraph`，把 graph 编译成 compute work，并交给 `OfflineRenderGraphExecutor` 执行。executor 通过 `VulkanResourceManager` 同步 storage buffer、创建或复用 compute pipeline、绑定 descriptor、dispatch `offline_primary_ray.comp`，最后让 output buffer 可以被 CPU readback。

它复用的是低层 Vulkan 基础设施：

| 复用 | 不复用 |
|---|---|
| `VulkanDevice` | swapchain |
| `VulkanBuffer` | realtime swapchain attachment |
| `VulkanCommandBufferManager` | realtime render pass / framebuffer lifecycle |
| `FrameGraph` / `RenderWorkItem` | realtime swapchain target |
| `PipelineCache` / `VulkanPipelineRef` | realtime viewport 状态 |
| shader 编译产物 | editor viewport 状态 |

这个边界让 offline renderer 可以向 path tracing、Vulkan ray tracing、denoiser、AOV 输出演进，而不会被 realtime renderer 的帧循环和 swapchain 生命周期牵住。

## Image Writer 是输出模块

`OfflineImageWriter` 接收 `OfflineImageOutputRequest`，其中包含：

| 字段 | 用途 |
|---|---|
| `job` | scene/profile/camera/output path |
| `image` | Vulkan readback 得到的线性 RGBA float |
| `scenePath` / `job.profileName` | metadata 复现信息 |
| `buildInfo` | 合成后的二进制版本标签 |
| `toneMapping` | PNG preview 的 exposure、mode、gamma |

写出策略：

| 输出 | 实现 |
|---|---|
| EXR | TinyEXR，RGBA half float，scene-linear |
| PNG | stb_image_write，CPU ACES tone mapping + gamma |
| JSON | `std::ofstream` 写 sidecar metadata |
| RGBA32F | 原始 float buffer，调试用 |

EXR/PNG 的具体编码只出现在 writer 和底层 image IO 实现里；`core`、scene loader、offline storage resource 生成和 renderer 都不需要理解图像库细节。

## 当前 MVP 的可扩展点

| 想扩展 | 入口 |
|---|---|
| 增加 exposure CLI 参数 | `offline_render_cli.*` + `OfflineToneMappingSettings` |
| 增加 AOV 输出 | `OfflineReadbackImage` 或新的 AOV buffer + `OfflineImageWriter` |
| 切换 integrator shader | `OfflineIntegrator` factory + 具体 integrator pipeline 选择 |
| 支持 HDR environment sampling | `OfflineSceneLoader` + `SceneResourceTable` + storage resources + shader binding |
| 输出 multipart EXR | 保持 `OfflineImageWriter` API，替换 writer 内部 TinyEXR 调用 |

## 我们已经学会了什么

我们已经把 offline renderer 拆成了九段：CLI、scene document、profile、scene loader、offline FrameGraph、RenderWorkItem/storage resources、BVH、compute executor、image writer。每一段都有清晰输入输出，所以后续实现 path tracing 时，可以明确知道应该改哪一层。

## 下一步

继续读 [源码阅读路线](04-code-reading-guide.md)，我们会把上面的流水线落实到具体文件、函数和测试，再进入 [实现自己的 Path Tracing](05-implement-path-tracing.md)。
