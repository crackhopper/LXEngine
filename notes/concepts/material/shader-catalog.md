# 内置 Shader 清单：每一份 GLSL 负责哪一步

`assets/shaders/glsl/` 像一排不同工位的工具：有的工具给物体表面上色，有的工具只写 shadow depth，有的工具把 HDR 环境烘焙成 cubemap，有的工具负责把 HDR 画面收尾到屏幕。我们阅读 shader 时，先看它属于哪条流水线，再看它声明了哪些 binding，因为 binding 决定了 C++ 侧必须提供哪些资源。

当前 CMake 会把 `.vert`、`.frag`、`.comp` 编译成同名 `.spv`。`.spv` 是生成物；理解和修改逻辑时，我们看 GLSL 源文件。

## 先按用途分组

| 分组 | Shader 家族 | 当前入口 |
|---|---|---|
| 材质 / surface pass | `render_paths/Forward/pbr`、`render_paths/Deferred/pbr_gbuffer`、`techniques/OfflineRT/offline_pbr_direct_ray` | `RenderPathGraph` pass 引用 shader URI，并消费 `material.bsdf` |
| Material contract | `common/materials/*.contract.glsl` | `.material v2` 的 `bsdf.source`，用于参数/ABI reflection 和 source variant include |
| Shadow | `render_paths/Forward/shadow_depth_only` | RenderPathGraph 的 `Shadow` pass |
| Debug / 诊断 | `render_paths/Debug/debug_overlay`、`debug_line`、`mesh_debug`、`minimal`、`texture_cube_probe` | RenderPathGraph debug overlay、debug draw、resize 诊断或 cubemap probe |
| HDR 后处理 | `skybox`、`render_paths/Post/bloom_threshold`、`render_paths/Post/bloom_blur_h`、`render_paths/Post/bloom_blur_v`、`render_paths/Post/post_process` | RenderPathGraph / fullscreen pass 消费 HDR、bloom 和 post-process source |
| IBL bake | `equirect_to_cubemap`、`ibl_irradiance_convolve`、`ibl_prefilter_env`、`ibl_brdf_lut` | `bake_environment_ibl` / `bake_standard_pbr_brdf_lut` graph 声明 bake pass，旧手写 bake helper 已删除 |
| Offline compute | `techniques/OfflineRT/offline_pbr_direct_ray` | CLI 默认创建 offline compute shader，再经 `RenderWorkCompiler` 进入 compute pipeline |
| 共享片段 | `scene_lights_ubo.glsl` | GLSL include 片段，声明 scene lights UBO 结构 |

## 材质 Forward Shader

这些 shader 直接决定物体表面的颜色。同样的 mesh 进来，shader 决定 surface 数据怎样进入 Forward、Deferred 或 OfflineRT 路径。

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `render_paths/Forward/pbr` | `render_paths/Forward/pbr.vert` / `.frag` | Forward surface pass，消费 `material.bsdf`、scene camera、directional `LightUBO` 和 HDR/depth target contract | `material.bsdf`、`scene.camera`、`LightUBO` |
| `render_paths/Deferred/pbr_gbuffer` | `render_paths/Deferred/pbr_gbuffer.vert` / `.frag` | Deferred GBuffer pass，输出 surface/material 数据 | `material.bsdf`、geometry、scene camera |
| `render_paths/Deferred/deferred_lighting` | `render_paths/Deferred/deferred_lighting.vert` / `.frag` | Deferred lighting fullscreen pass，读取 GBuffer 和 directional `LightUBO` | GBuffer targets、`LightUBO` |
| `common/materials/standard_pbr.contract.glsl` | contract include | standard-pbr BSDF 参数、packed source record 和 accessor ABI | `baseColor`、`metallic`、`roughness`、texture slot 等 material envelope |

PBR shader 的重点是 source contract 和 render path contract 的组合。`.material v2` 提供 `bsdf.source` 和参数 envelope；RenderPathGraph pass 提供 shader URI、attachment、geometry、source/target 和 render state；material source resolver 负责生成会进入 `PipelineKey` 的 material type/source variant。

