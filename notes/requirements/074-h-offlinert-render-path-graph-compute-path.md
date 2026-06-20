# REQ-074-h: OfflineRT RenderPathGraph Compute Path

> 2026-06-20 校准：当前实现已按 Superpowers spec
> `docs/superpowers/specs/2026-06-20-074-h-offlinert-framegraph-executor-hard-cut-design.md`
> 硬切到 output profile -> RenderPathGraph -> FrameGraphExecutor。旧
> Offline job / file-local graph / offline shader side channel 已不再作为
> production 路径；本文保留为 active 需求记录，后续只接受围绕 graph schema、
> render feature、BVH/ray program table 和 smoke gate 的补充。

## 背景

OfflineRT 已从“代码里拼一个离线 compute pass”推进到“OutputProfile 指向 RenderPathGraph asset”。当前完成态主线是：

```text
scene.outputProfiles.<profile>.renderPathGraph
  -> RenderPathGraph asset
  -> FrameGraph compile
  -> RenderWorkCompiler
  -> prepared pass work + RenderInputDesc
  -> Vulkan FrameGraphExecutor
  -> FrameGraphExecutionPayload
  -> OfflineImageWriter
```

剩余问题不在底层 compiler 单轨，而在 OfflineRT 的后续能力：

- 完善 graph schema 对 compute pass、readback、runtime values 和 output extents 的表达。
- 扩展 render feature 的 BVH derived resource、ray program table 和 hardware RT pipeline extra。
- 扩展 material scheme 的 hit shader URI 与 hit shader ABI。
- 用 smoke/golden 证明 Helmet/BMW/package 场景不回流旧路径。

历史说明：旧 queue/item/offline pass-name 模型已由 `REQ-073-e2` hard cut 删除，不能作为本 REQ 的实现目标或未来扩展点。

## 承接与边界

| 来源 | 本 REQ 承接内容 | 当前边界 |
|---|---|---|
| `REQ-073-a` | Offline PBR direct shader 已能使用 Material Accessor / BSDF ABI | 本 REQ 不改 Material v3 字段、BSDF 参数或采样算法 |
| `REQ-073-b` | `SceneResourceTableUploadView` 已包含 offline 可用的 material/object/mesh/texture 数据 | 本 REQ 只消费 upload view；不重新定义 resource table |
| `REQ-073-c` | final material source variant / RenderPathNode pipeline identity 已建立 | OfflineRT shader 也应从 graph pass URI 和 final reflection 进入 pipeline input |
| `REQ-073-d` | realtime 已迁到 `render_paths/...` URI | OfflineRT shader URI 也要迁到 graph-driven `render_paths/OfflineRT/...` |
| `REQ-073-e2` | `FramePass` / `RenderWorkCompiler` / `RenderInput` / `RenderInputDesc` 单轨模型 | OfflineRT compute 必须复用该模型，不新增 public compiler/queue 系统 |
| `REQ-074-i` | hard cut 后的 Helmet/BMW offline smoke 和 package readiness gate | 本 REQ 负责默认 OfflineRT graph path 的结构切换；074-i 负责复杂场景 smoke |

## 目标

1. 新增 OfflineRT RenderPathGraph asset，作为 pass / shader / source / target / compute dispatch 的结构入口。
2. 扩展 RenderPass contract，使 compute pass 不需要伪造 raster-only 字段，并把 `compute` block 保存到 graph/pass metadata。
3. 扩展 shared resource vocabulary，让 OfflineRT sources/targets 通过同一套 graph vocabulary gate 和 shader reflection validation。
4. 让 offline renderer 从 OutputProfile 的 RenderPathGraph 构建 FrameGraph，并输出 graph path diagnostics。
5. 让 OfflineRT compute work 从 compiled `FramePass`、compute metadata、shader reflection、`RenderComputeInput` 和 offline domain payload 生成。
6. 保持旧 OfflineRT bridge 删除状态：不得恢复 offline shader side channel、file-local graph builder 或 offline-only executor。
7. pipeline preload / lookup 的正向输入来自 `RenderInputDesc[].pipelineBuildDesc`，不是旧 queue-derived preload 或旧 work item。

## 非目标

- 不实现新的 path tracing BSDF、采样器、降噪、多 bounce 或 MIS。
- 不实现 package 文件格式；由 `REQ-074-c` 处理。
- 不实现 Vulkan pipeline cache blob 持久化；由 `REQ-074-e` 处理。
- 不做 offline/realtime 图像等价阈值验收；由 `REQ-075-b` 处理。
- 不推进 `REQ-073-j` 的 transparent/BMW realtime path。
- 不新增第二套 public graph / contract / compiler 系统。

## 需求

### R1: OfflineRT RenderPathGraph Asset

当前默认 graph asset 是 `assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml`。

