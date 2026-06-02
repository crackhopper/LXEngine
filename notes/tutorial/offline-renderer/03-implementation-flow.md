# Offline Renderer 实现结构：从 Scene 到 Image Writer

Offline renderer 像一条离线实验流水线：editor 产出场景说明书，compiler 把说明书整理成标准样品，GPU builder 把样品装进 buffer，Vulkan compute 执行实验，image writer 把结果保存成可复现记录。

## 总体流水线

| 阶段 | 代码入口 | 输入 | 输出 |
|---|---|---|---|
| CLI 参数 | `src/tools/lxe_offline_render/offline_render_cli.*` | `--scene` / `--profile` / overrides | `OfflineRenderCliOptions` |
| Scene 读取 | `src/infra/scene_io/scene_document.*` | `.scene.yaml` | `SceneDocument` |
| Profile 选择 | `src/core/offline/offline_render_profile.*` | `scene.offlineRender` + CLI overrides | `ResolvedOfflineRenderProfile` |
| IR 编译 | `src/infra/offline/offline_scene_compiler.*` | `SceneDocument` | `OfflineSceneIR` |
| GPU 打包 | `src/backend/vulkan/offline/gpu_scene_builder.*` | `OfflineSceneIR` | triangle/material/camera params |
| BVH 构建 | `src/backend/vulkan/offline/compute_bvh_builder.*` | triangle buffer | `GpuBvhNode` + reordered triangles |
| Compute 执行 | `src/backend/vulkan/offline/vulkan_offline_renderer.*` | GPU buffers + shader | `OfflineReadbackImage` |
| 文件写出 | `src/infra/offline/offline_image_writer.*` | linear RGBA float | EXR / PNG / JSON / raw |

这条流水线的关键是层次分离：`core` 只定义离线 IR 和 profile；`infra` 负责 scene/asset/output 这些文件系统边界；`backend/vulkan/offline` 才拥有 Vulkan pipeline、descriptor 和 shader dispatch。

## CLI 只编排，不渲染

`src/tools/lxe_offline_render/main.cpp` 做四件事：

1. 解析命令行参数。
2. 读取 scene 并解析 profile。
3. 调用 compiler 和 renderer。
4. 调用 `writeOfflineImageOutputs()` 写文件。

它不直接写 EXR，也不理解 TinyEXR；这些细节封在 `OfflineImageWriter` 里。这个边界让后续 GUI、批处理或测试入口可以复用同一套 writer，而不用复制 CLI 的文件写出逻辑。

## SceneDocument 到 OfflineSceneIR

`OfflineSceneCompiler` 的任务是“裁剪”：实时 scene 文档包含 editor camera、节点层级、可见性、mesh/material URI、light 和 environment。离线 renderer 不需要 editor 操作状态，也不需要 realtime draw item，所以 compiler 把可离线计算的字段整理进 `OfflineSceneIR`。

| Scene 文档信息 | Offline IR |
|---|---|
| `scene.gameplayCameraPath` / `--camera` | `OfflineCameraIR` |
| mesh node + transform | `OfflineInstanceIR` + `OfflineMeshIR` |
| material URI / fallback parameters | `OfflineMaterialIR` |
| directional light | `OfflineDirectionalLightIR` |
| environment HDR URI | `OfflineEnvironmentIR` |
| loader / resolver warning | `OfflineSceneIR.warnings` |

资产路径统一通过 `OfflineAssetResolver` 处理。内置资产、项目相对路径和 `cache://` URI 都在这一层收敛为本地文件路径。

## GPU Buffer 合同

`GpuSceneBuilder` 把 IR 变成 shader storage buffer。这里最重要的是 C++ 与 GLSL 的布局一致。

| C++ struct | GLSL struct | 用途 |
|---|---|---|
| `GpuTriangle` | `Triangle` | 三角形位置、法线、材质索引 |
| `GpuMaterial` | `Material` | baseColor、metallic、roughness、emissive |
| `GpuBvhNode` | `BvhNode` | bounds 和 child/triangle range |
| `GpuCameraParams` | `CameraParams` | 相机 basis、尺寸、samples、light、environment |

每次我们新增材质字段、光源 buffer、纹理索引或 AOV，都必须同步修改：

| 必改位置 | 原因 |
|---|---|
| `OfflineSceneIR` | CPU 侧表达能力 |
| `OfflineSceneCompiler` | 从 scene/material 文件采集数据 |
| `GpuSceneBuilder` | 打包成 GPU buffer |
| GLSL compute shader | 按同一 layout 读取 |
| `test_offline_gpu_scene` | 固定 layout 和基础数据 |

## VulkanOfflineRenderer 的职责边界

`VulkanOfflineRenderer` 是 headless compute 执行器。它初始化 headless `VulkanDevice`，创建 compute pipeline，分配 storage buffer，绑定 descriptor，dispatch `offline_primary_ray.comp`，最后把 output buffer map 回 CPU。

它复用的是低层 Vulkan 基础设施：

| 复用 | 不复用 |
|---|---|
| `VulkanDevice` | swapchain |
| `VulkanBuffer` | realtime FrameGraph |
| `VulkanCommandBufferManager` | material pass / render queue |
| shader 编译产物 | editor viewport 状态 |

这个边界让 offline renderer 可以向 path tracing、Vulkan ray tracing、denoiser、AOV 输出演进，而不会被 realtime renderer 的帧循环和 swapchain 生命周期牵住。

## Image Writer 是输出模块

`OfflineImageWriter` 接收 `OfflineImageOutputRequest`，其中包含：

| 字段 | 用途 |
|---|---|
| `job` | scene/profile/camera/output path |
| `image` | Vulkan readback 得到的线性 RGBA float |
| `scenePath` / `profileName` | metadata 复现信息 |
| `buildInfo` | 合成后的二进制版本标签 |
| `toneMapping` | PNG preview 的 exposure、mode、gamma |

写出策略：

| 输出 | 实现 |
|---|---|
| EXR | TinyEXR，RGBA half float，scene-linear |
| PNG | stb_image_write，CPU ACES tone mapping + gamma |
| JSON | `std::ofstream` 写 sidecar metadata |
| RGBA32F | 原始 float buffer，调试用 |

TinyEXR 和 stb_image_write 只出现在 writer 实现文件里；`core`、scene compiler、GPU builder 和 renderer 都不知道第三方图像库存在。

## 当前 MVP 的可扩展点

| 想扩展 | 入口 |
|---|---|
| 增加 exposure CLI 参数 | `offline_render_cli.*` + `OfflineToneMappingSettings` |
| 增加 AOV 输出 | `OfflineReadbackImage` 或新的 AOV buffer + `OfflineImageWriter` |
| 切换 integrator shader | `VulkanOfflineRenderer::loadComputeShader()` 周边 pipeline 选择 |
| 支持 HDR environment sampling | `OfflineSceneCompiler` + `GpuSceneBuilder` + descriptor layout |
| 输出 multipart EXR | 保持 `OfflineImageWriter` API，替换 writer 内部 TinyEXR 调用 |

## 我们已经学会了什么

我们已经把 offline renderer 拆成了八段：CLI、scene document、profile、IR compiler、GPU packing、BVH、compute renderer、image writer。每一段都有清晰输入输出，所以后续实现 path tracing 时，可以明确知道应该改哪一层。

## 下一步

继续读 [源码阅读路线](04-code-reading-guide.md)，我们会把上面的流水线落实到具体文件、函数和测试，再进入 [实现自己的 Path Tracing](05-implement-path-tracing.md)。
