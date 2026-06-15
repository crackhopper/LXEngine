# 材质系统总览：SurfaceMaterial 与 RenderPathGraph 分工

材质系统现在像两张分开的单据：`.material` 是表面参数单，只说明一个物体表面是什么材质；`RenderPathGraph` 是渲染工艺单，说明当前 Forward、Deferred 或 OfflineRT 路径要跑哪些 pass、每个 pass 用哪个 shader、读写哪些资源、使用什么固定功能状态。

这条边界是 hardcut 后的主线：材质只拥有 surface contract；shader、pass、render state、attachment 和 geometry contract 都来自 RenderPathGraph。

## 核心对象

| 对象 | 当前职责 | 主要代码 / 资产 |
|---|---|---|
| `.material` | `schema: lxe.material.v2` 的 BSDF envelope | `assets/materials/pbr.material` |
| `MaterialResourceParser` | 解析 BSDF 参数、资源 URI、材质依赖，并创建 `MaterialInstance` | `src/infra/material_loader/material_resource_parser.*` |
| `MaterialContractReflection` | 描述 BSDF contract、storage ABI 和 accessor ABI | `src/core/asset/material_contract.hpp` |
| `MaterialInstance` | 保存 BSDF type、material source signature、参数 envelope、资源依赖和少量非 surface shader binding 状态 | `src/core/asset/material_instance.hpp` |
| `RenderPathGraph` | 声明 render path 下的 pass DAG、shader URI、source/target、geometry/attachment contract 和 render state | `assets/render_paths/*.render-path.yaml` |
| `RenderPassNode` | 单个 pass 的 stage/dispatch/filter/rendering/geometry/renderState | `src/core/asset/render_effect.hpp` |
| `RenderFeature` | tone mapping、shadow、post effect 等算法参数 envelope | `assets/effects/*.render-feature.yaml` |
| `SceneResourceTable` | 持有 material、texture、render path graph、render feature、shader 等带 generation 的资源 handle | `src/core/scene/scene_resource_table.hpp` |
| `RenderWorkItem` | 某个 pass 下的一次 pipeline work；raster 是 draw，offline 可以是 dispatch | `src/core/scene/scene.hpp` |
| `PipelineKey` | `MaterialTypeVariant + RenderPathNodeSignature` 组合出的 pipeline cache 身份 | `src/core/pipeline/pipeline_key.hpp` |

## 推荐阅读顺序

1. [Material Contract v2](material-contract-v2.md)：先建立材质定义、contract metadata、parser、shader variant 和 pipeline identity 的完整模型。
2. [从 .material 到 MaterialInstance](file-to-instance.md)：看 v2 material 文件怎样成为 runtime instance。
3. [Shader 在材质中的角色](shader.md)：看 contract source、RenderPathGraph pass shader 和系统 ABI 如何配合。
4. [内置 Shader 清单](shader-catalog.md)：认识 Forward、Deferred、PostProcess、OfflineRT shader 家族。
5. [MaterialInstance：运行时状态](material-instance.md)：看 envelope、source signature、resource dependency 和非 surface binding 状态。
6. [多 Pass 材质怎样变成 RenderWork](pass-rendering-flow.md)：把 RenderPathGraph、FrameGraph、RenderWorkQueue 串起来。
7. [什么是 Pipeline](what-is-pipeline.md)：建立当前 pipeline identity 的模型。
8. [Contract 如何影响 Pipeline](contract-and-pipeline.md)：理解 material type/source variant 与 RenderPathNode signature 怎样组成 pipeline identity。
9. [创建与排错自定义材质](custom-template.md)：按当前 v2 authoring 路径写材质、contract 和 render path。
10. [Hardcut 当前边界](hardcut-boundary.md)：把 bindless、variant、FrameGraph 和资源责任边界放在同一页检查。

## 当前边界先记住

| 问题 | 当前答案 |
|---|---|
| `.material` 负责什么 | 负责 `schema`、`renderClass`、`bsdf.type`、`bsdf.source`、typed parameters、tags 和 metadata |
| RenderPathGraph 负责什么 | 负责 pass DAG、shader URI、source/target、attachment、geometry 和 render state |
| 一个 RenderPath 能有多个 pass 吗 | 可以；Forward 示例包含 Shadow、Forward、PostProcess、DebugOverlay |
| `RenderFeature` 负责什么 | 提供 tone mapping、shadow、post effect 等算法参数 envelope |
| 参数值会影响 pipeline 吗 | 普通 BSDF 参数值不影响；material type/source contract 和 shader variant signature 会影响 |
| mesh/object 是否是 `PipelineKey` 独立轴 | mesh layout/topology 通过 RenderPathNode geometry contract 校验；object transform 和 visibility 属于 draw 数据 |
| target 是否是 `PipelineKey` 独立轴 | attachment/target contract 已纳入 RenderPathNode signature |
| bindless 和 indirect 数据在哪里收口 | `SceneResourceTable` 持有 typed resources、material refs、source-local records 和 draw/mesh/object 表 |

## 当前内置材质先分清状态

| 类型 | 当前用途 | 说明 |
|---|---|---|
| `standard-pbr` | glTF / 实时 PBR 主路径 | 当前最完整的 factor + texture storage 示例，Damaged Helmet 生成材质使用它 |
| `matte` / `uber` | PBRT-style diffuse / general material | 适合导入或手写 PBRT 参数 envelope |
| `metal` / `substrate` | PBRT-style conductor / layered material | 适合 spectrum、roughness 与 layered 参数练习 |

## 当前主数据流

```text
schema: lxe.material.v2
  -> MaterialResourceParser
  -> MaterialInstance(bsdf type, envelopes, material source signature)
  -> SceneResourceTable registers material/resource dependencies

schema: lxe.render-path-graph.v1
  -> RenderPathGraphResourceParser
  -> RenderPathGraph(RenderPassNode...)
  -> FramePass / RenderWorkQueue
  -> RenderWorkItem
  -> PipelineKey(MaterialTypeVariant, RenderPathNodeSignature)
  -> PipelineBuildDesc
  -> Vulkan PipelineCache
```

## 继续阅读

- [MaterialInstance 源码分析](../../source_analysis/src/core/asset/material_instance.md)
- [RenderWorkQueue 源码分析](../../source_analysis/src/core/frame_graph/render_queue.md)
