# REQ-073-f: Environment Map Skybox Direct Lighting

> 2026-06-15 插入：`REQ-073-e2` 已完成 RenderWorkCompiler 单轨 hard cut。原 `REQ-073-f` 之后未开始的 transparent / OfflineRT / PBRT 工作已顺延到 `REQ-073-j` 到 `REQ-075-a`。本 REQ 先把 scene environment / skybox 直接光照收束到 RenderPathGraph + RenderFeature + SceneResourceTable 正向路径，作为后续 probe bake 和 IBL lighting 的基础。

## 背景

当前代码已经有环境贴图相关能力：`SceneDocument` 能保存 `scene.environment`，`SceneRuntime` 能加载 HDR environment 并生成 `SkyboxMap`，`skybox` shader 能采样 cubemap，Forward/Deferred PBR shader 也已有 scene-level IBL binding。问题在于执行路径仍混有后端 helper：`VulkanPostProcessBuilder::createSkyboxBackgroundMaterial()` 手动创建 fullscreen skybox material，`VulkanRealtimeRenderer::initScene()` 还会通过私有 `IblBakeRenderer` 在运行期补 bake 资源。

本 REQ 只处理第一层：EnvMap 作为可见远场背景进入 realtime graph，并建立
`feature.environmentLighting.parameters.environmentMap.uri`、`SkyboxMap` 和
`feature.environmentLighting` shader 参数的统一资源 / 参数合同。它不实现 surface lighting 对 EnvMap 的 BRDF 采样，不实现 local
reflection probe，不做 irradiance/specular IBL 间接光照，不扩展 PBRT 高阶材质。

2026-06-16 校准：单色环境光不是独立 ambient fallback，而是 EnvMap 的一种
source variant。EnvMap resource URI 和 shader-visible 参数都属于
`feature.environmentLighting` 的 RenderFeature YAML，并通过 shader reflection /
resource dependency 校验。单色环境使用显式 `environmentMap.uri:
builtin:env/white_cube` 注册 live 1x1 白色 cubemap，再由同一个 feature 的
color / intensity 等参数得到最终 radiance。073-f 正向路径不再以
`scene.environment` 作为 graph source；旧 scene environment 字段只允许作为迁移输入
或 negative audit，不能满足 `SkyboxMap` 依赖。

2026-06-16 准备项：已引入 Khronos glTF Sample Environments 的
`neutral/ggx/specular.ktx2` 到 `assets/env/khronos/neutral/ggx/`。该文件是
GGX 预过滤 specular cubemap，不是普通 equirect HDR。当前只要求
`TextureLoader::loadKtx2Cubemap()` 支持这个 uncompressed
`VK_FORMAT_R16G16B16A16_SFLOAT` cubemap 子集；BasisU / supercompressed KTX2 /
通用 KTX2 材质贴图不属于本 REQ。

## 目标

1. 让 skybox/background pass 由 RenderPathGraph 显式声明，而不是由 backend helper 注入。
2. 让 texture EnvMap 与单色 EnvMap 收敛到同一条 `feature.environmentLighting.parameters.environmentMap.uri` -> SceneResourceTable -> shader sampler 路径。
3. 让 environment / skybox shader 参数通过 RenderFeature 配置和反射校验进入 shader binding，不再由 scene 字段或 C++ 常量补齐。
4. 让 `SkyboxMap` / environment feature UBO 作为 scene-level system resource 与 feature-owned descriptor 从正向路径进入 descriptor plan。
5. 删除或拒绝手动 `MaterialInstance` skybox helper 正向路径。
6. 用 Helmet / BMW 小场景证明环境贴图可见、可诊断、不会绕过 RenderInputDesc。

## 非目标

