# Hardcut 当前边界：Bindless、Variants 与 FrameGraph

材质系统 hardcut 后有三条稳定边界：SurfaceMaterial 只管 BSDF envelope，RenderPathGraph 管 pass/shader/render state，SceneResourceTable 管资源 handle、material storage 和 draw/resource 表。读材质相关代码时，先判断改动落在哪条边界里。

## Bindless 解决资源接口

| 机制 | 当前承担的职责 |
|---|---|
| `SceneResourceTable` | 统一持有 mesh、material、texture、feature、shader、graph handle |
| material ref table | 让 draw 通过 material ref index 找到 source-local material record |
| source-local material records | 保存 contract packer 生成的材质数据 |
| texture/material/object/draw/mesh table | 给 renderer 提供可批处理、可上传的资源视图 |
| default texture set | 给 contract storage field 提供稳定默认资源 |

这条边界的核心是：`.material` 声明 typed envelope；packer 生成 source-local record；SceneResourceTable 提供统一上传视图。Shader 读取的是 material ref、source record 和 texture table，而不是每个材质单独拼一套绑定。

## Variant 只保留结构差异

| 差异类型 | 当前归属 |
|---|---|
| Forward / Deferred / OfflineRT pass 契约 | RenderPathGraph pass |
| cull / depth / blend / attachment format | RenderPathNode signature |
| material contract/source 差异 | material type/source variant |
| 普通参数和纹理资源差异 | material envelope、source-local record、resource handle |
| object transform / visibility | draw/object 数据 |

不是所有差异都应该变成 shader variant。`roughness`、`baseColor`、贴图 URI 这类运行时数据进入 material storage；只有 contract source、accessor ABI、pass shader、render state、attachment 和 geometry contract 这类结构差异进入 pipeline identity。

## FrameGraph 执行 Pass DAG

RenderPathGraph 声明 pass；FrameGraph 根据 source/target 依赖排序和校验；RenderWorkCompiler 把匹配的 scene/material/geometry 组合成 draw 或 compute input，并准备 pipeline-facing desc。

```text
RenderPathGraph
  -> FrameGraph resource dependency validation
  -> RenderWorkCompiler
  -> RenderInput + RenderInputDesc
  -> PipelineKey(MaterialTypeVariant, RenderPathNodeSignature)
  -> backend pipeline / draw / dispatch
```

Surface pass 通过 `input.material.type` 选择 material；debug/object pass 可以通过 `input.object.renderClass` 选择 object；post、shadow、debug、offline pass 通过自己的 `input`、`sources` 和 `targets` 声明资源合同。材质文件不参与 pass DAG 决策。

## 当前检查清单

| 想确认 | 看哪里 |
|---|---|
| material 参数是否合法 | `.material` + `.contract.glsl` metadata |
| shader variant 是否该生成 | pass shader 是否需要 `LX_MATERIAL_CONTRACT_SOURCE`，graph 是否声明 `material.bsdf` |
| pipeline key 为什么变化 | `materialTypeVariant` 与 `RenderPathNodeSignature` |
| draw 为什么没有进入某 pass | `input.material.type`、`input.object.renderClass`、visibility mask 与 scene material/object |
| 资源是否进上传视图 | `SceneResourceTableUploadView` 的 material refs、source records、texture handles |
| pass 顺序和 target 是否正确 | RenderPathGraph `sources` / `targets` 与 FrameGraph validation |

## 我们已经学会了什么

Hardcut 后的材质系统把资源、variant 和 pass 执行分别收口：材质 contract 定义 surface，SceneResourceTable 承载资源和 material records，RenderPathGraph/FrameGraph 执行 pass DAG，PipelineKey 只保留真正会改变 pipeline 的结构差异。

## 继续阅读

- [Material Contract v2](material-contract-v2.md)
- [RenderPathGraph Pass Contract](render-path-pass-contract.md)
- [Contract 如何影响 Pipeline](contract-and-pipeline.md)
