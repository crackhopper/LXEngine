# Vulkan Backend

> Vulkan backend 是 core 抽象到 Vulkan API 的落地点。它不决定“什么时候开始一帧”，也不负责业务层 update hook；这些编排职责已经上移到 `EngineLoop`。backend 负责把 `FrameGraph` 和 `RenderWorkItem` 真实提交到 GPU。
>
> 当前事实以 `src/backend/vulkan/` 和本页说明为准。

## 现在怎么读

这页是当前 Vulkan backend 的维护入口。更细的源码阅读从下方“从哪里进入源码”开始；文件级解释优先看 `notes/source_analysis/` 中已经生成的页面。

## 一页版总结

- `VulkanRenderer` 负责 orchestration：初始化、`initScene()`、`uploadData()`、`draw()`
- `VulkanDevice` 负责 instance/device/queue/surface format/depth format
- `VulkanSwapchain` 负责 swapchain image、depth、framebuffer、同步对象
- `VulkanResourceManager` 负责 CPU 资源镜像与 pipeline cache
- `VulkanCommandBuffer` 在执行阶段汇合 pipeline、descriptor、vertex/index buffer、push constants 或 compute dispatch 参数
- `VulkanRendererImpl` 现在只是 `VulkanRenderer` 的私有实现对象，renderer 继承边界由 `VulkanRenderer` 对外承担
- `backend::offline::VulkanOfflineRenderer` 是离线路径协调入口：它按 `OfflineRenderSettings.integrator` 选择显式 integrator，当前可用实现是 `software-compute`

## 当前实现最重要的约束

- 所有可能触发 Vulkan loader 初始化的可执行程序，都必须在 `main()` 一开始调用 `LX_core::expSetEnvVK()`
- 这不是“可选清理项”，而是为了抑制 implicit validation layer 自动加载时额外产生的 `.log` 文件；调用必须早于 window / renderer / Vulkan instance 初始化
- Linux 下的窗口化 Vulkan/SDL 测试还要求 `libSDL3.so.0` 可用，并且必须有视频设备来源：真实桌面会话，或者 `Xvfb`
- 在 headless Linux shell 里，优先用 `xvfb-run -a ./src/test/<test-binary>`；这就足够让 SDL 拿到 video device，不需要额外装完整桌面
- 如果没跑在桌面或 `Xvfb` 下，这类测试通常会以 `No available video device` 明确 skip；先排环境，再排 renderer 逻辑
- 物理设备选择现在按“先判定功能是否满足，再按设备类型偏好排序”处理；独显优先，但集显/虚拟 GPU 只要满足队列、扩展和 surface 要求也允许启动
- descriptor 路由按 binding name，不按硬编码 slot 枚举
- scene-level UBO 已经在 queue 构建阶段合并好，backend 按 `RenderWorkItem` 中的资源录制 descriptor
- 离线 compute 路径上传 `SceneResourceTableUploadView` 导出的统一 `SceneGpu*` SSBO，并把 `SceneSoftwareBvh` 作为派生加速 buffer；backend 不创建第二份 scene IR
- `VulkanResourceManager` 不直接持有 pipeline map，而是委托给 `PipelineCache`；graphics 和 compute 都通过 `getOrCreatePipeline(item)` 返回 `VulkanPipelineRef`
- `VulkanResourceManager` 现在按 `IGpuResource::getBackendCacheIdentity()` 做 cache key，资源身份来自显式 backend cache identity
- GPU 资源缓存带短暂闲置宽限期：资源漏同步一帧不会立刻销毁重建，但长期不用仍会被 `collectGarbage()` 回收
- `FrameGraph` 当前执行 4 个 `Pass_Shadow` cascade，再执行 `Pass_Forward` 和需要的 debug / overlay 路径
- `kMaxFramesInFlight` 在 `VulkanRenderer` 内部只有一个定义，初始化路径和 draw 路径共用同一来源
- Vulkan viewport 现在固定使用正高度：`x=0`、`y=0`、`width=w`、`height=h`；Vulkan 需要的 Y 翻转由 camera projection matrix 承担

