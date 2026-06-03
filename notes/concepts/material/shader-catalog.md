# 内置 Shader 清单：每一份 GLSL 负责哪一步

`assets/shaders/glsl/` 像一排不同工位的工具：有的工具给物体表面上色，有的工具只写 shadow depth，有的工具把 HDR 环境烘焙成 cubemap，有的工具负责把 HDR 画面收尾到屏幕。我们阅读 shader 时，先看它属于哪条流水线，再看它声明了哪些 binding，因为 binding 决定了 C++ 侧必须提供哪些资源。

当前 CMake 会把 `.vert`、`.frag`、`.comp` 编译成同名 `.spv`。`.spv` 是生成物；理解和修改逻辑时，我们看 GLSL 源文件。

## 先按用途分组

| 分组 | Shader 家族 | 当前入口 |
|---|---|---|
| 材质 forward | `blinnphong_0`、`pbr`、`rtr_experiment_template`、`rtr_shadertoy_quantum_core` | `.material` 文件经 `GenericMaterialLoader` 编译和反射 |
| Shadow | `shadow_depth_only` | material 的 `Shadow` pass |
| Debug / 诊断 | `debug_line`、`mesh_debug`、`minimal`、`texture_cube_probe` | debug draw、overlay、resize 诊断或 cubemap probe |
| HDR 后处理 | `skybox`、`bloom_threshold`、`bloom_blur_h`、`bloom_blur_v`、`post_process` | `VulkanPostProcessBuilder` 手动装配 fullscreen material |
| IBL bake | `equirect_to_cubemap`、`ibl_irradiance_convolve`、`ibl_prefilter_env`、`ibl_brdf_lut` | `IblBakeRenderer` 手动创建 bake draw |
| Offline compute | `offline_primary_ray` | `VulkanOfflineRenderer` 读取 `.comp.spv` 并校验 descriptor 合同 |
| 共享片段 | `scene_lights_ubo.glsl` | GLSL include 片段，声明 scene lights UBO 结构 |

## 材质 Forward Shader

这些 shader 直接决定物体表面的颜色。它们像不同的刷漆方案：同样的 mesh 进来，shader 决定我们用简化光照、PBR、实验 toon 风格，还是屏幕空间 raymarch 风格。

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `blinnphong_0` | `blinnphong_0.vert` / `.frag` | 当前通用 Blinn-Phong forward shader，支持无光照、光照、UV、顶点色、法线贴图、蒙皮和 CSM 阴影变体 | `CameraUBO`、`LightUBO`、`MaterialUBO`、`ShadowMap0..3`、可选 `albedoMap` / `normalMap`、可选 `Bones` |
| `pbr` | `pbr.vert` / `.frag` | 金属/粗糙度 PBR forward shader，消费材质贴图和 scene-level IBL 资源 | `CameraUBO`、`MaterialUBO`、`LightUBO`、`IrradianceMap`、`PrefilteredEnvMap`、`BrdfLut`、`EnvironmentUBO` |
| `rtr_experiment_template` | `rtr_experiment_template.vert` / `.frag` | 自定义材质教程和 RTR 实验模板；用少量参数展示“材质 UBO -> fragment 颜色”的完整合同 | `CameraUBO`、`MaterialUBO`、`LightUBO` |
| `rtr_shadertoy_quantum_core` | `rtr_shadertoy_quantum_core.vert` / `.frag` | 程序化 shader gallery 的 Shadertoy 风格 SDF/raymarch shader | `CameraUBO`、`ShadertoyUBO`、`iChannel0` |

`blinnphong_0` 是当前变体最多的 shader。它的顶点输入会随宏收缩：`USE_LIGHTING` 打开 normal/world position 路径，`USE_UV` 打开 `inUV` 和 `albedoMap`，`USE_NORMAL_MAP` 额外要求 tangent 和 `normalMap`，`USE_SKINNING` 额外要求 bone ids/weights 和 `Bones`。这就是 `shader-reflection` 规格中特别要求 `blinnphong_0` 的 vertex input reflection 必须随变体变化的原因。

`pbr` 的重点不是变体数量，而是资源归属边界。`MaterialUBO`、`albedoMap`、`normalMap`、`metallicRoughnessMap`、`aoMap`、`emissiveMap` 属于材质；`IrradianceMap`、`PrefilteredEnvMap`、`BrdfLut`、`EnvironmentUBO` 属于 scene-level IBL。这样同一个 HDR environment 可以服务整场景，而不是写进每个 `.material`。

```yaml
shader: pbr                              # -> assets/shaders/glsl/pbr.vert/.frag
variants:
  HAS_METALLIC_ROUGHNESS: true           # -> 编译时启用对应 sampler2D binding
  HAS_NORMAL_MAP: true                   # -> 顶点阶段也会输出 vTBN
parameters:
  MaterialUBO.baseColorFactor: [1.0, 1.0, 1.0, 1.0] # -> pbr.frag MaterialUBO
resources:
  normalMap: white                       # -> material-owned Texture2D
```

