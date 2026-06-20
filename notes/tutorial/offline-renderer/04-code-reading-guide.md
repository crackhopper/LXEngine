# Offline Renderer 源码阅读路线：从命令到像素

读 offline renderer 像追一张快递单：不要一开始就拆 Vulkan 的每个包装细节，而是先看包裹从哪里进入、经过哪些中转站、最后如何交付成文件。当前包裹是 `assets/scenes/generated/helmet_standard_pbr.scene.yaml` 加一个 output profile，交付结果是 `.exr`、`.png`、`.json` 和 `.rgba32f`。

这一页的目标不是穷尽每行代码，而是建立一条稳定的阅读路线。以后我们把 software BVH 换成 hardware RT、加入 AOV 或 HDR display output 时，也可以沿着同一条路线判断应该改哪里、测哪里。

## 先从一次命令开始

先运行一个小尺寸 smoke，保证我们读的是一个可复现路径：

```bash
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml \
  --profile raytrace \
  --width 64 \
  --height 64 \
  --out artifacts/offline/smoke
```

这个命令会触发当前主链路：

```text
main.cpp
  -> scene_document
  -> offline_render_profile
  -> OfflineSceneLoader
  -> RenderPathGraph loader
  -> FrameGraph compile
  -> RenderWorkCompiler::buildInputs(...)
  -> RenderWorkCompiler::prepare(...)
  -> VulkanFrameGraphExecutor
  -> standard_pbr_primary_ray.comp
  -> OfflineImageWriter
```

阅读源码时，我们每次只问一个问题：这一层接收什么，输出什么，谁消费它。

## 第一站：CLI 只负责把实验参数收齐

先读：

```text
src/tools/lxe_offline_render/main.cpp
```

`main.cpp` 是编排层。它的阅读重点是调用顺序：

| 调用 | 作用 | 继续读哪里 |
|---|---|---|
| CLI parse | 把 `--scene`、`--profile`、尺寸和输出路径变成结构化 overrides | `main.cpp` |
| `loadSceneDocument()` | 把 `.scene.yaml` 读成 scene 文档 | `src/infra/scene_io/scene_document.*` |
| `resolveRenderProfileDocument()` | 合并 scene output profile 和 CLI overrides | `src/core/offline/offline_render_profile.*` |
| `OfflineSceneLoader::load()` | 把 scene 文档加载进 `Scene` / `SceneResourceTable` | `src/infra/offline/offline_scene_loader.*` |
| renderer render | 读取 profile 指定的 render path graph 并执行 | `src/backend/vulkan/offline/vulkan_offline_renderer.*` |
| `writeOfflineImageOutputs()` | 写 EXR / PNG / JSON / raw | `src/infra/offline/offline_image_writer.*` |

这里有一个重要边界：CLI 不直接理解 TinyEXR、stb、descriptor set、shader layout 或 pass 执行。它只是把实验参数和实验输入交给下游模块。

## 第二站：Scene YAML 里哪些字段真正进入 Offline

接着打开：

```text
assets/scenes/generated/helmet_standard_pbr.scene.yaml
src/infra/scene_io/scene_document.hpp
src/infra/scene_io/scene_document.cpp
src/infra/offline/offline_scene_loader.hpp
src/infra/offline/offline_scene_loader.cpp
```

offline renderer 当前重点读取这些：

| YAML 字段 | 当前进入哪里 | 说明 |
|---|---|---|
| selected `OutputProfile.camera` | active camera resource | profile 选择相机 |
| selected `OutputProfile.renderPathGraph` | graph loader | profile 选择本次渲染流程 |
| camera node `transform` + `camera` 参数 | camera resource | transform 推导 pose，camera 提供 projection |
| `mesh.uri` | mesh/object resource | glTF mesh 进入 resource table |
| `material.uri` | `MaterialInstance` | standard-pbr 参数和贴图 |
| `light.kind: Directional` | light resource | Forward 和 OfflineRT direct lighting |
| `skybox.mode: finite` | 普通 mesh/material 节点 | finite 环境房间参与普通 scene selection |
| `skybox.mode: infinite` | environment lighting feature | Forward IBL 和 RT miss environment |

