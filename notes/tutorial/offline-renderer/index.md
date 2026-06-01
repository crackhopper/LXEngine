# Offline Renderer：把场景送进离线实验室

Offline renderer 像一间独立的渲染实验室：editor 和 realtime renderer 负责搭景、调材质、保存场景；offline renderer 读取同一份 scene，把它编译成更适合离线计算的数据，再用 Vulkan compute 跑一个可替换的 integrator。

当前实现已经能从 `assets/scenes/ibl_metal_sphere.scene.yaml` 读取相机、几何、材质、方向光和环境配置，构建 CPU BVH，上传到 headless Vulkan compute pipeline，并输出线性 HDR `rgba32f` 调试图。它还不是完整 path tracer，但它已经把“场景文件 → 离线 IR → GPU buffer → compute shader → readback”的主链路打通了。

## 核心对象

| 对象 | 当前角色 | 实验室类比 |
|---|---|---|
| `scene.offlineRender` | 在 scene YAML 里声明离线 profile | 实验参数单 |
| `OfflineSceneCompiler` | 把 editor scene 文档编译成离线 IR | 把布景清单整理成实验输入 |
| `OfflineSceneIR` | 离线渲染器消费的 CPU 场景表示 | 标准化样品 |
| `GpuSceneBuilder` | 把 IR 打包成 std430 GPU buffer 数据 | 装入实验仪器的托盘 |
| `ComputeBvhBuilder` | 构建当前 compute shader 可遍历的 BVH | 空间索引目录 |
| `backend::offline::VulkanOfflineRenderer` | headless Vulkan compute 执行器 | 实验仪器本体 |
| `offline_primary_ray.comp` | 当前 integrator shader | 第一版实验程序 |

## 阅读顺序

1. [运行离线渲染器](01-run-offline-renderer.md)：从构建、profile、命令行输出开始，先确认链路能跑。
2. [实现自己的 Path Tracing](02-implement-path-tracing.md)：理解需要扩展哪些 IR、buffer、shader 和测试点。

## 当前边界

| 能力 | 当前状态 | 说明 |
|---|---|---|
| Headless Vulkan compute | 可用 | 不依赖 swapchain；在 llvmpipe 上也能跑 smoke |
| Scene YAML profile | 可用 | `preview` / `mvp` / `reference` 可在同一 scene 内共存 |
| 方向光 | 可用 | 当前取第一个 directional light，没有 shadow map |
| 环境光 | 部分可用 | 当前 shader 使用程序化环境色；HDR 纹理采样仍在后续阶段 |
| 材质 | 部分可用 | 支持 baseColor、metallic、roughness、emissive 的打包路径 |
| 输出文件 | 调试可用 | 当前 CLI 写 `.rgba32f`；EXR/PNG 属于下一阶段输出模块 |
| 多 bounce path tracing | 未实现 | 当前 shader 是 primary-ray + 直接光 + 简单环境反射 |

## 继续阅读

- [PBR + IBL 教程](../pbr-ibl/index.md)
- [场景系统](../../scene-system/index.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
- [REQ-054-b](../../requirements/054-b-vulkan-compute-offline-renderer-mvp.md)
