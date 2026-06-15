# 资源与 Shader 合同：谁拥有哪个 binding

PBR + IBL 的 binding 边界像摄影棚里的两类物品：材质自带的是球表面的涂层参数，场景提供的是棚内光线。我们不把棚内光线写进每个 `.material`，否则同一个环境要在所有材质里重复维护。

## PBR material 只描述表面

`assets/materials/pbr_gold.material` 当前使用 `.material v2` surface envelope：

```yaml
schema: lxe.material.v2
bsdf:
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
  parameters:
    baseColor: { kind: rgb, value: [1.0, 0.766, 0.336] }
    metallic: { kind: float, value: 1.0 }
    roughness: { kind: float, value: 0.25 }
    ao: { kind: float, value: 1.0 }
```

| 字段 | 归属 | 运行时对象 |
|---|---|---|
| `baseColor` | material-owned envelope | `MaterialInstance` 参数 envelope / packed material storage |
| `metallic` | material-owned envelope | 金属度 |
| `roughness` | material-owned envelope | 粗糙度 |
| texture 参数 | material-owned dependency | `SceneResourceTable` typed texture handle |

## IBL bindings 来自 scene

PBR shader 还会消费 scene-level IBL 资源：

| Binding | 类型 | 归属 |
|---|---|---|
| `IrradianceMap` | `TextureCube` | scene-level IBL |
| `PrefilteredEnvMap` | `TextureCube` | scene-level IBL |
| `BrdfLut` | `Texture2D` | scene-level IBL |
| `EnvironmentUBO` | `UniformBuffer` | scene-level IBL |

这些资源由 scene/environment 或 bake pipeline 提供，不写进 `.material`。RenderWorkQueue / descriptor resolver 会在 pass 生成 work item 时把 scene-level resources 拼进去。

## 为什么不写进 `.material`

| 资源 | 不放在 material 的原因 | 正确入口 |
|---|---|---|
| `IrradianceMap` | 同一场景内所有 PBR 物体共享 diffuse 环境光 | `Scene::setIblEnvironmentResources(...)` |
| `PrefilteredEnvMap` | mip 链来自同一个 HDR environment bake | IBL bake / scene environment |
| `BrdfLut` | 是全局 BRDF 查表，不属于某个表面 | 默认或 bake pipeline 生成 |
| `EnvironmentUBO` | 控制环境强度和 roughness mip 数 | `scene.environment` / renderer environment data |

这样拆分后，金属球换材质不会破坏环境；场景换 HDR 环境也不需要改每个材质文件。

## 当前 bake 合同

`assets/shaders/glsl/` 下已有四组 bake shader：

| Shader | 作用 | 当前状态 |
|---|---|---|
| `equirect_to_cubemap` | HDR equirectangular 转 skybox cubemap | renderer bake 已执行 |
| `ibl_irradiance_convolve` | 生成 diffuse irradiance cubemap | renderer bake 已执行 |
| `ibl_prefilter_env` | 生成 roughness mip prefiltered cubemap | renderer bake 已执行 |
| `ibl_brdf_lut` | 生成 BRDF LUT | renderer bake 已执行 |

GPU bake executor 创建 cubemap face/mip attachment，执行上述 shader，并把结果注册成可被 descriptor lookup 消费的 scene-level resources。

## 下一步

进入 [03 HDR 到屏幕的 Post 流程](03-hdr-post-process-flow.md)。
