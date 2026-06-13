# REQ-073-f: Realtime Material Path Hard Cut And Smoke

> 2026-06-13 顺延：原 `REQ-073-e` 因 `REQ-073-c` 进一步拆出 URI migration 而顺延为本 REQ。本 REQ 是 realtime material path 的 clean gate。`REQ-073-b` 提供数据基础，`REQ-073-c` 完成 shader variant，`REQ-073-d` 完成 URI/术语硬切，`REQ-073-e` 完成 indirect batching。本文件只负责删除/隔离旧 realtime fallback，并用 Helmet/BMW smoke 证明默认路径不能再绕过 Material v3 source contract。

## 背景

在 foundation、shader variant 和 indirect batching 都落地后，系统仍可能保留这些旧入口：

- 旧共享 `MaterialUBO` / `SceneGpuMaterialRecord` 作为 PBR 参数真相。
- per-material descriptor 或 non-bindless per-item draw fallback。
- material-local technique / defaultTechnique。
- `techniques/...` shader URI 默认路径。
- debug color、默认材质、跳过 draw 等“看似能渲染”的隐藏兜底。

这些路径如果继续存在，会让 Helmet/BMW smoke 和后续 package 序列化证明的是混合状态，而不是干净 Material v3 + RenderPathGraph + bindless/indirect 默认路径。

## 承接自 073-a / 073-b 的未完成项

| 来源 | 本 REQ 承接内容 | 为什么属于 073-f |
|---|---|---|
| `REQ-073-a` T3 / T4 / T5 | shader 实际采样 factor × texture，并通过默认纹理后输出非全黑或明确诊断 | source record packing 和默认 texture slot 已在 073-a/b 建立；只有 clean realtime 默认路径能证明 shader 采样不是旧 fallback |
| `REQ-073-a` T9 | Helmet/BMW 低分辨率 realtime 非全黑 validation | 视觉 smoke 必须等 source storage、shader variant 和 indirect/bindless path 都成立后执行，否则会把旧路径误判为成功 |
| `REQ-073-b` 未完成项 | 删除旧 realtime `SceneGpuMaterialRecord` / per-material descriptor / non-bindless fallback | 073-b 为过渡保留旧字段不再作为 Material v3 真相；本 REQ 是删除或隔离旧默认成功路径的 clean gate |
| `REQ-073-b` 未完成项 | realtime smoke 使用 `render_paths/...`、bindless/indirect stats 和 source-reflected material records 证明新路径 | 这些验收必须在 073-c/d/e 后进行，否则无法判断失败来自 shader variant、URI 迁移、batch、table 还是视觉内容 |

## 目标

1. realtime 默认路径只消费 source-reflected material storage 和 bindless-ready tables。
2. 删除或隔离旧 `MaterialUBO` PBR truth、per-material descriptor、non-bindless draw fallback。
3. 删除默认路径中的 material-local technique / defaultTechnique / `techniques/...` URI。
4. 无法渲染时 fail-fast 并输出诊断，不用 debug 色、默认材质或跳过 draw 隐藏问题。
5. Helmet 和 BMW M6 realtime smoke 输出非全黑或明确 unsupported diagnostic；失败不得进入 OfflineRT / package 后续阶段。

## 非目标

- 不实现 material storage foundation；由 `REQ-073-b` 处理。
- 不实现 shader variant；由 `REQ-073-c` 处理。
- 不迁移 shader URI / RenderPath 术语；由 `REQ-073-d` 处理。
- 不实现 indirect batching；由 `REQ-073-e` 处理。
- 不处理 OfflineRT config hard cut；由 `REQ-073-h` 处理。
- 不实现 package、BC7、pipeline cache blob 或 offline/realtime 等价阈值。

## 需求

### R1: Remove Old Material Truth From Realtime Default Path

realtime 默认路径 SHALL 不再把旧共享 `MaterialUBO` / `SceneGpuMaterialRecord` 当作 PBR 参数真相。

要求：

- Forward / Deferred PBR shader 只通过 Material Accessor ABI 读取材质。
- upload path 的正向验证只读取 source-reflected material record。
- `REQ-073-b` 如果为了过渡仍保留旧 material span 或 legacy shadow index，本 REQ 必须把它从 realtime 默认成功路径中删除或隔离到 named legacy rejection/audit。
- ordinary positive tests 不得以旧 `MaterialUBO` 字段作为成功条件。

### R2: Remove Per-material Descriptor Fallback

realtime 默认路径 SHALL 删除或隔离 per-material descriptor 和 non-bindless per-item draw fallback。

规则：

- geometry pass descriptor resources 来自 global texture/sampler/material/object/draw/mesh tables。
- 缺 bindless table、缺 default texture slot、缺 source-local material index 时必须 fail-fast。
- 不允许自动回退到 per-material descriptor 后继续通过 smoke。

