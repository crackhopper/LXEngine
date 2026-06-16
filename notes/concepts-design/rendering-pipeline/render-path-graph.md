# RenderPathGraph：渲染路线说明书

RenderPathGraph 可以先想成一张工厂流程图：它不存放材质参数，也不提交 Vulkan 命令，而是说明这一条渲染路线有哪些工序，每道工序用哪个 shader，读哪些输入，写哪些输出，以及这道工序从哪里取得 work input。

在 LXEngine 里，这张流程图写在 `assets/render_paths/*.render-path.yaml`。当前默认实时路径包括 Forward / Deferred 及其 bloom 变体；OfflineRT 的 graph asset 仍属于 [REQ-074-h](../../requirements/074-h-offlinert-render-path-graph-compute-path.md) 后续工作。

## 根字段说明这是什么路线

```yaml
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward

features:
  toneMapping:
    uri: effects/tone_mapping.render-feature.yaml

passes:
  - id: Forward
    # pass contract...
```

| 字段 | 当前含义 | C++ 结构 |
|---|---|---|
| `schema` | 文件格式版本；当前只接受 `lxe.render-path-graph.v1` | parser gate |
| `name` | graph 的人类可读名字，也用于 diagnostic | `RenderPathGraph::name` |
| `renderPath` | 渲染路线类型；当前 enum 支持 `Forward`、`Deferred`、`OfflineRT` | `RenderPathGraph::renderPath` |
| `features` | graph 依赖的 RenderFeature，例如 tone mapping | `RenderPathGraph::features` |
| `passes` | 有序 pass 列表，每个元素会变成 `RenderPassNode` | `RenderPathGraph::passes` |

根字段和 pass 字段都走 strict parser。旧字段、拼错字段、未建模字段不会被悄悄忽略。

## Pass 是一条可执行工序

当前 Forward 主 pass 的结构大致如下：

```yaml
- id: Forward
  stage: raster
  dispatch: draw
  shader: render_paths/Forward/pbr

  input:
    kind: scene-renderables
    material:
      type: [matte, uber, metal, substrate, standard-pbr]
      required: true
    geometry:
      vertex: position-only
      topology: triangle-list

  rendering:
    mode: dynamic
    attachments:
      - target: hdr.color
        format: RGBA16Float
        samples: 1
        layers: 1
      - target: depth.main
        format: D32Float
        samples: 1
        layers: 1
        depth: true

  sources:
    - geometry.vertex
    - geometry.index
    - material.bsdf
    - scene.camera
    - scene.lights
  targets: [hdr.color, depth.main]

  renderState:
    cullMode: Back
    depthTest: true
    depthWrite: true
    depthOp: LessEqual
    blendEnable: false
```

这些字段分成五类：

| 类别 | 字段 | 解决的问题 |
|---|---|---|
| 身份 | `id` | 这道 pass 叫什么，后续 `FramePass::name` 和 diagnostics 都用它 |
| 执行形态 | `stage`、`dispatch` | 选择 raster / compute pipeline，以及 draw / fullscreen / compute 执行方式 |
| work input | `input` | 声明 compiler 从 scene renderables、fullscreen triangle 或 compute dispatch 取输入 |
| graph 资源 | `sources`、`targets`、`features` | 声明 pass 读写哪些 graph resource |
| pipeline 合同 | `shader`、`rendering`、`renderState`、`input.geometry` | shader、attachment、固定功能状态和 draw geometry contract |

旧的 top-level `filters` 和 top-level `geometry` 已不再是当前正向 schema。material/object 过滤和 geometry contract 都属于 `input` 子树；旧写法会被 parser / contract test 拒绝。

## input 决定 work 来源

scene-renderable raster pass 使用：

```yaml
input:
  kind: scene-renderables
  material:
    type: [matte, uber, metal, substrate, standard-pbr]
    required: true
  geometry:
    vertex: position-only
    topology: triangle-list
```

`input.material.type` 替代旧 `filters.bsdf`。`material.required: true` 表示没有材质的 renderable 不能进入这道 pass。

对象类型过滤放在 `input.object.renderClass`：

```yaml
input:
  kind: scene-renderables
  object:
    renderClass: [debug.mesh]
  material:
    required: false
  geometry:
    vertex: position-only
    topology: line-list
```

这个形态用于 debug mesh：它只接收 `debug.mesh`，允许无材质输入，并由显式 debug shader 负责渲染。

fullscreen raster pass 使用：

```yaml
input:
  kind: fullscreen-triangle
```

它不会遍历 scene renderables。`RenderWorkCompiler` 会生成一个内置 fullscreen-triangle `RenderDrawInput`，backend 记录三角形 draw。

## stage 和 dispatch 不是同一个轴

| `stage` | `dispatch` | `input.kind` | 例子 |
|---|---|---|---|
| `raster` | `draw` | `scene-renderables` | Shadow、Forward、Deferred GBuffer、DebugOverlay |
| `raster` | `fullscreen` | `fullscreen-triangle` | PostProcess、Bloom、DeferredLighting |
| `compute` | `compute` | `compute-dispatch` | 当前 offline software-compute 的 `OfflineCompute` pass |

