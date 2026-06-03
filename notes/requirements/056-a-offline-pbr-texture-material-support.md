# REQ-056-a: Offline PBR 纹理材质支持

> 2026-06-01 新增：本 REQ 紧随 MVP，补齐离线渲染质量最关键的 PBR 纹理支持。当前仍在讨论中，未开始。

## 背景

`REQ-054-b` 的 MVP 为了尽快走通 Vulkan compute 离线渲染，只支持常量 PBR 参数和简单 albedo/baseColor 纹理。但真实渲染质量高度依赖更完整的纹理集：normal、metallic/roughness、AO、emissive 都会显著影响 ground truth 的可信度。

因此完整纹理材质不进入 MVP，但必须作为紧随其后的需求。

## 目标

1. Offline renderer 支持 PBR material-owned texture set。
2. 对齐实时 PBR shader 的材质数据合同。
3. 支持 glTF 常见 PBR 纹理。
4. 为后续 path tracing reference 打好材质数据基础。
5. 扩展 `REQ-054-b` 定义的 `MaterialInstance material parameters`，而不是绕过它。
6. 只负责纹理/材质数据进入 GPU，不负责提升积分器或多 bounce 渲染算法。

## 需求

### R1: PBR texture set

支持以下 texture binding：

| Binding | 含义 |
|---|---|
| `albedoMap` | base color |
| `normalMap` | tangent-space normal |
| `metallicRoughnessMap` | glTF-style B=metallic, G=roughness |
| `aoMap` | ambient occlusion |
| `emissiveMap` | emissive |

要求：

- `albedoMap` 已在 `REQ-054-b` 提供简单支持；本 REQ 负责把它纳入完整 texture table / color space / sampler 规则。
- 没有纹理时使用 material scalar/default。
- texture 和 scalar 的组合规则与 realtime PBR 尽量一致。
- 未支持的 texture 不能静默当成黑图，应有诊断或明确 fallback。
- texture 引用先进入 `MaterialInstance material parameters.textureRefs`，再由 `GpuSceneBuilder`
  编译成 GPU texture table index。

### R2: GPU texture table

Offline GpuScene 需要可索引 texture table。

要求：

- material buffer 中存 texture index 或 sentinel。
- compute shader 能按 material 查 texture。
- 使用固定上限 descriptor array；不引入 bindless。
- 支持 sampler 基本策略：linear repeat / clamp。
- 本 REQ 的 shader 改动只验证 texture lookup 与材质参数组合，不引入 path tracing 积分器逻辑。

### R3: UV 与 tangent 数据

要求：

- triangle 数据包含 UV。
- normal map 需要 tangent basis。
- 缺 tangent 时可以：
  - 使用当前 SceneBuilder 的 tangent 生成能力；
  - 或禁用 normal map 并输出诊断。

策略必须明确，不能随机使用错误 tangent。

### R4: Texture loading 与格式转换

复用当前 texture loader。

要求：

- 支持 sRGB/baseColor 与 linear data 的区分。
- normal/ORM/AO/emissive 的 color space 规则明确。
- GPU 上传格式和 shader sampling 一致。

### R5: glTF PBR bridge

离线 renderer 必须能消费当前 editor/runtime 已经桥接的 glTF PBR 资源。

至少覆盖 DamagedHelmet：

- baseColor
- metallicRoughness
- normal
- occlusion
- emissive

### R5.1: Cache material bridge

离线材质编译器必须能消费 `REQ-053-b` 生成的 `converted/material.yaml`。

要求：

- 支持 `model: pbr-metallic-roughness`。
- 所有 texture URI 通过统一 asset resolver 解析 `cache://`。
- color space 与 channel mapping 按 `material.yaml` 声明进入 `MaterialInstance material parameters`。
- 缺失纹理使用 `defaults` 中的 scalar fallback。

### R6: 测试覆盖

覆盖：

- material texture indices 写入 GpuScene。
- `material.yaml` 能编译成 `MaterialInstance material parameters`。
- baseColor texture 影响输出颜色。
- metallicRoughness texture 影响 roughness/metallic。
- normal map 缺 tangent 时有明确行为。
- DamagedHelmet 离线编译时包含 PBR 纹理。

### R7: Demo 2 材质资产支撑

本 REQ 负责让高质量资产 demo 具备可信材质表现。

要求：

- 能消费 `REQ-053-b` 下载/转换的 PBR texture set。
- 高质量 demo scene 中至少一个模型或地面材质使用 baseColor、normal、metallic/roughness 或 AO 纹理。
- 离线输出与禁用纹理的输出有可观察差异。
- 缺失纹理时诊断应指向转换后的 engine path 和 cache asset id。

## 修改范围

- `src/core/asset/`
- `src/infra/material_loader/`
- `src/infra/texture_loader/`
- `src/infra/mesh_loader/gltf_mesh_loader.*`
- offline scene compiler / GpuScene builder
- Vulkan compute descriptor/pipeline
- `assets/shaders/glsl/`
- tests

## 边界与约束

- 本 REQ 不实现多 bounce。
- 本 REQ 不实现 Cook-Torrance reference BRDF 的完整积分策略；该工作进入 `REQ-057-a`。
- 本 REQ 不实现 progressive accumulation、environment importance sampling 或 AOV。
- 本 REQ 不实现 clear coat、transmission、subsurface、anisotropy。
- 本 REQ 不实现 bindless texture；固定上限 descriptor array 是当前约束。
- 本 REQ 不要求 editor UI。

## 依赖

- `REQ-054-a`
- `REQ-055-a`
- `REQ-049-a`

## 后续工作

- `REQ-057-a` 在完整 PBR path tracing 中消费这些纹理。

## 实施状态

讨论中，未开始。
