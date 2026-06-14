# 内置 Shader 清单：每一份 GLSL 负责哪一步

`assets/shaders/glsl/` 像一排不同工位的工具：有的工具给物体表面上色，有的工具只写 shadow depth，有的工具把 HDR 环境烘焙成 cubemap，有的工具负责把 HDR 画面收尾到屏幕。我们阅读 shader 时，先看它属于哪条流水线，再看它声明了哪些 binding，因为 binding 决定了 C++ 侧必须提供哪些资源。

当前 CMake 会把 `.vert`、`.frag`、`.comp` 编译成同名 `.spv`。`.spv` 是生成物；理解和修改逻辑时，我们看 GLSL 源文件。

## 先按用途分组

| 分组 | Shader 家族 | 当前入口 |
|---|---|---|
| 材质 / surface pass | `techniques/Forward/pbr`、`techniques/Deferred/pbr_gbuffer`、`techniques/OfflineRT/offline_pbr_direct_ray` | `RenderPathGraph` pass 引用 shader URI，并消费 `material.bsdf` |
| Material contract | `common/materials/*.contract.glsl` | `.material v2` 的 `bsdf.source`，用于参数/ABI reflection 和 source variant include |
| Shadow | `techniques/Forward/shadow_depth_only` | RenderPathGraph 的 `Shadow` pass |
| Debug / 诊断 | `debug_line`、`mesh_debug`、`minimal`、`texture_cube_probe` | debug draw、overlay、resize 诊断或 cubemap probe |
| HDR 后处理 | `skybox`、`bloom_threshold`、`bloom_blur_h`、`bloom_blur_v`、`post_process` | RenderPathGraph / fullscreen pass 消费 HDR、bloom 和 post-process source |
| IBL bake | `equirect_to_cubemap`、`ibl_irradiance_convolve`、`ibl_prefilter_env`、`ibl_brdf_lut` | `IblBakeRenderer` 手动创建 bake draw |
| Offline compute | `offline_primary_ray` | `createOfflinePrimaryRayShader()` 包装为 `IShader`，再经 offline `RenderWorkItem` 进入 compute pipeline |
| 共享片段 | `scene_lights_ubo.glsl` | GLSL include 片段，声明 scene lights UBO 结构 |

## 材质 Forward Shader

这些 shader 直接决定物体表面的颜色。同样的 mesh 进来，shader 决定 surface 数据怎样进入 Forward、Deferred 或 OfflineRT 路径。

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `techniques/Forward/pbr` | `techniques/Forward/pbr.vert` / `.frag` | Forward surface pass，消费 `material.bsdf`、scene camera/light 和 HDR/depth target contract | `material.bsdf`、`scene.camera`、`scene.lights` |
| `techniques/Deferred/pbr_gbuffer` | `techniques/Deferred/pbr_gbuffer.vert` / `.frag` | Deferred GBuffer pass，输出 surface/material 数据 | `material.bsdf`、geometry、scene camera |
| `techniques/Deferred/deferred_lighting` | `techniques/Deferred/deferred_lighting.vert` / `.frag` | Deferred lighting fullscreen pass，读取 GBuffer 和 scene light | GBuffer targets、scene lights |
| `common/materials/standard_pbr.contract.glsl` | contract include | standard-pbr BSDF 参数、packed source record 和 accessor ABI | `baseColor`、`metallic`、`roughness`、texture slot 等 material envelope |

PBR shader 的重点是 source contract 和 render path contract 的组合。`.material v2` 提供 `bsdf.source` 和参数 envelope；RenderPathGraph pass 提供 shader URI、attachment、geometry、source/target 和 render state；material source resolver 负责生成会进入 `PipelineKey` 的 material type/source variant。

## Shadow Shader

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `shadow_depth_only` | `shadow_depth_only.vert` / `.frag` | Shadow pass 的深度写入 shader；fragment 阶段为空，因为目标只需要 depth | `ObjectPC.model`、`LightUBO.shadowViewProj`、可选 `Bones` |

`shadow_depth_only.vert` 把 mesh position 乘上 model 和当前 cascade 的 `shadowViewProj`。renderer 在每次 shadow pass 前更新 `LightUBO.shadowViewProj`，所以同一份 shader 可以被不同 cascade 重复使用。开启 `USE_SKINNING` 时，它和 surface forward pass 一样读取 `Bones`，保证蒙皮物体在 shadow map 里的轮廓和主渲染 pass 对齐。

## Debug 和诊断 Shader

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
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
| `bloom_threshold` | `bloom_threshold.vert` / `.frag` | 从 HDR scene color 中提取超过阈值的亮部 | `SceneColor`、`BloomThresholdUBO` |
| `bloom_blur_h` | `bloom_blur_h.vert` / `.frag` | bloom 横向高斯近似模糊 | `BloomSource` |
| `bloom_blur_v` | `bloom_blur_v.vert` / `.frag` | bloom 纵向高斯近似模糊 | `BloomSource` |
| `post_process` | `post_process.vert` / `.frag` | 合并 HDR scene color 与 bloom，执行 exposure、tone mapping 和 gamma | `SceneColor`、`BloomColor`、`PostProcessUBO` |

这些 fullscreen shader 的顶点阶段通常只用 `gl_VertexIndex` 生成一个覆盖全屏的大三角形，不需要 mesh vertex buffer。当前目标是让 PostProcess / Bloom 这类 pass 也来自 RenderPathGraph；仍保留的 builder 手写路径应被视为待硬切的执行层细节。

