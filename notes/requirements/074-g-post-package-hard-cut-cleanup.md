# REQ-074-g: Post-package Hard Cut And Cleanup

> 2026-06-13 更新：本 REQ 在 package restore、GPU pipeline cache restore 和 BMW M6 加载性能验收完成后，再做一次架构硬切与清理。`REQ-073-e` 已完成 realtime hard cut，`REQ-073-g` 已完成 OfflineRT hard cut；本 REQ 聚焦 package / cache 实现期间新增或保留的临时 bridge，使新架构成为 offline/realtime 对比的唯一默认入口。

## 背景

`REQ-073-e`、`REQ-073-g` 和 `REQ-074-b` 在 package 前切掉会污染 canonical data 的旧路径。package 和 GPU cache 完成后，还可能留下为了迁移保留的 bridge、debug fallback、旧测试 fixture 或临时 adapter。进入 offline/realtime render equivalence 前，需要再次清理，避免对比结果被旧路径绕过。

## 目标

1. 删除 package / GPU cache 实现期间保留的临时 bridge。
2. 禁止 default validation 走旧 source parse fallback、legacy descriptor 或 non-package restore shortcuts。
3. 收窄 legacy token audit allowlist。
4. 为 `REQ-075-a` offline/realtime 对比提供唯一默认架构路径。

## 需求

### R1: Bridge Inventory

列出并处理所有临时 bridge：

- material bridge。
- render submission bridge。
- offline render path bridge。
- package restore fallback。
- GPU cache fallback。
- test-only fixture bridge。

每项必须删除、迁移到 explicit debug-only，或记录为 unsupported diagnostic。

### R2: Default Path Hard Cut

Helmet/BMW validation default path SHALL be:

```text
package or source SceneResourceTable
  -> Material v3 source-reflected material contract/storage
  -> RenderPathGraph
  -> bindless/indirect GPU upload
  -> optional compatible pipeline cache
  -> realtime/offline render entry
```

No old material-local technique, `MaterialUBO`, per-material descriptor, silent non-bindless fallback, OfflineShaderProvider or hardcoded OfflineRT FrameGraph may satisfy this path.

### R3: Audit Tightening

Legacy audit allowlist SHALL be narrowed after package completion.

Ordinary positive tests must not mention old tokens except in named negative audits.

### R4: Documentation Status

Update relevant active requirements to reflect completed / superseded / handed-off status.

## 测试

### T1: Legacy Audit

rg/audit rejects old positive fixtures and production paths.

### T2: Default Validation Path

Helmet/BMW default validation runs only through new architecture path.

### T3: Debug-only Isolation

Any remaining legacy/debug path requires explicit flag and is absent from package/equivalence tests.

## 修改范围

- legacy bridge code
- tests and fixtures
- validation profiles
- package and render docs/requirements statuses

## 边界与约束

- 不 add new package features。
- 不 tune performance。
- 不 implement image equivalence thresholds; `REQ-075-a` handles that。

## 依赖

- `REQ-074-f`: BMW M6 package load performance comparison。

## 后续工作

- `REQ-075-a`: offline/realtime render equivalence on new architecture。

## 实施状态

未实施。
