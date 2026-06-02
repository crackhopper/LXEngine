# Vulkan Offline Renderer：从 Scene IR 到 Compute Readback

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页把 offline renderer 当成一条独立实验管线来读，入口是
[src/backend/vulkan/offline/vulkan_offline_renderer.hpp](../../../../../../src/backend/vulkan/offline/vulkan_offline_renderer.hpp)。
关注的问题是：为什么离线渲染不直接复用 realtime FrameGraph，而是先把
scene 文档编译成 `OfflineSceneIR`，再通过共享资源表打包成 compute shader 的 indexed storage buffer。

可以先带着一个问题阅读：我们要怎样在不创建 swapchain 的情况下，从同一份
`.scene.yaml` 得到一张可复现实验图？答案就在 `OfflineSceneCompiler`、
`OfflineRaySceneBuilder`、`OfflineBvhBuilder`、`VulkanOfflineRenderer` 和
`OfflineImageWriter` 的分层里。

源码入口：[vulkan_offline_renderer.hpp](../../../../../src/backend/vulkan/offline/vulkan_offline_renderer.hpp)

关联源码：

- [offline_scene.hpp](../../../../src/core/offline/offline_scene.hpp)
- [offline_ray_scene.hpp](../../../../src/core/offline/offline_ray_scene.hpp)
- [offline_scene_compiler.hpp](../../../../src/infra/offline/offline_scene_compiler.hpp)

## vulkan_offline_renderer.hpp

源码位置：[vulkan_offline_renderer.hpp](../../../../../src/backend/vulkan/offline/vulkan_offline_renderer.hpp)

### VulkanOfflineRenderer 是 headless compute 执行器

`VulkanOfflineRenderer` 是离线渲染实验场当前的 Vulkan 后端入口。它接收
`OfflineRenderJob`，内部初始化 headless `VulkanDevice`，创建 compute pipeline，
上传 indexed ray scene/material/BVH/camera buffer，dispatch compute shader，再把线性
float RGBA readback 回 CPU。

它故意不复用 realtime `FrameGraph`、swapchain 和 draw item，因为离线 renderer 的
目标是可复现实验、ground truth 对比和 path tracing 迭代。共享点放在更低层：
Vulkan device、buffer、command manager、shader 编译产物和 core/infra 的 scene
输入链路。

## offline_scene.hpp

源码位置：[offline_scene.hpp](../../../../src/core/offline/offline_scene.hpp)

### OfflineSceneIR 是离线实验室的标准样品

`OfflineSceneIR` 是实时 scene 文档和离线 integrator 之间的隔离层。
实时渲染需要 `SceneNode`、component、FrameGraph、material pass 和 editor
状态；离线渲染只需要相机、几何、材质、光源、环境以及可复现实验参数。

因此这组 IR 类型刻意不携带 Vulkan 句柄，也不直接复用实时
`RenderingItem`。它把 `.scene.yaml` 中能够离线计算的事实收敛成稳定数据，
让 CPU compiler、GPU packing、path tracing shader 和输出模块可以独立演进。

## offline_scene_compiler.hpp

源码位置：[offline_scene_compiler.hpp](../../../../src/infra/offline/offline_scene_compiler.hpp)

### Compiler 把 editor 文档翻译成离线输入

`OfflineSceneCompiler` 位于 `infra`，因为它同时理解 scene YAML 文档、
资产 URI 解析和 core 层 `OfflineSceneIR`。它的职责不是渲染，也不是保存
editor 状态，而是把可见 mesh instance、材质参数、相机、方向光和环境配置
整理成离线 renderer 可消费的紧凑数据。

这个边界让 `lxe_offline_render` CLI 不依赖 `src/demos/lxe_editor/`。
后续支持 glTF、HDR environment、albedo texture 或 bake cache 时，优先扩展
compiler/resolver 到 IR 的这条输入链路，而不是让 Vulkan offline renderer
反向读取 editor 数据结构。