- 不实现 reflection probe capture / bake；由 `REQ-073-h` 处理。
- 不实现 Forward / Deferred surface lighting 对 constantColor、HDR、KTX2 或 probe EnvMap 的采样贡献；由 `REQ-073-g` 处理。
- 不解决所有 RenderFeature 参数架构遗留；由 `REQ-073-i` 做 hard cut。
- 不做 post-process gamma / tone mapping 架构清理；由 `REQ-073-i` 统一处理。

## 需求

### R1: Skybox RenderPathGraph Pass

Forward / Deferred graph SHALL 显式声明 skybox/background pass 或等价 fullscreen input。

要求：

- pass 使用 `input.kind: fullscreen-triangle` 或明确的 background input kind。
- shader URI 使用 `render_paths/...`，不得使用 root `skybox` short name 作为正向 graph 输入。
- sources 至少包含 `feature.environmentLighting`；该 feature 同时声明 EnvMap texture URI 和 shader 参数。
- targets 写入 HDR scene color，不直接写 swapchain。
- pass order 由 graph 决定，backend 不得按 pass name 临时插入 skybox draw。
- skybox/background pass 是 graph-authored background effect pass，不是 material object；本 REQ 不通过新增 skybox scene object / material category 来少建一个 pass。
- background pass 只填充没有几何覆盖的 HDR 背景像素；它不计算物体表面的 environment lighting。
- Forward / Deferred surface lighting 将在 `REQ-073-g` 读取同一个 EnvMap / feature 合同；本 REQ 只保证资源与参数合同已经可被后续 lighting pass 复用。
- background pass 需要读取 `depth.main` 作为 read-only depth attachment 时，必须由 RenderPathGraph schema 明确表达；不得由 backend 按 pass name 或 shader name 临时绑定 depth image。
- schema 应在 rendering attachment / target contract 上支持显式 `attachmentUsage`，例如 `color-attachment-write`、`depth-attachment-read-only`、`depth-attachment-write`、`depth-attachment-read-write`；`depth.main` 可以出现在 `sources` 和 rendering attachment contract 中，但只有 writable usage 才进入 `targets` depth write。
- FrameGraph / backend 必须从该 schema 事实生成 `LOAD` + `depthWrite=false` 的 depth attachment 绑定，并避免把 read-only depth 误判为重复写 `depth.main`。

### R2: RenderFeature Owns Environment Parameters

环境背景参数 SHALL 由 RenderFeature 声明。

最低参数：

| 参数 | 说明 |
|---|---|
| `environmentMap` | EnvMap resource URI；单色环境使用 `builtin:env/white_cube`，texture 环境使用 HDR / KTX2 URI |
| `visibleInBackground` | 是否作为相机背景绘制；不等同于 lighting contribution |
| `intensity` | EnvMap radiance multiplier，背景和后续 surface lighting 共用 |
| `color` | EnvMap radiance tint；单色环境用白 cubemap * color，背景和后续 surface lighting 共用 |
| `rotation` | 环境贴图 yaw 或等价方向参数 |

这些参数必须：

- 在 `.render-feature.yaml` 中配置。
- 被 parser 保存为 typed parameter。
- 被 shader reflection / binding plan 校验。
- 由 feature-owned UBO 或等价 descriptor 进入 shader。
- `environmentMap.uri` 必须被资源解析流程注册为 live `SkyboxMap` payload；它是 feature resource parameter，不是 scene 字段。
- 由依赖该 feature 的 pass shader 通过 shared `common/environment_lighting.glsl` 或等价 common shader lib 消费；feature YAML 的 binding/member 必须和反射结果一致。
- `color` / `intensity` 定义 EnvMap 源 radiance；`REQ-073-g` 在 Forward surface lighting 阶段复用同一组值，再用 `feature.surfaceLighting` 的 IBL 开关与 diffuse/specular multiplier 控制 contribution 强度。

不得在 `scene.environment`、`VulkanPostProcessBuilder`、`VulkanRealtimeRenderer` 或 scene runtime 中用 hardcoded values 代替。

### R3: Feature-Level Environment Resource

