# REQ-074-i: OfflineRT Smoke And Package Readiness Gate

> 2026-06-14 再校准：OfflineRT old bridge deletion 已并入 `REQ-074-h`。本 REQ 不再负责删除 `OfflineShaderProvider`、`offlineShader`、`ensureOfflineRayTracePass()`、`createOfflineRenderFrameGraph()` 或 `Pass_OfflineRayTrace` 默认分支；这些默认路径 hard cut 必须在 `REQ-074-h` 完成。本 REQ 只作为 hard cut 后的 Helmet/BMW offline smoke 和 package readiness gate。

## 背景

`REQ-074-h` 将 OfflineRT 默认入口硬切到 RenderPathGraph / FrameGraph / SceneResourceTable / offline compute compiler 路径，并删除旧硬编码 bridge。

本 REQ 接在 `REQ-074-h` 后面，回答一个更具体的问题：旧 bridge 删除后，复杂资产和 package 前置状态是否仍然可靠。它不是第二次迁移，也不是保留兼容层的理由。

## 承接与边界

| 来源 | 本 REQ 承接内容 | 当前边界 |
|---|---|---|
| `REQ-074-h` | OfflineRT graph-driven compute path、old bridge hard cut、small scene default path | 本 REQ 只做复杂场景 smoke 和 readiness gate |
| `REQ-073-e` | `RenderDrawInput` / compiler model | 本 REQ 不改 compiler 模型 |
| `REQ-073-j` | transparent / BMW realtime follow-up | 本 REQ 只验证 OfflineRT，不扩展 realtime transparent path |
| `REQ-074-b` | package canonical state readiness | 本 REQ 给 package 阶段提供 offline graph path readiness 证据 |

## 目标

1. 用 Helmet 和 BMW M6 证明 hard cut 后的 OfflineRT 默认路径可诊断、可运行或明确拒绝 unsupported feature。
2. 证明 OfflineRT 默认路径只通过 RenderPathGraph、SceneResourceTable、FrameGraph、offline `RenderDrawInput` 派生 / compute compiler 和 shared pipeline path 工作。
3. 收窄 legacy token audit allowlist，防止旧 bridge 在 package 前回流。
4. 给 `REQ-074-b` 提供 package readiness 证据。

## 非目标

- 不删除旧 bridge；删除职责属于 `REQ-074-h`。
- 不添加新的 offline integrator 算法。
- 不实现 package 文件格式或 pipeline cache blob。
- 不做 offline/realtime 图像等价阈值比较。

## 需求

### R1: Hard Cut Audit Gate

运行 legacy bridge audit，确认 production 默认路径和 ordinary positive tests 中不再正向引用：

- `OfflineShaderProvider`
- `OfflineRenderJob::offlineShader` / `offlineShader`
- `ensureOfflineRayTracePass`
- `createOfflineRenderFrameGraph`
- `Pass_OfflineRayTrace` 默认 work branch
- `OfflinePrimaryRayCompute`
- `techniques/OfflineRT`

这些 token 只允许出现在：

- 历史需求文档；
- named negative audit；
- legacy rejection diagnostic；
- 注释中明确说明“旧路径已拒绝 / 已删除”的位置。

### R2: Helmet OfflineRT Smoke

Helmet OfflineRT direct render SHALL：

- 使用默认 `assets/render_paths/offline_ray_tracer.render-path.yaml`。
- 使用 `render_paths/OfflineRT/...` shader URI。
- 从 SceneResourceTable upload view 构建 offline descriptor resources。
- 通过 offline `RenderDrawInput` 派生 / compute compiler 生成 compute dispatch。
- 通过 shared resource manager / pipeline cache 创建或复用 compute pipeline。
- 输出非全黑图像，或输出明确 diagnostic 说明缺失的 supported feature。

不得回退到旧 provider、旧 material pass injection 或 hardcoded FrameGraph。

### R3: BMW M6 OfflineRT Smoke

BMW M6 OfflineRT direct render SHALL：

- 走同一条 RenderPathGraph / SceneResourceTable / offline compute compiler 路径。
- 输出非全黑图像；或
- 对暂不支持的 PBRT / material / texture feature 输出明确 diagnostic。

diagnostic 至少包含：

- graph asset；
- pass id；
- shader URI；
- unsupported material/source identity；
- source-local material storage / materialRef 解析状态；
- texture array 解析状态。

### R4: Package Readiness Evidence

本 REQ 完成时 SHALL 产出 package 前置证据：

- OfflineRT graph asset 是默认入口。
- scene canonical state 是 SceneResourceTable。
- shader URI、pass、source/target、compute dispatch 来自 RenderPathGraph。
- material truth 不来自 material-local offline pass。
- output/readback resource 来自 graph compute block。
- legacy bridge audit 无 production 默认路径 hit。

## 测试

### T1: Legacy Offline Bridge Audit

运行 rg/audit，断言旧 offline bridge token 只出现在 named negative audit、历史文档或已标注 legacy rejection diagnostic 中。

### T2: Helmet Offline Smoke

运行 Helmet OfflineRT direct render，断言输出和 diagnostics 满足 R2。

### T3: BMW M6 Offline Smoke

运行 BMW M6 OfflineRT direct render，断言输出或 unsupported diagnostic 满足 R3。

### T4: Package Readiness Report

生成 package readiness report / test log，列出 R4 的证据。

## 修改范围

- offline smoke fixtures and integration tests
- legacy bridge audit tests
- `src/tools/lxe_offline_render/`
- `src/backend/vulkan/offline/` diagnostics
- package readiness notes/tests touched by OfflineRT smoke

## 边界与约束

- 不恢复任何已由 `REQ-074-h` 删除的旧 default path。
- 不用 path/name substring 选择 strictness；strictness 来自 validation profile/property。
- 不把 `techniques/OfflineRT` 作为 resolver fallback。
- 不让 `VulkanRenderer` facade、`VulkanRealtimeRenderer` 或 editor realtime path 增加 offline branch。

## 依赖

- `REQ-074-h`: OfflineRT RenderPathGraph compute path and old bridge hard cut。
- `REQ-073-e`: `RenderDrawInput` / compiler model。
- `REQ-073-j`: transparent BMW material path and smoke。

## 后续工作

- `REQ-074-a`: Texture compression pipeline with BC7。
- `REQ-074-b`: package canonical state readiness gate。

## 实施状态

未实施。
