# RenderPathGraph Pass Contract

RenderPathGraph 是当前渲染步骤的真值来源。材质只声明 surface contract；一个 pass 怎样执行、用哪个 shader、读哪些 source、写哪些 target、需要哪些 attachment 和 render state，都由 `schema: lxe.render-path-graph.v1` 文件声明。

## 一个 Surface Pass 的形状

Forward 主 pass 的核心结构如下：

```yaml
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward

passes:
  - id: Forward
    stage: raster
    dispatch: draw
    shader: techniques/Forward/pbr
    filters:
      renderClass: [surface.opaque]
      bsdf: [matte, uber, metal, substrate, standard-pbr]
    sources:
      - geometry.vertex
      - geometry.index
      - material.bsdf
      - scene.camera
      - scene.lights
    targets: [hdr.color, depth.main]
    rendering:
      mode: dynamic
      attachments:
        - target: hdr.color
          format: RGBA16Float
        - target: depth.main
          format: D32Float
          depth: true
    geometry:
      topology: triangle-list
      vertexLayout: mesh.pbr
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      depthOp: LessEqual
      blendEnable: false
```

这个 pass 同时定义了三个边界：

| 边界 | 字段 | 作用 |
|---|---|---|
| material 匹配 | `filters.renderClass` / `filters.bsdf` | 决定哪些 material instance 能进入这个 pass |
| shader 输入 | `sources` | 声明 pass shader 需要 geometry、material、scene 或 feature 数据 |
| pipeline 结构 | `shader` / `rendering` / `geometry` / `renderState` | 决定 shader stage、attachment、vertex input、topology 和 fixed-function state |

## `material.bsdf` 的含义

`material.bsdf` 是 pass 对材质 surface 数据的依赖声明。它和 `.material` 中的 `bsdf.source` 配合：

```text
.material bsdf.source
  -> MaterialContractReflection
  -> MaterialSourceVariantResolver
  -> pass shader include LX_MATERIAL_CONTRACT_SOURCE
  -> specialized shader variant
```

如果 pass shader 包含 `LX_MATERIAL_CONTRACT_SOURCE`，pass 就必须声明 `material.bsdf`。Resolver 会用这条 source 检查 pass shader、material contract 和 graph filter 是否一致。

## Pass Contract 怎样进入 Pipeline

`RenderPassNode` 会被转换成 `RenderPathNodeSignature`。签名包含 pass id、shader URI、stage、dispatch、source、target、attachment、geometry 和 render state。`PipelineKey` 再把它和 material type/source variant 组合：

```text
PipelineKey::build(materialTypeVariant, renderPathNodeSignature)
```

普通材质参数值不会进入这个 key。改变 `roughness` 或 `baseColor` 会更新 material data；改变 pass shader、attachment format、render state 或 material contract source 才会改变 pipeline identity。

## 多 Pass RenderPath

一个 RenderPathGraph 可以有多个 pass。Forward 示例通常包含：

| Pass | 作用 |
|---|---|
| `Shadow` | 生成 shadow depth |
| `Forward` | 画 surface material 到 HDR color/depth |
| `PostProcess` | tone mapping / bloom / gamma |
| `DebugOverlay` | editor 叠加信息 |

这些 pass 之间通过 `sources` 和 `targets` 形成资源依赖。FrameGraph 会根据资源读写关系排序并校验 source/target 是否存在。

## 我们已经学会了什么

RenderPathGraph pass contract 是当前 pipeline 结构的中心。`.material` 提供 surface 数据；pass contract 提供 shader、source/target、geometry、attachment 和 render state；两者通过 material source variant 和 `RenderPathNodeSignature` 汇合。

## 下一步

- [Contract 如何影响 Pipeline](contract-and-pipeline.md)
- [多 Pass 材质怎样变成 RenderWork](pass-rendering-flow.md)
- [Material Contract v2](material-contract-v2.md)