## 显示出口的色彩边界

我们可以把显示出口想成一扇只允许通过一次的门：HDR lighting 先在线性空间里计算，tone mapping 把很宽的亮度范围压到屏幕能表达的 `0..1`，最后才进入显示设备需要的 sRGB 编码。如果同一张图在 shader 里做一次 gamma，又写进 sRGB swapchain 让硬件再做一次 sRGB 编码，结果会变亮、发白，局部颜色对比也会被压弱。

当前 Vulkan realtime 路径按下面的职责分工处理：

| 阶段 | 空间 | 当前职责 |
|---|---|---|
| `Forward` / `DeferredLighting` | linear HDR | 输出 `RGBA16Float` HDR color，不做最终显示 gamma |
| `PostProcess` tone mapping | display-linear | 执行 ACES 或 Reinhard，把 HDR 压到 `0..1` 的线性显示值 |
| sRGB swapchain | sRGB encoded | 当目标格式是 `BGRA8Srgb` / `RGBA8Srgb` 时，`PostProcess` 输出 linear，Vulkan sRGB attachment 在写入时完成 linear-to-sRGB 编码 |
| UNORM swapchain fallback | sRGB encoded | 当目标格式只是 `BGRA8` / `RGBA8` 时，`PostProcess` 通过 `outputEncodingMode = Srgb` 手动做 `linear -> sRGB gamma` |
| realtime profile PNG | sRGB encoded file | 离屏 profile 读回 `RGBA16Float` 后，由 CPU 的 `writeToneMappedPng(...)` 做一次 tone mapping + gamma，生成可查看 PNG |

这意味着 tone mapping 仍然是必须的。sRGB swapchain 或硬件只负责最后的 transfer function 编码，不负责把 HDR 高亮压回可显示范围。没有 tone mapping，超过 `1.0` 的 HDR 值会直接夹断；有 tone mapping 但重复 gamma，图像会比 profile 输出更亮、更灰。

格式类型也必须保留这个边界。`ImageFormat::BGRA8` 和 `ImageFormat::BGRA8Srgb` 不是同一种 pipeline target；`ImageFormat::RGBA8` 和 `ImageFormat::RGBA8Srgb` 也一样。`VulkanDevice` 选择到 `VK_FORMAT_B8G8R8A8_SRGB` / `VK_FORMAT_R8G8B8A8_SRGB` 时，`makeSwapchainTarget()` 会保留 sRGB 语义，让 `VkPipelineRenderingCreateInfo`、render pass attachment 和 swapchain image view 使用同一类格式。这样可以避免 pipeline 以 UNORM 创建、实际 image view 却是 SRGB 的 validation error。

调试这类问题时，我们先问三个问题：

| 问题 | 应该看到的事实 |
|---|---|
| HDR pass 是否已经做了最终 gamma | 不应该。PBR/Forward shader 输出 HDR 或 display-linear 中间结果，不能写最终 sRGB |
| PostProcess 是否知道目标编码 | 应该。`PostProcessUBO.outputEncodingMode` 由目标 `ImageFormat` 决定，而不是由 scene 名字、相机或测试路径推断 |
| pipeline target 和 swapchain image view 是否同格式族 | 应该。sRGB surface 必须形成 sRGB pipeline target，UNORM surface 才形成 UNORM target |

### Debug color-transfer export

`render debug export-path color-transfer [camera-path] [out-dir]` runs a
diagnostic render path that exports HDR, tone-mapped linear, sRGB-attachment,
UNORM manual-sRGB, and fixed ramp targets into one bundle. This command is for
root-cause localization. It is not a production color-management policy.

The important rule is that raw LDR PNG outputs from this bundle are written
without an extra CPU gamma pass. If `debug.final.srgb` is dark while
`debug.final.unorm_manual_srgb` is correct, the next fix must inspect attachment
format, dynamic rendering pipeline format, image view format, or readback. If
the ramp targets agree but the helmet targets diverge, the bug is probably in
PBR/tone mapping/input data rather than Vulkan sRGB attachment conversion.

