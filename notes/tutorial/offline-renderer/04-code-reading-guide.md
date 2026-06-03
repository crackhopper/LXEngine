# Offline Renderer 源码阅读路线：从命令到像素

读 offline renderer 像追一张快递单：不要一开始就拆 Vulkan 的每个包装细节，而是先看包裹从哪里进入、经过哪些中转站、最后如何交付成文件。当前 MVP 的包裹就是 `assets/scenes/ibl_metal_sphere.scene.yaml`，交付结果是 `smoke.exr`、`smoke.png`、`smoke.json` 和 `smoke.rgba32f`。

这一页的目标不是穷尽每行代码，而是建立一条稳定的阅读路线。以后我们把 software-compute shader 替换成 path tracing、加入纹理采样、AOV 或 HDR display output 时，也可以沿着同一条路线判断应该改哪里、测哪里。

## 先从一次命令开始

先运行一个小尺寸 smoke，保证我们读的是一个可复现路径：

```bash
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --profile mvp \
  --samples 1 \
  --width 64 \
  --height 64 \
  --out artifacts/offline/smoke
```

这个命令会触发当前主链路：

```text
main.cpp
  -> offline_render_cli.cpp
  -> scene_document
  -> offline_render_profile
  -> OfflineSceneLoader
  -> SceneResourceTable::buildUploadView()
  -> SceneSoftwareBvh
  -> SoftwareComputeOfflineIntegrator
  -> offline_primary_ray.comp
  -> OfflineImageWriter
```

阅读源码时，我们每次只问一个问题：这一层接收什么，输出什么，谁消费它。

## 第一站：CLI 只负责把实验参数收齐

先读：

```text
src/tools/lxe_offline_render/main.cpp
src/tools/lxe_offline_render/offline_render_cli.hpp
src/tools/lxe_offline_render/offline_render_cli.cpp
```

`offline_render_cli.cpp` 只做命令行解析。它把 `--scene`、`--profile`、`--width`、`--height`、`--samples`、`--max-bounce`、`--seed` 和 `--out` 整理进 `OfflineRenderCliOptions`。这里不读 mesh，不创建 Vulkan，也不写 EXR。

`main.cpp` 是编排层。它的阅读重点是调用顺序：

| 调用 | 作用 | 继续读哪里 |
|---|---|---|
| `parseOfflineRenderCliArguments()` | 把命令行变成结构化 overrides | `offline_render_cli.cpp` |
| `loadSceneDocument()` | 把 `.scene.yaml` 读成 scene 文档 | `src/infra/scene_io/scene_document.*` |
| `resolveRenderProfileDocument()` | 合并 scene output profile、offline settings 和 CLI overrides | `src/core/offline/offline_render_profile.*` |
| `loader.load()` | 把 scene 文档裁剪进 `SceneResourceTable` | `offline_scene_loader.cpp` |
| `renderer.render()` | 选择 explicit integrator | `vulkan_offline_renderer.cpp` |
| `SoftwareComputeOfflineIntegrator::render()` | 执行 headless Vulkan compute | `software_compute_offline_integrator.cpp` |
| `writeOfflineImageOutputs()` | 写 EXR / PNG / JSON / raw | `offline_image_writer.cpp` |

这里有一个重要边界：CLI 不直接理解 TinyEXR、stb、descriptor set 或 shader layout。它只是把实验参数和实验输入交给下游模块。

## 第二站：Scene YAML 里哪些字段真正进入 offline

接着打开：

```text
assets/scenes/ibl_metal_sphere.scene.yaml
src/core/offline/offline_render_profile.hpp
src/core/offline/offline_render_profile.cpp
src/infra/offline/offline_scene_loader.hpp
src/infra/offline/offline_scene_loader.cpp
src/core/offline/offline_render_job.hpp
```

`ibl_metal_sphere.scene.yaml` 里同时有实时渲染字段和离线字段。offline renderer 当前重点读取这些：

| YAML 字段 | 当前进入哪里 | 说明 |
|---|---|---|
| selected `OutputProfile.cameraPath` | `CameraResource` | `--profile` 选择 output profile 后，相机从 profile 进入 loader |
| `scene.environment` | deferred environment record | 当前 shader 用 output background 和简单环境项；HDR 纹理采样还没接入 |
| `scene.outputProfiles` | `OutputProfile` | 相机、宽高、outputFormat、outDir |
| `scene.offlineRender` | `OfflineRenderSettings` | integrator、samples、maxBounce、seed、profile、shadows |
| camera node `transform` + `camera` 参数 | `CameraResource` | transform 推导 pose，camera 提供 projection |
| `mesh.uri` | `MeshResource` + `ObjectResource` | 当前 MVP 支持内置 plane / sphere |
| `material.uri` + overrides | `MaterialInstance` | baseColor、metallic、roughness、emissive |
| `light.kind: Directional` | `LightResource` | 当前取方向光参与直接光和阴影测试 |

