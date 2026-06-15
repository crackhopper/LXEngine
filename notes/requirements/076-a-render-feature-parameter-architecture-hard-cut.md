# REQ-076-a: RenderFeature Parameter Architecture Hard Cut

> 2026-06-15 插入：本 REQ 记录当前 PostProcess / skybox / IBL 路径暴露出的架构问题：shader 里有 feature/effect 级参数，但运行时仍可能由 C++ 手写 UBO 或手动 `MaterialInstance` 提供，导致 RenderFeature 参数事实源被绕过。原 `REQ-076-a` path tracing reference 顺延为 `REQ-076-g`。

## 背景

当前 `assets/effects/tone_mapping.render-feature.yaml` 已定义 tone mapping 参数；`PostProcess` shader 也声明 `PostProcessUBO.gamma` 等字段。但 realtime builder 仍会在 `VulkanPostProcessBuilder::createStandardPostProcessMaterial()` 中手动创建 fullscreen material，并由 C++ 直接写入 `PostProcessUBO`。这让 shader binding completeness 可以通过，却没有证明 RenderFeature 参数被消费。

同类风险也会出现在 skybox、reflection probe bake、IBL lighting 等 effect：如果参数在 shader 中存在，但 graph/feature schema 没有作为唯一事实源进入 binding plan，反射校验就只能校验“有 descriptor”，不能校验“descriptor 来自正确的 RenderFeature”。

## 目标

1. RenderFeature 参数成为 pass/effect-level shader 参数的唯一事实源。
2. 建立 RenderFeature parameter schema -> shader binding/member reflection -> descriptor binding plan 的校验链路。
3. 禁止 default render path 用 C++ hardcoded values、manual `MaterialInstance` 或 backend helper 填充 feature UBO。
4. 把 PostProcess gamma/output encoding、skybox environment 参数、IBL lighting 参数纳入同一规则。
5. 给后续 agent 一个明确 audit：feature 参数未配置、未反射、未绑定时必须失败。

## 非目标

- 不改变 material source / surface parameter 所有权。
- 不新建第二套 public graph/effect 系统。
- 不一次性重写所有 shader；按 PostProcess、skybox、IBL lighting、bake path 分批 hard cut。
- 不用本 REQ 实现新的视觉效果。

## 需求

### R1: RenderFeature Parameter Schema

RenderFeature SHALL 支持足够表达 shader UBO member 的 typed schema。

最低字段：

| 字段 | 说明 |
|---|---|
| `kind` | float / int / bool / enum / vec 等 |
| `value` | 默认值 |
| `binding` | 目标 shader binding 名 |
| `member` | 目标 UBO member 名 |
| `range` | 可选数值范围 |
| `required` | 是否 required |

未知字段必须 fail-fast。parser 接受的字段必须被保存并参与校验。

### R2: Feature-To-Shader Reflection Validation

RenderPathGraph compile / RenderWorkCompiler prepare SHALL 校验 feature 参数和 shader reflection 的匹配。

要求：

- graph 声明的 feature dependency 必须 resolve 到 live RenderFeature payload。
- shader 反射中 required feature UBO member 必须有对应 RenderFeature parameter。
- RenderFeature parameter 类型、offset/size、binding name 和 shader member 匹配失败时 rejected。
- 未被 shader 消费的 feature parameter 可以诊断为 warning 或 strict-profile error，但不能静默失效。

### R3: Runtime Binding Builder

runtime SHALL 从 RenderFeature payload 构建 descriptor/UBO。

要求：

- binding data 来自 feature parameter values 和 runtime-derived explicit facts。
- runtime-derived facts 必须有 schema，例如 swapchain output encoding 可作为 `outputEncodingMode`，不能复用 `gamma` 字段表达隐式开关。
- binding builder 输出进入 `RenderInputDesc.bindingPlan`，供 backend 消费。
- backend 不重新解释 feature parameter。

### R4: Manual MaterialInstance Hard Cut

默认 fullscreen/post/skybox/effect path SHALL NOT 手动创建 material instance 作为 feature UBO 容器。

旧路径处理：

- `createStandardPostProcessMaterial`、`createSkyboxBackgroundMaterial` 等 helper 要么删除，要么改成消费 graph/feature facts 的内部实现。
- positive tests 不得通过手写 `writeShaderBindingParameter(PostProcessUBO, ...)` 证明 feature 参数可用。
- 任何保留 helper 必须有 owner requirement 和 removal target。

### R5: PostProcess Gamma Fix

PostProcess SHALL 拆分显示 gamma 与输出编码模式。

要求：

- `gamma` 表达 tone mapping/display gamma，来自 `feature.toneMapping`。
- `outputEncodingMode` 表达 shader 是否需要手动 sRGB encode，由 target format 派生，但必须进入显式 schema/binding。
- sRGB target 输出 linear；UNORM target shader 手动 encode。
- shader、feature、target format 三者不一致时 fail-fast 或输出 diagnostic。

### R6: Cross-Path Audit

完成时审计以下路径：

- PostProcess / Bloom。
- Skybox / environment background。
- ReflectionFilter / BrdfLutBake。
- Forward / Deferred IBL lighting。
- DebugOverlay / debug fullscreen effects where relevant。

同类路径不得保留 C++ hardcoded UBO 正向路径。

## 测试

- RenderFeature parser rejects unknown parameter fields and accepts binding/member schema。
- shader reflection test fails when feature parameter is missing or wrong type。
- RenderWorkCompiler prepare rejects graph feature dependency missing live payload。
- PostProcess test verifies `gamma` and `outputEncodingMode` are separate reflected members。
- negative audit proves manual `writeShaderBindingParameter(PostProcessUBO, gamma)` no longer satisfies default path。
- rg audit covers `createStandardPostProcessMaterial|createSkyboxBackgroundMaterial|writeShaderBindingParameter\\(.*PostProcessUBO|IblBakeRenderer|bakeStaticEnvironment` default positive path hits.

## 修改范围

- `src/core/asset/render_effect.*`
- RenderFeature parser and tests
- RenderPathGraph dependency resolution
- RenderWorkCompiler binding plan validation
- Vulkan fullscreen/post/skybox/effect builders
- PostProcess / skybox / IBL feature assets and shaders
- notes/concepts material/render path docs

## 边界与约束

- object/surface material parameters stay in Material source contracts。
- feature/effect/pass-level parameters stay in RenderFeature。
- runtime-derived parameters must be explicit schema entries, not hidden C++ side channels。
- no placeholder payload may satisfy feature dependency。

## 依赖

- `REQ-073-f`: environment map skybox direct lighting。
- `REQ-074-g`: reflection probe and bake render path。
- `REQ-075-a`: IBL lighting post effect。

## 后续工作

- `REQ-076-b`: transparent BMW material path and smoke。
- `REQ-076-f`: offline/realtime equivalence on new architecture。

## 实施状态

未实施。