最低结构：

```yaml
schema: lxe.render-path-graph.v1
name: OfflineRayTracer
renderPath: OfflineRT

passes:
  - id: OfflineCompute
    stage: compute
    dispatch: compute
    shader: render_paths/OfflineRT/standard_pbr_primary_ray
    sources:
      - scene.camera
      - scene.geometry
      - scene.materials
      - scene.textures
      - scene.lights
      - scene.bvh
      - offline.profile
    targets:
      - offline.output
    compute:
      dispatchFrom: output.resolution
      localSize: [8, 8, 1]
      readback: OutputPixels
```

要求：

- shader URI 使用 `render_paths/OfflineRT/...`。
- pass id 只是 graph identity，不能成为代码创建 compute work 的 special-case trigger。
- sources / targets 必须进入 RenderPathGraph parser、FrameGraph read/write、`GraphResourceRegistry` vocabulary gate 和 shader reflection resource contract validation。
- `offline.profile` 表示 render profile / output profile 对 dispatch 和 shader params 的输入依赖。
- `offline.output` 是 graph target；首版可以映射到 existing `OutputPixels` readback storage，但不能只作为注释字段。

### R2: Stage-Specific RenderPass Contract

RenderPass parser 和 `RenderPassNode` SHALL 按 `stage` / `dispatch` 校验字段。

| pass 类型 | 必要字段 | 不要求或禁止字段 |
|---|---|---|
| raster + draw | `renderState`、`rendering`、`input`、sources、targets | `compute` |
| raster + fullscreen | `renderState`、`rendering`、`input`、sources、targets | `geometry`、`compute` |
| compute + compute | `compute`、sources、targets | raster-only `renderState`、`rendering`、`geometry` 不再必需 |

compute block 最低字段：

| 字段 | 说明 |
|---|---|
| `dispatchFrom` | dispatch 尺寸来源；首版支持 `output.resolution` |
| `localSize` | shader workgroup size，例如 `[8, 8, 1]` |
| `readback` | offline 输出 readback resource 名称，例如 `OutputPixels` |

unknown field 必须 fail-fast。parser 接受的字段必须进入 `RenderPassNode` / `FramePass` 并被 FrameGraph、compiler 或 executor 消费。

### R3: Graph Resource Vocabulary And Reflection Validation

OfflineRT graph 使用的资源名必须由 RenderPathGraph pass 显式声明，并通过现有 shader reflection 路径校验 binding contract。本 REQ 不新增 OfflineRT-specific registry，也不引入第二套 public validation 入口。

最低资源：

| 资源 | 类型 | 用途 |
|---|---|---|
| `scene.camera` | imported source | active camera / ray frame |
| `scene.geometry` | imported source | positions / indices / meshes / primitives |
| `scene.materials` | imported source | source-local material records / material table |
| `scene.textures` | imported source | bindless texture array |
| `scene.lights` | imported source | direct light inputs |
| `scene.bvh` | imported source 或 generated source | software BVH payload |
| `offline.profile` | imported source | output/profile/sample/seed 参数 |
| `offline.output` | graph target | output storage/readback resource |

要求：

- parser 不能接受未建模 resource name 后忽略。
- `GraphResourceRegistry::makeDefault()` 必须识别这些 resource name / category。
- registry 不表达 pass-specific “必须读取哪些 resource”；required sources / targets 来自 graph asset。
- shader resource binding 校验复用当前 RenderPathGraph shader compile / reflection path。
- `scene.bvh` 若首版仍由 upload view 现场生成，diagnostics 必须标明它是 graph source 的 derived payload。
- `offline.output` 必须映射到 executor readback resource；缺失时 fail-fast。

### R4: FrameGraph From OfflineRT RenderPathGraph

OfflineRT SHALL 使用 `buildFrameGraphFromRenderPathGraph()` 或同一套 RenderPathGraph -> FrameGraph 构建逻辑。

要求：

- `FramePass` 保存 compute pass metadata：stage、dispatch、shader URI、sources/targets、compute block、readback resource、local size、dispatch source。
- `FrameGraph::compile()` 只校验 graph-declared reads/writes 的 vocabulary、producer/consumer DAG、imported/target/write-mode 规则。
- shader resource contract 校验由 graph pass shader URI、resolved shader payload 和 shader reflection 完成。
- work compiler selection / `RenderInput` family / pipeline-facing `RenderInputDesc` 的底层规则由 `REQ-073-e2` 定义。
- graph builder 不按 legacy pass token、shader path substring 或 offline domain special case 现场改写 reads/writes/shader/dispatch。
- 不允许恢复 file-local default graph builder；完成态 default graph path 必须来自 output profile 的 graph asset。

### R5: Output Profile Uses Graph Reference