## IBL Bake Shader

IBL bake shader 像把一张全景灯光照片加工成几种棚灯工具：先把 equirectangular HDR 变成 cubemap，再生成 diffuse irradiance、roughness prefilter 和 BRDF LUT，最后 PBR shader 才能快速采样。

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `equirect_to_cubemap` | `equirect_to_cubemap.vert` / `.frag` | 把 equirectangular HDR 贴图投影到 cubemap 六个 face | `EquirectangularMap`、`CaptureViewUBO` |
| `ibl_irradiance_convolve` | `ibl_irradiance_convolve.vert` / `.frag` | 对 skybox cubemap 做 diffuse hemisphere convolution | `SkyboxMap`、`CaptureViewUBO` |
| `ibl_prefilter_env` | `ibl_prefilter_env.vert` / `.frag` | 按 roughness/mip 采样 skybox，生成 prefiltered environment mip chain | `SkyboxMap`、`CaptureViewUBO`、`PrefilterUBO` |
| `ibl_brdf_lut` | `ibl_brdf_lut.vert` / `.frag` | fullscreen 生成 PBR specular IBL 使用的 BRDF lookup texture | 无 descriptor binding |

`IblBakeRenderer` 会为 cubemap 的每个 face 和 prefilter 的每个 mip 更新 `CaptureViewUBO` / `PrefilterUBO`，然后执行对应 shader。当前 `ibl_prefilter_env.frag` 使用 `textureLod` 近似 prefilter；它已经满足当前 bake 资源合同，但不是完整重要性采样实现。

## Offline Compute Shader

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `offline_primary_ray` | `offline_primary_ray.comp` | 离线 renderer 的 camera ray compute shader；遍历 BVH、求三角形交点、做直接光/环境/高光着色，写入输出像素 buffer | set 0 binding 0..8 的 SSBO 合同 |

`offline_primary_ray.comp` 不走 graphics pipeline。它用 `local_size_x = 8, local_size_y = 8` 分块调度，每个 invocation 对应一个像素采样。C++ 侧的 `createOfflinePrimaryRayShader()` 会反射 `.comp.spv`，并校验九个 binding 是否存在：`SceneVertices`、`SceneIndices`、`SceneMeshes`、`ScenePrimitives`、`SceneObjects`、`SceneMaterials`、`SceneBvhNodes`、`SceneFrameParams`、`OutputPixels`。随后 `RenderWorkQueue` 为 `Pass_OfflineRayTrace` 生成 `ComputeDispatch` 类型的 `RenderWorkItem`，再由 `PipelineBuildDesc::fromRenderWorkItem` 和 `PipelineCache` 创建 compute pipeline。这里的合同比普通材质更像数据表 schema：字段顺序和 buffer 布局必须和 `core/scene` 里的 `SceneGpu*` CPU 结构保持一致。

## 共享 GLSL 片段

| 文件 | 用途 | 关键合同 |
|---|---|---|
| `scene_lights_ubo.glsl` | 声明 `DirectionalLightRecord`、`PointLightRecord`、`SpotLightRecord` 和 `SceneLightsUBO` | `layout(set = 0, binding = 1) uniform SceneLightsUBO` |

这是 include 片段，不会单独编译成 `.spv`。它在 shader 目录里保存 scene lights 的统一 UBO layout，避免需要多光源数据的 shader 各自手写不同结构。修改它时，我们需要同步检查引用它的 shader 和 C++ 侧 scene light buffer 布局。

## 修改 Shader 时先检查这些边界

| 改动 | 需要同步检查 |
|---|---|
| 新增/删除 vertex input | mesh `VertexLayout`、`ShaderReflector` 输出、`SceneNode` validation |
| 新增 material contract 参数 | `.contract.glsl`、`MaterialContractReflection`、material parser/packer、`.material v2` envelope |
| 新增 material texture 参数 | material dependency registration、`SceneResourceTable` typed handle、shader accessor |
| 新增 system-owned binding | `shader_binding_ownership.hpp`、renderer 注入资源、descriptor set/binding |
| 新增 shader/material source variant | material source resolver、specialized `.spv`、`materialTypeVariant`、pipeline identity tests |
| 修改 post/IBL fullscreen binding | RenderPathGraph source/target、fullscreen shader reflection、IBL bake renderer binding |
| 修改 compute SSBO schema | `createOfflinePrimaryRayShader()` descriptor 校验、`RenderWorkQueue` offline item 构建和 `core/offline` CPU 数据结构 |

## 我们已经学会了什么

内置 shader 不是一堆彼此无关的 GLSL 文件，而是几条渲染流水线上的合同集合。surface shader 通过 material contract 和 RenderPathGraph 连接 `.material v2`；post 和 IBL bake shader 通过 fullscreen/bake source contract 消费资源；offline compute shader 用 SSBO schema 连接 `SceneResourceTableUploadView`，并通过同一套 `RenderWorkItem` / `PipelineCache` 路径执行。读 shader 时，我们先判断它在哪条流水线上工作，再检查它要求 C++ 提供哪些资源。

## 下一步

- [Shader 在材质中的角色](shader.md)
- [PBR + IBL：资源与 Shader 合同](../../tutorial/pbr-ibl/02-resource-and-shader-contract.md)
- [自定义材质：YAML 与 Shader 合同](../../tutorial/custom-material/02-material-yaml-and-shader-contract.md)
- [源码分析：Shader](../../source_analysis/src/core/asset/shader.md)
