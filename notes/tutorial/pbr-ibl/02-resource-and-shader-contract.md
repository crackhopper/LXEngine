# 资源与 Shader 合同：谁拥有哪个 binding

PBR + IBL 的 binding 边界像摄影棚里的两类物品：材质自带的是球表面的涂层参数，场景提供的是棚内光线。我们不把棚内光线写进每个 `.material`，否则同一个环境要在所有材质里重复维护。

## PBR material 只描述表面

`assets/materials/pbr_gold.material` 的关键字段：

```yaml
shader: pbr

passes:
  Forward:                         # -> MaterialTemplate::setPassDefinition(Pass_Forward, ...)
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true

parameters:
  MaterialUBO.baseColorFactor: [1.0, 0.766, 0.336, 1.0]
  MaterialUBO.metallicFactor: 1.0
  MaterialUBO.roughnessFactor: 0.25
  MaterialUBO.ao: 1.0

resources:
  albedoMap: white                 # -> material-owned Texture2D fallback
```

| 字段 | 归属 | 运行时对象 |
|---|---|---|
| `MaterialUBO.baseColorFactor` | material-owned | `MaterialInstance` 参数缓冲 |
| `MaterialUBO.metallicFactor` | material-owned | 金属度，金属球为 `1.0` |
| `MaterialUBO.roughnessFactor` | material-owned | 粗糙度，当前为 `0.25` |
| `albedoMap` | material-owned | 材质贴图资源 |

## IBL bindings 来自 scene

PBR shader 还声明这组 scene-level binding：

| Binding | 类型 | Set / Binding | 归属 |
|---|---|---|---|
| `IrradianceMap` | `TextureCube` | set 3 binding 0 | scene-level IBL |
| `PrefilteredEnvMap` | `TextureCube` | set 3 binding 1 | scene-level IBL |
| `BrdfLut` | `Texture2D` | set 3 binding 2 | scene-level IBL |
| `EnvironmentUBO` | `UniformBuffer` | set 3 binding 3 | scene-level IBL |

这些名字由 `shader_binding_ownership.hpp` 标记为系统归属。`RenderQueue` 在发现 shader 消费 IBL binding 时，会把 `Scene::getIblEnvironmentResources()` 的结果追加到该 draw item。普通 BlinnPhong 材质不会收到这组资源。

## 为什么不写进 `.material resources`

| 资源 | 不放在 material 的原因 | 正确入口 |
|---|---|---|
| `IrradianceMap` | 同一场景内所有 PBR 物体共享 diffuse 环境光 | `Scene::setIblEnvironmentResources(...)` |
| `PrefilteredEnvMap` | mip 链来自同一个 HDR environment bake | `Scene::setIblEnvironmentResources(...)` |
| `BrdfLut` | 是全局 BRDF 查表，不属于某个表面 | 默认或 bake pipeline 生成 |
| `EnvironmentUBO` | 控制环境强度和 roughness mip 数 | `scene.environment` -> `EnvironmentData` |

这样拆分后，金属球换材质不会破坏环境；场景换 HDR 环境也不需要改每个材质文件。

## 当前 bake 合同

`assets/shaders/glsl/` 下已有四组 bake shader：

| Shader | 作用 | 当前状态 |
|---|---|---|
| `equirect_to_cubemap` | HDR equirectangular 转 skybox cubemap | shader 合同已测试 |
| `ibl_irradiance_convolve` | 生成 diffuse irradiance cubemap | shader 合同已测试 |
| `ibl_prefilter_env` | 生成 roughness mip prefiltered cubemap | shader 合同已测试 |
| `ibl_brdf_lut` | 生成 BRDF LUT | shader 合同已测试 |

真实 GPU bake 执行、cubemap face/mip attachment 和 debug dump 仍属于 [REQ-048-a](../../requirements/048-a-ibl-gpu-bake-pipeline.md) 的后续切片。

## 下一步

进入 [03 HDR 到屏幕的 Post 流程](03-hdr-post-process-flow.md)。