Output profile SHALL 以 RenderPathGraph 作为渲染结构输入。

要求：

- CLI/profile 可以显式选择 `assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml`。
- 未指定 `--profile` 时使用 scene 的 `defaultOutputProfile`。
- output width/height 和输出路径来自 output profile，并可由 CLI override。
- shader 由 RenderPathGraph pass 的 shader URI 解析。
- profile/output/default graph selection 属于 CLI、render profile resolver 和 renderer 编排；这些路径不得持有 shader side channel。

### R6: OfflineRT RenderComputeInput Payload

OfflineRT SHALL 作为 generic compute compiler 的 offline domain payload 接入。

要求：

- compute compiler selection 遵循 `REQ-073-e2`；本 REQ 不引入 OfflineRT-specific public compiler API。
- OfflineRT 首版作为 offline domain compute payload 生成一个 scene-wide compute dispatch，不进入 realtime raster batching。
- shaderInfo 来自 FramePass / resolved RenderPathGraph shader payload / final material source variant。
- compute group count 来自 compute block 的 `dispatchFrom` 和 `localSize`。
- descriptor resources 来自 pass sources/targets、shader reflection binding contract、`SceneResourceTableUploadView` 和 domain storage adapter；缺少任一 required source / target / binding 必须 fail-fast。
- compute compiler 输出的 `RenderInputDesc` 必须包含 readback resource 映射，例如 `offline.output` -> `OutputPixels`。
- compute pipeline signature 和 derived `PipelineKey` 来自 shader URI / final variant identity、compute storage layout、SceneResourceTable upload layout、output target signature、offline profile variant、RenderPathNode signature。

### R7: Shared Scene Parsing And SceneResourceTable

offline SHALL 复用 SceneResourceTable canonical data。

要求：

- material、mesh、texture、camera、light、RenderPathGraph dependencies 都来自 scene/resource loading 的 canonical state。
- OfflineRT descriptor resource 构建必须是 shared `SceneResourceTableUploadView` -> compute descriptor resources 的确定性 builder。
- builder 输出要可诊断：SceneResourceTable upload counts、mesh/primitive/object/material/materialRef/source-local material storage/texture counts、descriptor resource names、BVH node count、readback resource。
- 不允许通过 material template 注入 offline material-local pass 来证明材质支持 offline graph；pass 支持来自 RenderPathGraph 和 shader/resource contract。

### R8: Pipeline Creation Reuse

OfflineRT compute pipeline SHALL 继续复用 backend pipeline 创建和 cache 入口。

要求：

- `PipelineBuildDesc` collection 能表达 compute dispatch pipeline。
- pipeline preload 的完成态输入来自 `RenderInputDesc[]` 中的 `PipelineBuildDesc`。
- Vulkan offline executor 继续通过 shared resource manager / pipeline cache 获取 pipeline；正向 lookup 输入来自 compiler-produced `RenderInputDesc`。
- 不引入 `OfflinePipelineFactory`、`OfflineGraph` 或第二套 public graph / contract 系统。
- pipeline preload diagnostics 至少包含 pipeline key、shader URI、pass id、compute local size、dispatch group count。

### R9: Diagnostics And Legacy Hard Cut Accounting

OfflineRT graph path SHALL 输出可审计 diagnostics：

- RenderPathGraph asset URI。
- compute pass id、stage、dispatch、shader URI。
- dispatch source、local size、derived group count。
- SceneResourceTable upload view resource counts，包括 source-local material storage / material refs / texture array counts。
- offline descriptor resource list。
- pipeline key / pipeline preload count。
- readback resource name。
- shader URI resolver failure 的 graph asset、pass id、pass stage、shader URI、expected namespace、resolver search path。

如果缺少 shader、scene geometry/materials/textures/BVH、offline output、profile 或 required source，必须 fail-fast。

### R10: Offline / Realtime Renderer Boundary

OfflineRT graph path SHALL 保持 offline renderer 边界。

要求：

- 默认 OfflineRT path 使用 `backend::offline::VulkanOfflineRenderer` / offline integrator / offline executor。
- `gpu::Renderer`、`VulkanRealtimeRenderer`、`VulkanRenderer` facade 不新增 offline render 分支。
- headless device、readback、output writer 和 offline job 生命周期停留在 offline namespace 或显式 foundation 层。
- realtime renderer 只可共享 backend resource manager、pipeline cache、shader compiler 等 foundation；不能持有 offline profile、offline output 或 offline readback 状态。

## 测试

### T1: Current Legacy Leakage Audit

新增或强化 audit，证明旧 OfflineRT bridge 不再作为 production/default path。旧 token 如需在文档中出现，必须明确标注 historical/deleted。

### T2: OfflineRT Graph Asset Parse

解析 `assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml`，断言：

