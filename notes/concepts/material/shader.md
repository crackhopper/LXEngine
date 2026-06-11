# Shader 在材质中的角色

在材质系统里，shader 不只是 GPU 上要执行的程序。它更像一份合同：它声明需要哪些顶点输入、哪些 descriptor binding、哪些宏变体会改变代码形状。材质系统通过这份合同决定 template 接口、instance 资源和 pipeline identity。

## ShaderProgramSet 把逻辑名、变体和编译结果放在一起

`MaterialPassDefinition` 不直接保存一个裸 shader 文件路径，而是保存 `ShaderProgramSet`：

| 字段 | 当前作用 |
|---|---|
| `shaderName` | `.material shader:` 或 pass 内 `shader:` 指向的 basename |
| `variants` | 当前 pass 启用的宏组合，例如 `USE_LIGHTING: true` |
| `shader` | `CompiledShader`，里面有 stage bytecode、reflection bindings、vertex inputs |

`shaderName + enabled variants` 会进入 `ShaderProgramSet::getPipelineSignature()`。所以开启一个 variant 不只是改参数，它会生成不同 shader 程序，也会影响 pipeline identity。

## Reflection 是 shader 合同的机器可读版本

`GenericMaterialLoader` 编译 `assets/shaders/glsl/<shader>.vert/.frag` 后，会调用 `ShaderReflector` 得到两类结构信息：

| Reflection 输出 | 被谁使用 | 用途 |
|---|---|---|
| `ShaderResourceBinding` | `MaterialTemplate` / `SceneNode` / backend | descriptor binding 名字、set/binding、类型、buffer member 布局 |
| `VertexInputAttribute` | `SceneNode` / `PipelineBuildDesc` | 校验 mesh vertex layout，并过滤 pipeline vertex input state |

例如 `rtr_experiment_template.frag` 里的 `MaterialUBO` 会变成一个 material-owned UBO binding：

```glsl
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec3 surfaceColor;
    float mixAmount;
    vec4 accentColor;
    int mode;
} material;
```

loader 反射出 `surfaceColor`、`mixAmount`、`accentColor`、`mode` 的类型和 offset 后，`.material parameters` 才能按 `MaterialUBO.mode` 这种 key 写入。

## Binding ownership 把材质资源和系统资源分开

同一份 shader 里可能同时使用材质参数、相机、光照、骨骼。当前代码用名字表划分所有权：

| Binding 名字 | 归属 | 期望类型 | 当前来源 |
|---|---|---|---|
| `CameraUBO` | system-owned | `UniformBuffer` | camera / scene-level resource |
| `LightUBO` | system-owned | `UniformBuffer` | legacy/simple light path |
| `SceneLightsUBO` | system-owned | `UniformBuffer` | scene light collection |
| `Bones` | renderable/system-owned | `UniformBuffer` | `SkeletonComponent` |
| 其他名字 | material-owned | UBO/SSBO/Texture2D/TextureCube | `MaterialInstance` |

`MaterialTemplate::rebuildMaterialInterface()` 会跳过 system-owned binding，只把剩下的 material-owned binding 放进 canonical 表。`SceneNode::rebuildValidatedCache()` 还会检查系统保留名字的 descriptor 类型是否正确，例如 `CameraUBO` 不能被 shader 写成 texture。

离线 compute 和后续 bindless 路径还有一类 scene-owned SSBO，它们不属于单个
`MaterialInstance`，而是来自 `SceneResourceTableUploadView`。例如
`offline_primary_ray.comp` 使用 `SceneVertices`、`SceneIndices`、
`SceneMeshes`、`ScenePrimitives`、`SceneObjects`、`SceneMaterials` 和
`SceneFrameParams`。这些 binding 描述整张 scene 的 GPU 数据合同；材质系统只负责
把 `MaterialInstance` 参数折叠进 `SceneGpuMaterialRecord`，不直接拥有这些 SSBO。

## resources 字段只对应材质纹理

`.material resources` 很容易被误解成“shader 里能访问的所有资源声明”。当前实现不是这样：

```yaml
resources:
  albedoMap: white        # -> MaterialInstance::setTexture(StringID("albedoMap"), ...)
```

它只能给 material-owned `Texture2D` / `TextureCube` binding 设置默认纹理。`CameraUBO`、`SceneLightsUBO`、`Bones` 这些系统名字不应该写在这里。它们由 scene/camera/light/skeleton 路径提供，并在 `RenderWorkQueue::build(...)` 生成 work item 时拼到 descriptor resources 里。

## Variants 是 pass 级 shader 结构

```yaml
shader: blinnphong_0
defaultTechnique: Forward

variants:
  USE_LIGHTING: true      # -> 全局默认 variant
  USE_UV: true

techniques:
  Forward:
    passes:
      Opaque:
        variants:
          USE_NORMAL_MAP: false # -> 与全局 variants 合并后编译本 pass shader
  Deferred:
    passes:
      GBuffer:
        shader: blinnphong_0_gbuffer
  OfflineRT:
    passes:
      RayTrace:
        shader: offline_pbr_direct_ray
        stage: compute
```

当前 loader 会先选择 technique，再把全局 variants 和 selected technique pass 内 variants 合并，编译该 pass 的 shader。`variantRules` 可以声明依赖关系，例如 `USE_NORMAL_MAP` 需要 `USE_LIGHTING` 和 `USE_UV` 同时开启。

## 我们已经学会了什么

shader 在材质系统里提供两类事实：一类是可执行代码，另一类是 reflection 合同。template 用合同建立 binding 接口，instance 用合同写参数和纹理，pipeline 用 shader 名和 enabled variants 建身份。

## 下一步

- [从 .material 到 MaterialInstance](file-to-instance.md)
- [MaterialInstance：运行时状态](material-instance.md)
- [源码分析：Shader](../../source_analysis/src/core/asset/shader.md)