`SceneResourceTable` 是阅读这块的核心。它像离线实验室的标准样品：不携带 editor 操作状态，也没有 Vulkan 句柄。它只保留 render work compiler 和 backend 能消费的 scene/resource 事实。

## 第三站：Render Path Graph 决定这次跑什么

同一 scene 的四个 profile 指向四个 graph：

```text
assets/render_paths/forward_offline_direct.render-path.yaml
assets/render_paths/forward_offline_ibl_only.render-path.yaml
assets/render_paths/forward_offline_direct_ibl.render-path.yaml
assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml
```

读 graph 时关注三类字段：

| 字段 | 作用 |
|---|---|
| pass input/filter | 决定哪些 scene object/material 参与这一 pass |
| features/material/shader | 决定 shader variant、descriptor 和 pipeline build desc |
| target/readback | 决定写入哪个 offscreen target，以及 executor 如何把结果读回 |

readback 不是 pass。它是 pass/output contract：graph 声明哪个 target 或 buffer 要读回，executor 在资源写完后复制成 payload。

## 第四站：RenderWorkCompiler 把 Graph 变成 Work

然后读：

```text
src/core/frame_graph/render_work_compiler.cpp
src/core/frame_graph/render_work_build_context.hpp
src/core/frame_graph/render_input.hpp
```

`RenderWorkCompiler` 做两件事：

1. `buildInputs(...)`：根据 pass input/filter 选中 scene renderables 或 compute dispatch，生成 draw/dispatch work。
2. `prepare(...)`：根据 shader、material、render feature、descriptor 和 target/readback contract 生成 accepted `RenderInputDesc`。

对于 Forward offline graph，它会生成 graphics work；对于 OfflineRT graph，它会生成 compute dispatch。二者都要经过同一种 descriptor/pipeline 准备流程。

## 第五站：Render Feature 提供参数和派生资源

继续读：

```text
assets/effects/environment_lighting.render-feature.yaml
assets/effects/offline_ray_tracer.render-feature.yaml
src/core/frame_graph/render_feature_derived_resource_producer.hpp
src/infra/resource_parsers/render_feature_resource_parser.cpp
```

普通 render feature 提供 shader 参数和 descriptor 资源。BVH 这类 feature 还会提供 derived resource：它不只是一个 YAML 常量，而是根据 scene/resource table 构建出的 GPU 资源。当前 software BVH 走这个模型；未来 hardware RT 可以在同一个抽象下构建 BLAS/TLAS 和 shader table。

Ray program table 也放在 render feature：它描述 hit group index 与 hit shader 入口的关系。具体 hit shader 属于 material scheme；software RT 的统一 hit 入口通过 switch/if 调用 material hit shader，hardware RT 则可以用同一份表协助构建 hit shader table。

## 第六站：FrameGraphExecutor 执行 Raster 和 Compute

接着读：

```text
src/backend/vulkan/vulkan_frame_graph_executor.hpp
src/backend/vulkan/vulkan_frame_graph_executor.cpp
src/backend/vulkan/offline/vulkan_offline_renderer.cpp
```

`VulkanFrameGraphExecutor` 会按 compiled graph 顺序执行 pass：

| Pass 类型 | 当前用途 | 输出 |
|---|---|---|
| graphics/raster | Forward direct-only、IBL-only、direct+IBL 对比 | offscreen attachment readback |
| compute | OfflineRT primary ray | storage buffer 或 target readback |

executor 负责 target 创建、resource sync、pipeline 绑定、descriptor 绑定、command recording、barrier 和 readback。offline renderer 只负责选择 scene/profile/graph 和把 payload 交给 writer。

## 第七站：shader 决定当前画面为什么长这样

当前主要 shader：

