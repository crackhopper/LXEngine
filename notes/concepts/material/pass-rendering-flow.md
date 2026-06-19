# RenderPathGraph 怎样变成 RenderInput

Surface material 只说明“表面是什么”；真正的多 pass 来自 active `RenderPathGraph`。同一个 object 可能在 Shadow pass 写 depth，在 Forward pass 写 HDR color，随后 fullscreen Bloom pass 把 HDR color 写到 swapchain，DebugOverlay 再追加调试线。LXEngine 不会把整张 graph 一次性交给 backend，而是把它拆成“某个 pass 下的一份 pipeline work”。

当前这份 work 分成两层：`RenderInput` 描述要画什么或 dispatch 什么；`RenderInputDesc` 描述这份输入对应的 pipeline、shader variant、binding plan 和诊断结果。Realtime raster 路径通常生成 `RenderDrawInput`；offline compute 路径生成 `RenderComputeInput`。

## 先把这条生产线校准清楚

我们可以把一次渲染想成“按工艺单加工场景物料”。`.scene.yaml` 是输入来源，
`SceneResourceTable` 是物料仓库，`RenderPathGraph` 是工艺单，`FrameGraph` 把
工艺单里的 pass 依赖排成 DAG，`RenderWorkCompiler` 再为每个 pass 打包可执行
输入，executor 最后把这些输入真正送到 backend。

| 阶段 | 当前职责 | 不负责什么 |
|---|---|---|
| Scene / SceneDocument | 提供 node、camera、light、mesh、material、feature 等输入事实 | 不决定 pass 顺序 |
| SceneResourceTable | 持有解析后的 typed resources 和 generation handle | 不执行 shader |
| RenderPathGraph | 声明 pass、source/target、input contract、shader、render state | 不持有 runtime draw payload |
| FrameGraph::compile | 校验 source/target 并按资源依赖生成稳定 DAG 顺序 | 不打包 `RenderInput`，不创建 pipeline |
| RenderWorkCompiler | 按 DAG 中的单个 pass 筛选 scene facts，生成 `RenderInput` / `RenderInputDesc` | 不提交 Vulkan command buffer |
| Executor / backend | 按 compiled graph 顺序取 pass + prepared input，绑定资源和 pipeline 后执行 | 不重新解释 scene schema |

所以“build FrameGraph”更准确地说是编译 pass/resource DAG。`RenderInput` 的打包
发生在 DAG 已经确定之后；执行 pipeline 则是 executor/backend 的职责。

## RenderPathGraph pass 是结构真值

`assets/render_paths/forward_main.render-path.yaml` 当前包含这些 pass：

| Pass | 作用 | 关键 source / target |
|---|---|---|
| `Shadow` | 写 shadow depth | `scene.camera` / `scene.lights` -> `shadow.main` |
| `Forward` | 采样 material BSDF 并写 HDR color/depth | `material.bsdf`、`scene.camera`、`scene.lights` -> `hdr.color`、`depth.main` |
| `Bloom` | 读 HDR color 和 bloom feature，写 swapchain | `hdr.color`、`feature.bloom` -> `swapchain.color` |
| `DebugOverlay` | 追加 debug overlay 线段 | `scene.camera` -> `debug.overlay` |

pass 节点同时声明 `stage`、`dispatch`、`shader`、`input`、`sources`、`targets`、`rendering.attachments` 和 `renderState`。这些字段会进入 RenderPathNode signature，成为 pipeline identity 的一部分。

当前 Forward 默认 graph 已经把 bloom 作为一个显式 fullscreen pass 放在
`assets/render_paths/forward_main.render-path.yaml` 中。是否产生 bloom contribution
由 `feature.bloom` 的参数控制，而不是切换到另一份 `forward_bloom` graph。

## 一个 RenderInput 只属于一个 pass

`RenderWorkCompiler::buildInputs()` 按单个 `FramePass` 生成输入：

| 字段 | 当前含义 |
|---|---|
| `RenderInputKind` | `Draw` 或 `Compute` |
| `RenderDrawInput.source` | `SceneRenderable` 或 `FullscreenTriangle` |
| `object` / `mesh` / `material` handle | scene renderable draw 的 typed handle |
| `drawCommands` | draw index、index count、instance count 等 draw 数据 |
| `debugOnly` | debug overlay 类输入的可见性标记 |
| `RenderComputeInput.groupCountX/Y/Z` | compute dispatch 的 group count |
| `readbackResource` | offline compute 当前用 `OutputPixels` 指定 readback buffer |

所以多 pass 不是“一个 draw 内部循环多个 pass”，而是“同一个 renderable 在不同 pass 下产生不同 input”。Fullscreen/post pass 没有 mesh draw；offline compute pass 没有 graphics vertex/index 输入。

## SceneNode 先提供可验证事实

对象进入 render input 前需要先被验证成 pass 可消费的事实：

| 校验 | 为什么在这里做 |
|---|---|
| graph pass `input.material.type` 是否命中 BSDF type | 防止 surface material 被错误 pass 消费 |
| graph pass `input.object.renderClass` 是否命中 object render class | 防止 debug/object pass 消费错误对象 |
| shader payload 是否真实存在 | 不能用 metadata-only 或 placeholder payload 满足渲染依赖 |
| material source variant 是否已解析 | specialized shader variant 必须是 live typed payload |
| mesh vertex layout / topology 是否满足 input geometry contract | pipeline vertex input 和 topology 必须可构建 |
| system-owned binding 类型是否正确 | `CameraUBO`、scene light、bones 等名字有固定 ABI |
| material/feature-owned resource 是否齐全 | shader 需要的 envelope 或 feature 参数必须真实注册 |

