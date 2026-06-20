# Vulkan Offline Renderer：从 Output Profile 到 FrameGraphExecutor Readback

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页把 offline renderer 当成一条独立实验管线来读，入口是
`src/backend/vulkan/offline/vulkan_offline_renderer.hpp`。
关注的问题是：为什么离线渲染不维护第二套 offline job graph，而是把
output profile 指向的 RenderPathGraph 交给统一 `FrameGraphExecutor`。

可以先带着一个问题阅读：我们要怎样在不创建 swapchain 的情况下，从同一份
`.scene.yaml` 得到 Forward/IBL/OfflineRT 多种可复现实验图？答案就在
`VulkanOfflineRenderRequest`、`RenderWorkCompiler`、`VulkanFrameGraphExecutor`
和 `OfflineImageWriter` 的分层里。

源码入口：`src/backend/vulkan/offline/vulkan_offline_renderer.hpp`

关联源码：

- `src/backend/vulkan/offline/vulkan_offline_renderer.cpp`
- `src/backend/vulkan/vulkan_frame_graph_executor.hpp`
- `src/core/frame_graph/frame_graph_executor.hpp`
- `src/core/offline/offline_render_result.hpp`

## vulkan_offline_renderer.hpp

源码位置：`src/backend/vulkan/offline/vulkan_offline_renderer.hpp`

### Offline render request 是 headless executor 的边界

`VulkanOfflineRenderRequest` 不再表达一套 offline-only job graph。它把
offline CLI/profile resolver 已经决定好的事实交给 Vulkan 后端：scene resource
table、选中的 output profile、离线 runtime 参数、profile 名、输出路径和
RenderPathGraph URI。这样 backend 不需要猜 shader、pass 或 readback 目标；它只需
加载 graph、编译 FrameGraph、准备 pass work，再交给统一 `FrameGraphExecutor`。

`VulkanOfflineRenderResult` 同样保持很薄：executor 的 payload 是真实 readback 合同，
`OfflineReadbackImage` 是 writer 需要的线性 RGBA 图像视图。文件格式、PNG tone
mapping 和 metadata 写出留给 infra/offline writer，而不是混进 Vulkan 执行层。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 补充说明

## 从命令行到 shader 的阅读顺序

我们读 offline renderer 时，不要先跳进 Vulkan descriptor 细节。更稳的顺序是：

| 顺序 | 文件 | 读什么 |
|---|---|---|
| 1 | `src/tools/lxe_offline_render/main.cpp` | CLI 如何选择 scene、profile、camera 和输出路径 |
| 2 | `src/infra/scene_io/scene_document.*` | `.scene.yaml` 如何被解析成共享文档 |
| 3 | `src/infra/offline/offline_scene_loader.*` | scene 文档如何进入 `Scene` / `SceneResourceTable` |
| 4 | `assets/render_paths/*.render-path.yaml` | output profile 选择的 graph 如何声明 pass、target 和 readback |
| 5 | `src/core/frame_graph/render_work_compiler.*` | graph pass 如何变成 draw/dispatch work 和 `RenderInputDesc` |
| 6 | `src/backend/vulkan/vulkan_frame_graph_executor.*` | pipeline、descriptor、graphics/compute pass、barrier 和 readback 如何连起来 |
| 7 | `assets/shaders/glsl/render_paths/OfflineRT/standard_pbr_primary_ray.comp` | 当前 OfflineRT shader 如何生成相机 ray、遍历 BVH、计算直接光和 miss environment |
| 8 | `src/infra/offline/offline_image_writer.*` | executor payload 如何写成 EXR、PNG、JSON 和 raw dump |

## 当前 MVP 的关键边界

| 已经实现 | 后续扩展 |
|---|---|
| headless Vulkan raster / compute pass 和 readback payload | Vulkan hardware ray tracing pipeline |
| render feature derived resource 形式的 software BVH | hardware BLAS/TLAS、GPU build |
| baseColor / metallic / roughness / emissive 常量材质 | albedo/normal/metallicRoughness/AO/emissive 纹理 |
| infinite skybox miss environment sampling | HDR environment importance sampling |
| EXR/PNG/JSON/raw 输出 | AOV、variance、multipart EXR |
| camera ray + direct directional light | 多 bounce path tracing、MIS、Russian roulette |

这个页面解释的是已经落地的 MVP 管线。更完整的分步实现结构在
[Offline Renderer / 实现结构](../../../../../tutorial/offline-renderer/03-implementation-flow.md)，
真正实现自定义 path tracing 时，入口教程在
[Offline Renderer / Path Tracing](../../../../../tutorial/offline-renderer/05-implement-path-tracing.md)。
