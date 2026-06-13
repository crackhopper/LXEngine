# REQ-073-f: OfflineRT RenderPathGraph Compute Path

> 2026-06-13 后移：本 REQ 原为 `REQ-073-d`。`REQ-073-b` 已拆成 material storage foundation、shader variant、indirect batching 和 realtime hard cut 四段，因此 OfflineRT RenderPathGraph compute path 后移为 `REQ-073-f`，位于 `REQ-073-e` realtime clean gate 之后、`REQ-074-a` 之前。目标是把 OfflineRT 从代码硬编码 pass/shader 迁移到 RenderPathGraph 配置路径。

## 背景

当前代码已经有几块基础：

- `RenderPath` enum 和 parser 已支持 `OfflineRT`。
- RenderPass parser 已支持 `stage: compute` 和 `dispatch: compute`。
- offline Vulkan executor 已经通过 `resourceManager.getOrCreatePipeline(item)` 创建 compute pipeline，和 realtime 共享后端 pipeline cache/resource manager 入口。
- offline scene storage 已经从 `SceneResourceTable::buildUploadView()` 生成 descriptor resources。

但当前 offline 默认入口仍然有几处硬编码：

- `assets/render_paths/` 没有 `offline_ray_tracer.render-path.yaml`。
- offline integrator 直接调用 `createOfflineRenderFrameGraph(output)` 生成单 pass FrameGraph。
- `lxe_offline_render` / `OfflineSceneLoader` 通过 `OfflineShaderProvider` 注入 `techniques/OfflineRT/offline_pbr_direct_ray`。
- `RenderWorkQueue` 通过 `Pass_OfflineRayTrace` 名字创建 compute item。
- RenderPass parser 虽然认识 compute pass，但仍无条件要求 `renderState`，这会把 compute pass 写成伪 raster pass。

因此，本 REQ 的核心不是重新实现 offline renderer，而是把 offline 的 shader/pass/work-item 来源改为 RenderPathGraph，让 offline 和 realtime 共享同一条 scene/resource/graph/pipeline 组织方式。

## 目标

1. 增加 `OfflineRT` 的 RenderPathGraph asset。
2. 让 RenderPass contract 按 stage/dispatch 区分 raster 和 compute pass。
3. 让 offline integrator 从 RenderPathGraph 构建 FrameGraph。
4. 让 offline compute work item 从 FramePass / pass contract 生成，而不是从 pass 名字硬编码生成。
5. 保留现有 compute pipeline backend 创建路径，不引入第二套 pipeline 系统。
6. 让 offline 默认路径使用 realtime 同源 scene parsing、SceneResourceTable 和 resource dependency validation。

## 非目标

- 不修改 `REQ-073-a` 的 Material v3 字段、材质 type 或 shader common 设计。
- 不实现新的 path tracing BSDF、采样器、降噪或多 bounce 算法。
- 不实现 package 文件格式；由 `REQ-074-c` 处理。
- 不实现 Vulkan pipeline cache blob 持久化；由 `REQ-074-e` 处理。
- 不做 offline/realtime 图像等价阈值验收；由 `REQ-075-a` 处理。
- 不删除所有旧 offline bridge；最终 hard cut 由 `REQ-073-g` 完成。

## 需求

### R1: OfflineRT RenderPathGraph Asset

新增 `assets/render_paths/offline_ray_tracer.render-path.yaml`。

最低结构：