- `renderPath == OfflineRT`。
- 存在一个 `stage=compute` / `dispatch=compute` pass。
- shader URI 为 `render_paths/OfflineRT/standard_pbr_primary_ray`。
- compute block 被解析并保存。
- 不需要 raster `renderState`。

### T3: Stage-Specific Contract Negative Tests

新增负向测试：

- compute pass 缺少 `compute` block 失败。
- compute pass 使用 legacy/unknown field 失败。
- raster pass 携带 `compute` block 失败。
- compute pass 不再因为缺少 `renderState` 失败。
- legacy OfflineRT shader URI 在 migrated validation profile 下失败，diagnostic 包含 graph asset、pass id、pass stage、shader URI、expected namespace 和 resolver search path。

### T4: Unified Resource Registry And Reflection Contract

构造 OfflineRT graph 并编译 FrameGraph，断言：

- `scene.camera`、`scene.geometry`、`scene.materials`、`scene.textures`、`scene.lights`、`scene.bvh`、`offline.profile` 可作为 source 校验。
- `offline.output` 可作为 target 校验。
- 缺少或拼错 source/target 时，FrameGraph compile 输出 pass id + resource name。
- 测试使用 `GraphResourceRegistry::makeDefault()`，不得创建 OfflineRT-specific registry 来绕过 realtime 默认校验。
- required binding 缺失、graph source/target 与 shader reflection binding contract 不匹配时失败。

### T5: Output Profile Graph Selection

构造 default output profile 解析，断言：

- profile 解析默认 OfflineRT graph asset。
- shader 来自 graph pass shader URI。
- output 参数进入 target/readback mapping。
- shader side channel 不再存在于正向输入或 fixture。

### T6: Compiler And Pipeline Desc

构造 OfflineRT compiled FrameGraph + offline scene upload view，断言：

- `RenderWorkCompiler::buildInputs()` 生成 `RenderComputeInput`。
- `prepare()` 生成 accepted `RenderInputDesc`。
- desc 包含 compute dispatch groups、readback mapping、pipeline build desc、binding/resource dependency facts。
- pipeline preload 从 `RenderInputDesc[].pipelineBuildDesc` 收集。

### T7: Offline Executor Smoke

运行小分辨率 offline render，断言：

- graph asset path 出现在 diagnostics。
- compute desc 被 accepted 并提交。
- output readback resource 匹配 `offline.output`。
- 输出非空且没有 legacy fallback observation。

### T8: rg Hard Cut Audit

实现完成报告必须包含旧 token 审计；`src` / `assets` 中 legacy offline bridge token zero-hit。docs 里出现时必须在 historical/deleted 语境中。

## 修改范围

- `assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml`。
- `assets/shaders/glsl/render_paths/OfflineRT/...`。
- RenderPathGraph parser / pass contract / compute block DTO。
- `GraphResourceRegistry::makeDefault()` and shader reflection resource contract tests。
- offline job/profile graph selection。
- shared scene/resource hydration required by offline path。
- offline descriptor resource builder from `SceneResourceTableUploadView`。
- offline executor pipeline/readback path that consumes `RenderInputDesc`。
- OfflineRT graph diagnostics and smoke tests。

## 边界与约束

- Use current repo facts only；不得只改命名不删双轨。
- 不新增第二套 public graph / contract system。
- 不把 compute 塞进 `RenderDrawInput`。
- 不让 pass name、shader path substring 或 OfflineRT special case 决定 compiler。
- 不保留旧 work fallback success path。
- 不把 docs 中的旧术语继续描述为推荐路径；current fact 只能作为历史/已删除说明。

## 依赖

- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。
- `REQ-073-e2`: RenderInput compiler hard cut。

## 下游工作

- `REQ-074-i`: hard cut 后的 Helmet/BMW offline smoke 和 package readiness gate。
- `REQ-074-e`: pipeline cache serialization 应从 `RenderInputDesc` / `PipelineBuildDesc` 收集 pipeline cache metadata。

## 实施状态

部分完成。

已完成的底层事实：

- offline compute 当前已经走 file-local `OfflineCompute` pass、`FramePass`、`RenderWorkCompiler`、`RenderComputeInput` 和 `RenderInputDesc`。
- Vulkan offline pipeline/upload/execute 已消费 desc-backed pipeline facts。
- 旧 offline pass token 已从 `src` / `assets` 删除。

仍属本 REQ 的剩余工作：

- 添加默认 OfflineRT RenderPathGraph asset。
- 完整建模 compute block 和 OfflineRT resource vocabulary。
- 保持 graph pass shader URI 作为唯一正向 shader 来源。
- 保持 file-local graph builder 删除状态。
- 从 graph-driven `RenderInputDesc[].pipelineBuildDesc` 完成 OfflineRT pipeline desc / preload 事实。
