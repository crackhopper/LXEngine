# Offline Renderer：把场景送进离线实验室

Offline renderer 像一间独立的渲染实验室：editor 和 realtime renderer 负责搭景、调材质、保存场景；offline renderer 读取同一份 scene，把它编译成更适合离线计算的数据，再用 Vulkan compute 跑一个可替换的 integrator。

当前实现已经能从 `assets/scenes/ibl_metal_sphere.scene.yaml` 读取相机、几何、材质、方向光和环境配置，构建 CPU BVH，上传到 headless Vulkan compute pipeline，并把同一份线性 HDR readback 写成 EXR、tone-mapped PNG、JSON metadata 和 `.rgba32f` 调试图。它还不是完整 path tracer，但它已经把“场景文件 → 离线 IR → GPU buffer → compute shader → readback → 输出文件”的主链路打通了。

## 核心对象

| 对象 | 当前角色 | 实验室类比 |
|---|---|---|
| `scene.offlineRender` | 在 scene YAML 里声明离线 profile | 实验参数单 |
| `OfflineSceneCompiler` | 把 editor scene 文档编译成离线 IR | 把布景清单整理成实验输入 |
| `OfflineSceneIR` | 离线渲染器消费的 CPU 场景表示 | 标准化样品 |
| `OfflineRaySceneBuilder` | 把 IR 注册到共享资源表，再导出 indexed ray buffers | 装入实验仪器的托盘 |
| `OfflineBvhBuilder` | 基于 primitive / vertex / index 关系构建 BVH | 空间索引目录 |
| `backend::offline::VulkanOfflineRenderer` | headless Vulkan compute 执行器 | 实验仪器本体 |
| `offline_primary_ray.comp` | 当前 integrator shader | 第一版实验程序 |
| `OfflineImageWriter` | 写出 EXR / PNG / JSON / raw dump | 实验记录员 |

## 阅读顺序

1. [运行离线渲染器](01-run-offline-renderer.md)：从构建、profile、命令行输出开始，先确认链路能跑。
2. [EXR 与 PNG 输出](02-output-and-exr-viewers.md)：理解输出文件、tone mapping 和 EXR 查看工具配置。
3. [实现结构](03-implementation-flow.md)：按代码路径理解 scene、IR、GPU buffer、compute、readback 和 writer 如何连接。
4. [源码阅读路线](04-code-reading-guide.md)：按“命令入口 → 场景 IR → GPU buffer → compute shader → 输出文件”的顺序读代码，建立可调试的心智模型。
5. [实现自己的 Path Tracing](05-implement-path-tracing.md)：理解需要扩展哪些 IR、buffer、shader 和测试点。

## 当前边界

| 能力 | 当前状态 | 说明 |
|---|---|---|
| Headless Vulkan compute | 可用 | 不依赖 swapchain；在 llvmpipe 上也能跑 smoke |
| Scene YAML profile | 可用 | `preview` / `mvp` / `reference` 可在同一 scene 内共存 |
| 方向光 | 可用 | 当前取第一个 directional light，没有 shadow map |
| 环境光 | 部分可用 | 当前 shader 使用程序化环境色；HDR 纹理采样仍在后续阶段 |
| 材质 | 部分可用 | 支持 baseColor、metallic、roughness、emissive 的打包路径 |
| 输出文件 | 可用 | 当前 CLI 写 `.exr`、`.png`、`.json` 和 `.rgba32f` |
| 多 bounce path tracing | 未实现 | 当前 shader 是 primary-ray + 直接光 + 简单环境反射 |

## 继续阅读

- [PBR + IBL 教程](../pbr-ibl/index.md)
- [场景系统](../../scene-system/index.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
- [运行离线渲染器](01-run-offline-renderer.md)
- [EXR 与 PNG 输出](02-output-and-exr-viewers.md)
