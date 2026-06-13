# REQ-073-d: RenderPath Shader URI Migration And Terminology Hard Cut

> 2026-06-13 拆分：本 REQ 从原 `REQ-073-c` 拆出，只负责把默认 shader URI 和术语从旧 `techniques/...` 硬切到 `render_paths/...`。`REQ-073-c` 先建立 material source shader variant / final reflection / pipeline identity，本 REQ 在这个基础上迁移默认 asset、resolver、测试和 rejection diagnostics。

## 背景

当前仓库已经有 `assets/render_paths/*.render-path.yaml`，但 shader 源和部分测试、工具、默认路径仍引用 `assets/shaders/glsl/techniques/...`。这会造成两个问题：

- `techniques` 是旧 material-local 术语，容易让实现继续把 pass shader 当成材质内部 technique。
- 后续 realtime hard cut 如果同时承担 shader source variant、URI 迁移和旧 fallback 删除，会很难判断失败原因。

因此，我们先在 `REQ-073-c` 稳定 final shader variant，再在本 REQ 单独完成 URI / 术语硬切。完成后，后续 `REQ-073-e` 只面对已经稳定的 shader/pipeline identity 和 RenderPath 术语。

## 目标

1. 默认 RenderPathGraph / shader asset 使用 `render_paths/...` URI。
2. shader resolver 支持 `render_paths/...` 到 `assets/shaders/glsl/render_paths/...` 的解析。
3. 默认 realtime shader 源迁移到 `assets/shaders/glsl/render_paths/Forward/` 和 `assets/shaders/glsl/render_paths/Deferred/`。
4. 旧 `techniques/...` 只能出现在 legacy negative test、历史文档或显式 rejection diagnostic 中。
5. 文档、asset、parser diagnostic 和测试名使用 RenderPath / pass shader 术语，不再把 pass shader 称为 material-local technique。

## 承接自 073-a / 073-b / 073-c 的未完成项

| 来源 | 本 REQ 承接内容 | 为什么属于 073-d |
|---|---|---|
| `REQ-073-a` 未完成项 | `techniques/...` 到 `render_paths/...` 的默认 URI 迁移 | URI 迁移是默认 asset / resolver / 测试硬切，不能放在材质合同层 |
| `REQ-073-b` 未完成项 | shader source tree、runtime path 和 positive tests 不再依赖 `techniques/...` | 073-b 只修复 build/runtime source 同步，未迁移默认 URI |
| `REQ-073-c` 后续 | source-variant shader 的 base URI 使用 RenderPath 术语 | final shader identity 稳定后，URI 迁移才能避免留下旧 resolver fallback |

## 非目标

- 不实现 material source shader variant；由 `REQ-073-c` 处理。
- 不要求 raster work item 全部进入 indirect batch；由 `REQ-073-e` 处理。
- 不删除 realtime 旧 draw/descriptor fallback；由 `REQ-073-f` 处理。
- 不处理 OfflineRT 默认配置入口硬切；由 `REQ-073-g` / `REQ-073-h` 处理。
- 不实现 package、BC7 或 pipeline cache blob。

## 需求

### R1: Shader Source Tree Migration

默认 realtime shader source SHALL 迁移到 `assets/shaders/glsl/render_paths/...`。

最低迁移：

| 旧路径 | 新路径 |
|---|---|
| `assets/shaders/glsl/techniques/Forward/*` | `assets/shaders/glsl/render_paths/Forward/*` |
| `assets/shaders/glsl/techniques/Deferred/*` | `assets/shaders/glsl/render_paths/Deferred/*` |

规则：

- 迁移后的正向 build target 不得继续从旧路径编译 default realtime shader。
- 如果旧路径暂时作为 negative fixture 保留，目录、文件名或测试必须明确标注 legacy rejection。
- 不允许复制两份都作为正向成功路径。

### R2: RenderPathGraph Asset URI Migration

默认 RenderPathGraph asset SHALL 使用 `render_paths/...` shader URI。

要求：

- `assets/render_paths/forward_main.render-path.yaml` 等默认 asset 的 shader 字段使用 `render_paths/...`。
- ordinary positive tests 使用 `render_paths/...`。
- migrated validation profile 下，`techniques/...` URI 失败并输出迁移 diagnostic。

### R3: Resolver Hard Cut

shader resolver SHALL 支持 `render_paths/...`，并禁止把 `techniques/...` 作为默认 fallback。

要求：

- `render_paths/Forward/pbr` 解析到 `assets/shaders/glsl/render_paths/Forward/pbr.*`。
- resolver diagnostic 必须区分 missing shader、legacy URI、unsupported stage。
- 旧 `techniques/...` 只能通过 named legacy rejection path 触发失败，不得静默重定向。

### R4: RenderPath Terminology Boundary

文档、asset、parser diagnostic 和测试名 SHALL 使用 RenderPath / pass shader 术语。

允许保留：

- 历史需求文档中的旧术语。
- legacy rejection / negative audit fixture。
- 明确标注为旧路径的兼容测试。

禁止：

- default asset 中继续出现 material-local technique / defaultTechnique。
- ordinary positive test 继续把 pass shader 称为 technique。
- `src/test` 普通正向 fixture 继续使用 `techniques/...` 证明 current path。

### R5: URI Migration Diagnostics

migrated validation profile SHALL 输出可审计 diagnostics：

- rejected legacy shader URI。
- expected `render_paths/...` URI。
- RenderPathGraph asset URI。
- pass id / stage。
- resolver search path。

无法解析或遇到 legacy URI 时必须停止渲染准备，不能隐藏为 fallback shader。

## 测试

### T1: Default Asset URI Migration

解析默认 Forward / Deferred RenderPathGraph asset，断言 shader URI 使用 `render_paths/...`。

### T2: Resolver New Path

构造 `render_paths/Forward/pbr` 和 `render_paths/Deferred/pbr_gbuffer`，断言 resolver 找到新目录下的 shader source / SPIR-V 输出。

### T3: Legacy URI Rejection

构造 `techniques/...` shader URI fixture，断言 migrated validation profile 拒绝它，并输出 expected `render_paths/...` diagnostic。

### T4: Positive Test Audit

rg/audit ordinary positive tests、default assets 和 runtime default path，断言 `techniques/...` 只出现在 legacy negative test、历史文档或 rejection diagnostic。

### T5: Shader Build Tree

运行 shader build target，断言 default realtime shader 从 `render_paths/...` 编译；旧 `techniques/...` 不再是 default build 成功条件。

## 修改范围

- `assets/render_paths/*.render-path.yaml`
- `assets/shaders/glsl/render_paths/Forward/`
- `assets/shaders/glsl/render_paths/Deferred/`
- `assets/shaders/CMakeLists.txt`
- shader resolver / runtime path helpers
- render resource parser tests and legacy URI audits
- default realtime runtime path references

## 边界与约束

- 不让 `techniques/...` 成为 resolver fallback。
- 不保留两套正向 shader source tree。
- 不把 OfflineRT 旧 provider / config hard cut 塞入本 REQ；OfflineRT 默认入口由 `REQ-073-g` / `REQ-073-h` 处理。
- 不在本 REQ 删除 realtime material fallback；这里仅完成 URI 和术语硬切。

## 依赖

- `REQ-073-c`: Material source shader variant boundary。

## 后续工作

- `REQ-073-e`: Indirect material batching and diagnostics。
- `REQ-073-f`: Realtime material path hard cut and smoke。
- `REQ-073-g`: OfflineRT RenderPathGraph compute path。

## 实施状态

未实施。
