# REQ-073-i: RenderFeature Parameter Architecture Hard Cut

> 2026-06-16 校准：本 REQ 属于 `REQ-073` 当前渲染闭环的架构收口。它记录当前 PostProcess / skybox / IBL 路径暴露出的架构问题：shader 里有 feature/effect 级参数，但运行时仍可能由 C++ 手写 UBO 或手动 `MaterialInstance` 提供，导致 RenderFeature 参数事实源被绕过。advanced path tracing reference 已归入 `REQ-075-c`。

## 背景

当前 `assets/effects/tone_mapping.render-feature.yaml` 已定义 tone mapping 参数；`PostProcess` shader 也声明 `PostProcessUBO.gamma` 等字段。但 realtime builder 仍会在 `VulkanPostProcessBuilder::createStandardPostProcessMaterial()` 中手动创建 fullscreen material，并由 C++ 直接写入 `PostProcessUBO`。这让 shader binding completeness 可以通过，却没有证明 RenderFeature 参数被消费。

同类风险也会出现在 skybox、reflection probe bake、IBL lighting 等 effect：如果参数在 shader 中存在，但 graph/feature schema 没有作为唯一事实源进入 binding plan，反射校验就只能校验“有 descriptor”，不能校验“descriptor 来自正确的 RenderFeature”。

`REQ-073-f` 已确认 environment 边界：EnvMap resource URI 和 shader-visible
environment 参数都属于 `feature.environmentLighting`。单色环境光也必须作为
EnvMap source variant 进入同一条 feature resource 路径，例如
`environmentMap.uri: builtin:env/white_cube` 提供 live 白色 cubemap，再由同一
feature 的 color / intensity 决定 radiance。073-i hard cut 必须把这个规则纳入
校验，不能让 `scene.environment`、C++ helper 或 placeholder resource 绕过
RenderFeature。

## 目标

1. RenderFeature 参数成为 pass/effect-level shader 参数的唯一事实源。
2. 建立 RenderFeature parameter schema -> shader binding/member reflection -> descriptor binding plan 的校验链路。
3. 禁止 default render path 用 C++ hardcoded values、manual `MaterialInstance` 或 backend helper 填充 feature UBO。
4. 把 PostProcess gamma/output encoding、skybox environment 参数、IBL lighting 参数纳入同一规则。
5. 禁止 `scene.environment` 继续作为 realtime environment 资源或 shader 参数事实源。
6. 给后续 agent 一个明确 audit：feature 参数未配置、未反射、未绑定时必须失败。

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
- 如果 pass 声明 `feature.environmentLighting`，pass shader 必须 include `common/environment_lighting.glsl` 或等价 common shader lib，并反射出该 feature 声明的 binding/member。

### R3: Runtime Binding Builder

runtime SHALL 从 RenderFeature payload 构建 descriptor/UBO。

要求：

- binding data 来自 feature parameter values 和 runtime-derived explicit facts。
- runtime-derived facts 必须有 schema，例如 swapchain output encoding 可作为 `outputEncodingMode`，不能复用 `gamma` 字段表达隐式开关。
- scene document 字段不得提供 environment feature value；`scene.environment` 不能贡献 EnvMap resource handle 或 shader-visible 参数。
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

### R6: Environment Feature Hard Cut

Environment lighting SHALL follow the same RenderFeature hard cut as postprocess.

要求：

- environment resource URI 必须由 `feature.environmentLighting.parameters.environmentMap.uri` 提供；`scene.environment` 不能满足 realtime graph 的 EnvMap dependency。
- `scene.environment` 中的 `ambientColor`、`ambientIntensity`、`intensity`、`rotation`、`uri` 等 environment 字段必须被删除、迁移到 feature，或在 strict profile 中 fail-fast。
- constant-color environment 通过显式 `environmentMap.uri: builtin:env/white_cube` 或等价内置 URI 表达，不能在缺 URI 时隐式创建默认 cubemap。
- 内置白 cubemap 是 live `SkyboxMap` payload，不是 placeholder；descriptor plan 缺少 live payload 时必须 rejected。
- `feature.environmentLighting.render-feature.yaml` 声明 `environmentMap` resource parameter 以及 color / intensity / rotation / skybox visibility 等 shader-visible 参数，并保存 resource binding 与 UBO binding/member schema。
- Forward、DeferredLighting、SkyboxBackground 以及后续 IBL pass 依赖 environment feature 时，必须使用同一个 feature payload 和 common shader ABI，不得各自手写 UBO。

### R7: Cross-Path Audit

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
- RenderFeature environment tests reject missing `environmentMap.uri`, prove explicit `builtin:env/white_cube` creates a live `SkyboxMap`, and prove scene-side environment fields cannot satisfy the dependency。
- Environment feature reflection tests prove `feature.environmentLighting` binding/member schema matches `common/environment_lighting.glsl` consumers。
- negative audit proves manual `writeShaderBindingParameter(PostProcessUBO, gamma)` no longer satisfies default path。
- rg audit covers `createStandardPostProcessMaterial|createSkyboxBackgroundMaterial|writeShaderBindingParameter\\(.*PostProcessUBO|ambientColor|ambientIntensity|IblBakeRenderer|bakeStaticEnvironment` default positive path hits.

## 修改范围

- `src/core/asset/render_effect.*`
- RenderFeature parser and tests
- RenderPathGraph dependency resolution
- RenderWorkCompiler binding plan validation
- Vulkan fullscreen/post/skybox/effect builders
- PostProcess / skybox / IBL feature assets and shaders
- scene environment parser / loader hard-cut tests
- notes/concepts material/render path docs

## 边界与约束

- object/surface material parameters stay in Material source contracts。
- feature/effect/pass-level parameters stay in RenderFeature。
- environment resource identity and shader-visible environment controls stay in `feature.environmentLighting`。
- runtime-derived parameters must be explicit schema entries, not hidden C++ side channels。
- no placeholder payload may satisfy feature dependency。

## 依赖

- `REQ-073-f`: environment map skybox direct lighting。
- `REQ-073-g`: reflection probe and bake render path。
- `REQ-073-h`: IBL lighting post effect。

## 后续工作

- `REQ-073-j`: transparent BMW material path and smoke。
- `REQ-075-b`: offline/realtime equivalence on new architecture。

## 实施状态

未实施。
