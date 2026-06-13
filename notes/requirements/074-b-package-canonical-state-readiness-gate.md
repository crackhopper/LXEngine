# REQ-074-b: Package Canonical State Readiness Gate

> 2026-06-13 更新：realtime 架构 clean 和 Helmet/BMW smoke gate 已前移到 `REQ-073-f`，OfflineRT config hard cut 和 smoke gate 已后移到 `REQ-073-h`。本 REQ 位于 `REQ-074-a` 之后、package 文件格式之前，只做 package 前 canonical state readiness 复核：确认 Material v3、bindless/indirect、OfflineRT config path 和 BC7 texture metadata 形成干净可序列化状态。

## 背景

package 会把当前场景的 canonical resource state 固化为二进制。`REQ-073-f` 已负责切断 realtime 旧材质、旧渲染提交、旧 descriptor fallback 和旧术语路径；`REQ-073-h` 已负责切断 OfflineRT 旧 shader/provider/pass injection；`REQ-074-a` 又新增了 texture encoding/compression metadata。因此 package 文件格式之前还需要一次轻量复核，确认 package 将要保存的是 canonical resource state，而不是派生 GPU 状态或旧 fallback 状态。

## 目标

1. 复核 `REQ-073-f` realtime hard cut 仍有效。
2. 复核 `REQ-073-h` OfflineRT config hard cut 仍有效。
3. 复核 `REQ-074-a` 的 texture encoding/compression metadata 属于 canonical resource metadata。
4. 复核 SceneResourceTable persisted state 不包含 GPU/backend 派生状态。
5. 给 package 需求提供 clean canonical data 前置条件。

## 需求

### R1: Hard Cut Still Holds

本 REQ SHALL 重新运行 `REQ-073-f` 和 `REQ-073-h` 的 legacy hard cut audit，确认 package candidate path 不触达旧 `MaterialUBO`、material-local technique、per-material descriptor fallback、non-bindless per-item draw、OfflineShaderProvider、offline material pass injection 或 hardcoded OfflineRT FrameGraph。

### R2: Canonical State Readiness Gate

新增 package-readiness audit，但不要求 `.lxpkg` 文件格式或 package writer 已存在：

- SceneResourceTable persisted state 不包含 backend pointer。
- persisted material state 不包含 shader/pass/render state。
- persisted render flow 来自 RenderPathGraph resource。
- persisted offline render flow 来自 `offline_ray_tracer.render-path.yaml` 或 equivalent OfflineRT RenderPathGraph resource。
- texture/material/object indices 可由 resource table canonical state 重建。
- texture encoding/compression metadata 可由 resource table canonical state 重建或直接持久化。
- audit 输出“可序列化 canonical state”报告，供 `REQ-074-c/d` 消费。

## 测试

### T1: Hard Cut Regression Audit

重新运行 `REQ-073-f` / `REQ-073-h` hard cut audit，证明 package candidate path 未恢复旧 material/render/offline fallback。

### T2: Canonical State Readiness

构造 Helmet/BMW canonical state report，断言 persisted state 只包含 resource table canonical state、RenderPathGraph references 和 texture encoding/compression metadata；不写 `.lxpkg`。

## 修改范围

- material parser / scene asset loader
- RenderPathGraph parser / validation diagnostics
- SceneResourceTable canonical state audit
- tests / audits
- active requirements docs terminology references where directly related

## 边界与约束

- 不定义 `.lxpkg` 文件格式；由 `REQ-074-c` 处理。
- 不实现 package restore；由 `REQ-074-d` 处理。
- 不实现 loading UI。
- 不重复 `REQ-073-f` / `REQ-073-h` 的旧路径删除；这里只做 regression/readiness audit。

## 依赖

- `REQ-073-f`: realtime material path hard cut and smoke。
- `REQ-073-h`: OfflineRT config hard cut and smoke。
- `REQ-074-a`: texture compression pipeline。

## 后续工作

- `REQ-074-c`: LxScenePackage file format。

## 实施状态

未实施。