`SkyboxMap` SHALL 是由 RenderFeature 资源参数驱动、注册到 `SceneResourceTable` 的 system resource。

要求：

- RenderFeature loading 解析 `feature.environmentLighting.parameters.environmentMap.uri` 后注册 live EnvMap payload。
- `scene.environment` 不作为 073-f 正向 graph dependency；`ambientColor`、`ambientIntensity`、`intensity`、`rotation`、`uri` 等旧 scene-side environment 字段不能满足 `SkyboxMap` 依赖，必须 fail-fast 或通过迁移工具转成 feature 参数。
- 缺少 `environmentMap.uri` 表示无环境；validator 可以跳过 background pass 或把 feature contribution 置零，但不得隐式创建默认 cubemap。
- 单色环境必须使用显式内置 URI，例如 `builtin:env/white_cube`，并注册 live 1x1 白色 cubemap 作为 `SkyboxMap`。
- equirect HDR/EXR、KTX2 cubemap、内置白 cubemap 都必须向 shader 暴露同名 `SkyboxMap`，shader 不需要 `hasTexture` 分支。
- scene loading 后续接入 Khronos neutral GGX 时，应显式区分 equirect HDR/EXR
  与 KTX2 cubemap；不能把 prefiltered cubemap 当作普通 HDR panorama 重新采样。
- `SkyboxMap` 不能作为 material-owned texture resource。
- 缺少 environment 时，graph source 可显式 absent；shader binding 不得靠 placeholder 假装满足。
- 如果 profile 允许无环境图，validator 必须把 feature intensity 置零或跳过 pass，并输出可诊断状态。

### R4: Read-Only Depth Attachment Contract

RenderPathGraph SHALL express read-only depth attachment usage for
`SkyboxBackground`.

要求：

- schema 允许 pass 声明 `depth.main` 是 depth-test input，而不是 depth target write。
- `sources` 包含 `depth.main` 时，binding name 可以为空；它表示 fixed-function depth attachment，不表示 sampled texture descriptor。
- `targets` 只包含真正写入的 graph resources；`SkyboxBackground` 不得把 `depth.main` 放进 `targets` 来绕过 schema 缺口。
- schema 必须允许同一个 graph resource 同时出现在 `sources` 和 `targets`，但只有当对应 attachment usage 是显式 read-write 或其他合法 read+write 模式时才成立。
- FrameGraph build / validation 必须区分 dependency read、attachment read-only、attachment write 和 attachment read-write；不能用“同名 resource 同时出现在 sources/targets”这个形状直接判为非法。
- RenderWorkCompiler / PipelineBuildDesc / backend dynamic rendering 必须保留 attachment usage 事实，不能通过 C++ pass-name 分支补齐。
- validator 必须拒绝同一 pass 将同一个 depth resource 声明为 read-only attachment 却又放进 writable `targets`。

### R5: Reflection Validation And Diagnostics

skybox shader 的 binding 与 graph/source/feature 必须一致。

测试应覆盖：

- shader 反射包含 `SkyboxMap` 和 environment feature UBO。
- graph 声明缺少 `feature.environmentLighting` 或 feature 缺少 `environmentMap.uri` 时 fail-fast。
- RenderFeature 缺少 required parameter 时 fail-fast。
- backend descriptor plan 缺少 live `SkyboxMap` 时 fail-fast，而不是渲染黑色并当作成功。
- scene 文件继续声明 `ambientColor` / `ambientIntensity` / `environment.uri` 等 environment 输入时不能满足正向路径；strict profile 应 fail-fast，证明单色环境源不再走 scene 字段双轨。
- 现有 Forward/Deferred 临时 constantColor lighting 代码不是本 REQ 的验收条件；实现时可以删除，也可以保持零贡献路径，最终 surface lighting 规则由 `REQ-073-g` 收口。

### R6: Remove Manual Skybox Material Injection

完成态默认 realtime path SHALL NOT 调用手写 skybox fullscreen material helper 作为正向路径。