## Shadow Shader

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `render_paths/Forward/shadow_depth_only` | `render_paths/Forward/shadow_depth_only.vert` / `.frag` | Shadow pass 的深度写入 shader；fragment 阶段为空，因为目标只需要 depth | `ObjectPC.model`、`LightUBO.shadowViewProj`、可选 `Bones` |

`shadow_depth_only.vert` 把 mesh position 乘上 model 和当前 cascade 的 `shadowViewProj`。renderer 在每次 shadow pass 前更新 `LightUBO.shadowViewProj`，所以同一份 shader 可以被不同 cascade 重复使用。开启 `USE_SKINNING` 时，它和 surface forward pass 一样读取 `Bones`，保证蒙皮物体在 shadow map 里的轮廓和主渲染 pass 对齐。

## Debug 和诊断 Shader

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `render_paths/Debug/debug_overlay` | `render_paths/Debug/debug_overlay.vert` / `.frag` | RenderPathGraph debug overlay pass，绘制 editor/debug target overlay | `DebugOverlayUBO`、debug source texture |
| `debug_line` | `debug_line.vert` / `.frag` | 画 debug line；顶点颜色直接传到 fragment | `CameraUBO`、`inPos`、`inColor` |
| `mesh_debug` | `mesh_debug.vert` / `.frag` | mesh overlay/轮廓类调试材质，fragment 输出 `MeshOverlayUBO.color` | `CameraUBO`、`ObjectPC.model`、`MeshOverlayUBO` |
| `minimal` | `minimal.vert` / `.frag` | resize / swapchain 诊断 demo 的最小 shader；没有 UBO、descriptor 或 push constant | `inPos`、`inColor` |
| `texture_cube_probe` | `texture_cube_probe.vert` / `.frag` | 采样 cubemap 资源，便于观察环境贴图或 bake 结果 | `EnvironmentMap` |

这组 shader 的价值在于隔离变量。`minimal` 完全不依赖引擎资源上传，适合判断问题是否来自 swapchain、depth attachment 或 command buffer。`debug_line` 和 `mesh_debug` 则保留 camera/model 路径，用来观察世界空间辅助线和 mesh 覆盖效果。

## Skybox、Bloom 和 Post Process

这些 shader 像摄影棚的后期部门：几何物体已经画进 HDR target，它们负责背景、亮部提取、模糊和最终 tone mapping。

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `skybox` | `skybox.vert` / `.frag` | fullscreen 背景 pass，从 camera 反推出世界方向并采样 `SkyboxMap` | `CameraUBO`、`SkyboxMap`、`EnvironmentUBO` |
| `render_paths/Post/bloom_threshold` | `render_paths/Post/bloom_threshold.vert` / `.frag` | 从 HDR scene color 中提取超过阈值的亮部 | `SceneColor`、`BloomThresholdUBO` |
| `render_paths/Post/bloom_blur_h` | `render_paths/Post/bloom_blur_h.vert` / `.frag` | bloom 横向高斯近似模糊 | `BloomSource` |
| `render_paths/Post/bloom_blur_v` | `render_paths/Post/bloom_blur_v.vert` / `.frag` | bloom 纵向高斯近似模糊 | `BloomSource` |
| `render_paths/Post/post_process` | `render_paths/Post/post_process.vert` / `.frag` | 合并 HDR scene color 与 bloom，执行 exposure、tone mapping 和 gamma | `SceneColor`、`BloomColor`、`PostProcessUBO` |

这些 fullscreen shader 的顶点阶段通常只用 `gl_VertexIndex` 生成一个覆盖全屏的大三角形，不需要 mesh vertex buffer。PostProcess / Bloom 的 graph 入口必须使用 `render_paths/Post/...`，resolver 不再接受 `post_process` 这类 root short name 或直接 `.frag` 文件路径。仍保留的 builder 手写路径应被视为待硬切的执行层细节。

