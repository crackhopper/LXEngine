# REQ-074-g: Post-package Hard Cut And Cleanup

> 2026-06-16 校准：本 REQ 原为 `REQ-073-g`，因 `REQ-073` 渲染闭环继续插入 environment IBL bake、reflection probe 和 Forward 收敛工作，现保留在 `REQ-074-g`。本 REQ 只清理 package / GPU cache 实现期间新增或保留的临时 bridge；transparent/BMW realtime hard cut 和 OfflineRT 默认入口 hard cut 分别由 `REQ-073-j`、`REQ-074-h`、`REQ-074-i` 处理。

## 背景

`REQ-074-b` 在 package 前复核 canonical data，`REQ-074-c/d/e/f` 继续实现 package 文件、restore、GPU cache 和 BMW M6 加载性能验收。package 和 GPU cache 完成后，仍可能留下为了迁移保留的 package restore bridge、cache fallback、debug fixture 或临时 adapter。进入后续 realtime / OfflineRT hard cut 与 offline/realtime equivalence 前，需要先把 package/cache 自己引入的临时路径清理掉。

## 目标

1. 删除 package / GPU cache 实现期间保留的临时 bridge。
2. 禁止 default validation 走旧 source parse fallback、legacy descriptor 或 non-package restore shortcuts。
3. 收窄 legacy token audit allowlist。
4. 为后续 `REQ-073-j/c/d/f` 提供不含 package/cache shortcut 的默认路径。

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

Package/cache 实现不得用 old material-local technique、`MaterialUBO`、per-material descriptor、silent non-bindless fallback、metadata-only package resource 或 cache shortcut 满足这条路径。更宽的 transparent/BMW 和 OfflineRT 默认入口 hard cut 继续由 `REQ-073-j/c/d` 处理。

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
- 不 implement image equivalence thresholds; `REQ-075-b` handles that。

## 依赖

- `REQ-074-f`: BMW M6 package load performance comparison。

## 后续工作

- `REQ-073-j`: transparent / BMW realtime material path and smoke。
- `REQ-074-h`: OfflineRT RenderPathGraph compute path。
- `REQ-074-i`: OfflineRT smoke and package readiness gate。
- `REQ-075-b`: offline/realtime render equivalence on new architecture。

## 实施状态

未实施。