允许保留的代码只能是：

- 被新 graph path 消费的内部 builder，并且输入完全来自 RenderPathGraph / RenderFeature / SceneResourceTable。
- named negative audit 中证明旧 helper 不再能正向渲染。

## 测试

- RenderPathGraph parser 测试：skybox pass 声明、sources/targets 和 feature dependency 被解析。
- RenderPathGraph parser / FrameGraph 测试：`depth.main` read-only attachment 被解析为 depth-test input，不产生重复 write；非法 read+write depth 声明被拒绝。
- RenderFeature parser 测试：environment lighting 参数缺失、未知字段、类型错误都会失败。
- RenderWorkCompiler 测试：skybox fullscreen input 生成 typed input 和 accepted `RenderInputDesc`。
- RenderFeature resource loader 测试：`environmentMap.uri: builtin:env/white_cube` 注册 live `SkyboxMap`；缺 URI 不隐式创建；scene-side environment 字段被拒绝或不能满足该依赖。
- Vulkan smoke：启用 `builtin:env/white_cube` + feature color/intensity 后输出非黑 skybox；关闭 `visibleInBackground` 后背景贡献消失。
- rg audit：`createSkyboxBackgroundMaterial`、root `skybox` shader URI、manual skybox material injection 不出现在 default positive path；scene-side `ambientColor` / `ambientIntensity` 只允许出现在 migration / negative-test / 073-g handoff 文档中。

## 修改范围

- `assets/render_paths/*`
- `assets/effects/*.render-feature.yaml`
- skybox shader URI / shader reflection tests
- RenderFeature -> descriptor binding path
- SceneResourceTable environment resources
- `common/environment_lighting.glsl` 或等价 shared shader lib
- Vulkan realtime skybox execution path
- Helmet / BMW environment smoke fixtures

## 边界与约束

- 物体表面参数仍然属于 material source / material instance，不放进 RenderFeature。
- pass/feature 级参数必须走 RenderFeature，不允许 C++ 硬编码。
- 073-f 正向路径不使用 `scene.environment`；EnvMap URI 与 shader 参数同属 `feature.environmentLighting`。
- 单色环境、HDR panorama、KTX2 cubemap 都是 EnvMap source variant，不能维护 ambient-only 双轨。
- 本 REQ 的 “directly visible” 指可见背景，不指物体表面 environment lighting；surface lighting 由 `REQ-073-g` 统一处理。
- backend 不能手动创建 material instance 来绕过 shader binding reflection。
- 不引入第二套 public graph / effect system。

## 依赖

- `REQ-073-e2`: RenderWorkCompiler / RenderInputDesc 单轨模型。
- `REQ-073-d`: RenderPath shader URI hard cut。

## 后续工作

- `REQ-073-g`: environment HDR async IBL bake and runtime lighting。
- `REQ-073-h`: reflection probe IBL extension。
- `REQ-073-i`: RenderFeature parameter architecture hard cut。

## 实施状态

已实施（2026-06-16）。

实现内容：

- 新增 `assets/effects/environment_lighting.render-feature.yaml`，由
  `feature.environmentLighting` 声明 `environmentMap.uri`、`color`、
  `intensity`、`rotation`、`visibleInBackground` 以及 shader
  binding/member 元数据；默认使用 Khronos neutral GGX KTX2 cubemap。
- Forward / Deferred 的 main 与 bloom graph 都新增 graph-authored
  `SkyboxBackground` pass：写 `hdr.color`，以 `depth.main` 作为
  read-only depth attachment，source 显式包含
  `feature.environmentLighting`。
- 新增 `common/environment_lighting.glsl` 与
  `render_paths/Skybox/skybox_background.{vert,frag}`；root `shader: skybox`
  不再作为正向 graph shader URI。
- RenderFeature parser 保存 `uri`、`binding`、`member`、`required` 等参数合同，
  并对 `environmentLighting.parameters.environmentMap.uri` 缺失 fail-fast。
