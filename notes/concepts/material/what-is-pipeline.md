# 什么是 Pipeline

Pipeline 可以先理解成“GPU 厨房的一套固定设备配置”：这套配置决定用哪份 shader、顶点数据怎样解释、三角形怎样剔除、深度怎样测试、颜色怎样混合。多个 render input desc 如果结构事实完全相同，就可以复用同一套配置。

在 LXEngine 里，pipeline 不是材质本身，也不是 shader 本身。它是一次 `RenderInputDesc` 的材质类型/源码变体与 RenderPath pass contract 被归约后的结果。Realtime draw input 创建 graphics pipeline；offline compute input 创建 compute pipeline。

## PipelineKey 回答“能不能复用”

当前 `PipelineKey` 只包一个结构化 `StringID`。它由两部分组合：

```text
PipelineKey::build(
  materialTypeVariant,       # BSDF type + material source contract + shader variant identity
  renderPathNodeSignature    # pass id + shader URI + render state + rendering/geometry/attachment/resource contract
)
```

| Signature | 当前来源 | 表达什么 |
|---|---|---|
| `materialTypeVariant` | `MaterialInstance` / shader material source resolver | 材质类型、source contract、已解析 shader variant |
| `renderPathNodeSignature` | `RenderPassNode` / `FramePass` | pass 的 shader、stage/dispatch、render state、geometry、attachment、source/target contract |

mesh layout/topology 通过 pass geometry contract 校验；attachment/target 结构包含在 RenderPathNode signature 里。object transform 和 visibility 属于 draw 数据。

## PipelineBuildDesc 回答“怎样创建”

`PipelineKey` 只负责身份，不保存创建 pipeline 所需的所有数据。真正创建时要用 `PipelineBuildDesc`：

| 字段 | 从哪里来 |
|---|---|
| `type` | `RenderInputKind` 和 pass dispatch 推导出的 `PipelineBuildType::Graphics` / `Compute` |
| `key` | `RenderInputDesc.pipelineKey` |
| `shaderVariantKey` | input desc 的 shader/material variant |
| `target` / `attachments` | RenderPathNode attachment contract 和 runtime target |
| `renderingMode` | dynamic/traditional rendering mode |
| `stages` | shader stages |
| `bindings` | shader reflection bindings |
| `vertexLayout` | graphics input 的 vertex buffer layout 按 shader input 过滤 |
| `renderState` | RenderPathNode / pass 的 fixed-function state |
| `topology` | geometry contract / index buffer topology |

这也是为什么文档里要区分 pipeline identity 和 pipeline build input：前者用于 cache lookup，后者用于 cache miss 时创建对象。

## 哪些变化通常影响 pipeline

| 会影响 pipeline identity 或创建输入 | 为什么 |
|---|---|
| 换 RenderPathGraph pass shader URI | shader stages 变了 |
| 改 material source contract / specialized variant | `materialTypeVariant` 变了 |
| 改 render state | cull/depth/blend 是固定功能 pipeline state |
| 改 attachment/target contract | render pass / dynamic rendering attachment 兼容性变了 |
| 改 geometry vertex/topology contract | vertex input / topology 创建输入变了 |
| compute shader SSBO schema 变化 | compute pipeline layout 变了 |

普通 object transform、单个节点名字、camera 数值、light 数值都不是 pipeline 身份。

## 哪些变化不影响 pipeline

| 不影响 pipeline identity | 为什么 |
|---|---|
| 改 BSDF 参数值 | 只是 material parameter/storage 数据变化 |
| 改 texture URI 指向的具体资源 | resource handle 变化，不是 pipeline state |
| 改节点 transform | per-draw data 变化 |
| 改 light 数值 | scene-level GPU record / UBO 数据变化 |
| 关闭某个 pass 或 input 不命中 | 影响是否产出 input，不改变该 pass 自身 key |

这些变化仍然会影响画面，但不应该触发 pipeline 重建。

## PipelineCache 放在 backend

pipeline 的最终对象是 backend 资源。当前 Vulkan backend 通过 `PipelineCache` 按 `PipelineKey` 复用 graphics 和 compute pipeline：

```text
RenderWorkCompiler::prepare(...)
  -> accepted RenderInputDesc.pipelineBuildDesc
  -> backend PipelineCache::preload(descs)
  -> 执行 input 时按 desc.pipelineKey 查找 pipeline
```

如果预构建漏了，运行时 cache miss 会补建并打印 warning；上层仍应尽量从 `RenderWorkCompiler::prepare(...)` 产物提前收集 pipeline 构建需求。

## 我们已经学会了什么

Pipeline 是 input desc 结构的复用单位。当前 `PipelineKey` 由 material type/source variant 与 RenderPathNode signature 组成；`PipelineBuildDesc` 则携带创建 pipeline 所需的 shader、layout、render state、attachment 和 topology 输入。

## 下一步

- [Contract 如何影响 Pipeline](contract-and-pipeline.md)
- [多 Pass 材质怎样变成 RenderInput](pass-rendering-flow.md)
- [Pipeline identity 源码分析](../../source_analysis/src/core/pipeline/pipeline_identity.md)
