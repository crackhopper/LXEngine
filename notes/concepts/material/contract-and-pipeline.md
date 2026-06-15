# Contract 如何影响 Pipeline

当前 pipeline identity 只看会改变 pipeline 结构的事实：material side 的 type/source shader variant，以及 graph side 的 pass signature。材质参数值、texture handle、object transform 和 visibility 都是运行时数据，不是 pipeline key 的独立轴。

## 当前 Signature 链路

```text
MaterialContractReflection / material source resolver
  -> materialTypeVariant

RenderPassNode
  -> getRenderPathNodeSignature(node)

RenderWorkItem
  -> PipelineKey::build(materialTypeVariant, renderPathNodeSignature)

PipelineBuildDesc::fromRenderWorkItem(item)
  -> shader stages / bindings / render state / target / attachments / topology
```

`MaterialInstance` 的普通参数值不会进入 key。它的 BSDF type、source URI、reflection hash、storage/accessor ABI 和 resolved shader variant 才是 material-side identity 的结构事实。

## 材质字段和 Pipeline 的关系

| 内容 | 当前是否影响 pipeline identity | 说明 |
|---|---|---|
| `bsdf.type` | 是 | 参与 material type/source variant |
| `bsdf.source` contract | 是 | contract、storage ABI、accessor ABI 会影响 shader variant |
| BSDF 参数值 | 否 | 写入 envelope / material storage 数据 |
| texture/spectrum resource URI | 通常否 | 影响 resource handle 和 bindless slot；不改变 shader variant |
| RenderPathGraph `shader` | 是 | pass shader URI 属于 RenderPathNode signature |
| RenderPathGraph `renderState` | 是 | cull/depth/blend 属于 fixed-function pipeline state |
| RenderPathGraph attachment/geometry contract | 是 | target format/depth/topology/vertex contract 属于 pass contract |
| object transform / visibility | 否 | 影响 draw 数据或是否入队 |

## Source Variant 是 Shader 边界

同一个 pass shader 可以针对不同 material contract 编译出不同 shader variant：

```text
techniques/Forward/pbr
  + standard_pbr.contract.glsl
  -> pbr.standard_pbr.frag.spv
```

这类 variant 会进入 `materialTypeVariant`。Resolver 同时校验 pass shader 与 graph `sources` 是否一致：需要 `LX_MATERIAL_CONTRACT_SOURCE` 的 shader 必须由声明了 `material.bsdf` 的 pass 消费。

## RenderPathNodeSignature 是 Graph 边界

`getRenderPathNodeSignature(...)` 把 pass 的结构信息纳入签名：

| 类别 | 例子 |
|---|---|
| pass identity | pass id、stage、dispatch |
| shader identity | shader URI |
| resource contract | sources、targets、attachment format/depth |
| geometry contract | topology、vertex layout |
| fixed state | cull、depth、blend |

因此改变 `roughness` 不会重建 pipeline；改变 Forward pass 的 shader URI、depth format、blend state 或 material contract source 会改变 key。

## 我们已经学会了什么

材质影响 pipeline，是因为它的 type/source contract 决定 shader variant；RenderPathGraph 影响 pipeline，是因为它声明 pass shader、attachment、geometry 和 render state。最终 key 是 `MaterialTypeVariant + RenderPathNodeSignature`。

## 下一步

- [什么是 Pipeline](what-is-pipeline.md)
- [多 Pass 材质怎样变成 RenderWork](pass-rendering-flow.md)
- [Pipeline cache 子系统](../../subsystems/pipeline-cache.md)
