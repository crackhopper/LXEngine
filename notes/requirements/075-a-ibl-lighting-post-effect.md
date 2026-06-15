# REQ-075-a: IBL Lighting Post Effect

> 2026-06-15 插入：原 `REQ-075-a` offline/realtime equivalence 顺延为 `REQ-076-f`。本 REQ 在 environment skybox 和 reflection probe bake path 之后，让 Forward / Deferred lighting 消费全局和局部 IBL 资源，并把 IBL lighting effect 参数纳入 RenderFeature / shader reflection 合同。

## 背景

当前 Forward / Deferred shader 已有 `IrradianceMap`、`PrefilteredEnvMap`、`BrdfLut` 和 `EnvironmentUBO` binding，也已有 `HAS_IBL` 相关逻辑。问题在于资源来源、probe set、lighting 参数和 shader 合同还没有统一到 graph + feature 事实源。后续如果继续用 C++ 注入默认资源或 hardcoded UBO，会再次绕过 RenderFeature 反射校验。

本 REQ 的 “post effect” 指 lighting pipeline 中的环境光照 effect：它发生在 surface material 参数解析之后，消费 scene-level IBL/probe resources，并在 Forward / DeferredLighting 中合成 diffuse / specular environment contribution。它不是 screen-space `PostProcess` tone mapping。

## 目标

1. 定义 `feature.iblLighting` 或等价 RenderFeature，承载 IBL lighting 参数。
2. 建立统一 environment lighting shader include，供 Forward 和 DeferredLighting 复用。
3. 让 graph sources 显式声明 `scene.reflectionProbes` / `scene.environmentLighting`。
4. 让 probe set、DiffuseSH、PrefilteredEnvMap、BrdfLut 通过 SceneResourceTable live resources 进入 descriptor。
5. 缺少资源或参数时 fail-fast 或显式 zero contribution，不靠 C++ placeholder 伪装成功。

## 非目标

- 不实现 reflection probe bake；由 `REQ-074-g` 处理。
- 不解决 PostProcess gamma/output encoding；由 `REQ-076-a` 处理。
- 不实现 PBRT 高阶材质；由 `REQ-076-e` 处理。
- 不做 offline/realtime equivalence；由 `REQ-076-f` 处理。

## 需求

### R1: IBL Lighting RenderFeature

新增 IBL lighting feature 参数。

最低参数：

| 参数 | 说明 |
|---|---|
| `enabled` | 是否启用 IBL contribution |
| `diffuseIntensity` | diffuse SH / irradiance contribution |
| `specularIntensity` | prefiltered specular contribution |
| `maxProbeCount` | shader 支持的 probe count 上限 |
| `fallbackMode` | 无 probe/resource 时的显式策略：zero 或 fail |

这些参数必须由 `.render-feature.yaml` 配置，并进入 shader binding reflection 校验。不能由 `EnvironmentData` C++ 默认值替代。

### R2: Shared Environment Lighting Shader

Forward 和 DeferredLighting SHALL 使用同一个 common include 计算 IBL。

要求：

- common shader 读取同一套 probe/resource binding。
- surface material 只提供 albedo、roughness、metallic、normal 等物体参数。
- IBL 参数、probe count、resource indices 不进入 `.material`。
- probe set 为空时按 RenderFeature fallbackMode 返回 zero contribution 或 fail-fast。

### R3: Graph Source Declaration

Forward / Deferred graph SHALL 显式声明 IBL lighting sources。

最低 sources：

- `scene.reflectionProbes`
- `scene.brdfLuts`
- `feature.iblLighting`

缺少任一 required source 时，RenderInputDesc 必须 rejected with diagnostic。

### R4: Live SceneResourceTable Payloads

SceneResourceTable SHALL 提供 live IBL resources：

- global probe record。
- local probe records。
- `DiffuseSH9` 或等价 diffuse payload。
- `PrefilteredEnvMap` cubemap。
- `BrdfLut` texture。

metadata-only payload 不得满足 shader binding。placeholder 黑贴图只能在 explicit fallback profile 下使用，并且 diagnostic 必须可见。

### R5: No C++ Feature Injection

完成态禁止通过 backend C++ 手动写 IBL lighting UBO 来绕过 feature 参数。

特别禁止：

- hardcoded `iblIntensity` / mip count 作为 shader 参数事实源。
- 手动创建 IBL fullscreen/material instance。
- 根据 shader name 或 pass name 注入 probe resources。

## 测试

- RenderFeature parser validates `feature.iblLighting` typed parameters。
- shader compiler/reflection tests validate Forward and DeferredLighting IBL bindings。
- RenderPathGraph parser tests require `scene.reflectionProbes` and `feature.iblLighting` where shader needs them。
- SceneResourceTable tests reject metadata-only probe set。
- Vulkan smoke: zero probe set gives zero environment contribution; valid global probe changes shaded PBR output。
- rg audit: IBL intensity / probe count hardcoded backend paths absent from positive default path。

## 修改范围

- `assets/effects/*.render-feature.yaml`
- Forward / Deferred render path graph assets
- common environment lighting GLSL include
- SceneResourceTable IBL/probe resources
- shader compiler / render resource parser / render work compiler tests
- Vulkan IBL lighting smoke

## 边界与约束

- material owns object/surface parameters only。
- RenderFeature owns pass/effect-level IBL lighting parameters。
- backend must consume graph/feature/resource facts, not author them.
- 不新增第二套 public effect system。

## 依赖

- `REQ-073-f`: environment map skybox direct lighting。
- `REQ-074-g`: reflection probe and bake render path。

## 后续工作

- `REQ-076-a`: RenderFeature parameter architecture hard cut。
- `REQ-076-f`: offline/realtime equivalence。

## 实施状态

未实施。
