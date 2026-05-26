# REQ-049-a: PBR IBL Material Contract

> 2026-05-26 新增：本 REQ 把现有 PBR shader/material 从“局部 Cook-Torrance 直射光实验”推进到可消费 IBL 的正式材质合同。

## 背景

当前已有 `assets/shaders/glsl/pbr.vert`、`pbr.frag` 和 `assets/materials/pbr_gold.material`。`pbr.frag` 已包含 GGX NDF、Smith geometry、Schlick Fresnel 和 metallic/roughness 参数，但仍有几个缺口：

- ambient 是固定 `vec3(0.03)`，不是 IBL。
- shader 内做 tone mapping 和 gamma，和标准 post stack 冲突。
- glTF metallic/roughness、normal、AO 等贴图尚未完整接入 editor 场景。
- IBL cubemap / BRDF LUT 尚无系统级 binding 合同。

本 REQ 建立 PBR material 与 scene-level IBL resources 的稳定接口。

## 目标

1. PBR shader 输出线性 HDR color。
2. PBR shader 使用 scene-level IBL resources。
3. metallic/roughness/ao/normal/baseColor 贴图绑定形成清晰合同。
4. `.material` 能声明 PBR 参数和材质自有纹理。
5. glTF PBR metadata 可以桥接到 PBR material。

## 需求

### R1: PBR shader 移除最终显示映射

`pbr.frag` 不再执行最终 tone mapping 和 gamma。

要求：

- 输出线性 HDR color。
- exposure、tone mapping、gamma 只在 `REQ-046-a` 的 post-process pass 中执行。
- shader 中保留直接光 Cook-Torrance BRDF。

### R2: Scene-level IBL binding contract

定义 PBR shader 的 IBL system-owned bindings。

建议 binding：

| Binding | Type | Ownership |
|---|---|---|
| `IrradianceMap` | `TextureCube` | scene/system-owned |
| `PrefilteredEnvMap` | `TextureCube` | scene/system-owned |
| `BrdfLut` | `Texture2D` | scene/system-owned |
| `EnvironmentUBO` | `UniformBuffer` | scene/system-owned |

要求：

- 这些 binding 不由 `.material resources` 设置。
- scene-level resource injection 负责把 bake 产物拼进 draw item。
- 缺少 IBL 时提供默认黑色 irradiance/prefilter 和中性 LUT，或关闭 IBL contribution。

### R3: Specular IBL split-sum

PBR shader 支持 specular IBL。

要求：

- 使用反射向量采样 `PrefilteredEnvMap`。
- roughness 映射到 mip level。
- 使用 `BrdfLut` 进行 split-sum。
- metal surface 的 F0 颜色来自 baseColor / metallic 混合。

### R4: Diffuse IBL

PBR shader 支持 diffuse irradiance。

要求：

- 使用 normal 采样 `IrradianceMap`。
- diffuse contribution 受 baseColor、metallic 和 AO 影响。
- metallic 越高 diffuse 越弱。

### R5: PBR material-owned texture set

正式定义 PBR material 自有资源。

建议 binding：

| Binding | Type | Meaning |
|---|---|---|
| `albedoMap` | `Texture2D` | base color |
| `normalMap` | `Texture2D` | tangent-space normal |
| `metallicRoughnessMap` | `Texture2D` | glTF-style B=metallic, G=roughness |
| `aoMap` | `Texture2D` | ambient occlusion |
| `emissiveMap` | `Texture2D` | emissive |

要求：

- `.material` 可提供默认纹理或 placeholder。
- variant 规则确保 shader 中不存在的 binding 不被 `.material` 设置。
- normal map 仍要求 mesh tangent 可用；无 tangent 时 material loader 应禁用 normal variant 或给出诊断。

### R6: glTF PBR bridge

`lxe_editor` 的 glTF material bridge 支持 PBR material。

要求：

- DamagedHelmet 的 baseColor、metallicRoughness、normal、AO、emissive metadata 可桥接到 PBR material。
- 没有 tangent 时 normal map 不自动启用。
- Blinn-Phong bridge 仍可存在作为非 PBR fallback，但 PBR demo 不再使用它。

### R7: 测试覆盖

至少覆盖：

- `pbr.frag` 不包含最终 tone mapping/gamma 逻辑。
- PBR shader reflection 包含 IBL bindings。
- material loader 能加载 PBR gold material。
- scene-level IBL resources 能进入 PBR draw item descriptor resources。
- glTF PBR metadata 能桥接到 PBR material 绑定。
- metallic=1/roughness 低的测试材质能采样 prefiltered env map。

## 修改范围

- `assets/shaders/glsl/pbr.*`
- `assets/materials/pbr_gold.material`
- `src/core/asset/shader_binding_ownership.*`
- `src/core/scene/`
- `src/demos/lxe_editor/scene_builder.*`
- `src/infra/material_loader/`
- `src/infra/mesh_loader/gltf_mesh_loader.*`
- `src/test/integration/`

## 边界与约束

- 本 REQ 不实现 IBL bake；只消费 `REQ-048-a` 的产物。
- 本 REQ 不实现 deferred/G-buffer。
- 本 REQ 不实现 clear coat、anisotropy、subsurface 或 transmission。
- 本 REQ 不把 IBL resources 写入 `.material resources`。

## 依赖

- `REQ-046-a`
- `REQ-047-a`
- `REQ-048-a`
- `openspec/specs/material-system/spec.md`
- `openspec/specs/mesh-loading/spec.md`

## 后续工作

- `REQ-050-a`：IBL metal sphere test scene。
- `REQ-A-local-reflection-probe-bake`：局部反射探针扩展。

## 实施状态

Draft，未实施。
