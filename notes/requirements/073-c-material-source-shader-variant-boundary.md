# REQ-073-c: Material Source Shader Variant Boundary

> 2026-06-13 再拆分：原 `REQ-073-c` 同时覆盖 material source shader variant、shader URI 迁移和 RenderPath 术语硬切，范围仍然偏大。本 REQ 只负责让 `bsdf.source` 成为 shader 编译、反射和 pipeline identity 的结构维度；`techniques/...` 到 `render_paths/...` 的默认 URI 迁移拆到 `REQ-073-d`。

## 背景

`REQ-073-a` 已定义 material contract source 和 Material Accessor ABI；`REQ-073-b` 已让 source-reflected material record 进入 bindless-ready upload view。下一步必须让 RenderPath shader 在编译/反射时拿到具体 material source，而不是在 shader runtime 里写 `if materialType` 或 `switch source`。

当前风险是：

- RenderPathGraph 仍可能只保存 base shader URI，缺少 material source variant 维度。
- shader reflection / pipeline build desc 如果基于 base shader，会漏掉 variant 后的 storage binding。
- 需要 `LX_MATERIAL_CONTRACT_SOURCE` 的 shader 被普通 `CompileShaders` / `BuildTest` 裸编译时会失败；正确修复是建立 variant-aware build target，而不是移除 shader fail-fast。

因此，本 REQ 只稳定“最终 shader 事实”：source variant key、compile injection、final reflection、pipeline key 和 build target 边界。URI 迁移是独立硬切，由 `REQ-073-d` 处理。

## 目标

1. RenderPath shader variant key 包含 material source signature / reflection hash。
2. shader compiler 在编译期注入 `LX_MATERIAL_CONTRACT_SOURCE` 或等价 source include metadata。
3. shader reflection、descriptor layout、pipeline build desc 和 pipeline key 都来自 variant 后最终 shader。
4. pass shader 只调用 Material Accessor ABI，不写 material source/type runtime branch。
5. shader build target 明确区分 base source 和 material source variant，`BuildTest` 不能裸编译需要 source variant 的 shader。

## 承接自 073-a / 073-b 的未完成项

| 来源 | 本 REQ 承接内容 | 为什么属于 073-c |
|---|---|---|
| `REQ-073-a` R5 / T6 / T7 | final shader variant、variant 后 shader reflection、compile/reflection key 和 `PipelineKey` 接入 | 这些事实必须在 RenderPath shader resolver 和 pipeline identity 层形成，不能由材质合同层或 base shader reflection 代替 |
| `REQ-073-a` T7 / T10 | 同 source / 不同材质参数值不拆 pipeline 的 pipeline-key 验证 | pipeline key 是否只包含 source variant identity，需要在最终 shader variant 后验证 |
| `REQ-073-b` 未完成项 | `BuildTest` / `CompileShaders` 对 `pbr.frag`、`pbr_gbuffer.frag`、`offline_pbr_direct_ray.comp` 的裸编译失败 | 这些 shader 已经声明需要 `LX_MATERIAL_CONTRACT_SOURCE`；正确修复是建立 variant-aware shader build target，而不是移除 shader 中的 fail-fast |

## 非目标

- 不实现 source-local material storage；已由 `REQ-073-b` 处理。
- 不迁移 `techniques/...` 到 `render_paths/...`；由 `REQ-073-d` 处理。
- 不要求 raster work item 全部进入 indirect batch；由 `REQ-073-e` 处理。
- 不删除 realtime 旧 draw/descriptor fallback；由 `REQ-073-f` 处理。
- 不处理 OfflineRT 配置入口；由 `REQ-073-g` / `REQ-073-h` 处理。
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

### R5: Shader Build Target Boundary

shader build / test target SHALL 明确表达 source-variant shader 的编译时机。

要求：

- 需要 `LX_MATERIAL_CONTRACT_SOURCE` 的 shader source 不得作为普通 base shader 裸编译成功条件。
- `CompileShaders`、`BuildTest` 或等价 build target 要么提供 material source variant context，要么排除 variant-only base source 并由独立 variant target 覆盖。
- 缺 source variant context 的失败必须命名 shader URI、缺失宏或 source、以及应使用的 variant build path。
- 不允许通过删除 shader `#error`、注入空 source、或 fallback 到旧 `MaterialUBO` 让裸编译通过。

### R6: Diagnostics

variant resolver SHALL 输出可审计 diagnostics：

- base shader URI。
- material source URI 和 source signature。
- variant key / compile key / reflection key。
- final shader descriptor summary。
- unsupported source 或 accessor ABI 缺失的原因。

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

### T5: Pipeline Key Includes Variant

构造同 pass / target / vertex layout / 不同 source 的 work item，断言 pipeline key 不同；同 source / 不同 material 参数值时 pipeline key 相同。

### T6: CompileShaders Variant Boundary

运行 shader build / `BuildTest` 相关目标，断言：

- source-variant shader 不再被普通裸编译目标误判为失败。
- variant build target 能为 Forward、Deferred 和 OfflineRT direct shader 提供 source context。
- 人为移除 source context 时失败信息指向缺失 variant，而不是静默 fallback。

### T7: Diagnostics

覆盖缺 shader、缺 source、unsupported source、reflection failure，断言 diagnostics 包含 base shader、source URI、variant key 和失败原因。

## 修改范围

- RenderPathGraph shader resolver / parser validation
- shader compiler variant key / include injection
- CMake / shader build targets for source variants
- shader reflection cache
- `PipelineBuildDesc` / `PipelineKey` material source variant 输入
- Forward / Deferred / OfflineRT source-variant compile tests

## 边界与约束

- 不写 shader runtime source/type branch。
- 不用 base shader reflection 代替 variant shader reflection。
- 不在本 REQ 删除旧 renderer fallback；这里只保证新 variant 语义可用且可诊断。
- 不把 URI 迁移混进本 REQ；`techniques/...` 到 `render_paths/...` 的默认路径硬切由 `REQ-073-d` 承接。

## 依赖

- `REQ-073-a`: Material source contract 和 Material Accessor ABI。
- `REQ-073-b`: source-local material storage / upload view foundation。

## 后续工作

- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。
- `REQ-073-e`: Indirect material batching and diagnostics。
- `REQ-073-f`: Realtime material path hard cut and smoke。
- `REQ-073-g`: OfflineRT RenderPathGraph compute path。

## 实施状态

未实施。
