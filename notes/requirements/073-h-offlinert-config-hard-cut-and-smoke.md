# REQ-073-h: OfflineRT Config Hard Cut And Smoke

> 2026-06-13 顺延：本 REQ 原为 `REQ-073-g`，因 `REQ-073-c` 进一步拆出 URI migration 而顺延为 `REQ-073-h`，紧跟 `REQ-073-g`。2026-06-14 重新校准后，`REQ-073-f` 已变为 transparent/BMW follow-up；本 REQ 的 offline hard cut 依赖 `REQ-073-g` 的 compute graph path 和 `REQ-073-e` 的 node-level queue model，而不是旧版 073f 范围。它负责删除 OfflineRT 旧硬编码入口，并用 Helmet/BMW smoke 验证 offline 默认路径只通过 RenderPathGraph、SceneResourceTable、FrameGraph 和统一 pipeline 创建路径工作。

## 背景

`REQ-073-g` 会先让 OfflineRT 能通过 RenderPathGraph 配置路径运行。配置路径跑通后，仍可能残留旧代码：

- `OfflineShaderProvider`。
- `OfflineLoadedScene::offlineShader` / `OfflineRenderJob::offlineShader`。
- `ensureOfflineRayTracePass()` 对 material template 的 pass 注入。
- `createOfflineRenderFrameGraph(output)`。
- `RenderWorkQueue` 内基于 `Pass_OfflineRayTrace` 的特殊分支。
- `techniques/OfflineRT/...` shader URI 或旧 shader 目录。
- 正向测试继续证明旧 provider 能工作。

这些 bridge 如果保留到 package 阶段，会污染 canonical state：package 可能保存的是一部分 graph 配置、一部分代码注入出来的 shader/pass 状态。进入 `REQ-074` 之前，需要把 offline 默认路径硬切到配置驱动。

## 承接自 073-a / 073-b 的未完成项

| 来源 | 本 REQ 承接内容 | 为什么属于 073-h |
|---|---|---|
| `REQ-073-a` 未完成项 | OfflineRT 默认入口 hard cut 和 smoke | 073-a 的 accessor ABI 不是入口硬切；旧 provider / hardcoded frame graph 必须等 073-g graph compute path 可运行后再删除 |
| `REQ-073-b` 未完成项 | 删除 OfflineRT provider/framegraph bridge，禁止用旧 side channel 证明 material source 可渲染 | 073-b 只保证 source records 可进入 offline 相关测试；默认 CLI/integrator 入口是否干净由本 REQ 判定 |
| `REQ-073-e` / `REQ-073-g` 传递项 | Helmet/BMW offline smoke 不得回退旧 material truth、旧 shader URI 或 pass injection | package 前必须证明 realtime 和 offline 默认路径都面对同一套 canonical SceneResourceTable / RenderPathGraph 状态 |

## 目标

1. 删除 OfflineRT 默认路径中的 shader/provider/pass/framegraph 硬编码。
2. 让 offline 默认路径只能从 RenderPathGraph 创建 FrameGraph 和 compute node data。
3. 迁移 OfflineRT shader URI 到 `render_paths/...`。
4. 收窄 offline 相关 legacy token audit allowlist。
5. 用 Helmet/BMW smoke 证明 offline direct render 仍可用。

## 非目标

- 不添加新的 offline integrator 算法。
- 不实现 package 或 pipeline cache blob。
- 不做 offline/realtime 图像等价阈值比较。
- 不调整 `REQ-073-a`。

## 需求

### R1: Remove Offline Shader Injection

默认路径 SHALL 删除或隔离以下概念：

- `OfflineShaderProvider`。
- `OfflineLoadedScene::offlineShader`。
- `OfflineRenderJob::offlineShader`。
- `ensureOfflineRayTracePass()`。
- material template 中临时注入的 `OfflineRayTrace` pass。

规则：

- shader 来源只能是 RenderPathGraph pass 的 shader URI。
- 如果旧 provider API 暂时保留给历史测试，必须迁移到 named legacy rejection/audit，不能作为正向路径。
- offline scene loader 不能因为材质缺少 offline pass 就注入 pass；它只负责解析 scene/resource/profile。

### R2: Remove Hardcoded Offline FrameGraph

默认路径 SHALL 删除或隔离 `createOfflineRenderFrameGraph(output)`。

要求：

- software compute integrator 从 job/render profile 中取得 RenderPathGraph。
- FrameGraph 由 RenderPathGraph 构建。
- pass target、sources、compute dispatch 和 readback resource 全部来自 graph/pass contract。
- 缺少 OfflineRT graph asset 时 fail-fast，而不是回退到硬编码 single pass。

### R3: Remove Pass-Name Work Queue Branch

`RenderWorkQueue` SHALL 删除基于 `pass == Pass_OfflineRayTrace` 的默认分支。

要求：