`SceneResourceTable` 是阅读这块的核心。它像离线实验室的标准样品：不携带 editor 状态，不携带 realtime draw item，也没有 Vulkan 句柄。它只保留离线计算需要的事实。

读 `OfflineSceneLoader::load()` 时，可以按这几个函数理解：

| 函数 | 阅读重点 |
|---|---|
| `visitNode()` | 递归遍历 scene node，把相机、灯光、mesh instance 注册进 table |
| `loadMaterial()` | 从 `.material` 和 node override 中提取离线材质常量 |
| `makeBuiltinMesh()` | 当前只把内置 plane / sphere 展开成 CPU 顶点和索引 |
| `load()` | 处理 environment、默认相机、空场景和 warning |

如果 smoke 图里没有球、没有地面、没有光，优先从这一层查：scene 文档是否加载、相机路径是否匹配、mesh URI 是否被支持、材质是否被 fallback。

## 第三站：GPU buffer 是 C++ 和 GLSL 的合同

然后读：

```text
src/core/scene/scene_gpu_records.hpp
src/core/scene/scene_resource_table.cpp
src/core/raytracing/software_bvh.hpp
src/core/raytracing/software_bvh.cpp
assets/shaders/glsl/offline_primary_ray.comp
src/test/integration/test_offline_gpu_scene.cpp
```

`SceneResourceTable::buildUploadView()` 导出 shader storage buffer 所需的统一 GPU records；`SceneSoftwareBvh::build()` 只从这份只读 upload view 派生 BVH 节点和 primitive 重排引用。这里最重要的不是算法，而是 layout 合同和索引关系。

| C++ 结构 | GLSL 结构 | 当前用途 |
|---|---|---|
| `SceneGpuVertexRecord` | `lxSceneVertexRecord` | position、normal、uv、tangent |
| `SceneGpuMeshRecord` | `lxSceneMeshRecord` | vertex/index offset 与 geometry index |
| `SceneGpuPrimitiveRecord` | `lxScenePrimitiveRecord` | index offset、mesh/material/object index |
| `SceneGpuObjectRecord` | `lxSceneObjectRecord` | object/world transform、bounds、visibility |
| `SceneGpuMaterialRecord` | `lxSceneMaterialRecord` | baseColor、PBR 参数、emissive、texture flags |
| `SceneGpuFrameParams` | `SceneFrameParams` | 相机 basis、尺寸、samples、seed、maxBounce、shadow 开关、光照和环境参数 |

读 `SceneSoftwareBvh::build()` 时，关注四件事：

1. table 把 mesh 展成 compact vertex/index records，shader 直接按全局 compact index 查顶点。
2. table 把材质参数压进 `SceneGpuMaterialRecord`，texture flag/index 预留给后续 bindless。
3. table 把 object transform 保留在 `SceneGpuObjectRecord`，BVH 使用 world-space bounds。
4. integrator 根据 active camera 和 output profile 写入 `SceneGpuFrameParams`。

修改这层时，一定同步看 `offline_primary_ray.comp`。C++ 的 `alignas(16)`、`static_assert(sizeof(...))` 和 GLSL 的 `layout(std430)` 必须一起维护。否则 shader 会按错误偏移读 buffer，画面可能不是崩溃，而是颜色、阴影或相机方向悄悄错掉。

## 第四站：BVH 让 shader 不必遍历所有三角形

继续读：

```text
src/core/raytracing/software_bvh.hpp
src/core/raytracing/software_bvh.cpp
assets/shaders/glsl/offline_primary_ray.comp
```

`SceneSoftwareBvh` 当前在 CPU 上为 primitive buffer 构建一棵紧凑 BVH，再把节点上传给 compute shader。它不是最终的高性能加速结构，但已经让 MVP 有了 closest-hit 查询和 shadow ray 查询。

阅读时重点看两个约定：

