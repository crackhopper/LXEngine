# REQ-076-a: Advanced Offline Path Tracing Reference

> 2026-06-14 重排：本需求原为 `REQ-057-a`，现放到 `REQ-075-a` 之后。它不再服务旧 offline MVP，而是在 Material v3、OfflineRT RenderPathGraph、package/cleanup 和 offline/realtime equivalence 的新架构稳定后，实现更完整的光追 reference。

## 背景

当前代码已有 Vulkan/software compute direct offline renderer、`OfflineRenderJob`、SceneResourceTable upload view、EXR/PNG 输出、OfflineRT shader 基础和 offline/realtime 对比工具。`REQ-073-g/h` 会把 OfflineRT 默认路径硬切到 RenderPathGraph 配置；`REQ-075-a` 会验证新架构下 realtime/offline direct 输出一致性。

在这些基础稳定之前，直接实现完整 path tracing 容易把算法问题和架构 bridge 混在一起。因此本 REQ 后置到 `REQ-075-a` 之后，目标变成“在干净架构上扩展更完整的光追效果”，而不是继续修补旧 MVP。

## 目标

1. 在 OfflineRT RenderPathGraph 默认路径上实现更完整的 path tracing reference。
2. 对齐 Material v3 / PBRT source contract 中已经标记 supported 的 BSDF。
3. 支持 multi-bounce、progressive accumulation、environment importance sampling 和可复现随机种子。
4. 输出 AOV 和 metadata，帮助分析 realtime/offline 差异。
5. 产出一张可作为质量基线的高质量离线 reference 图。

## 非目标

- 不恢复旧 `OfflineShaderProvider`、`offlineShader` side channel 或 hardcoded FrameGraph。
- 不实现 Vulkan hardware ray tracing；如需要，应单独起新 REQ。
- 不实现 denoiser、spectral rendering、bidirectional path tracing 或 MLT。
- 不实现 editor 调用入口；editor 入口由 `REQ-076-b` 承接。
- 不定义新的材质 source contract；PBRT 高阶材质支持由 `REQ-073-i` 决定。

## 需求

### R1: Material v3 / PBRT BSDF Reference

离线 path tracer SHALL 消费 Material v3 source records、SceneResourceTable upload view 和 RenderPathGraph pass contract。

要求：

- 使用与 realtime 同源的 material source reflection hash。
- 支持 metallic/roughness workflow 的 GGX / Smith / Schlick 基础 BRDF。
- 对 `REQ-073-i` 标记 supported 的 PBRT source，提供对应 reference path。
- 对未支持 BSDF 输出明确 unsupported diagnostic，不静默近似。
- AOV/metadata 记录 material source URI、source reflection hash 和 selected BSDF path。

### R2: Multi-bounce Path Tracing

支持 `maxBounce > 1`。

要求：

- 使用可复现随机采样。
- 支持 diffuse/specular 路径。
- 支持 Russian roulette 或固定深度截断；首版可固定深度。
- direct、indirect、environment contribution 在 shader/metadata/AOV 中可区分。
- directional light direct sampling 至少与当前 direct renderer 对齐；point/spot/area light 可后续扩展。

### R3: Progressive Accumulation

支持渐进式累积。

要求：

- sample count 可配置。
- seed 可配置且默认稳定。
- 支持中间结果输出或 future progress callback。
- 输出 metadata 记录 samples、maxBounce、seed、integrator、RenderPathGraph asset。

### R4: Environment Importance Sampling

HDR environment 不能只做均匀采样。

要求：

- 构建 environment luminance distribution。
- 支持亮点区域 importance sampling。
- 明确 BSDF sampling 与 environment sampling 的组合策略。
- 首版可不做 MIS，但必须为 MIS 留接口和 metadata 字段。

### R5: AOV 输出

至少支持：

| AOV | 含义 |
|---|---|
| beauty | 最终 radiance |
| albedo | base color / diffuse albedo |
| normal | world-space normal |
| depth | camera depth |
| roughness | material roughness |
| metallic | material metallic |
| direct | direct lighting contribution |
| indirect | indirect bounce contribution |
| environment | sky/environment contribution |

EXR 输出可以使用多通道或多文件，但 metadata 必须记录 AOV 命名和尺寸。

### R6: Reference Scene And Output

最终验收需要一张高质量离线 ray tracing reference 图。

场景形态：

- 使用 `REQ-053-b` assets-downloader 管理外部 HDRI、模型和 PBR/PBRT texture。
- 至少包含一个直接光源和一个 environment 输入。
- 使用 supported Material v3 / PBRT source。
- 使用 multi-sample、multi-bounce path tracing。

输出：

- scene-linear EXR ground truth。
- tone-mapped PNG preview。
- metadata 记录 scene/profile/assets/samples/maxBounce/seed/material sources。

## 测试

- fixed seed 下输出稳定。
- roughness/metallic 改变 specular 结果。
- samples 增加时 variance 下降的统计检查。
- environment importance sampling 能命中高亮区域。
- AOV buffer 非空且维度正确。
- unsupported PBRT source fail-fast 且 diagnostic 包含 source URI。

## 修改范围

- OfflineRT RenderPathGraph / compute shader。
- offline integrator sampling utilities。
- SceneResourceTable upload view 的 material/texture 消费。
- accumulation buffers。
- EXR/AOV writer。
- tests / fixtures。

## 边界与约束

- 只在 `REQ-073-g/h` hard cut 后的新 OfflineRT 默认路径上实现。
- 不新增旧式 offline material pass。
- 不把 research integrator registry 作为前置；本 REQ 先交付一个明确 reference integrator。
- 不要求 editor UI。

## 依赖

- `REQ-073-g`: OfflineRT RenderPathGraph compute path。
- `REQ-073-h`: OfflineRT config hard cut and smoke。
- `REQ-073-i`: specialized PBRT BSDF contracts，只有选择 PBRT 高阶 source 时需要。
- `REQ-075-a`: offline/realtime equivalence on new architecture。

## 后续工作

- `REQ-076-b`: editor offline render integration。
- 未来 bake route 可基于本 reference integrator 生成 bake targets，届时重新起 active REQ。
- Vulkan hardware ray tracing backend 如需要，单独起新 REQ。

## 实施状态

2026-06-14 重排后状态：后置 active，未实施。

当前已有 direct offline renderer、SceneResourceTable 输入、EXR/PNG 输出和 OfflineRT shader 基础；但 multi-bounce path tracing、MIS/environment sampling、reference AOV、PBRT 高阶材质 reference 和高质量基线输出仍未完成。