`manifest.json` is part of the evidence, not just an index. It records the
render-path graph URI, camera path, CPU preview tone-mapping transform,
surface/swapchain formats, per-target stats, and per-pass attachment contract,
pipeline color format, shader URI, and `outputEncodingMode`. For the canonical
comparison, `debug.final.srgb` must use an sRGB attachment with linear shader
output, while `debug.final.unorm_manual_srgb` must use a UNORM attachment with
manual sRGB shader output. A mismatch in those fields means the render boundary
is wrong before any image comparison is meaningful.

## 阴影调试复盘

方向光阴影像一台临时搬到灯光方向上的正交相机：我们先用 active camera 的视锥决定要覆盖的世界范围，再从灯光方向渲染一张深度图，Forward pass 里的像素再拿自己的世界坐标回到这张深度图里查深度。

这次阴影位置不匹配的根因不在 shader，也不在 cull mode，而在 CPU 侧矩阵链路里。`CameraComponent` 的 view matrix 走 node transform / quaternion 路径，而 directional shadow cascade 走 `Mat4f::lookAt(...)`。`lookAt` 当时把相机基向量按错误的行列位置写入矩阵；在项目的列主序乘法约定下，这会让 shadow pass 使用的 light view 与我们用 debug camera 看到的 light view 不一致。

最后用一个很小的证据闭环定位问题：

| 证据 | 说明 |
|---|---|
| `probe shadow-project <node> <camera> <light>` 投影同一个 cube 的 8 个端点 | 把 camera、cascade0、shadow、debugView 四条矩阵路径放在同一张像素坐标表里比较 |
| light-view debug camera 的正交参数与 cascade debug view 一致 | 说明不是 ortho bounds / near / far 本身错 |
| 修复前 cube 端点在 camera 路径和 cascade 路径里的像素位置不同 | 说明错误发生在 view matrix 或矩阵布局，而不是 shadow map 采样 |
| `Mat4T::lookAt` 的 basis-row 测试先失败，修复后通过 | 把根因收敛到共享数学函数，而不是 Vulkan pass 录制 |
| 修复后 probe 中 camera / cascade0 / shadow / debugView 的端点投影一致 | 说明 shadow map 使用的矩阵已经能被 light-view camera 复现 |

这里有一个维护边界需要记住：`cam light-view` 创建的是调试相机，它只用于观察 directional shadow cascade 的相机参数，不应该抢占 editor 的 active camera。active camera 的当前事实由 `CameraComponent::isActive()` 表达，EditorState 只负责在 preview/editor 模式切换时把这个状态同步到 scene。

### 动态移动 editor camera 时的白色区域

当前每帧的顺序是：

```text
EngineLoop::tickFrame()
  updateHook()                 # editor camera / rig 更新
  renderer.uploadData()        # DirectionalLight::updateShadowCascadesForCamera(active camera)
  renderer.draw()              # Shadow pass -> Forward pass
```

因此，active camera 移动后，directional light cascade 和 shadow map 会在同一帧重新计算。调试时可以用两步确认：

```text
probe shadow-project /cube_caster /editor_cam /dir_light 1920 1009
render debug dump shadow.cascade0 data/debug/dump/<name>.bmp
```

如果移动相机后 probe 里的 `debugViewParams` 改变，并且新的 `shadow.cascade0` dump 也改变，就说明 shadow map 已经随 active camera 更新。Forward 里某些区域变白，通常不是“没有实时读取 shadow map”，而是当前 shadow volume 只按 active camera 视锥拟合：落到 shadow map UV 之外的像素会按 shader 里的边界规则返回 `1.0`，也就是无阴影。这个行为在 `assets/shaders/glsl/blinnphong_0.frag` 的 `sampleShadowMap(...)` 里收口。

