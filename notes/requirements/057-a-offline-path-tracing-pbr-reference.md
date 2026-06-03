# REQ-057-a: Offline Path Tracing PBR Reference

> 2026-06-01 新增：本 REQ 将 MVP 扩展为可对比实时 PBR 的 reference renderer。当前仍在讨论中，未开始。

## 背景

`REQ-054-b` 只要求 camera ray + 简单材质 + environment 的最小闭环。`REQ-056-a`
补齐 PBR 纹理/材质数据管线。为了成为真正可用的 ground truth，本 REQ 在这些
数据能力之上实现更完整的 PBR BRDF、多 bounce、progressive accumulation 和
environment importance sampling。

本 REQ 对应用户优先级中的 B 阶段前半：可对比 PBR reference。

## 目标

1. 对齐实时 PBR 的 Cook-Torrance 材质语义。
2. 支持多 bounce path tracing。
3. 支持 progressive accumulation。
4. 支持 environment importance sampling。
5. 输出 debug AOV，帮助分析实时/离线差异。
6. 明确区分直接光、间接光和 environment/skybox 输入。

## 需求

### R1: Cook-Torrance PBR reference

离线 shader 消费 `REQ-056-a` 写入 `MaterialInstance material parameters` / GpuScene 的纹理与材质数据，
并使用与 realtime PBR 对齐的 BRDF 语义：

- GGX NDF
- Smith geometry
- Schlick Fresnel
- metallic/roughness workflow
- baseColor / normal / ORM / AO / emissive texture

要求：

- 与 `assets/shaders/glsl/pbr.frag` 的核心参数解释一致。
- 差异必须记录在文档中。

### R2: 多 bounce path tracing

支持 `maxDepth > 1`。

要求：

- 使用随机采样。
- 支持 diffuse/specular 路径。
- 支持 Russian roulette 或固定深度截断；首版可固定深度，后续优化。
- 每 pixel 多 sample accumulation。

### R2.1: 直接光与间接光

离线 reference 必须同时考虑直接光与间接光。

要求：

- 支持 directional light 的直接光采样。
- 后续扩展 point / spot / area light。
- `REQ-054-b` 已提供 directional hard shadow；本 REQ 扩展软阴影、area light
  采样和更完整的 direct-light integration。
- direct lighting、indirect lighting、environment lighting 的贡献在 shader/metadata/AOV 中可区分。
- skybox/environment 被视为远场 radiance 输入，不等同于完整 indirect lighting 解。
- realtime IBL 可以用 offline path tracing 结果校验，但 offline reference 不应只依赖 IBL 近似。

### R3: Progressive accumulation

支持渐进式累积。

要求：

- sample count 可配置。
- seed 可配置或可复现。
- 默认 seed 规则继承 `REQ-054-b`：`hash(seed, pixel, sample)` 或等价稳定规则。
- 支持中间结果输出或 future progress callback。
- 输出 metadata 记录 samples/maxDepth/seed。

### R4: Environment importance sampling

HDR environment 不能只做均匀采样。

要求：

- 构建 environment sampling distribution。
- 支持亮点区域 importance sampling。
- environment sampling 与 BSDF sampling 的组合策略明确。
- MVP 可先不做 MIS，但必须为 MIS 留接口。

### R5: AOV 输出

至少支持：

| AOV | 含义 |
|---|---|
| beauty | 最终 radiance |
| albedo | base color |
| normal | world-space normal |
| depth | camera depth |
| roughness | roughness |
| metallic | metallic |

EXR 输出可以使用多通道或多文件，具体由 `REQ-055-a` 的 writer 能力扩展。

### R6: 测试覆盖

覆盖：

- fixed seed 下输出稳定。
- roughness/metallic 改变 specular 结果。
- samples 增加时 variance 下降的统计检查。
- environment importance sampling 可命中高亮区域。
- AOV buffer 非空且维度正确。

### R7: Demo 2 - 高质量离线 ray tracing 渲染图

本 REQ 的最终用户可见验收物是一张高质量离线 ray tracing reference 图。

场景形态：

- 由 `REQ-053-b` assets-downloader 管理外部 HDRI、模型和 PBR texture。
- 使用 `REQ-056-a` 支持的纹理材质。
- 至少包含一个直接光源，例如 directional light。
- 包含 skybox/environment 作为远场 radiance 输入。
- 使用多 sample、多 bounce path tracing。

输出：

- scene-linear EXR ground truth。
- tone-mapped PNG preview。
- metadata 记录 scene/profile/assets/samples/maxDepth/seed。

验收重点：

- 画面质量明显高于 `REQ-054-b` 的 MVP 基准图。
- direct / indirect / environment 贡献可通过 AOV 或参数开关分析。
- 输出可作为实时 renderer 的视觉和数值参考。

## 修改范围

- offline integrator shader
- GpuScene material/texture data 的消费逻辑
- accumulation buffers
- random sampling utilities
- EXR/AOV writer
- tests

## 边界与约束

- 本 REQ 不实现 denoiser。
- 本 REQ 不要求 Vulkan hardware RT。
- 本 REQ 不实现 spectral rendering。
- 本 REQ 不实现 bidirectional path tracing / MLT。
- 本 REQ 不要求 editor integration。

## 依赖

- `REQ-054-a`
- `REQ-055-a`
- `REQ-056-a`

## 后续工作

- `planned/` 中的 bake route 文档记录未来基于 reference integrator 生成 bake targets 的路线；未来执行时重新取 active REQ 编号。
- `REQ-059-a` 把 integrator 扩展成研究 sandbox。

## 实施状态

讨论中，未开始。
