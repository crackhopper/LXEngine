# REQ-075-a: Offline Realtime Equivalence On New Architecture

> 2026-06-12 更新：本 REQ 在 Material v3、bindless/indirect、OfflineRT config hard cut、BC7、package restore、GPU pipeline cache 和 post-package cleanup 完成后，重新执行 offline 与 realtime 渲染对比。目标是验证新架构上的 Helmet/BMW 输出一致性，而不是验证旧 bridge。

## 背景

只有当材质、resource table、OfflineRT RenderPathGraph、package、GPU cache 和 legacy cleanup 都完成后，offline/realtime 对比才有意义。否则对比可能只是证明旧路径仍能渲染，不能证明新架构正确。本 REQ 的材质对比基线是 `REQ-073-a` 定义的同一 `bsdf.source`、source reflection hash 和 Material Accessor ABI，而不是旧 C++ material type 或旧 `MaterialUBO`。

## 目标

1. 使用新 architecture default path 执行 offline/realtime direct validation。
2. 覆盖 source SceneResourceTable 和 package restore 输入。
3. 覆盖 Helmet 和 BMW M6。
4. 使用 diagnostics-aware compare 定位材质、object、BRDF 或 coverage 差异。

## 需求

### R1: Validation Inputs

Each scene SHALL support:

- source SceneResourceTable path。
- package restore path。
- compatible GPU pipeline cache path if available。

### R2: Render Paths

Compare:

- Forward realtime direct。
- Deferred realtime direct if supported。
- OfflineRT direct。

Validation uses RenderPathGraph, not material-local technique or OfflineShaderProvider.

### R3: Diagnostics

Compare output SHALL include:

- color metrics。
- material id / object id。
- `bsdf.source` URI and source reflection hash。
- Material Accessor ABI input/output debug data when available。
- direct input hash or equivalent debug data。
- edge/coverage vs BRDF/input mismatch classification。

### R4: Gates

Gate SHALL fail on:

- material interior mismatch above threshold。
- BRDF mismatch above threshold。
- missing material/object coverage。
- realtime/offline 使用不同 `bsdf.source` 或 source reflection hash。
- pass shader 绕过 Material Accessor ABI。
- legacy path usage。

Edge/coverage mismatches are reported separately and can have their own threshold.

## 测试

### T1: Helmet Equivalence

Helmet source/package realtime/offline direct comparison passes.

### T2: BMW M6 Equivalence

BMW M6 source/package realtime/offline direct comparison passes or reports specific unsupported materials with diagnostics.

### T3: Legacy Path Guard

Equivalence test fails if old material/render fallback is used.

## 修改范围

- validation profile / render CLI
- compare tool
- Helmet/BMW fixtures
- offline/realtime render tests

## 边界与约束

- Does not add new material features。
- Does not add package features。
- Unsupported PBRT features must be reported explicitly, not silently approximated unless `REQ-073-a` source contract and projection rules allow it。

## 依赖

- `REQ-073-e`: OfflineRT config hard cut and smoke。
- `REQ-074-g`: post-package hard cut and cleanup。

## 实施状态

未实施。