| 约定 | 作用 |
|---|---|
| `SceneSoftwareBvhNode` 固定 32 字节 | 保持 C++ / GLSL 节点布局一致 |
| `BVH_LEAF_NODE_FLAG` | shader 用它区分内部节点和叶子节点 |

shader 里的 `traceScene()` 会：

1. 从根节点开始。
2. 用 `intersectAabb()` 判断 ray 是否进入节点 bounds。
3. 如果是叶子节点，遍历其中 primitive，并通过 mesh/index/vertex 调用相交逻辑。
4. 如果是内部节点，把左右子节点压入固定大小 stack。
5. 返回最近命中的 `hitT`、normal 和 materialIndex。

以后我们做 path tracing 时，camera ray、shadow ray、reflection ray、diffuse bounce 都会复用这类查询能力。

## 第五站：software-compute 是 headless compute 执行器

接着读：

```text
src/backend/vulkan/offline/vulkan_offline_renderer.hpp
src/backend/vulkan/offline/vulkan_offline_renderer.cpp
src/backend/vulkan/offline/software_compute_offline_integrator.cpp
```

它和实时 renderer 最大区别是：没有窗口，没有 swapchain，没有 FrameGraph。`VulkanOfflineRenderer` 只负责选择 explicit integrator；当前 `software-compute` integrator 才拥有 headless Vulkan device、compute pipeline、storage buffer 和 output buffer。

`software-compute` 的阅读顺序如下：

| 步骤 | 代码动作 | 理解方式 |
|---|---|---|
| `validateOfflineRenderJob()` | 检查 output、active camera、upload view 和 BVH 能否成立 | 检查实验样品 |
| `SceneResourceTable::buildUploadView()` | 导出统一 scene GPU records | 装入实验样品 |
| `SceneSoftwareBvh::build()` | 生成 BVH 节点并重排 primitives | 建立空间索引 |
| `ensurePipeline()` | 创建 descriptor layout、pipeline layout、compute pipeline、descriptor pool | 准备实验仪器 |
| 创建 `VulkanBuffer` | vertex/index/mesh/primitive/object/material/bvh/params/output 都是 storage buffer | 准备 shader 可读写内存 |
| `vkUpdateDescriptorSets()` | 绑定 9 个 buffer 到 binding 0..8 | 对齐 GLSL binding |
| `vkCmdDispatch()` | 按 8x8 workgroup 覆盖整张图 | 每个 invocation 计算一个像素 |
| `vkCmdPipelineBarrier()` | compute 写完后允许 host read | GPU/CPU 同步边界 |
| `map()` + `memcpy()` | 读回 `OfflineReadbackImage.rgba` | 得到 CPU 可写文件的 float 图 |

这里最值得记住的数字是 descriptor binding：

| Binding | Buffer |
|---|---|
| 0 | `SceneVertices` |
| 1 | `SceneIndices` |
| 2 | `SceneMeshes` |
| 3 | `ScenePrimitives` |
| 4 | `SceneObjects` |
| 5 | `SceneMaterials` |
| 6 | `SceneBvhNodes` |
| 7 | `SceneFrameParams` |
| 8 | `OutputPixels` |

如果以后新增纹理、light array、AOV 或 accumulation buffer，descriptor set layout、GLSL binding、测试都要一起变。

## 第六站：shader 决定当前画面为什么长这样

现在读：

```text
assets/shaders/glsl/offline_primary_ray.comp
```

当前 shader 是 `software-compute` integrator，不是完整 path tracer。它对每个像素做这些事：

1. 根据像素坐标和 sample jitter 生成 camera ray。
2. 调 `traceScene()` 找最近交点。
3. 没命中时返回 `environmentColor(dir)`，所以画面上半部分是程序化天空灰蓝色，地平线附近会过渡到地面色。
4. 命中时读取材质，计算直接方向光、shadow ray、简化 specular 和简化环境反射。
5. 把 sample 平均后写入 `OutputPixels.pixels[pixelIndex]`。

所以我们在 tev 里看到“小球、地面阴影、水平线上方灰色背景”是符合当前 MVP 的。这个结果证明 camera ray、BVH、shadow ray、材质常量、环境色和 output buffer 都至少连通了。

它当前还没有做这些：

| 未实现 | 影响 |
|---|---|
| 多 bounce path tracing | 没有真实全局光照收敛 |
| HDR environment texture sampling | 背景不是读取 `.hdr` 纹理 |
| 重要性采样 | 高 samples 也不是最终 path tracing 质量 |
| 物理完整 Cook-Torrance BRDF | specular 是 MVP 近似 |
| denoise / accumulation history | 每次 CLI 生成一张独立图 |