`PostProcessUBO` 当前包含 `exposure`、`toneMappingMode`、`gamma` 和
`bloomIntensity`。这些值不是 surface material 的参数；它们由
`VulkanPostProcessBuilder::createStandardPostProcessMaterial(...)` 创建
PostProcess fullscreen `MaterialInstance` 时写入 shader binding parameter。
`assets/effects/tone_mapping.render-feature.yaml` 仍然作为
`feature.toneMapping` 的 graph 依赖存在，但当前 realtime builder 并没有把这份
feature asset 的参数逐项读出后注入 `PostProcessUBO`。

`gamma` 现在还承担了输出编码选择的职责。backend 在创建 PostProcess material
前会检查 swapchain target：

| Target format | Builder 传入的输出编码 | 写入 `PostProcessUBO.gamma` |
|---|---|---|
| sRGB swapchain target | `Linear` | `1.0` |
| UNORM swapchain target | `Srgb` | `2.2` |

shader 只读取 `gamma` 并调用 `lxLinearToSrgbGamma(mapped, gamma)`。因此
`gamma = 1.0` 表示 shader 不做 gamma 编码，把 linear mapped color 交给
sRGB attachment；`gamma = 2.2` 表示 shader 自己做 gamma 编码后写入 UNORM
attachment。这个设计能表达当前行为，但它把两个概念压进了一个字段。更清晰的
后续设计应像 debug color transfer shader 那样加入显式
`outputEncodingMode`，让 shader 同时读取“是否编码”和“gamma 数值”。

## IBL Bake Shader

IBL bake shader 像把一张全景灯光照片加工成几种棚灯工具：先把 equirectangular HDR 变成 cubemap，再生成 diffuse irradiance、roughness prefilter 和 BRDF LUT，最后 PBR shader 才能快速采样。

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `equirect_to_cubemap` | `equirect_to_cubemap.vert` / `.frag` | 把 equirectangular HDR 贴图投影到 cubemap 六个 face | `EquirectangularMap`、`CaptureViewUBO` |
| `ibl_irradiance_convolve` | `ibl_irradiance_convolve.vert` / `.frag` | 对 skybox cubemap 做 diffuse hemisphere convolution | `SkyboxMap`、`CaptureViewUBO` |
| `ibl_prefilter_env` | `ibl_prefilter_env.vert` / `.frag` | 按 roughness/mip 采样 skybox，生成 prefiltered environment mip chain | `SkyboxMap`、`CaptureViewUBO`、`PrefilterUBO` |
| `ibl_brdf_lut` | `ibl_brdf_lut.vert` / `.frag` | fullscreen 生成 PBR specular IBL 使用的 BRDF lookup texture | 无 descriptor binding |

当前 bake 入口由 `assets/render_paths/bake_environment_ibl.render-path.yaml`
和 `assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml` 描述。
graph 明确列出 source、target、shader URI 和 readback 需求；backend 的低层 helper
不再由 realtime scene 初始化直接触发，旧的手写 bake helper 已从默认路径中移除。
`ibl_prefilter_env.frag` 仍使用 `textureLod` 近似 prefilter；它满足当前 bake 资源合同，
但不是完整重要性采样实现。

## Offline Compute Shader

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `techniques/OfflineRT/offline_pbr_direct_ray` | `techniques/OfflineRT/offline_pbr_direct_ray.comp` | 当前 CLI 默认的 camera ray compute shader；遍历 BVH、求三角形交点、做直接光/环境/高光着色，写入输出像素 buffer | scene storage buffers、frame params、`OutputPixels` 和 texture array |

`offline_pbr_direct_ray.comp` 不走 graphics pipeline。它用 `local_size_x = 8, local_size_y = 8` 分块调度，每个 invocation 对应一个像素采样。C++ 侧会为 `techniques/OfflineRT/offline_pbr_direct_ray` 创建 offline shader，`RenderWorkCompiler` 为 file-local `OfflineCompute` pass 生成 `RenderComputeInput`，并在 `RenderInputDesc` 里准备 compute `PipelineBuildDesc` 和 binding plan。这里的合同比普通材质更像数据表 schema：字段顺序和 buffer 布局必须和 `core/scene`、`core/offline` 里的 CPU 结构保持一致。