- RenderPathGraph / FrameGraph / Vulkan dynamic rendering 保留
  `attachmentUsage`，支持 `depth-attachment-read-only`，避免把背景 pass 的
  depth test 误判为 depth write。
- `SceneResourceTable` 从 live `RenderFeature` 注册 `SkyboxMap` 与
  `EnvironmentLightingUBO`；`builtin:env/white_cube` 生成 live 1x1 cubemap，
  KTX2 texture EnvMap 由 RenderFeature parser adapter 预加载并通过 texture
  handle 解析，不使用 placeholder 资源。
- RenderWorkCompiler 对 `feature.environmentLighting` 的 shader reflection、
  `SkyboxMap`、`EnvironmentLightingUBO` member 与 feature 参数 schema 做一致性校验。
- `VulkanPostProcessBuilder::createSkyboxBackgroundMaterial()` 已从生产代码删除；
  `VulkanRealtimeRenderer` 使用 graph pass facts 加载 fullscreen skybox shader
  与 read-only depth 动态渲染状态。
- realtime output profile 不再单独执行 Forward 或手写 skybox 分支；它复用当前
  realtime FrameGraph，只把 `swapchain.color` 终点替换为同格式 offscreen
  attachment，然后直接 readback 最终 PNG，不再对导出结果做额外 CPU
  tone-mapping / gamma 处理。

验证结果：

- `cmake --build build --target CompileShaders test_render_resource_parsers test_render_path_graph_pass_contract test_render_work_compiler test_shader_compiler test_scene_resource_upload_view_v2 test_vulkan_post_process_builder lxe_editor`：通过。
- `ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_path_graph_pass_contract|test_render_work_compiler|test_shader_compiler|test_scene_resource_upload_view_v2|test_vulkan_post_process_builder)"`：6/6 通过。
- `cmake --build build --target test_lxe_editor_render_debug_dump && ctest --test-dir build --output-on-failure -R test_lxe_editor_render_debug_dump`：通过。
- `ctest --test-dir build --output-on-failure -R test_helmet_standard_pbr_realtime_smoke`：通过；该 smoke 校验正常 realtime export 的最终 PNG 背景非黑。
- normal realtime export 输出
  `artifacts/073-f/helmet_neutral_realtime_export.png` 与
  `artifacts/073-f/helmet_neutral_realtime_export.json`；统计为
  `averageLuminance=0.541954`、`litPixelCount=34362/36864`。
- legacy live editor FrameGraph `render debug dump hdr.color` 仅作为交叉验证使用，
  输出
  `artifacts/073-f/helmet_neutral_direct_with_background.png` 与
  `artifacts/073-f/helmet_neutral_direct_with_background_hdr_dump.png`；
  背景采样像素非黑，左上 100x100 平均亮度约 184/255。
- rg audit：`createSkyboxBackgroundMaterial` 和 `shader: skybox` 在 `src/`
  与 `assets/` 中无命中；剩余 `scene.environment`、`ambientColor`、
  `ambientIntensity`、`skyboxEnabled`、`IblBakeRenderer`、
  `bakeStaticEnvironment` 命中属于旧 scene 数据、迁移/negative audit 或
  `REQ-073-g` / `REQ-073-h` / `REQ-073-i` 后续工作。

剩余边界：

- Surface environment lighting / IBL contribution 由 `REQ-073-g` 负责；073-f
  只实现可见背景和共享 EnvMap feature/resource 合同。
- Reflection probe bake 仍由 `REQ-073-h` 负责。
- RenderFeature 参数架构 hard cut、post-process 参数彻底收口仍由
  `REQ-073-i` 负责。
- live dump / `render debug dump` 是 legacy 诊断路径，短期只用于交叉验证；
  后续 debug-path 应显式声明需要导出的中间 attachment，并最终 hard cut
  legacy live dump。