- compute node data 由 `FramePass.stage`、`FramePass.dispatch` 和 compute pass metadata 决定。
- pass 名只作为 identity/diagnostic，不作为创建 compute item 的唯一条件。
- unsupported compute pass 输出明确 diagnostic。

### R4: OfflineRT Shader URI Hard Cut

OfflineRT shader SHALL 使用 `assets/shaders/glsl/render_paths/OfflineRT/`。

要求：

- `techniques/OfflineRT/...` 不能出现在默认代码路径、positive tests 或 default assets。
- shader resolver 不得对 `techniques/OfflineRT/...` 做静默 fallback。
- old URI 只能出现在 negative audit、历史需求文档或 legacy rejection diagnostic 中。

### R5: SceneResourceTable And Descriptor Clean Boundary

offline 默认路径 SHALL 只从 SceneResourceTable canonical state 和 RenderPathGraph pass contract 派生 GPU descriptor resources。

要求：

- no material-local offline pass truth。
- no private offline scene material truth。
- no shader/provider side channel。
- offline storage descriptor builder 如果保留，必须只作为 `SceneResourceTableUploadView` -> descriptor resources 的函数。
- diagnostics 必须列出 descriptor resources 与 pass sources 的对应关系。

### R6: Smoke Gate

hard cut 后 SHALL 立即运行 offline smoke。

最低覆盖：

- Helmet OfflineRT direct smoke：输出非全黑，diagnostics 证明使用 `offline_ray_tracer.render-path.yaml`。
- BMW M6 OfflineRT direct smoke：输出非全黑或输出明确 unsupported material/feature diagnostic。
- small scene package-independent smoke：证明 source SceneResourceTable path 可直接进入 OfflineRT graph。
- pipeline diagnostics：compute pipeline key、preload count、cache lookup 路径可见。

smoke 失败时不得进入 `REQ-074-a`。

### R7: Audit Tightening

新增或强化 audit：

- production 默认路径不再正向引用 `OfflineShaderProvider`。
- production 默认路径不再正向引用 `OfflineRenderJob::offlineShader`。
- production 默认路径不再正向调用 `createOfflineRenderFrameGraph`。
- production 默认路径不再正向调用 `ensureOfflineRayTracePass`。
- production/default assets 不再使用 `techniques/OfflineRT`。
- ordinary positive tests 不再把旧 provider/pass injection 当作成功路径。

## 测试

### T1: Legacy Offline Bridge Audit

运行 rg/audit，断言旧 offline bridge token 只出现在 named negative audit、历史文档或已标注 legacy diagnostics 中。

### T2: Offline CLI Uses RenderPathGraph

运行 `lxe_offline_render` 小场景，断言：

- job 中没有 `offlineShader` side channel。
- integrator 使用 RenderPathGraph 构建 FrameGraph。
- render diagnostics 输出 graph asset、pass id、shader URI 和 compute dispatch。

### T3: Legacy Provider Rejection

构造旧 provider / material pass injection fixture，断言 default validation profile 拒绝它，或只在显式 legacy-negative test 中解析为 rejection。

### T4: Helmet Offline Smoke

Helmet OfflineRT direct render 输出非全黑，并证明：

- shader URI 来自 `render_paths/OfflineRT/...`。
- descriptor resources 来自 SceneResourceTable upload view。
- pipeline 通过 `getOrCreatePipeline(item)` 获取。

### T5: BMW M6 Offline Smoke

BMW M6 OfflineRT direct render：

- 输出非全黑；或
- 对暂不支持的 PBRT/material feature 输出明确 diagnostic，且不能回退到旧 provider/pass injection。

### T6: Build And Parser Tests

运行相关 parser、frame graph、offline GPU scene、shader compiler 测试，确保删除旧 bridge 后没有 positive fixture 依赖旧术语。

## 修改范围

- `src/core/offline/offline_render_job.hpp`
- `src/core/offline/offline_render_work_graph.*`
- `src/infra/offline/offline_scene_loader.*`
- `src/tools/lxe_offline_render/main.cpp`
- `src/core/frame_graph/render_queue.*`
- `src/backend/vulkan/offline/software_compute_offline_integrator.*`
- `assets/render_paths/offline_ray_tracer.render-path.yaml`
- `assets/shaders/glsl/render_paths/OfflineRT/`
- offline integration tests and legacy audits

## 边界与约束

- 不留下两个可通过的 offline 默认入口。
- 不用 path/name substring 选择 strictness；strictness 来自 validation profile/property。
- 不把旧 `techniques/OfflineRT` 作为 resolver fallback。
- 不把 package、pipeline cache 或 equivalence 工作提前塞入本 REQ。

## 依赖

- `REQ-073-g`: OfflineRT RenderPathGraph compute path。
- `REQ-073-e`: RenderPathNode batching, diagnostics and indirect submission。
- `REQ-073-f`: transparent BMW material path and smoke。
- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。

## 后续工作

- `REQ-074-a`: Texture compression pipeline with BC7。
- `REQ-074-b`: package canonical state readiness gate。

## 实施状态

未实施。