```yaml
schema: lxe.render-path-graph.v1
name: OfflineRayTracer
renderPath: OfflineRT

passes:
  - id: OfflineRayTrace
    stage: compute
    dispatch: compute
    shader: render_paths/OfflineRT/offline_pbr_direct_ray
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

- shader URI 必须使用 `render_paths/...`，不能使用 `techniques/...`。
- pass identity 仍然是 `OfflineRayTrace`，但它只是配置中的 pass id，不再是代码硬编码分支的触发条件。
- sources/targets 必须进入 RenderPathGraph / FrameGraph resource validation，不能只作为注释存在。
- `offline.profile` 表示 render profile / output profile 对 compute dispatch 和 shader params 的输入依赖。

### R2: Stage-Specific RenderPass Contract

RenderPass parser 和 `RenderPassNode` SHALL 按 stage/dispatch 校验字段。

规则：

| pass 类型 | 必要字段 | 禁止或不要求字段 |
|---|---|---|
| raster + draw | `renderState`、geometry/material/camera sources、targets | `compute` |
| raster + fullscreen | `renderState`、sampled sources、targets | `compute` |
| compute + compute | `compute`、sources、targets | raster-only `renderState` 不再必需 |

compute block 最低字段：

| 字段 | 说明 |
|---|---|
| `dispatchFrom` | dispatch 尺寸来源，首版支持 `output.resolution` |
| `localSize` | shader workgroup size，例如 `[8, 8, 1]` |
| `readback` | offline 输出 readback resource 名称，例如 `OutputPixels` |

约束：

- unknown field 必须 fail-fast。
- parser allowlist 中出现的字段必须进入 `RenderPassNode` 或 parser-local DTO，再被 FrameGraph / work-item build 消费。
- compute pass 不得为了通过 parser 而填写无意义 raster `renderState`。
- raster pass 不得携带 compute block。

### R3: FrameGraph From OfflineRT RenderPathGraph

OfflineRT SHALL 使用 `buildFrameGraphFromRenderPathGraph()` 或同一套 RenderPathGraph -> FrameGraph 构建逻辑。

要求：

- `FramePass` 能保存 compute pass metadata。
- `FrameGraph::compile()` 能校验 `offline.output`、`scene.bvh`、`offline.profile` 等 offline resources。
- `FrameGraph::build()` 仍只负责逐 pass 调用 `RenderWorkQueue`，不写 offline 特例。
- `createOfflineRenderFrameGraph(output)` 只能作为过渡兼容入口，默认路径不得继续调用。

### R4: Offline Render Job Uses RenderPath Reference

`OfflineRenderJob` SHALL 以 RenderPathGraph / render profile 作为渲染结构输入，而不是以 `offlineShader` 作为主要入口。

要求：

- CLI/profile 可以显式选择 `assets/render_paths/offline_ray_tracer.render-path.yaml`。
- 未指定时使用默认 OfflineRT RenderPathGraph asset。
- output width/height、sample count、integrator 参数等仍来自 offline render profile。
- shader 由 RenderPathGraph pass 的 shader URI 解析，不由 `OfflineShaderProvider` 注入。

### R5: Config-Driven Offline Compute Work Item

`RenderWorkQueue` SHALL 根据 `FramePass.stage == Compute` 和 `FramePass.dispatch == Compute` 生成 offline compute work item。

要求：

- 不再通过 `pass == Pass_OfflineRayTrace` 决定是否创建 work item。
- shaderInfo 来自 FramePass / resolved RenderPathGraph shader payload。
- compute group count 来自 compute block 的 `dispatchFrom` 和 `localSize`。
- descriptor resources 来自 pass sources / SceneResourceTable upload view / offline storage resources。
- `objectSignature`、`materialSignature` 和 `PipelineKey` 来自 compute pass 的结构事实，例如 shader URI、compute storage layout、SceneResourceTable upload layout、output target signature、offline profile variant；不能继续硬编码成临时字符串。

### R6: Shared Scene Parsing And SceneResourceTable

offline SHALL 复用 realtime 的 scene parsing 和 SceneResourceTable canonical data。

要求：

- `OfflineSceneLoader` 只能作为 offline profile / output path 包装层，不能维护另一套 scene resource truth。
- material、mesh、texture、camera、light、RenderPathGraph dependencies 都进入 SceneResourceTable。
- `buildOfflineSceneStorageResources(job)` 首版可以保留，但它必须是 `SceneResourceTableUploadView` -> offline descriptor resources 的适配层。
- 不允许通过 material template 注入 `OfflineRayTrace` pass 来证明材质支持 offline；材质结构来自 `REQ-073-a`，pass 支持来自 RenderPathGraph 和 shader variant/resource contract。

### R7: Pipeline Creation Reuse

OfflineRT compute pipeline SHALL 继续复用 backend pipeline 创建和 cache 入口。

要求：

- `PipelineBuildDesc::fromRenderWorkItem()` 能表达 compute dispatch pipeline。
- `FrameGraph::collectAllPipelineBuildDescs()` 能收集 OfflineRT compute pipeline desc。
- Vulkan offline executor 继续通过 `resourceManager.getOrCreatePipeline(item)` 获取 pipeline。
- 不引入 `OfflinePipelineFactory`、`OfflineGraph` 或第二套 public graph/contract 系统。

### R8: Diagnostics

OfflineRT 默认路径 SHALL 输出可审计 diagnostics：

- 使用的 RenderPathGraph asset。
- compute pass id、shader URI、dispatch group count、local size。
- SceneResourceTable upload view resource counts。
- offline descriptor resource list。
- pipeline key / pipeline preload count。
- readback resource 名称。

如果缺少 shader、scene.bvh、offline.output、profile 或 required source，必须 fail-fast。

## 测试

### T1: OfflineRT Graph Asset Parse

解析 `assets/render_paths/offline_ray_tracer.render-path.yaml`，断言：

- `renderPath == OfflineRT`。
- 存在一个 `stage=compute` / `dispatch=compute` pass。
- shader URI 为 `render_paths/OfflineRT/offline_pbr_direct_ray`。
- compute block 被解析并保存。

### T2: Stage-Specific Contract Negative Tests

新增负向测试：

- compute pass 缺少 compute block 失败。
- compute pass 使用 legacy/unknown field 失败。
- raster pass 携带 compute block 失败。
- compute pass 不再因为缺少 `renderState` 失败。
- `techniques/OfflineRT/...` shader URI 在 migrated validation profile 下失败。

### T3: Offline FrameGraph Build

从 OfflineRT RenderPathGraph 构建 FrameGraph，断言：

- FrameGraph pass 数和 pass identity 来自 graph asset。
- reads/writes 来自 sources/targets。
- compute metadata 进入 FramePass。
- compile 阶段能识别缺失 source/target resource。

### T4: Config-Driven Offline Work Item

使用小场景构建 offline FrameGraph，断言：

- RenderWorkQueue 生成 `RenderWorkKind::ComputeDispatch`。
- work item shader 来自 graph pass。
- group count 来自 output resolution 和 local size。
- descriptor resources 来自 SceneResourceTable upload view。
- pipeline key 不依赖 hardcoded `OfflinePrimaryRayCompute` 字符串。

### T5: Pipeline Preload

offline render graph collect pipeline descs，断言 compute pipeline desc 可以被 Vulkan resource manager preload，并由 executor 的 `getOrCreatePipeline(item)` 复用。

### T6: Default Offline CLI Path

`lxe_offline_render` 默认读取 OfflineRT RenderPathGraph asset 并完成小场景 direct render。此测试允许旧 bridge 暂时存在，但默认路径必须打印 RenderPathGraph diagnostics。

## 修改范围

- `assets/render_paths/offline_ray_tracer.render-path.yaml`
- `assets/shaders/glsl/render_paths/OfflineRT/`
- `src/core/asset/render_effect.hpp`
- `src/infra/resource_parsers/render_pass_node_parser.*`
- `src/infra/resource_parsers/render_path_graph_resource_parser.*`
- `src/core/frame_graph/frame_graph*`
- `src/core/frame_graph/render_queue.*`
- `src/core/offline/offline_render_job.hpp`
- `src/infra/offline/offline_scene_loader.*`
- `src/backend/vulkan/offline/software_compute_offline_integrator.*`
- offline render CLI and integration tests

## 边界与约束

- 不调整 `REQ-073-a`。
- 不把 OfflineRT 做成 material-local pass。
- 不让 `techniques/...` 成为新的默认路径。
- 不接受未消费的 parser allowlist 字段。
- 不为 offline 引入第二套 public graph 或 pipeline build 系统。
- 任何过渡 bridge 必须在代码或 diagnostics 中标记，并交给 `REQ-073-g` 删除。

## 依赖

- `REQ-073-c`: RenderPath terminology 和 shader URI migration 基础。
- `REQ-073-e`: realtime material path hard cut and smoke，保证 OfflineRT 开始前默认 material/render path 已干净。
- `REQ-072`: RenderPathGraph / SceneResourceTable closure audit 基础。

## 后续工作

- `REQ-073-g`: OfflineRT config hard cut and smoke。
- `REQ-074-a`: Texture compression pipeline with BC7。

## 实施状态

未实施。
