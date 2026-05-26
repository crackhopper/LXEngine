# REQ-048-a: IBL GPU Bake Pipeline

> 2026-05-26 新增：本 REQ 在加载期用 GPU 把 HDR 环境图预处理成实时 PBR/IBL 可消费的资源。

## 背景

IBL 不能在每个 fragment 中直接对整张 HDR 环境图积分。Filament 文档将 IBL 预处理拆为 specular pre-filter / split-sum approximation 与 diffuse irradiance；Unreal 和 Godot 也都把环境/探针数据转成可在运行时低成本采样的 cubemap 或 probe 资源。

本 REQ 只做静态 distant environment IBL：从 `assets/env/*.hdr` 输入生成全局环境资源。local reflection probe capture、box projection、多 probe blending 放到后续候选需求。

## 目标

1. 从 equirectangular HDR 生成 skybox cubemap。
2. 生成 diffuse irradiance cubemap。
3. 生成 specular prefiltered radiance mip chain。
4. 生成 BRDF integration LUT。
5. 把 bake 产物作为 scene-level IBL resources 暴露给材质。

## 需求

### R1: IBL bake resource model

新增 IBL 环境资源对象。

要求：

- 输入记录 HDR equirectangular texture。
- 输出至少包含：
  - `skyboxCubemap`
  - `irradianceCubemap`
  - `prefilteredRadianceCubemap`
  - `brdfLut`
- 输出资源可作为 `IGpuResource` 或 scene-level resource 被 descriptor 路由使用。
- 资源名称稳定，例如 `SkyboxMap`、`IrradianceMap`、`PrefilteredEnvMap`、`BrdfLut`。

### R2: Equirectangular to cubemap GPU pass

加载期执行 GPU pass，把 HDR panorama 转为 cubemap。

要求：

- 使用 fullscreen/cube render pass 或等价方式渲染 6 个 cubemap faces。
- 输出 float cubemap。
- face orientation 必须稳定并有测试或 debug dump 可验证。
- 输出可用于 skybox 背景和后续 irradiance/prefilter pass。

### R3: Diffuse irradiance bake

生成低分辨率 irradiance cubemap。

要求：

- 对输入 skybox cubemap 进行 diffuse hemisphere convolution。
- 分辨率可低于 skybox，例如 32 或 64。
- shader 和资源命名与 PBR shader 消费端一致。

### R4: Specular prefilter bake

生成按 roughness 分布的 prefiltered radiance mip chain。

要求：

- 输出 cubemap mip chain。
- mip level 对应 roughness 从 0 到 1。
- 使用 GGX importance sampling 或等价近似。
- shader 消费端可用 `textureLod(prefilteredEnvMap, R, lod)`。

### R5: BRDF LUT bake

生成 2D BRDF integration LUT。

要求：

- LUT 格式为 float 2D texture。
- 输入维度为 `NdotV` 和 roughness。
- PBR shader 可通过 LUT 进行 split-sum specular IBL。

### R6: Scene-level IBL resource injection

把 bake 产物注入 forward/post 渲染路径。

要求：

- IBL resources 属于 scene-level/system-owned resources，不属于单个 material instance。
- `RenderQueue::buildFromScene()` 或 scene-level resource 路径能把 IBL resources 合并进需要的 draw item。
- 若场景未配置 IBL，系统使用黑色/默认资源或跳过 IBL contribution，不能崩溃。

### R7: 测试覆盖

至少覆盖：

- IBL bake pipeline 可以从 `assets/env/studio_small_03_2k.hdr` 创建所有输出资源。
- FrameGraph/renderer 能执行 bake pass 并产出 cubemap attachment/texture。
- irradiance / prefilter / BRDF LUT shader 编译通过。
- bake 产物 descriptor binding name 与 PBR shader 约定一致。
- 可 dump 至少一个 cubemap face 或 BRDF LUT 用于人工验证。

## 修改范围

- `src/core/scene/`
- `src/core/frame_graph/`
- `src/backend/vulkan/`
- `src/backend/vulkan/details/device_resources/texture.*`
- `src/infra/texture_loader/`
- `assets/shaders/glsl/`
- `assets/materials/`
- `src/test/integration/`

## 边界与约束

- 本 REQ 不实现 local reflection probe。
- 本 REQ 不实现 probe blending。
- 本 REQ 不要求实时动态更新 IBL。
- 本 REQ 不要求 compute shader；若现有 backend 更适合 render-pass bake，可使用 graphics pipeline。

## 依赖

- `REQ-047-a`
- `REQ-046-a`
- `openspec/specs/frame-graph/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`

## 后续工作

- `REQ-049-a`：PBR material 消费 IBL resources。
- `REQ-A-local-reflection-probe-bake`：后续本地 probe capture、influence volume、多 probe blending。

## 实施状态

实施中。

已落地：

- IBL scene-level resource model 已补充 `skyboxCubemap`，并保持 `SkyboxMap`、`IrradianceMap`、`PrefilteredEnvMap`、`BrdfLut`、`EnvironmentUBO` 这些稳定 binding name。
- 已新增 IBL bake shader 合同：equirectangular HDR -> cubemap、irradiance convolution、prefiltered environment、BRDF LUT。当前先锁定 shader 编译和 descriptor ABI。
- VulkanTexture 已支持 cubemap/texture 单 mip、单 layer 的 RAII image view，可用于后续 render pass 按 face/mip 写入 bake 目标。

仍待落地：

- Vulkan renderable cubemap/mip-chain attachment API 仍需接入 framebuffer/render pass 执行层。
- GPU bake pass 执行、baked VulkanTexture adoption/register 路径。
- cubemap face / BRDF LUT dump 与方向验证。