### R3: Remove Material-local Technique Boundary

Material v3 默认路径 SHALL 不再读取 material-local technique / defaultTechnique 作为渲染结构真相。

要求：

- pass/shader 来源来自 RenderPathGraph。
- material 只声明 `bsdf.source` 和参数。
- `techniques/...` 不能出现在 default assets、positive tests 或 migrated validation profile 的 shader URI 中。
- old technique token 只允许出现在 legacy negative test、历史需求文档或 rejection diagnostic。

### R4: Fail-fast Rendering Diagnostics

当默认路径无法渲染时 SHALL 停止并输出诊断。

禁止：

- 静默切换 debug material。
- 输出固定 debug 色伪装成功。
- 跳过不可渲染 draw 后仍报告 smoke 通过。
- 把 unsupported source 当作黑色材质继续渲染。

diagnostics 至少包含：

- scene / asset URI。
- render path / pass id。
- material source URI / signature。
- pipeline key。
- missing table / unsupported capability / invalid index / shader reflection failure 的具体原因。

### R5: Helmet Realtime Smoke

Helmet validation SHALL 使用 Material v3 source contract 默认路径完成 realtime smoke。

最低验收：

- 输出非全黑。
- diagnostics 证明使用 source-reflected material records。
- diagnostics 证明使用 `render_paths/...` shader URI。
- diagnostics 证明没有 per-material descriptor fallback。
- material source / pipeline / batch / draw stats 可见。

### R6: BMW M6 Realtime Smoke

BMW M6 validation SHALL 使用 PBRT converter 输出的 Material v3 contract material 完成 realtime smoke。

最低验收：

- 输出非全黑；或
- 对当前明确不支持的 material source / feature fail-fast 并输出 unsupported diagnostic。

无论哪种情况，都不得回退到旧 material truth、debug material、旧 shader URI 或跳过 draw 的隐藏路径。

### R7: Audit Tightening

新增或强化 audit：

- realtime default path 不读取旧 `MaterialUBO` PBR 参数。
- production default path 不创建 per-material descriptor fallback。
- default assets 不使用 `techniques/...` shader URI。
- positive tests 不依赖 material-local technique/defaultTechnique。
- smoke test 日志中 fallback / unsupported draw count 必须为 0；明确 unsupported source 的负向测试除外。

## 测试

### T1: Legacy Material Truth Audit

rg/audit 正向路径，断言旧 `MaterialUBO` / old shared material record 只出现在 legacy rejection、历史文档或明确兼容层中。

### T2: Bindless-only Realtime Path

构造小场景，断言 Forward / Deferred geometry pass 只从 global tables 绑定资源；缺任一 required table 时 fail-fast。

### T3: Technique Boundary Rejection

构造 material-local technique/defaultTechnique 和 `techniques/...` shader URI fixture，断言 migrated validation profile 拒绝它们。

### T4: No Hidden Fallback

覆盖 unsupported material source、缺 shader variant、缺 texture slot、invalid material index，断言不能输出 debug 色或跳过 draw 后通过。

### T5: Helmet Realtime Smoke

运行 Helmet 低分辨率 realtime smoke，断言非全黑，并校验 source-reflected material、render_paths URI、bindless/indirect stats。

### T6: BMW M6 Realtime Smoke

运行 BMW M6 低分辨率 realtime smoke，断言非全黑或明确 unsupported diagnostic；两者都不能触发旧 fallback。

### T7: Build And Parser Tests

运行 shader compiler、render resource parser、SceneResourceTable upload view、RenderWorkQueue 和 headless auto validation 相关测试，确保普通正向 fixture 不再依赖旧路径。

## 修改范围

- realtime renderer submission / descriptor path
- `RenderWorkQueue` fallback boundary
- `SceneResourceTable` upload / diagnostics
- Forward / Deferred PBR shader and tests
- default RenderPathGraph assets
- Helmet / BMW validation tests and diagnostics
- legacy audit tests

## 边界与约束

- 不保留两个可通过的 realtime 默认入口。
- 不用 debug 色、默认材质或跳过 draw 隐藏不可渲染问题。
- 不用 path/name substring 选择 strictness；strictness 来自 validation profile/property。
- 不把 package、BC7、pipeline cache 或 OfflineRT hard cut 塞入本 REQ。

## 依赖

- `REQ-073-b`: Material storage and bindless upload foundation。
- `REQ-073-c`: Material source shader variant boundary。
- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。
- `REQ-073-e`: Indirect material batching and diagnostics。

## 后续工作

- `REQ-073-g`: OfflineRT RenderPathGraph compute path。
- `REQ-073-h`: OfflineRT config hard cut and smoke。
- `REQ-074-a`: Texture compression pipeline with BC7。

## 实施状态

未实施。