## Shadow Shader

| Shader | 文件 | 用途 | 关键合同 |
|---|---|---|---|
| `shadow_depth_only` | `shadow_depth_only.vert` / `.frag` | Shadow pass 的深度写入 shader；fragment 阶段为空，因为目标只需要 depth | `ObjectPC.model`、`LightUBO.shadowViewProj`、可选 `Bones` |

`shadow_depth_only.vert` 把 mesh position 乘上 model 和当前 cascade 的 `shadowViewProj`。renderer 在每次 shadow pass 前更新 `LightUBO.shadowViewProj`，所以同一份 shader 可以被不同 cascade 重复使用。开启 `USE_SKINNING` 时，它和 `blinnphong_0` 一样读取 `Bones`，保证蒙皮物体在 shadow map 里的轮廓和 forward pass 对齐。

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

这些 fullscreen shader 的顶点阶段通常只用 `gl_VertexIndex` 生成一个覆盖全屏的大三角形，不需要 mesh vertex buffer。它们的 reflection binding 不是由普通 `.material` 文件提供，而是在 `VulkanPostProcessBuilder` 里用 `StaticFullscreenShader` 手动声明，随后仍然走 `MaterialTemplate`、`MaterialInstance` 和 pipeline 路径。

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
| `offline_primary_ray` | `offline_primary_ray.comp` | 离线 renderer 的 primary ray compute shader；遍历 BVH、求三角形交点、做直接光/环境/简化高光着色，写入输出像素 buffer | set 0 binding 0..8 的 SSBO 合同 |

`offline_primary_ray.comp` 不走 graphics pipeline。它用 `local_size_x = 8, local_size_y = 8` 分块调度，每个 invocation 对应一个像素采样。C++ 侧的 `VulkanOfflineRenderer` 会反射 `.comp.spv`，并校验九个 binding 是否存在：`Vertices`、`Indices`、`Meshes`、`Primitives`、`Objects`、`Materials`、`BvhNodes`、`ParamsBuffer`、`OutputBuffer`。这里的合同比普通材质更像数据表 schema：字段顺序和 buffer 布局必须和 `core/offline` 里的 CPU 结构保持一致。

## 共享 GLSL 片段

| 文件 | 用途 | 关键合同 |
|---|---|---|
| `scene_lights_ubo.glsl` | 声明 `DirectionalLightRecord`、`PointLightRecord`、`SpotLightRecord` 和 `SceneLightsUBO` | `layout(set = 0, binding = 1) uniform SceneLightsUBO` |

这是 include 片段，不会单独编译成 `.spv`。它在 shader 目录里保存 scene lights 的统一 UBO layout，避免需要多光源数据的 shader 各自手写不同结构。修改它时，我们需要同步检查引用它的 shader 和 C++ 侧 scene light buffer 布局。

## 修改 Shader 时先检查这些边界

| 改动 | 需要同步检查 |
|---|---|
| 新增/删除 vertex input | mesh `VertexLayout`、`ShaderReflector` 输出、`SceneNode` validation |
| 新增 material-owned UBO 字段 | `.material parameters`、`MaterialInstance` 参数写入、std140 offset |
| 新增 material-owned texture | `.material resources` 默认值和 binding name |
| 新增 system-owned binding | `shader_binding_ownership.hpp`、renderer 注入资源、descriptor set/binding |
| 新增 variant 宏 | `.material variants`、`variantRules`、pipeline signature 和测试 fixture |
| 修改 post/IBL fullscreen binding | `VulkanPostProcessBuilder` 或 `IblBakeRenderer` 的手写 reflection binding |
| 修改 compute SSBO schema | `VulkanOfflineRenderer` descriptor 校验和 `core/offline` CPU 数据结构 |

## 我们已经学会了什么

内置 shader 不是一堆彼此无关的 GLSL 文件，而是几条渲染流水线上的合同集合。材质 shader 通过 reflection 连接 `.material`；post 和 IBL bake shader 由 renderer 手写 binding；offline compute shader 用 SSBO schema 连接 CPU 离线场景数据。读 shader 时，我们先判断它在哪条流水线上工作，再检查它要求 C++ 提供哪些资源。

## 下一步

- [Shader 在材质中的角色](shader.md)
- [PBR + IBL：资源与 Shader 合同](../../tutorial/pbr-ibl/02-resource-and-shader-contract.md)
- [自定义材质：YAML 与 Shader 合同](../../tutorial/custom-material/02-material-yaml-and-shader-contract.md)
- [源码分析：Shader](../../source_analysis/src/core/asset/shader.md)