校验成功后，节点缓存 pass 数据；`RenderWorkCompiler` 只消费这些已经通过验证的事实和 scene upload view。

## RenderWorkCompiler 是 per-pass 的

`FrameGraph::compile()` 只负责 pass 资源依赖和稳定顺序。编译后，backend 或 integrator 会按 `CompiledPass` 找回对应 `FramePass`，再调用 `RenderWorkCompiler`：

```text
FramePass(name = Shadow)
  -> RenderWorkCompiler::buildInputs(...)
  -> RenderWorkCompiler::prepare(...)

FramePass(name = Forward)
  -> RenderWorkCompiler::buildInputs(...)
  -> RenderWorkCompiler::prepare(...)
```

`RenderWorkCompiler::buildInputs(...)` 在 realtime domain 下会做筛选：

| 条件 | 当前含义 |
|---|---|
| `renderable->supportsPass(pass)` | active graph 的 pass input contract 命中，并且节点有对应 validated cache |
| visibility mask 命中当前 target camera | 当前 target 的相机能看见该对象 |
| `getValidatedPassData(pass)` 非空 | 节点已通过 pass-level validation |

`prepare(...)` 再把输入提升成 pipeline-facing 描述：`PipelineKey`、`PipelineBuildDesc`、shader URI、shader variant key、reflection identity、binding plan、resource dependencies、diagnostics 和 stats 都在 `RenderInputDesc` 中收口。

## FrameGraph 只负责编译资源依赖

当前 `FrameGraph` 已经不是只按插入顺序执行的 list。它的核心任务是：

```text
FrameGraph::compile(registry)
  -> 校验 graph resource source/target
  -> 非 imported source 连接到 producer
  -> 按资源依赖 DAG 排序
  -> 用 phase / stableOrder / 插入顺序做稳定兜底
```

它仍然不持有 backend attachment，也不做 attachment aliasing；backend 执行层负责把 compiled pass 转成具体 framebuffer/render pass/dynamic rendering 状态。

## Fullscreen pass 写入真实 swapchain 格式

当前 Forward graph 中，`Bloom` fullscreen pass 写逻辑目标 `swapchain.color`。
真正执行前，Vulkan backend 会先从设备选择出的 surface format 推导当前
swapchain target，并保留 `BGRA8Srgb` / `RGBA8Srgb` 这类 sRGB 信息。随后
`initScene()` 会把写 swapchain 的 fullscreen/debug pass target 与 attachment
contract 同步成真实 swapchain format。

这一步很重要：如果 surface image view 是 `VK_FORMAT_B8G8R8A8_SRGB`，而
pipeline contract 仍然按 `BGRA8` / UNORM 创建，就会把颜色编码责任说不清。
当前约定是：

| Swapchain target | Fullscreen shader 输出 | 谁做 sRGB encode |
|---|---|---|
| `BGRA8Srgb` / `RGBA8Srgb` | linear mapped color | Vulkan sRGB attachment |
| `BGRA8` / `RGBA8` | shader 手动 gamma 后的 sRGB-like color | shader |

Forward surface shader 仍通过 `feature.toneMapping` 和 `feature.forwardPass`
决定 tone mapping / gamma flow；`Bloom` fullscreen pass 当前读取 `hdr.color` 和
`feature.bloom`，用 `render_paths/Bloom/blit.frag` 输出到 swapchain。

## Debug dump 有两个观察面

`render debug dump <attachment> [path]` 当前只允许 dump frame graph
attachment，例如 `hdr.color`。这张图观察的是中间 HDR buffer：它还没有经过
fullscreen bloom blit 或 swapchain sRGB 写入。

同一个命令会同时安排一张 paired screen dump，路径为目标文件名加
`-screen.png`。这张图来自最终 swapchain copy，并且触发时会跳过 GUI frame，
所以它才接近“live 最终画面 minus UI”。旧的 `Forward` / `DebugOverlay`
pass dump 会临时 offscreen 重渲，已经不再是合法调试路径。

## PipelineBuildDesc 从 RenderInputDesc 提供

当 backend 需要预构建 pipeline 时，它从 `RenderWorkCompiler::prepare(...)` 的 accepted desc 里收集 `pipelineBuildDesc`，再按 `PipelineKey` 去重：

```text
RenderWorkCompiler::prepare(pass, context, inputs)
  -> RenderInputDesc(status = Accepted, pipelineBuildDesc = ...)
  -> PipelineCache::preload(descs)
```

`PipelineBuildDesc` 现在由 compiler/backend 过渡层明确构造。它携带 shader stages、reflection bindings、vertex layout、render state、topology、target、attachments 和 key。pipeline 构建输入不是直接从 `.material` 来的，而是从已经通过 scene/graph validation 组装好的 input desc 派生。

## 我们已经学会了什么

多 pass 来自 RenderPathGraph，而不是 SurfaceMaterial。Scene/graph validation 先把 object + material + pass contract 校验成可消费事实，FrameGraph 负责编译 pass 资源顺序，`RenderWorkCompiler` 再按 pass 生成 `RenderInput` 和 `RenderInputDesc`。backend 最终消费的单位始终是单 pass input。

## 下一步

- [什么是 Pipeline](what-is-pipeline.md)
- [Realtime 与 Offline：同一条输入准备流水线](../../concepts-design/rendering-pipeline/realtime-offline-shared-flow.md)
