# REQ-050-a: IBL Metal Sphere Test Scene

> 2026-05-26 新增：本 REQ 搭建一个能直接观察 PBR + IBL 效果的测试场景，以金属球反射 HDR 环境作为首个验收目标。

## 背景

底层 Post/HDR/IBL/PBR 能力需要一个稳定可运行的视觉入口。当前 `lxe_editor` 已有 builtin sphere primitive、camera/light/ground 构建、scene document、截图和 debug dump 路径。`assets/env/studio_small_03_2k.hdr` 已存在，但还没有被渲染管线消费。

本 REQ 不追求复杂关卡，也不做 local probe。目标是一个可复现、可截图、能明确证明金属材质在使用 IBL 的场景。

## 目标

1. 新增 IBL metal sphere scene preset。
2. 使用 HDR environment 作为 IBL 输入。
3. 金属球通过 PBR + IBL 反射环境。
4. 场景可在 `lxe_editor` 打开并通过 MCP/recording/use case 验证。
5. 提供可诊断的截图或 framebuffer dump 验收路径。

## 需求

### R1: 场景资产

新增 scene 文件。

要求：

- 包含 active camera。
- 包含 directional light，用于保留直射光对比。
- 包含 ground 或简单参考物。
- 包含一个 builtin sphere primitive。
- sphere 使用 PBR gold/metal material。
- scene 配置引用 `assets/env/studio_small_03_2k.hdr` 或 bake 后的 environment asset。

### R2: Skybox / environment background

场景能显示或间接体现 HDR environment。

要求：

- 支持采样 scene-level `SkyboxMap` 的 skybox 背景。
- 背景经过 post-process tone mapping。
- 背景与 IBL bake 输入一致，便于检查反射方向。

### R3: 金属球材质参数

金属球使用高金属度、低 roughness 参数。

建议默认值：

| Parameter | Value |
|---|---|
| `metallicFactor` | `1.0` |
| `roughnessFactor` | `0.15` 到 `0.3` |
| `baseColorFactor` | 金色或中性金属色 |
| `ao` | `1.0` |

要求：

- 参数通过 PBR material-owned UBO 设置。
- IBL cubemap/LUT 通过 scene-level resources 注入。
- shader 内不做 tone mapping。

### R4: Editor / command 入口

`lxe_editor` 提供稳定加载入口。

要求：

- scene catalog 或 project session 可发现该测试场景。
- 场景加载后不需要手工创建节点即可看到效果。
- camera 初始位置能直接看到金属球和背景。

### R5: 验证与截图

提供自动或半自动验证方式。

要求：

- use case 能启动 editor、加载 scene、等待渲染、截图。
- 截图中金属球不能是纯黑、纯白或无反射的 flat color。
- 可 dump `scene.hdrColor` 和 post-process output 用于排查。
- 如果当前环境没有 Vulkan/video device，测试应明确 skip 原因。

### R6: 测试覆盖

至少覆盖：

- scene document 能加载并 round-trip environment 配置。
- metal sphere node 使用 PBR material。
- renderer 初始化后 compiled FrameGraph 包含 Forward HDR、PostProcess 和 overlay 路径。
- scene-level IBL resources 被注入到 sphere draw item。
- screenshot/use case 文档化并可在有 Vulkan 环境时运行。

## 修改范围

- `assets/scenes/`
- `assets/materials/`
- `src/demos/lxe_editor/`
- `src/core/scene/`
- `src/test/integration/`
- `notes/use_cases/` 或现有 use-case/recording 目录（按当前项目事实选择）

## 边界与约束

- 本 REQ 不实现 local reflection probe。
- 本 REQ 不要求多个金属材质矩阵。
- 本 REQ 不要求 artist UI 编辑 IBL 参数。
- 本 REQ 不引入外部私有资产。

## 依赖

- `REQ-046-a`
- `REQ-047-a`
- `REQ-048-a`
- `REQ-049-a`

## 后续工作

- `REQ-051-a`：从 0 到 1 教程。
- `REQ-A-local-reflection-probe-bake`：在局部场景中捕获和混合 reflection probe。

## 实施状态

已完成。

已落地：

- 新增 `assets/scenes/ibl_metal_sphere.scene.yaml`，包含 gameplay camera、directional light、ground reference plane 和 builtin sphere。
- Scene document 已支持 `scene.environment`，可记录 HDR URI、skybox 开关、IBL 强度和 roughness mip count，并能 round-trip。
- Scene runtime 读取启用的 environment 后会加载 `hdrUri` 指向的 HDR texture，并生成 CPU preview/fallback IBL resources；同时保留 `EquirectangularMap` 输入供 VulkanRenderer 初始化时执行 GPU bake。渲染路径会优先消费 baked `SkyboxMap`、`IrradianceMap`、`PrefilteredEnvMap` 和 `BrdfLut`。
- Renderer 已在 Forward HDR pass 内追加 skybox fullscreen draw：`skybox` shader 采样 scene-level `SkyboxMap` cubemap，并按 `EnvironmentUBO` intensity 输出到 `scene.hdrColor`；背景随后和场景几何一起进入标准 PostProcess tone mapping。
- 金属球使用 `assets/materials/pbr_gold.material`，runtime 测试覆盖 PBR 材质参数、scene-level IBL resources 注入和 environment 保存回写。
- Runtime 测试覆盖 IBL metal sphere 的非黑 skybox preview 数据、skybox cubemap 非 1x1 方向数据和 prefiltered roughness mip chain；renderer/framegraph 测试覆盖 HDR attachment dump 能力。
- `SkyboxMap` 已进入 system-owned binding 合同，Forward skybox draw 可以消费 scene-level cubemap，不会把背景 cubemap 误归入 material-owned resource。
- 新增 `pbr_ibl` project template，初始化后 scene catalog 可通过 `ibl_metal_sphere` id 发现并打开该场景。
- 新增 `notes/use_cases/lxe_editor/verify-pbr-ibl-metal-sphere.md`，记录通过 MCP/editor 命令加载场景、开启 preview、dump `scene.hdrColor` 和截图/目检的验收流程。

验收补充：

- 截图/use-case 已文档化；远端实际执行和截图归档由用户最终验证阶段完成。