当前 offline storage resource 集合保留这些名字，实际绑定以 shader reflection 和 binding plan 为准：

| Binding / 名称 | 用途 |
|---|---|
| `ScenePositions` | compact vertex position buffer |
| `SceneAttributeStreams` / `SceneAttributeValues` | normal、uv、tangent 等属性流 |
| `SceneIndices` | compact index buffer |
| `SceneMeshes` / `ScenePrimitives` / `SceneObjects` | mesh、primitive、object records |
| `SceneMaterials` | simple/material record 路径和兼容记录 |
| `SceneBvhNodes` | CPU 构建的 compact BVH |
| `SceneFrameParams` | camera、尺寸、samples、light、background 和 compare mode |
| `OutputPixels` | compute 写出的 RGBA float output buffer |
| `SceneTextures` | offline direct ray shader 使用的 texture array |

## 共享 GLSL 片段

| 文件 | 用途 | 关键合同 |
|---|---|---|
| `scene_lights_ubo.glsl` | 声明 `DirectionalLightRecord`、`PointLightRecord`、`SpotLightRecord` 和 `SceneLightsUBO` | `layout(set = 0, binding = 1) uniform SceneLightsUBO` |

这是 include 片段，不会单独编译成 `.spv`。它在 shader 目录里保存 scene lights 的聚合 UBO layout，避免需要多光源数据的 shader 各自手写不同结构。当前主 Forward / Deferred shader 仍读取 `LightUBO` 单 directional direct light；修改 `scene_lights_ubo.glsl` 时，需要同步检查引用它的 shader 和 C++ 侧 scene light buffer 布局。

## 修改 Shader 时先检查这些边界

| 改动 | 需要同步检查 |
|---|---|
| 新增/删除 vertex input | mesh `VertexLayout`、`ShaderReflector` 输出、`SceneNode` validation |
| 新增 material contract 参数 | `.contract.glsl`、`MaterialContractReflection`、material parser/packer、`.material v2` envelope |
| 新增 material texture 参数 | material dependency registration、`SceneResourceTable` typed handle、shader accessor |
| 新增 system-owned binding | `shader_binding_ownership.hpp`、renderer 注入资源、descriptor set/binding |
| 新增 shader/material source variant | material source resolver、specialized `.spv`、`materialTypeVariant`、pipeline identity tests |
| 修改 post/IBL fullscreen binding | RenderPathGraph source/target、`render_paths/Post/...` fullscreen shader reflection、IBL bake renderer binding |
| 修改 compute SSBO schema | offline shader reflection/binding plan、`RenderWorkCompiler` offline desc 构建和 `core/offline` CPU 数据结构 |

## 我们已经学会了什么

内置 shader 不是一堆彼此无关的 GLSL 文件，而是几条渲染流水线上的合同集合。surface shader 通过 material contract 和 RenderPathGraph 连接 `.material v2`；post 和 IBL bake shader 通过 fullscreen/bake source contract 消费资源；offline compute shader 用 SSBO schema 连接 `SceneResourceTableUploadView`，并通过同一套 `RenderInputDesc` / `PipelineCache` 路径执行。读 shader 时，我们先判断它在哪条流水线上工作，再检查它要求 C++ 提供哪些资源。

## 下一步

- [Shader 在材质中的角色](shader.md)
- [PBR + IBL：资源与 Shader 合同](../../tutorial/pbr-ibl/02-resource-and-shader-contract.md)
- [自定义材质：YAML 与 Shader 合同](../../tutorial/custom-material/02-material-yaml-and-shader-contract.md)
- [源码分析：Shader](../../source_analysis/src/core/asset/shader.md)