立方体穿过平面、并且斜着摆放时，接触边缘还会放大 bias 问题。当前 `shadowBias` 按世界空间距离解释，CPU 在 `DirectionalLight::updateShadowCascadesForCamera(...)` 中为每个 cascade 写入 `cascadeDepthRanges`，shader 再把 world bias 除以当前 cascade 的 light-depth range，得到真正用于深度比较的 normalized bias。这样 `Shadow Distance` 变大时，bias 不会按固定 normalized 深度把阴影推离接触点。

后续要让移动相机时阴影更稳定，我们需要在 shadow volume 策略上继续收敛，例如为 simple shadow map 提供固定世界范围，或在相机视锥 bounds 上加入可见接收面与离屏 caster 的扩展范围。这个问题属于 shadow coverage 策略，不是这次 `lookAt` 矩阵修复的同一层。

## 屏幕坐标约定

当前 backend 统一采用一套 backend projection 约定：

| 层级 | 当前约定 | 代码入口 |
|---|---|---|
| editor / 屏幕像素 | 左上角原点，`x` 向右，`y` 向下 | `SceneInteractionController` / `ViewportOverlay` |
| CPU 交互 NDC | OpenGL 风格屏幕语义：上方像素对应正 `y`，下方像素对应负 `y` | `CameraComponent::pickRay()` / overlay project |
| Vulkan camera projection | 投影矩阵内翻转 `Y`，并输出 `0..1` 深度 | `CameraComponent::getProjMatrix(..., GraphicsAPI::Vulkan)` |
| Vulkan viewport | `VkViewport{0, 0, width, height, 0, 1}`，只负责正高度 framebuffer 映射 | `details/commands/command_buffer.cpp` |

这样做的结果是：CPU 侧的 pick / project 公式继续使用“上正下负”的 OpenGL 风格 NDC，GPU 侧由 Vulkan projection matrix 完成后端适配；viewport 不再插入第二次镜像。

## 离线 Integrator 边界

离线 renderer 像一台无窗口实验设备：入口仍在 Vulkan backend，但场景数据来自 core 层统一资源表。`VulkanOfflineRenderer::render(job)` 只做显式 integrator 选择；当前 `software-compute` integrator 会构建 offline `FrameGraph`，让 `Pass_OfflineRayTrace` 通过 `RenderWorkQueue` 产出 compute work item，再交给 `OfflineRenderGraphExecutor` 执行。

| Integrator | 当前职责 | 数据入口 |
|---|---|---|
| `software-compute` | 初始化 headless `VulkanDevice`，创建 offline FrameGraph，预构建 compute pipeline，按 pass upload plan 同步 SSBO，dispatch `offline_primary_ray.comp`，readback `OfflineReadbackImage` | `SceneResourceTableUploadView` + `SceneSoftwareBvh` |
| `hardware-ray-tracing` | 未实现；未来应创建 BLAS/TLAS/SBT 并复用同一份 scene GPU 记录 | `SceneResourceTableUploadView` |

`software-compute` 的 descriptor contract 使用统一 block 名：`SceneVertices`、`SceneIndices`、`SceneMeshes`、`ScenePrimitives`、`SceneObjects`、`SceneMaterials`、`SceneBvhNodes`、`SceneFrameParams` 和 `OutputPixels`。这些名字由 shader reflection 测试和 integrator 的 descriptor validation 同时保护。

## 从哪里进入源码

- 顶层：`src/backend/vulkan/vulkan_renderer.cpp`
- 资源：`src/backend/vulkan/details/resource_manager.cpp`
- pipeline：`src/backend/vulkan/details/pipelines/`
- descriptor：`src/backend/vulkan/details/descriptors/`
- draw 命令：`src/backend/vulkan/details/commands/`
- offline integrator：`src/backend/vulkan/offline/software_compute_offline_integrator.cpp`
- offline graph executor：`src/backend/vulkan/offline/offline_render_graph_executor.cpp`
- offline shader wrapper：`src/backend/vulkan/offline/offline_compute_shader.cpp`
