# Shader 在材质中的角色

在当前材质系统里，shader 属于 `RenderPathGraph` 的 pass 节点；`.material` 提供的是 `material.bsdf` 这类 pass source 所需的表面数据。

## Shader 合同分成两层

| 层 | 当前职责 | 例子 |
|---|---|---|
| Material contract shader | 描述 BSDF 参数、storage/accessor ABI，并提供 `lxLoadMaterialSurface` / BSDF 函数 | `assets/shaders/glsl/common/materials/standard_pbr.contract.glsl` |
| Render pass shader | 在某个 pass 中执行 raster/compute 逻辑，并通过 `LX_MATERIAL_CONTRACT_SOURCE` include contract | `assets/shaders/glsl/techniques/Forward/pbr.frag` |

Material contract 决定 `MaterialContractReflection`；Render pass shader 决定 `RenderPassNode` 的 shader payload、descriptor binding、vertex input 和 pipeline build 输入。

`MaterialContractReflector` 读取的是 `.contract.glsl` 顶部的注释 metadata 块，再校验实际 ABI 函数是否存在；它不是把整个 GLSL 编成 SPIR-V 后再猜参数表。这个解析入口在 `src/infra/material_loader/material_contract_reflector.cpp:742`。

## RenderPathGraph 选择 pass shader

```yaml
passes:
  - id: Forward
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
    sources:
      - geometry.vertex
      - geometry.index
      - material.bsdf
      - scene.camera
      - scene.lights
```

这意味着 shader 名、stage、dispatch、source/target、attachment 和 render state 的真值在 graph 中，而不是在 `.material` 中。

## Reflection 是机器可读的合同

Shader reflection 仍然重要，只是职责更明确：

| Reflection 输出 | 被谁使用 | 用途 |
|---|---|---|
| `ShaderResourceBinding` | graph validation / descriptor resolver / backend | 校验 descriptor binding 名字、set/binding、类型、buffer member |
| `VertexInputAttribute` | Scene validation / `PipelineBuildDesc` | 校验 mesh vertex layout，并过滤 pipeline vertex input state |
| material contract metadata | material parser / packer / shader variant resolver | 校验 BSDF 参数、storage ABI、accessor ABI |

Reflection 负责暴露真实资源合同。shader 需要的 material/feature/scene resource 必须注册并解析成 live payload。

## Contract source variant 是当前核心桥梁

Forward / Deferred surface shader 都不是裸编译入口。`pbr.frag` 开头要求外部定义 `LX_MATERIAL_CONTRACT_SOURCE`：

```glsl
#if defined(LX_MATERIAL_CONTRACT_SOURCE)
#include LX_MATERIAL_CONTRACT_SOURCE
#else
#error LX_MATERIAL_CONTRACT_SOURCE must be defined by the material shader variant
#endif
```

这段检查在 `assets/shaders/glsl/techniques/Forward/pbr.frag:7`。因此 pass shader 的工作方式是：

```text
RenderPathGraph pass shader = techniques/Forward/pbr
  + material instance source = common/materials/standard_pbr.contract.glsl
  -> MaterialSourceVariantResolver 编译 specialized shader variant
  -> shader 内部 include contract source
  -> main() 调 lxLoadMaterialSurface(...)
```

`MaterialSourceVariantResolver` 会先检查 pass shader 源码是否包含 `LX_MATERIAL_CONTRACT_SOURCE`。包含时，pass 必须在 `sources` 中声明 `material.bsdf`；不包含时，pass 不能声明 `material.bsdf`。这条一致性校验在 `src/infra/resource_parsers/material_source_variant_resolver.cpp:344`。

## Surface shader 如何使用材质数据

Forward PBR shader 的核心读法是：

```glsl
LxMaterialSurface surface =
    lxLoadMaterialSurface(vMaterialRefIndex, vUV, geometricNormal, tangentFrame);
```

`LxMaterialSurface` 是 contract 与 pass shader 之间的统一表面结构。`standard-pbr` contract 会从 `SceneMaterialRefs`、`SceneSourceMaterialRecords` 和 `SceneTextures` 读取 packed record / texture slot，再填出 `baseColor`、`metallic`、`roughness`、`normal`、`ao`、`emissive`。代码在 `assets/shaders/glsl/common/materials/standard_pbr.contract.glsl:87`。

`standard-pbr`、`matte`、`uber`、`metal`、`substrate` 都按 contract source 进入同一套 accessor ABI。Forward 和 Deferred pass 不直接理解每个 PBRT 参数名，而是消费 contract 输出的统一 `LxMaterialSurface`。

## Binding ownership 把材质资源和系统资源分开

| Binding / source | 归属 | 当前来源 |
|---|---|---|
| `material.bsdf` | material-owned surface data | `MaterialInstance` envelope + material storage |
| `feature.toneMapping` 等 | feature-owned | `RenderFeature` |
| `scene.camera` / `CameraUBO` | scene/system-owned | camera / scene resource |
| `scene.lights` / `LightUBO` / `SceneLightsUBO` | scene/system-owned | directional direct light 与聚合 light collection |
| `Bones` | renderable/system-owned | `SkeletonComponent` |
| offline scene SSBO | scene/system-owned | `SceneResourceTableUploadView` |

`.material` 不写 `CameraUBO`、`LightUBO`、`SceneLightsUBO` 或 offline SSBO。它们由 scene/resource table/graph 路径注入。当前主 Forward / Deferred shader 的直接光照仍读取 directional `LightUBO`；`SceneLightsUBO` 是三类 light 的聚合数据合同，只有 shader 显式 include/读取后才会参与多光源照明。

## Variant 靠 source contract 与 graph 边界收敛

当前关键变体是 material source variant：同一个 pass shader 可以针对不同 material contract 解析出 specialized shader variant，例如 `pbr.standard_pbr.frag.spv`。这类 variant 进入 `materialTypeVariant`，再与 `RenderPathNodeSignature` 一起生成 `PipelineKey`。

普通参数值，例如 `roughness: 0.35`，不会改变 shader variant 或 pipeline key。

## 我们已经学会了什么

shader 在材质系统里仍然提供可执行代码和 reflection 合同。但当前选择 shader 的位置是 RenderPathGraph；`.material` 只提供 material surface contract 所需的数据。

## 下一步

- [从 .material 到 MaterialInstance](file-to-instance.md)
- [MaterialInstance：运行时状态](material-instance.md)
- [源码分析：Shader](../../source_analysis/src/core/asset/shader.md)
