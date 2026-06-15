# REQ-073-f: Environment Map Skybox Direct Lighting

> 2026-06-15 插入：`REQ-073-e2` 已完成 RenderWorkCompiler 单轨 hard cut。原 `REQ-073-f` 之后未开始的 transparent / OfflineRT / PBRT 工作已顺延到 `REQ-076-b` 到 `REQ-076-e`。本 REQ 先把 scene environment / skybox 直接光照收束到 RenderPathGraph + RenderFeature + SceneResourceTable 正向路径，作为后续 probe bake 和 IBL lighting 的基础。

## 背景

当前代码已经有环境贴图相关能力：`SceneDocument` 能保存 `scene.environment`，`SceneRuntime` 能加载 HDR environment 并生成 `SkyboxMap`，`skybox` shader 能采样 cubemap，Forward/Deferred PBR shader 也已有 scene-level IBL binding。问题在于执行路径仍混有后端 helper：`VulkanPostProcessBuilder::createSkyboxBackgroundMaterial()` 手动创建 fullscreen skybox material，`VulkanRealtimeRenderer::initScene()` 还会通过私有 `IblBakeRenderer` 在运行期补 bake 资源。

本 REQ 只处理第一层：环境贴图作为远场背景和直接可见光源进入 realtime graph。它不实现 local reflection probe，不做 irradiance/specular IBL 间接光照，不扩展 PBRT 高阶材质。

## 目标

1. 让 skybox/background pass 由 RenderPathGraph 显式声明，而不是由 backend helper 注入。
2. 让 environment / skybox 参数通过 RenderFeature 配置和反射校验进入 shader binding，不再由 C++ 常量补齐。
3. 让 `SkyboxMap` / environment UBO 作为 scene-level system resource 从 SceneResourceTable 进入 descriptor。
4. 删除或拒绝手动 `MaterialInstance` skybox helper 正向路径。
5. 用 Helmet / BMW 小场景证明环境贴图可见、可诊断、不会绕过 RenderInputDesc。

## 非目标

- 不实现 reflection probe capture / bake；由 `REQ-074-g` 处理。
- 不实现 diffuse/specular IBL 间接光照；由 `REQ-075-a` 处理。
- 不解决所有 RenderFeature 参数架构遗留；由 `REQ-076-a` 做 hard cut。
- 不做 post-process gamma / tone mapping 架构清理；由 `REQ-076-a` 统一处理。

## 需求

### R1: Skybox RenderPathGraph Pass

Forward / Deferred graph SHALL 显式声明 skybox/background pass 或等价 fullscreen input。

要求：

- pass 使用 `input.kind: fullscreen-triangle` 或明确的 background input kind。
- shader URI 使用 `render_paths/...`，不得使用 root `skybox` short name 作为正向 graph 输入。
- sources 至少包含 `scene.environment` 和 `feature.environmentLighting`。
- targets 写入 HDR scene color，不直接写 swapchain。
- pass order 由 graph 决定，backend 不得按 pass name 临时插入 skybox draw。

### R2: RenderFeature Owns Environment Parameters

环境贴图直接光照参数 SHALL 由 RenderFeature 声明。

最低参数：

| 参数 | 说明 |
|---|---|
| `skyboxEnabled` | 是否绘制背景 |
| `intensity` | 环境亮度 multiplier |
| `rotation` | 环境贴图 yaw 或等价方向参数 |
| `visibleInBackground` | 背景可见性，不等同于 lighting contribution |

这些参数必须：

- 在 `.render-feature.yaml` 中配置。
- 被 parser 保存为 typed parameter。
- 被 shader reflection / binding plan 校验。
- 由 feature-owned UBO 或等价 descriptor 进入 shader。

不得在 `VulkanPostProcessBuilder`、`VulkanRealtimeRenderer` 或 scene runtime 中用 hardcoded values 代替。

### R3: Scene-Level Environment Resource

`SkyboxMap` SHALL 是 scene-level system-owned resource。

要求：

- scene loading 解析 `scene.environment.hdrUri` 后注册 live HDR/cubemap payload。
- `SkyboxMap` 不能作为 material-owned texture resource。
- 缺少 environment 时，graph source 可显式 absent；shader binding 不得靠 placeholder 假装满足。
- 如果 profile 允许无环境图，validator 必须把 feature intensity 置零或跳过 pass，并输出可诊断状态。

### R4: Reflection Validation And Diagnostics

skybox shader 的 binding 与 graph/source/feature 必须一致。

测试应覆盖：

- shader 反射包含 `SkyboxMap` 和 environment feature UBO。
- graph 声明缺少 `scene.environment` 时 fail-fast。
- RenderFeature 缺少 required parameter 时 fail-fast。
- backend descriptor plan 缺少 live `SkyboxMap` 时 fail-fast，而不是渲染黑色并当作成功。

### R5: Remove Manual Skybox Material Injection

完成态默认 realtime path SHALL NOT 调用手写 skybox fullscreen material helper 作为正向路径。

允许保留的代码只能是：

- 被新 graph path 消费的内部 builder，并且输入完全来自 RenderPathGraph / RenderFeature / SceneResourceTable。
- named negative audit 中证明旧 helper 不再能正向渲染。

## 测试

- RenderPathGraph parser 测试：skybox pass 声明、sources/targets 和 feature dependency 被解析。
- RenderFeature parser 测试：environment lighting 参数缺失、未知字段、类型错误都会失败。
- RenderWorkCompiler 测试：skybox fullscreen input 生成 typed input 和 accepted `RenderInputDesc`。
- Vulkan smoke：启用 `scene.environment` 后输出非黑 skybox；关闭 `skyboxEnabled` 后背景贡献消失。
- rg audit：`createSkyboxBackgroundMaterial`、root `skybox` shader URI、manual skybox material injection 不出现在 default positive path。

## 修改范围

- `assets/render_paths/*`
- `assets/effects/*.render-feature.yaml`
- skybox shader URI / shader reflection tests
- RenderFeature -> descriptor binding path
- SceneResourceTable environment resources
- Vulkan realtime skybox execution path
- Helmet / BMW environment smoke fixtures

## 边界与约束

- 物体表面参数仍然属于 material source / material instance，不放进 RenderFeature。
- pass/feature 级参数必须走 RenderFeature，不允许 C++ 硬编码。
- backend 不能手动创建 material instance 来绕过 shader binding reflection。
- 不引入第二套 public graph / effect system。

## 依赖

- `REQ-073-e2`: RenderWorkCompiler / RenderInputDesc 单轨模型。
- `REQ-073-d`: RenderPath shader URI hard cut。

## 后续工作

- `REQ-074-g`: reflection probe and baked render path。
- `REQ-075-a`: IBL lighting post effect / indirect lighting consumption。
- `REQ-076-a`: RenderFeature parameter architecture hard cut。

## 实施状态

未实施。