```text
assets/shaders/glsl/render_paths/Forward/pbr.frag
assets/shaders/glsl/render_paths/OfflineRT/standard_pbr_primary_ray.comp
assets/shaders/glsl/features/environment_lighting.glsl
```

Forward 对比图用 specialization / feature 参数控制 direct lighting 与 IBL；OfflineRT 现在是 primary ray + direct lighting，ray miss 时通过 `environment_lighting.glsl` 采样 infinite skybox。它还不是完整 path tracer。

当前未实现：

| 未实现 | 影响 |
|---|---|
| 多 bounce path tracing | 没有真实全局光照收敛 |
| hardware RT | 还没有 BLAS/TLAS 和 hardware shader table |
| 完整 material hit shader 分流优化 | software RT 先保持结构兼容，后续切硬件 RT |
| denoise / accumulation history | 每次 CLI 生成一张独立图 |

## 第八站：输出模块把 Payload 变成可检查资产

最后读：

```text
src/infra/offline/offline_image_writer.hpp
src/infra/offline/offline_image_writer.cpp
```

`OfflineImageWriter` 的输入是 executor readback payload。它不关心这张图来自 Forward raster、OfflineRT compute 还是未来 hardware RT pipeline。

| 输出 | 数据含义 | 用途 |
|---|---|---|
| `.exr` | RGBA half/float，scene-linear | 保留 HDR 原始渲染结果 |
| `.png` | ACES-like tone mapping + gamma 的 8-bit 预览 | 快速看画面 |
| `.json` | scene/profile/buildInfo/output metadata | 复现实验 |
| `.rgba32f` | 原始 float dump | 调试 readback |

## 用测试帮我们定位是哪一层坏了

阅读代码时可以配合这些测试缩小问题范围：

| 测试 | 主要保护 |
|---|---|
| `test_render_resource_parsers` | scene/render path/feature/material schema |
| `test_render_work_compiler` | graph 到 render input/desc |
| `test_shader_compiler` | shader 反射和 ABI |
| `test_vulkan_ibl_bake` | IBL bake 仍走 executor 并产出资源 |
| `test_offline_rt_cli_smoke` | Helmet OfflineRT profile 能输出可见图 |

常用验证命令：

```bash
cmake --build build --target \
  CompileShaders \
  lxe_offline_render \
  test_render_resource_parsers \
  test_render_work_compiler \
  test_shader_compiler \
  test_vulkan_ibl_bake

ctest --test-dir build --output-on-failure \
  -R 'test_render_resource_parsers|test_render_work_compiler|test_shader_compiler|test_vulkan_ibl_bake|test_offline_rt_cli_smoke'
```

## 改代码时按数据流提交

offline renderer 最容易出错的是“只改了一半”。例如我们要给 OfflineRT 增加新的 material hit shader，不能只在 shader 里写函数。至少要沿着数据流检查：

| 层 | 需要补什么 |
|---|---|
| material scheme | hit shader URI 和 material 参数 |
| render feature | ray program table / hit group index |
| scene/resource table | 选中对象、材质、贴图和 BVH 输入 |
| `RenderWorkCompiler` | descriptor、pipeline extra 和 graph readback |
| Vulkan executor | 对应资源同步和 pipeline 创建 |
| GLSL shader | 按同一 layout 读取并调用 hit shader |
| tests | parser、compiler、GPU layout、renderer smoke |
| output/docs | 说明当前支持哪些纹理和限制 |

## 我们已经学会了什么

我们已经把 offline renderer 的源码拆成八个阅读站点：CLI、scene/profile、render path graph、RenderWorkCompiler、render feature、FrameGraphExecutor、shader、image writer。每个站点都有明确输入输出，所以调试时可以按数据流逐层定位，而不是直接陷入 Vulkan 或 shader 细节。

## 下一步

继续读 [实现自己的 Path Tracing](05-implement-path-tracing.md)。在那里我们会沿着这条数据流思考：要把 primary-ray OfflineRT 变成 path tracer，应该在哪些 scene record、render feature、shader binding 和测试上扩展。
