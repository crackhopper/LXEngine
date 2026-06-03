# 材质模板为什么会影响 Pipeline

`MaterialTemplate` 像菜谱里的工艺说明：同样的食材，如果一步要求烘烤、另一步要求油炸，厨房设备配置就不同。材质模板保存的 shader、variants 和 render state 正是这些“会改变设备配置”的信息，所以它会进入 pipeline identity。

## Signature 链路

当前材质侧 signature 的链路是：

```text
ShaderProgramSet::getPipelineSignature()
  = compose(shaderName, enabled variants)

RenderState::getPipelineSignature()
  = compose(cull, depth test/write/op, blend factors)

MaterialPassDefinition::getPipelineSignature()
  = compose(shaderProgramSig, renderStateSig)

MaterialTemplate::getPipelineSignature(pass)
  = passDefinition.getPipelineSignature()

MaterialInstance::getPipelineSignature(pass)
  = compose(MaterialRender, templatePassSig)

PipelineKey::build(objectSig, materialSig)
```

`MaterialInstance` 在这里没有把参数值放进去。它只是把 template 的 pass signature 包成 material render signature。

## 材质字段和 pipeline 的关系

| 材质侧内容 | 当前是否影响 pipeline identity | 说明 |
|---|---|---|
| `shader` | 是 | shader basename 进入 `ShaderProgramSet` signature |
| enabled `variants` | 是 | enabled macro 名排序后进入 signature |
| pass `renderState` | 是 | cull/depth/blend 进入 `RenderState` signature |
| pass 名 | 间接影响 | queue 按 pass 选择对应 definition；signature 来自该 pass definition |
| `parameters` 值 | 否 | 写入 `ParameterBuffer` |
| `resources` 纹理 | 否 | 写入 descriptor resource |
| pass enable | 否 | 决定是否为该 pass 产出 item |
| system-owned UBO 内容 | 否 | scene-level resource 数据变化 |

这张表是写材质时最常用的判断准则：如果改动改变的是“画法结构”，通常影响 pipeline；如果只是“给同一画法换数据”，通常不影响。

## Variant 不是参数

```yaml
variants:
  USE_LIGHTING: true
```

`USE_LIGHTING` 会决定 GLSL 预处理后的代码形状。它可能改变 vertex input、fragment varying、descriptor binding，甚至 shader stage 内部逻辑。因此当前实现把 enabled variants 放入 pipeline signature。

与之相对：

```yaml
parameters:
  MaterialUBO.enableAlbedo: 1
```

`enableAlbedo` 是运行时 buffer 值。它不会重新编译 shader，也不会改变 pipeline key。

## RenderState 属于 pass，不属于 instance

`RenderState` 当前在 `MaterialPassDefinition` 里：

```yaml
passes:
  Forward:
    renderState:
      cullMode: None
      depthTest: true
      depthWrite: true
      blendEnable: false
```

这意味着同一个 template 的所有 instance 共享该 pass 的 render state。如果我们需要同一个 shader 和参数结构但不同 blend/cull 配置，当前应通过不同 material/template 或不同 pass 定义表达，而不是在 instance 上动态切换 render state。

## MaterialTemplate 不是 PipelineCache

`MaterialTemplate` 只负责生成稳定 signature 和提供 build desc 所需的 shader/render state 信息。真正持有 pipeline 生命周期的是 backend `PipelineCache`：

| 层 | 负责什么 |
|---|---|
| `MaterialTemplate` | pass 结构、shader program、render state、material interface |
| `SceneNode` | 组合 mesh/material，生成 per-pass `PipelineKey` |
| `FrameGraph` / `RenderWorkQueue` | 汇总 work items 和 unique build desc |
| backend `PipelineCache` | 按 `PipelineKey` 创建、保存、查找 pipeline 对象 |

## Roadmap 中哪些关系会变化

未来 bindless / ubershader 方向可能把部分材质特性从 variants 下沉为 uniform branch 或 bindless slot。那会减少某些 material feature 对 pipeline identity 的影响。

但这仍然是未实施设计。当前代码里，enabled variants 仍然进入 pipeline signature；descriptor layout 仍来自 shader reflection；`MaterialInstance` 仍按 pass 返回传统 descriptor resources。

## 我们已经学会了什么

材质模板影响 pipeline，是因为它保存了 shader 程序选择和固定功能状态。材质实例的大部分数据只影响 draw 使用的数据，不影响 pipeline 身份。判断一个改动是否会触发 pipeline 差异，要看它是否改变 shader/render state/mesh layout 这类结构事实。

## 下一步

- [什么是 Pipeline](what-is-pipeline.md)
- [未来路线：Bindless、Variants 与 FrameGraph](future-roadmap.md)
- [Pipeline cache 子系统](../../subsystems/pipeline-cache.md)