## offline_ray_scene.hpp

源码位置：[offline_ray_scene.hpp](../../../../src/core/offline/offline_ray_scene.hpp)

### OfflineRaySceneBuilder 固定共享资源到 shader buffer 的合同

`OfflineRaySceneBuilder` 把 `OfflineSceneIR` 注册进 `SceneResourceTable`，再从
snapshot 导出 compute shader 可以直接读取的 std430 storage buffer。它保留
vertex/index/mesh/primitive/object/material 的索引关系，ray hit 后可以用
barycentric 坐标插值 normal 和 uv。

这层也是未来 path tracing 扩展最容易出错的边界：新增材质参数、纹理索引、
light buffer 或 AOV 输出时，必须同步修改 C++ struct、GLSL struct、descriptor
layout 和 `test_offline_gpu_scene` 的 layout contract。

### OfflineBvhBuilder 是当前 shader 的 primitive 遍历索引

`OfflineBvhBuilder` 在 CPU 上为 primitive buffer 构建一棵紧凑 BVH，然后把节点
上传给 compute shader。当前节点布局把 bounds、left/first 和 packed
right/primitiveCount 放进两个 `vec4`，保持 32 字节 std430 合同。

这不是最终高性能加速结构，而是 MVP 的可验证起点。它让离线 renderer 先拥有
closest-hit 查询、shadow ray 查询和后续 path tracing 的基础空间索引；未来可以
替换 split 策略、实例层 BVH 或 Vulkan hardware ray tracing，但 shader 与测试必须
同步迁移节点编码。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 补充说明

## 从命令行到 shader 的阅读顺序

我们读 offline renderer 时，不要先跳进 Vulkan descriptor 细节。更稳的顺序是：

| 顺序 | 文件 | 读什么 |
|---|---|---|
| 1 | `src/tools/lxe_offline_render/main.cpp` | CLI 如何选择 scene、profile、camera 和输出路径 |
| 2 | `src/infra/scene_io/scene_document.*` | `.scene.yaml` 如何被解析成共享文档 |
| 3 | `src/infra/offline/offline_scene_compiler.*` | editor scene 文档如何被裁剪成离线 IR |
| 4 | `src/core/offline/offline_ray_scene.*` | IR 如何注册到共享资源表并打包成 indexed shader storage buffer |
| 5 | `src/core/offline/offline_ray_scene.*` | primitive 如何获得可遍历 BVH |
| 6 | `src/backend/vulkan/offline/vulkan_offline_renderer.*` | pipeline、descriptor、dispatch、readback 如何连起来 |
| 7 | `assets/shaders/glsl/offline_primary_ray.comp` | 当前 integrator 如何生成相机 ray、遍历 BVH、计算直接光和环境 |
| 8 | `src/infra/offline/offline_image_writer.*` | readback 如何写成 EXR、PNG、JSON 和 raw dump |

## 当前 MVP 的关键边界

| 已经实现 | 后续扩展 |
|---|---|
| headless Vulkan device 和 compute dispatch | Vulkan hardware ray tracing pipeline |
| CPU primitive BVH | 实例层 BVH、更好的 split、GPU build |
| baseColor / metallic / roughness / emissive 常量材质 | albedo/normal/metallicRoughness/AO/emissive 纹理 |
| 程序化 environment color | HDR environment 纹理采样与 importance sampling |
| EXR/PNG/JSON/raw 输出 | AOV、variance、multipart EXR |
| primary ray + direct directional light + 简单反射 | 多 bounce path tracing、MIS、Russian roulette |

这个页面解释的是已经落地的 MVP 管线。更完整的分步实现结构在
[Offline Renderer / 实现结构](../../../../../tutorial/offline-renderer/03-implementation-flow.md)，
真正实现自定义 path tracing 时，入口教程在
[Offline Renderer / Path Tracing](../../../../../tutorial/offline-renderer/05-implement-path-tracing.md)。
