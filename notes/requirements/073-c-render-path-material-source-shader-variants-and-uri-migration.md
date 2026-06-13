# REQ-073-c: RenderPath Material Source Shader Variants And URI Migration

> 2026-06-13 拆分：原 `REQ-073-b` 同时要求 material storage、shader variant、indirect batching、RenderPath 术语迁移和 realtime hard cut。本 REQ 只负责把 `bsdf.source` 变成 RenderPath shader 的编译期 source variant，并把 shader URI 从旧 `techniques/...` 迁移到 `render_paths/...`。

## 背景

`REQ-073-a` 已定义 material contract source 和 Material Accessor ABI；`REQ-073-b` 会让 source-reflected material record 进入 bindless-ready upload view。下一步需要让 RenderPath shader 在编译/反射时拿到具体 material source，而不是在 shader runtime 里写 `if materialType` 或 `switch source`。

当前风险是：

- RenderPathGraph 仍可能只保存 base shader URI，缺少 material source variant 维度。
- shader reflection / pipeline build desc 如果基于 base shader，会漏掉 variant 后的 storage binding。
- 旧 `assets/shaders/glsl/techniques/` 名称继续出现在默认 asset 中，会让后续 hard cut 同时承担术语迁移和渲染行为迁移。

因此，本 REQ 把 shader variant 和 URI 迁移先切出来，让后续 indirect batching 只面对最终 shader/pipeline 事实。

## 目标

1. RenderPath shader variant key 包含 material source signature / reflection hash。
2. shader compiler 在编译期注入 `LX_MATERIAL_CONTRACT_SOURCE` 或等价 source include metadata。
3. shader reflection、descriptor layout、pipeline build desc 和 pipeline key 都来自 variant 后最终 shader。
4. 默认 RenderPathGraph / shader asset 使用 `render_paths/...` URI。
5. 旧 `techniques/...` 只能出现在 legacy negative test、历史文档或显式 rejection diagnostic 中。

## 非目标

- 不实现 source-local material storage；由 `REQ-073-b` 处理。
- 不要求 raster work item 全部进入 indirect batch；由 `REQ-073-d` 处理。
- 不删除 realtime 旧 draw/descriptor fallback；由 `REQ-073-e` 处理。
- 不处理 OfflineRT 配置入口；由 `REQ-073-f` / `REQ-073-g` 处理。
- 不实现 package、BC7 或 pipeline cache blob。

## 需求

### R1: Material Source Variant Key

RenderPath shader resolver SHALL 为需要 Material Accessor ABI 的 pass 生成 source variant key。

key 至少包含：

| 字段 | 说明 |
|---|---|
| base shader URI | RenderPathGraph pass 中声明的 shader |
| material source URI | `.material` 的 `bsdf.source` |
| source reflection hash | contract source 反射 hash |
| source signature | material storage / accessor ABI 结构签名 |
| pass identity | RenderPath pass id / stage |

规则：

- 常量值、texture id、texture 是否存在、material URI、material name 不得进入 source variant key。
- 同一个 base shader 可以产生多个 source variant。
- 不同 source variant 可以产生不同 shader module、reflection payload 和 pipeline key。

### R2: Compile-time Contract Source Injection

shader compiler SHALL 在编译 source variant 时注入 material contract source。

要求：

- PBR pass shader 继续调用稳定 Material Accessor ABI。
- contract source 由 variant resolver 提供，不能由 shader runtime 分支选择。
- 缺少 `LX_MATERIAL_CONTRACT_SOURCE` 所需 source、source 不支持当前 pass、source 未实现 accessor ABI 时必须 fail-fast。
- diagnostics 必须列出 base shader URI、material source URI、variant key 和失败字段。

### R3: Reflection Uses Final Variant Shader

shader reflection / pipeline validation SHALL 使用 variant 后最终 shader。

要求：

- descriptor set / binding / push constant / storage layout 来自最终编译 shader。
- `PipelineBuildDesc::fromRenderWorkItem()` 使用 variant reflection，不使用 base shader 估算。
- `PipelineKey` 包含 material source variant identity。
- reflection cache key 与 shader compile key 对齐。

### R4: No Runtime Source Branch In Pass Shader

