# Vulkan Offline Renderer：从 Scene IR 到 Compute Readback

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页把 offline renderer 当成一条独立实验管线来读，入口是
[src/backend/vulkan/offline/vulkan_offline_renderer.hpp](../../../../../../src/backend/vulkan/offline/vulkan_offline_renderer.hpp)。
关注的问题是：为什么离线渲染不直接复用 realtime draw item，而是把
scene 文档加载进 `SceneResourceTable`，再通过 upload view 打包成 compute shader 的 storage buffer。

可以先带着一个问题阅读：我们要怎样在不创建 swapchain 的情况下，从同一份
`.scene.yaml` 得到一张可复现实验图？答案就在 `OfflineRenderJob`、
`OfflineSceneLoader`、`offline_scene_storage_resources`、`SoftwareComputeOfflineIntegrator`
和 `VulkanOfflineRenderer` 的分层里。

源码入口：[vulkan_offline_renderer.hpp](../../../../../src/backend/vulkan/offline/vulkan_offline_renderer.hpp)

关联源码：

- [offline_render_job.hpp](../../../../src/core/offline/offline_render_job.hpp)
- [offline_scene_storage_resources.hpp](../../../../src/core/offline/offline_scene_storage_resources.hpp)
- [offline_scene_loader.hpp](../../../../src/infra/offline/offline_scene_loader.hpp)
- [software_compute_offline_integrator.hpp](../../../../../src/backend/vulkan/offline/software_compute_offline_integrator.hpp)

## vulkan_offline_renderer.hpp

源码位置：[vulkan_offline_renderer.hpp](../../../../../src/backend/vulkan/offline/vulkan_offline_renderer.hpp)

### VulkanOfflineRenderer 是离线积分器协调入口

`VulkanOfflineRenderer` 是离线渲染实验场当前的 Vulkan 后端入口。它接收
`OfflineRenderJob`，先做 core 层 job 校验，再根据显式 integrator 名称选择离线
积分器。具体 headless Vulkan device、compute pipeline、buffer 上传、dispatch 和
readback 生命周期由被选中的 integrator 管理。

它和 realtime 路径复用 core `FrameGraph` / `RenderWorkItem` / resource table
输入链路；差异收敛在 integrator 的执行目标和 Vulkan headless 管线。离线渲染会在
job 的 `SceneResourceTable` 内建立 render-scope storage/output 资源，所以 render
入口接收可变 job，而不是把临时资源塞进独立旁路。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 补充说明

## 从命令行到 shader 的阅读顺序

我们读 offline renderer 时，不要先跳进 Vulkan descriptor 细节。更稳的顺序是：

| 顺序 | 文件 | 读什么 |
|---|---|---|
| 1 | `src/tools/lxe_offline_render/main.cpp` | CLI 如何选择 scene、profile、camera 和输出路径 |
| 2 | `src/infra/scene_io/scene_document.*` | `.scene.yaml` 如何被解析成共享文档 |
| 3 | `src/infra/offline/offline_scene_loader.*` | editor scene 文档如何被裁剪进 `SceneResourceTable` |
| 4 | `src/core/scene/scene_resource_table.*` | table 如何打包成 indexed shader storage buffer |
| 5 | `src/core/raytracing/software_bvh.*` | primitive 如何获得可遍历 BVH |
| 6 | `src/backend/vulkan/offline/software_compute_offline_integrator.*` | pipeline、descriptor、dispatch、readback 如何连起来 |
| 7 | `assets/shaders/glsl/techniques/OfflineRT/offline_pbr_direct_ray.comp` | 当前 integrator 如何生成相机 ray、遍历 BVH、计算直接光和环境 |
| 8 | `src/infra/offline/offline_image_writer.*` | readback 如何写成 EXR、PNG、JSON 和 raw dump |

## 当前 MVP 的关键边界

| 已经实现 | 后续扩展 |
|---|---|
| headless Vulkan device 和 compute dispatch | Vulkan hardware ray tracing pipeline |
| CPU primitive BVH | 实例层 BVH、更好的 split、GPU build |
| baseColor / metallic / roughness / emissive 常量材质 | albedo/normal/metallicRoughness/AO/emissive 纹理 |
| 程序化 environment color | HDR environment 纹理采样与 importance sampling |
| EXR/PNG/JSON/raw 输出 | AOV、variance、multipart EXR |
| camera ray + direct directional light + 简单反射 | 多 bounce path tracing、MIS、Russian roulette |

这个页面解释的是已经落地的 MVP 管线。更完整的分步实现结构在
[Offline Renderer / 实现结构](../../../../../tutorial/offline-renderer/03-implementation-flow.md)，
真正实现自定义 path tracing 时，入口教程在
[Offline Renderer / Path Tracing](../../../../../tutorial/offline-renderer/05-implement-path-tracing.md)。
