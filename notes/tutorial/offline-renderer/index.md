# Offline Renderer：把场景送进离线实验室

Offline renderer 像一间独立的渲染实验室：editor 和 realtime renderer 负责搭景、调材质、保存场景；offline renderer 读取同一份 scene，把它加载进统一的 `SceneResourceTable`，再通过 offline `FrameGraph` 生成一个 compute `RenderWorkItem`，最后在 headless Vulkan backend 中 dispatch 并 readback。

当前实现已经能从 `assets/scenes/ibl_metal_sphere.scene.yaml` 读取 output profile、离线 settings、相机、内置几何、材质常量、方向光和 background color。`software-compute` integrator 会构建 offline `FrameGraph`，由 `RenderWorkQueue` 把 `SceneResourceTable` 上传视图、CPU BVH、frame params 和 output buffer 收敛成 storage-buffer 资源，再复用 backend 的 pipeline / descriptor / command buffer 执行路径。它还不是完整 path tracer，但已经把“scene 文件 → SceneResourceTable → offline RenderWorkItem → Vulkan compute dispatch → readback → 输出文件”的主链路打通了。

## 核心对象

| 对象 | 当前角色 | 实验室类比 |
|---|---|---|
| `scene.offlineRender` | 在 scene YAML 里声明离线 profile | 实验参数单 |
| `OfflineSceneLoader` | 把 editor scene 文档加载进 `SceneResourceTable` | 把布景清单整理成标准样品 |
| `SceneResourceTable` | 离线、实时和 bindless 共用的 scene GPU 数据合同 | 标准化样品 |
| `SceneResourceTableUploadView` | 导出 indexed GPU records | 装入实验仪器的托盘 |
| `SceneSoftwareBvh` | 基于 primitive / vertex / index / object 关系构建 BVH | 空间索引目录 |
| `backend::offline::VulkanOfflineRenderer` | 选择显式 offline integrator | 实验调度台 |
| `software-compute` | 构建 offline FrameGraph 并运行 headless compute | 实验仪器本体 |
| `Pass_OfflineRayTrace` | 当前离线 compute pass | 单项实验流程 |
| `RenderWorkItem` | 描述一次 offline compute dispatch | 可执行工单 |
| `offline_primary_ray.comp` | 当前 integrator shader | 第一版实验程序 |
| `OfflineImageWriter` | 写出 EXR / PNG / JSON / raw dump | 实验记录员 |

## 阅读顺序

1. [运行离线渲染器](01-run-offline-renderer.md)：从构建、profile、命令行输出开始，先确认链路能跑。
2. [EXR 与 PNG 输出](02-output-and-exr-viewers.md)：理解输出文件、tone mapping 和 EXR 查看工具配置。
3. [实现结构](03-implementation-flow.md)：按代码路径理解 scene、FrameGraph、RenderWorkItem、storage buffer、compute、readback 和 writer 如何连接。
4. [源码阅读路线](04-code-reading-guide.md)：按“命令入口 → 场景资源表 → offline FrameGraph → compute shader → 输出文件”的顺序读代码，建立可调试的心智模型。
5. [实现自己的 Path Tracing](05-implement-path-tracing.md)：理解需要扩展哪些 scene record、storage resource、shader binding 和测试点。

## 当前边界

| 能力 | 当前状态 | 说明 |
|---|---|---|
| Headless Vulkan compute | 可用 | 不依赖 swapchain；通过 offline `FrameGraph` 生成 compute work |
| Scene YAML profile | 可用 | `preview` / `mvp` / `reference` 可在同一 scene 内共存 |
| Realtime/offline RenderWork 共享主干 | 可用 | offline 走 `RenderWorkBuildContext::offline`、`RenderWorkItem`、pipeline cache 和 command buffer |
| 方向光 | 可用 | 当前取第一个 directional light，没有 shadow map |
| 环境 / 背景 | 部分可用 | 当前 shader 读取 output profile 的 `backgroundColor`；HDR environment 纹理采样仍在后续阶段 |
| 材质 | 部分可用 | 支持 baseColor、metallic、roughness、emissive 的打包路径 |
| 输出文件 | 可用 | 当前 CLI 写 `.exr`、`.png`、`.json` 和 `.rgba32f` |
| Compare mode | 可用 | `compareMode: shaded` 或 `albedo` 进入 `SceneFrameParams.compareMode` |
| 多 bounce path tracing | 未实现 | 当前 shader 是 software-compute + 相机 ray + 直接光 + 简单反射 |

## 继续阅读

- [PBR + IBL 教程](../pbr-ibl/index.md)
- [GetStarted](../../get-started.md)
- [场景系统](../../scene-system/index.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