`stage` 说明 pipeline 类型，`dispatch` 说明执行方式，`input.kind` 说明 work 从哪里来。三者组合让 parser 可以拒绝非法组合，例如 fullscreen input 搭配 draw dispatch，或 compute input 搭配 raster stage。

## shader 是 pass shader

`shader` 指向 pass 的 base shader URI，例如：

| URI | 当前解析方向 |
|---|---|
| `render_paths/Forward/pbr` | `assets/shaders/glsl/render_paths/Forward/pbr.*` |
| `render_paths/Deferred/pbr_gbuffer` | `assets/shaders/glsl/render_paths/Deferred/pbr_gbuffer.*` |
| `post_process` / `debug_overlay` | 当前内置 shader 名称路径 |

材质文件不声明 pass shader。材质描述 surface contract；RenderPathGraph 声明 pass shader；source variant resolver 再把两者合成最终 shader variant 和 reflection facts。

## sources 和 targets 是资源合同

`sources` / `targets` 使用稳定 graph resource name。它们不是 Vulkan image 或 buffer 句柄，而是 pass 之间交接数据的名字。

| 资源族 | 例子 | 当前用途 |
|---|---|---|
| 几何输入 | `geometry.vertex`、`geometry.index` | draw pass 需要 mesh 几何 |
| 材质输入 | `material.bsdf` | pass shader 需要材质 source variant |
| 场景输入 | `scene.camera`、`scene.lights` | camera / light scene-level resource |
| 中间颜色 | `hdr.color`、`bloom.blur` | pass 间传递 color attachment |
| 深度 | `depth.main`、`shadow.main` | depth attachment 或 shadow depth |
| GBuffer | `gbuffer.albedoAlpha`、`gbuffer.normalRoughness`、`gbuffer.material` | Deferred path 输入输出 |
| feature | `feature.toneMapping` | RenderFeature 依赖 |
| 输出 | `swapchain.color`、`debug.overlay` | 最终显示或 debug overlay |

Parser 先检查字段形状，`FrameGraph::compile()` 再通过 `GraphResourceRegistry` 校验资源名、producer/consumer 关系和 write mode。

## rendering 与 renderState 进入 pipeline identity

`rendering` 描述 dynamic rendering attachment 形状，`renderState` 保存固定功能状态。attachment format、depth flag、cull/depth/blend state 都会进入 render path node signature，也就会影响 pipeline identity。

`input.geometry` 只属于 scene-renderable draw pass。Fullscreen pass 不需要 geometry，compute pass 也不需要 geometry。draw pass 的 mesh vertex layout 或 topology 不满足合同，应在 compiler prepare / validation 阶段拒绝，而不是拆出另一条隐式 pipeline。

## YAML 到当前 C++ 主线

```text
assets/render_paths/*.render-path.yaml
  -> RenderPathGraphResourceParser
  -> RenderPathGraph / RenderPassNode
  -> buildFrameGraphFromRenderPathGraph()
  -> FrameGraph / FramePass(input)
  -> FrameGraph::compile(GraphResourceRegistry)
  -> RenderWorkCompiler::buildInputs()
  -> RenderWorkCompiler::prepare()
  -> RenderInput[] + RenderInputDesc[]
  -> Vulkan pipeline/upload/execute
```

| YAML / 概念 | C++ 对象 |
|---|---|
| 整张 graph | `RenderPathGraph` |
| 单个 pass | `RenderPassNode` |
| `input` | `RenderPassInputContract` |
| `input.object.renderClass` | `RenderPassObjectInputFilter::renderClasses` |
| `input.material.type` / `required` | `RenderPassMaterialInputFilter` |
| `input.geometry` | `RenderPathGeometryContract` |
| `rendering.attachments[]` | `RenderPathAttachmentContract` |
| graph-built runtime pass | `FramePass` |
| `sources` | `FrameGraphRead` |
| `targets` | `FrameGraphWrite` |

## 我们已经学会了什么

RenderPathGraph 是渲染路线的结构说明书。它通过 `input` 子树把 pass 的 work 来源声明清楚，再把 shader、resources、attachments 和 render state 传给 `FramePass`。`FrameGraph::compile()` 只负责 graph 层排序和资源校验。

实际 draw / dispatch payload 由 `RenderWorkCompiler` 生成 typed `RenderInput[]`。

`RenderInputDesc[]` 只保存 validation、pipeline、binding、resource dependency、diagnostic 和 stats facts，并用 `inputIndex` 指向 typed input。Vulkan 执行同时消费 typed `RenderInput[]` 和对应 desc / pipeline-upload facts，不能从 desc 反推出执行数据。

## 下一步

- [FrameGraph：一帧的 Pass 排程表](framegraph.md)
- [RenderWorkCompiler：FramePass 之后的唯一工单编译器](render-work-compiler.md)
- [Realtime 与 Offline：共享同一条 compiler 主线](realtime-offline-shared-flow.md)