## 第七站：输出模块把 float 图变成可检查资产

最后读：

```text
src/infra/offline/offline_image_writer.hpp
src/infra/offline/offline_image_writer.cpp
src/test/integration/test_offline_image_writer.cpp
```

`OfflineImageWriter` 的输入是 `OfflineReadbackImage`：一张 CPU 侧 RGBA float 图。它不关心这张图来自 Vulkan、CPU path tracer 还是未来的 ray tracing pipeline。

| 输出 | 数据含义 | 用途 |
|---|---|---|
| `.exr` | RGBA half float，scene-linear | 保留 HDR 原始渲染结果 |
| `.png` | ACES-like tone mapping + gamma 的 8-bit 预览 | 快速看画面 |
| `.json` | scene/profile/buildInfo/output metadata | 复现实验 |
| `.rgba32f` | 原始 float dump | 调试 readback |

读 `toneMapLinearToSrgb8()` 时要记住：它只影响 PNG，不改变 EXR。EXR 是我们以后做 ground truth、误差比较、HDR 检查的主文件。

## 用测试帮我们定位是哪一层坏了

阅读代码时可以配合这些测试缩小问题范围：

| 测试 | 主要保护 |
|---|---|
| `test_offline_render_cli` | CLI 参数和 profile override |
| `test_offline_scene_loader` | scene YAML 到 `SceneResourceTable` |
| `test_offline_gpu_scene` | C++ GPU struct layout 和基础打包 |
| `test_vulkan_offline_renderer` | headless Vulkan compute + readback |
| `test_offline_image_writer` | EXR / PNG / JSON / raw 输出 |

常用验证命令：

```bash
cmake --build build --target \
  lxe_offline_render \
  test_offline_render_cli \
  test_offline_scene_loader \
  test_offline_gpu_scene \
  test_vulkan_offline_renderer \
  test_offline_image_writer

ctest --test-dir build --output-on-failure \
  -R 'test_offline_render_cli|test_offline_scene_loader|test_offline_gpu_scene|test_vulkan_offline_renderer|test_offline_image_writer'
```

如果 CLI smoke 失败，先看错误发生在哪一层：

| 现象 | 优先检查 |
|---|---|
| `requires --scene` / unknown option | `offline_render_cli.cpp` |
| camera not found | scene YAML 的 `gameplayCameraPath` 和 node path |
| unsupported mesh | `OfflineSceneLoader::makeBuiltinMesh()` 当前支持范围 |
| no visible primitives | instance visibility、mesh indices、upload view |
| failed to find offline compute shader SPIR-V | shader 编译产物和运行工作目录 |
| readback empty / non-finite | shader 输出、buffer 大小、barrier/readback |
| EXR/PNG 写出失败 | `OfflineImageWriter` 和输出目录权限 |

## 改代码时按数据流提交

offline renderer 最容易出错的是“只改了一半”。例如我们要给 path tracing 增加 albedo texture，不能只在 shader 里写 sampler。至少要沿着数据流检查：

| 层 | 需要补什么 |
|---|---|
| scene/material 文件 | 纹理 URI 和资产路径 |
| `MaterialInstance` / `SceneGpuMaterialRecord` | 纹理引用或采样参数 |
| `OfflineSceneLoader` | 从 material asset 读取字段 |
| GPU packing | texture index / material 参数 |
| Vulkan descriptor | image/sampler 或 texture buffer binding |
| GLSL shader | 按 layout 读取并采样 |
| tests | compiler、GPU layout、renderer smoke |
| output/docs | 说明当前支持哪些纹理和限制 |

这个顺序也适用于新增光源、AOV、accumulation、HDR environment、denoiser 或真正的 path tracing。

## 我们已经学会了什么

我们已经把 offline renderer 的源码拆成七个阅读站点：CLI、scene/profile、SceneResourceTable、GPU buffer、BVH、headless Vulkan compute、image writer。每个站点都有明确输入输出，所以调试时可以按数据流逐层定位，而不是直接陷入 Vulkan 或 shader 细节。

## 下一步

继续读 [实现自己的 Path Tracing](05-implement-path-tracing.md)。在那里我们会沿着这条数据流思考：要把 software-compute MVP 变成 path tracer，应该在哪些 scene record、buffer、descriptor、shader 和测试上扩展。
