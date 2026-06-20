# Offline Renderer：把场景送进离线实验室

Offline renderer 像一间独立的渲染实验室：editor 和 realtime renderer 负责搭景、调材质、保存场景；offline renderer 读取同一份 scene，把它加载进统一的 `SceneResourceTable`，再通过 offline `FrameGraph` 和 `RenderWorkCompiler` 生成一个 compute `RenderInput` / `RenderInputDesc`，最后在 headless Vulkan backend 中 dispatch 并 readback。

当前实现已经能从 `assets/scenes/generated/helmet_standard_pbr.scene.yaml` 读取 output profile、相机、glTF mesh、standard-pbr 材质、方向光、IBL/skybox 节点和 render path graph。`lxe_offline_render` 不再维护独立 offline job / offline work graph；它把被选中的 `OutputProfile.renderPathGraph` 交给 `FrameGraphExecutor`，由 `RenderWorkCompiler` 从 `SceneResourceTable`、render feature、material 和 readback contract 准备 pass work，再让 Vulkan backend 执行 raster 或 compute pass 并返回 payload。它还不是完整 path tracer，但已经把“同一份 scene → 不同 output profile / render path graph → FrameGraphExecutor → readback payload → 输出文件”的主链路打通了。

## 核心对象

| 对象 | 当前角色 | 实验室类比 |
|---|---|---|
| `scene.outputProfiles` | 在同一 scene YAML 里声明输出 profile 与 render path graph | 实验参数单 |
| `OfflineSceneLoader` | 把 editor scene 文档加载进 `SceneResourceTable` | 把布景清单整理成标准样品 |
| `SceneResourceTable` | 离线、实时和 bindless 共用的 scene GPU 数据合同 | 标准化样品 |
| `RenderPathGraph` | profile 选择的 pass DAG，声明 pass、目标、readback 和 feature | 实验流程图 |
| `RenderWorkCompiler` | 把 graph pass 准备成 draw/dispatch work 与 `RenderInputDesc` | 可执行工单生成器 |
| `FrameGraphExecutor` | 统一执行 raster / compute pass，并生成 readback payload | 实验仪器本体 |
| `offline_standard_pbr_raytrace.render-path.yaml` | OfflineRT raytrace profile 使用的 compute graph | 光追实验流程 |
| `forward_offline_*.render-path.yaml` | Forward 对比 profile 使用的 raster graph | 光栅对比流程 |
| `OfflineImageWriter` | 写出 EXR / PNG / JSON / raw dump | 实验记录员 |

## 阅读顺序

1. [运行离线渲染器](01-run-offline-renderer.md)：从构建、profile、命令行输出开始，先确认链路能跑。
2. [EXR 与 PNG 输出](02-output-and-exr-viewers.md)：理解输出文件、tone mapping 和 EXR 查看工具配置。
3. [实现结构](03-implementation-flow.md)：按代码路径理解 scene、FrameGraph、RenderInputDesc、storage buffer、compute、readback 和 writer 如何连接。
4. [源码阅读路线](04-code-reading-guide.md)：按“命令入口 → 场景资源表 → offline FrameGraph → compute shader → 输出文件”的顺序读代码，建立可调试的心智模型。
5. [实现自己的 Path Tracing](05-implement-path-tracing.md)：理解需要扩展哪些 scene record、storage resource、shader binding 和测试点。

## 当前边界

| 能力 | 当前状态 | 说明 |
|---|---|---|
| Headless Vulkan raster/compute | 可用 | 不依赖 swapchain；通过 `FrameGraphExecutor` 执行 profile 指定的 render path graph |
| Scene YAML profile | 可用 | `forward_no_ibl` / `ibl_only` / `forward_ibl` / `raytrace` 在同一 scene 内共存 |
| Realtime/offline render input 共享主干 | 可用 | offline 复用 `RenderWorkCompiler`、`RenderInputDesc`、pipeline cache、descriptor 和 command buffer |
| 方向光 | 可用 | 当前取第一个 directional light，没有 shadow map |
| 环境 / 背景 | 可用 | scene skybox 支持 finite/infinite；Forward IBL 和 OfflineRT miss path 都能消费 infinite environment |
| 材质 | 部分可用 | 支持 baseColor、metallic、roughness、emissive 的打包路径 |
| 输出文件 | 可用 | 当前 CLI 写 `.exr`、`.png`、`.json` 和 `.rgba32f` |
| Forward / IBL 对比 | 可用 | 同一 scene 可跑 direct-only、IBL-only、direct+IBL |
| 多 bounce path tracing | 未实现 | 当前 OfflineRT 是 primary ray + direct lighting + miss environment sampling |

## 继续阅读

- [PBR + IBL 教程](../pbr-ibl/index.md)
- [GetStarted](../../get-started.md)
- [场景系统](../../scene-system/index.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