Forward、Deferred 和后续 OfflineRT pass shader SHALL 不写 material type/source runtime branch。

禁止：

- `if materialType == ...`
- `switch materialSource`
- 在 pass shader 中解析多个 material storage layout。
- 因缺少 source variant 而落回旧共享 `MaterialUBO` 或 debug material。

### R5: Shader URI Migration

默认 shader URI SHALL 从 `techniques/...` 迁移到 `render_paths/...`。

要求：

- RenderPathGraph asset 的 shader 字段使用 `render_paths/...`。
- shader resolver 支持 `render_paths/...` 到 `assets/shaders/glsl/render_paths/...` 的解析。
- default assets、ordinary positive tests 和 migrated validation profile 不得使用 `techniques/...`。
- 旧 `techniques/...` URI 在 migrated validation profile 下失败，诊断说明迁移到 `render_paths/...`。

### R6: RenderPath Terminology Boundary

文档、asset、parser diagnostic 和测试名 SHALL 使用 RenderPath / pass shader 术语，不再把 pass shader 称为 material-local technique。

允许保留：

- 历史需求文档中的旧术语。
- legacy rejection / negative audit fixture。
- 明确标注为旧路径的兼容测试。

### R7: Diagnostics

variant resolver SHALL 输出可审计 diagnostics：

- base shader URI。
- material source URI 和 source signature。
- variant key / compile key / reflection key。
- final shader descriptor summary。
- rejected legacy URI 或 unsupported source 的原因。

无法编译或反射时必须停止渲染准备，不能隐藏为 fallback shader。

## 测试

### T1: Source Variant Key

构造同一个 base shader、两个不同 material source，断言：

- 生成两个 source variant key。
- 两个 variant 的 compile key / reflection key 不同。
- 同 source 的两个 material 参数值不同但 variant key 相同。

### T2: Contract Source Injection

编译需要 Material Accessor ABI 的 Forward / Deferred shader，断言：

- `LX_MATERIAL_CONTRACT_SOURCE` 被具体 source 解析。
- shader 可以调用 `lxLoadMaterialSurface`。
- 缺 source 或 source 缺 accessor ABI 时失败。

### T3: Final Shader Reflection

断言 pipeline desc / descriptor layout 来自 variant 后最终 shader，并包含 source 声明的 material storage binding。

### T4: No Runtime Branch Audit

rg/audit Forward / Deferred PBR pass shader，断言没有 material type/source runtime branch，也没有旧 `MaterialUBO` PBR 参数读取作为正向路径。

### T5: Shader URI Migration

迁移默认 RenderPathGraph asset，断言：

- default shader URI 为 `render_paths/...`。
- resolver 能找到 `assets/shaders/glsl/render_paths/...`。
- `techniques/...` 在 migrated validation profile 下失败。

### T6: Pipeline Key Includes Variant

构造同 pass / target / vertex layout / 不同 source 的 work item，断言 pipeline key 不同；同 source / 不同 material 参数值时 pipeline key 相同。

### T7: Diagnostics

覆盖缺 shader、缺 source、unsupported source、legacy URI、reflection failure，断言 diagnostics 包含 base shader、source URI、variant key 和失败原因。

## 修改范围

- RenderPathGraph shader resolver / parser validation
- shader compiler variant key / include injection
- shader reflection cache
- `PipelineBuildDesc` / `PipelineKey` material source variant 输入
- `assets/shaders/glsl/render_paths/Forward/`
- `assets/shaders/glsl/render_paths/Deferred/`
- default RenderPathGraph assets
- shader URI migration tests and audits

## 边界与约束

- 不写 shader runtime source/type branch。
- 不用 base shader reflection 代替 variant shader reflection。
- 不让 `techniques/...` 成为 resolver fallback。
- 不在本 REQ 删除旧 renderer fallback；这里只保证新 variant 和 URI 语义可用且可诊断。

## 依赖

- `REQ-073-a`: Material source contract 和 Material Accessor ABI。
- `REQ-073-b`: source-local material storage / upload view foundation。

## 后续工作

- `REQ-073-d`: Indirect material batching and diagnostics。
- `REQ-073-e`: Realtime material path hard cut and smoke。
- `REQ-073-f`: OfflineRT RenderPathGraph compute path。

## 实施状态

未实施。
