# REQ-056-a: 共享 PBR 纹理材质加载与离线/实时等价验证

> 2026-06-04 调整：本 REQ 不再是 offline-only 材质补丁。目标改为建立一套 editor、realtime profile output、offline renderer 共同使用的 glTF/PBR 资产加载路径，并用 DamagedHelmet 做无阴影、无 IBL、无 GI 的 PBR 直接光像素级等价验证。场景通过 instance 绑定多套带 tag 的材质，渲染流程按 tag 选择材质；没有对应 tag 的物体不参与该流程。

## 背景

`REQ-054-b` 的 offline MVP 走通了 Vulkan compute 离线渲染，但当前链路仍偏诊断用途：offline scene loader 主要消费 builtin primitive，`offline_primary_ray.comp` 使用简化 shading，PBR texture set 没有通过统一 GPU texture table 进入离线 shader。

实时路径已经能在 editor/runtime 中把本地 DamagedHelmet glTF 的 PBR metadata 桥接到普通 `MaterialInstance`，覆盖 albedo、metallic/roughness、normal、AO、emissive；但如果 editor、realtime 和 offline 各自维护材质解释逻辑，后续 ray tracer 会出现“实时能加载、离线不能加载”或“同一个材质在两边含义不同”。

因此本 REQ 的核心不是给 offline 单独补纹理，而是把 glTF mesh + material bridge 收敛成全局共享加载逻辑，让 editor、realtime profile output 和 offline renderer 从同一份 scene asset path、同一组 instance material tags 到同一份 `SceneResourceTable` 数据合同。

## 目标

1. 建立全局共享的 glTF mesh + PBR material bridge，editor、realtime、offline 都复用它。
2. 让 scene 中普通 glTF mesh URI 能进入 offline loader，不再只支持 builtin primitive。
3. 支持 DamagedHelmet 完整 PBR texture set：albedo、normal、metallic/roughness、AO、emissive。
4. 让 `SceneResourceTable` / offline upload view 能表达完整 PBR texture table index 和 scalar fallback。
5. 抽出 shader common PBR direct-light 函数，让 realtime `pbr.frag` 和新增 offline PBR direct ray shader 使用同一套公式。
6. 新增无 shadow、无 IBL、无 GI 的 Helmet PBR 等价场景，生成 realtime/offline linear EXR 并用 `lxe_compare_exr` 做像素级阈值比较。
7. 保留原 offline MVP shader 和原 MVP compare/diagnostic 流程可运行，不把旧 `offline_primary_ray.comp` 直接改成新 PBR 验证 shader。
8. 高清 Helmet 场景绑定三套材质 tag：`offline-pbr`、`realtime-pbr`、`realtime-blinnphong`，用于输出 1024x1024 的 offline PBR、realtime PBR、realtime Blinn-Phong 对比图。
9. PBR 渲染数据上传必须以 `SceneResourceTable::buildUploadView()` 为唯一数据视图；`MaterialInstance` 负责表达材质内容，不能成为 realtime/offline PBR 的第二条上传路径。

## 需求

### R1: 全局唯一 glTF/PBR 加载路径

glTF mesh、texture URI、PBR metadata、tangent fallback 和 material bridge 必须由共享 infra/core 路径负责。`src/demos/lxe_editor/` 不能继续维护 Helmet 专属的 PBR bridge 逻辑。

要求：

- scene 中 `mesh.uri: assets/models/damaged_helmet/DamagedHelmet.gltf` 必须能被 editor、realtime profile output 和 offline renderer 共同消费。
- `builtin://lxe_editor/helmet` 不再作为材质/mesh 加载入口保留；默认场景、demo 和测试必须使用显式 DamagedHelmet glTF URI，避免 editor-only 兼容分支。
- 共享加载路径输出 `Mesh` / `MeshBuffer`、generated tangent、`MaterialInstance`、texture bindings 和 material scalar。
- realtime 与 offline 可以有各自 GPU 上传和 draw/dispatch 后端，但不能各自解释 glTF texture channel、material defaults、color space、sampler 策略或 shader variant。
- 如果某个 glTF 材质字段当前不支持，必须由共享加载路径输出一致诊断；不能出现 realtime 忽略而 offline 接受，或反过来。

### R2: PBR texture set 与 MaterialInstance 合同

支持以下 material-owned texture binding：

| Binding | 含义 | glTF 来源 |
|---|---|---|
| `albedoMap` | base color | `pbrMetallicRoughness.baseColorTexture` |
| `normalMap` | tangent-space normal | `normalTexture` |
| `metallicRoughnessMap` | glTF-style B=metallic, G=roughness | `pbrMetallicRoughness.metallicRoughnessTexture` |
| `aoMap` | ambient occlusion | `occlusionTexture` |
| `emissiveMap` | emissive | `emissiveTexture` |

要求：

- texture 必须先进入 `MaterialInstance::setTexture(StringID, CombinedTextureSamplerSharedPtr)` 所维护的 binding-name 合同。
- scalar fallback 必须通过 `MaterialInstance` parameter buffer 表达，至少覆盖 `baseColorFactor`、`metallicFactor`、`roughnessFactor`、`ao`、`emissiveFactor`。
- PBR realtime/offline 渲染不能直接从 `MaterialInstance::getDescriptorResources(pass)` 上传材质 UBO/texture。渲染流程必须先把选中 tag 的 `MaterialInstance` 注册到 `SceneResourceTable`，再由 `buildUploadView()` 生成 shader 读取所需的 material record、texture table index 和 scalar fallback。
- 没有纹理时使用 material scalar/default 或明确 placeholder；不能静默当成黑图。
- DamagedHelmet 本地资产必须作为完整 PBR coverage 资产使用。它包含：

| 输入 | 文件 / glTF 字段 |
|---|---|
| Albedo | `Default_albedo.jpg`, `baseColorTexture` |
| Metallic | `Default_metalRoughness.jpg`, B channel |
| Roughness | `Default_metalRoughness.jpg`, G channel |
| AO | `Default_AO.jpg`, `occlusionTexture` |
| Normal | `Default_normal.jpg`, `normalTexture` |
| Emissive | `Default_emissive.jpg`, `emissiveTexture` + `emissiveFactor` |

### R2.5: Instance 多材质 tag 选择

一个 scene instance 可以绑定多套材质。每套材质由 `tag` 标识，渲染流程通过配置或 editor 命令选择一个 tag。

要求：

- scene node 的 `materials` 列表必须支持 `tag`、`uri`、`source`、`offline`、`materialOverrides` 和 `nodeMaterialOverrides`。
- offline render profile 通过 `offlineRender.materialTag` 选择离线材质；realtime output profile 通过 `outputProfiles.<name>.materialTag` 选择实时材质。
- `offlineRender.shader` 之类的 shader 直连配置无效，必须移除；shader 选择来自被选中 tag 对应的材质 pass。
- editor/runtime 必须提供命令按 tag 切换当前 scene 中 renderable 的活动材质。
- 切换活动 tag 时，scene 必须同步更新 `SceneResourceTable` 中的 active material handle 和 object/material index；不能只更新 `SceneNode` 的 validated cache。
- 渲染流程只收集拥有目标 tag 的物体；缺少该 tag 的物体必须从该流程排除。
- PBR 材质只保留一套普通 `pbr.material`。材质文件名、shader 代码和 loader 都不能包含 Helmet 或 glTF 专属分支。

### R3: Offline GPU texture table

Offline GPU scene 必须能按 material index 查到完整 PBR texture table index。

要求：

- `SceneGpuMaterialRecord` 或等价 offline material record 必须表达 `baseColorTexture`、`normalTexture`、`metallicRoughnessTexture`、`aoTexture`、`emissiveTexture`。
- texture index 缺失使用明确 sentinel 或统一 default texture。
- compute shader 使用固定上限 descriptor array；本 REQ 不引入 bindless。
- texture table 的填充来自 `SceneResourceTable::buildUploadView()` 对 active `MaterialInstance` 的统一 binding-name 合同解析，而不是 offline 专用材质配置，也不是 realtime 专用 descriptor 上传。
- 缺失纹理、超过固定上限、无法上传 texture 时必须有包含 material path / texture binding / resolved path 的诊断。

### R4: UV、tangent 与 normal map

normal map 在 realtime 与 offline 中必须同启同禁。

要求：

- triangle/vertex GPU record 必须包含 UV 和 tangent basis 所需数据。
- DamagedHelmet glTF 本身不声明 `TANGENT` accessor；共享加载路径必须在 indexed triangle + UV 数据存在时生成稳定 tangent。
- 如果某个 glTF 缺少生成 tangent 所需数据，normal map 必须在共享加载阶段被禁用并输出诊断；不能 realtime 使用 normal map、offline 退回 vertex normal。
- tangent handedness 必须进入 vertex data，并在 realtime shader 与 offline hit attributes 中以同一规则构造 TBN。

### R5: Texture loading、色彩空间与 sampler

PBR 纹理色彩空间和 sampler 行为必须对 realtime/offline 一致。

要求：

- baseColor 和 emissive 按 sRGB 语义处理。
- normal、metallicRoughness、AO 按 linear data 处理。
- 如果当前 `TextureLoader` / Vulkan upload 暂不支持真实 sRGB image format，本 REQ 必须实现或明确统一的 shader-side decode 策略；实时与离线必须相同。
- metallic/roughness channel mapping 固定为 glTF 规则：B=metallic，G=roughness。
- sampler 至少支持 linear repeat；clamp/repeat 策略来自共享 material/texture metadata，不由 realtime/offline 分别硬编码。

### R6: Shared PBR shader common

PBR 直接光公式必须放入 shader common 库。

要求：

- 新增或扩展 `assets/shaders/glsl/common/pbr.glsl`。
- `pbr.frag` 和新增 offline PBR direct ray compute shader 必须 include 同一套 Cook-Torrance direct-light 函数。
- common 函数至少覆盖：

| 输入项 | 用途 |
|---|---|
| baseColor | diffuse albedo / metallic F0 mix |
| normal | NDF、geometry、Fresnel、NdotL |
| metallic | diffuse/specular energy split |
| roughness | GGX distribution / geometry |
| AO | ambient or configured material occlusion term |
| emissive | final emissive add |
| directional light | direct lighting only |
| camera/view direction | specular view term |

- 等价 compare 场景中的 shadow、IBL、GI、environment contribution 必须通过 scene/profile/offlineRender/realtime render 配置关闭，不能为 Helmet 在 shader/backend 中写硬编码分支。
- common PBR 库不能提供默认 ambient 或 fallback environment 函数；AO 只能调制已经由配置显式启用的 ambient/IBL/environment 项，直接光 compare 中 AO 只能通过单项覆盖测试验证材质输入，不得隐式改变 direct-light 基线。
- 如果某项在特定 compare profile 中需要隔离测试，例如 emissive 归零，必须通过 scene material override 或 profile 配置实现，并另设单项测试覆盖该项。

### R7: Offline shader 模式与 MVP 回归

新增 PBR direct-light offline shader 时必须保留 MVP shader。

要求：

- 保留现有 `offline_primary_ray.comp` 的 MVP/diagnostic 行为。
- 新增 PBR 等价 shader，例如 `offline_pbr_direct_ray.comp`，或等价命名。
- scene/offlineRender 配置必须通过 material tag 选择使用 MVP shader 还是 PBR direct shader；不能保留独立 shader mode 或 asset-name 分支。
- 现有 `realtime_offline_compare_diagnostic.scene.yaml` 和相关 compare 目标必须继续可运行。
- 新 Helmet PBR compare scene 必须使用配置选择 PBR direct path；代码里不能根据 asset name 特判。

### R8: Realtime/offline PBR 等价 demo

新增一个使用本地 DamagedHelmet 的等价验证 scene。

要求：

- scene 使用普通 glTF mesh URI：`assets/models/damaged_helmet/DamagedHelmet.gltf`。
- scene/profile 配置关闭 shadow、IBL、GI、environment contribution。
- scene 使用同一方向光、相机、resolution、background、material state。
- realtime profile output 生成 linear EXR 和 PNG preview。
- offline render 生成 linear EXR 和 PNG preview。
- `lxe_compare_exr` 对 linear EXR 做像素级阈值比较，并作为 demo 验收目标。
- comparison 目标输出诊断 JSON 或清晰 console summary，便于后续 ray tracer 改动判断回归。

### R9: Assets downloader 兼容边界

本 REQ 的主 demo 使用 repo 内置 DamagedHelmet，不依赖联网或 `.asset_cache` 状态。但共享加载路径必须与 assets downloader 的 `cache://.../converted/...` 方向兼容。

要求：

- `cache://` URI 仍由统一 asset resolver 解析。
- 如果 downloader 后续产出 `converted/model.gltf`、`converted/model.glb` 或 `converted/material.yaml`，它必须走同一 glTF/PBR bridge 和 `MaterialInstance` 合同。
- `converted/material.yaml` 的 `model: pbr-metallic-roughness`、texture URI、color space、channel mapping、defaults 应能映射到同一 material bridge。
- 本 REQ 不要求联网下载 DamagedHelmet，也不要求 downloader 完整实现 glTF 归档拆包。

### R10: 测试覆盖

覆盖必须证明“共享流程”和“公式输入项不遗漏”。

要求：

- shared loader test：同一 glTF scene 能被 editor/runtime scene load 和 offline scene loader 消费。
- no-dual-bridge test：editor Helmet/builtin 入口委托共享 loader，不能保留独立 PBR bridge。
- material bridge test：DamagedHelmet 生成 PBR `MaterialInstance`，绑定 albedo、normal、metallicRoughness、AO、emissive。
- tangent test：DamagedHelmet 缺 `TANGENT` accessor 时共享 loader 生成 tangent，并让 normal map 在 realtime/offline 同时启用。
- offline GPU scene test：material texture indices 写入 offline texture table，缺失项使用 sentinel/default。
- shader compile/reflection test：MVP offline shader 与 PBR direct offline shader 都能编译，descriptor contract 明确。
- common shader test：`pbr.frag` 与 offline PBR shader 都 include shared PBR common。
- no-fallback-ambient test：realtime/offline PBR direct-light shader 和 shared common 中不存在硬编码默认环境光函数。
- resource-table-tag-switch test：scene 切换 active material tag 后，`SceneResourceTable::buildUploadView()` 中的 material record 和 primitive material index 同步反映新 tag。
- image compare test：Helmet PBR equivalence scene 的 realtime/offline linear EXR 在固定阈值内通过。
- per-input coverage test：baseColor、metallic、roughness、normal、AO、emissive 每一项都有测试证明它影响输出，并且 realtime/offline 的影响方向一致。
- MVP regression test：原 offline MVP diagnostic compare 流程仍可运行。

## 修改范围

- `src/core/asset/`
- `src/core/scene/`
- `src/infra/material_loader/`
- `src/infra/mesh_loader/gltf_mesh_loader.*`
- `src/infra/offline/offline_scene_loader.*`
- `src/infra/texture_loader/`
- `src/demos/lxe_editor/`（删除/改造 demo-local bridge，改为调用共享 loader）
- `src/backend/vulkan/offline/`
- `src/backend/vulkan/` 中 realtime profile output 必需的共享输出路径
- `assets/shaders/glsl/`
- `assets/shaders/glsl/common/`
- `assets/scenes/`
- `src/tools/lxe_offline_render/`
- `src/tools/lxe_realtime_render/`
- `src/tools/lxe_compare_exr/` 仅在阈值或报告输出需要扩展时修改
- tests

## 边界与约束

- 本 REQ 不实现多 bounce。
- 本 REQ 不实现 path tracing 积分器、progressive accumulation、environment importance sampling、denoiser 或 AOV。
- 本 REQ 不实现 GI。
- 本 REQ 不实现 IBL 等价；IBL 在 compare scene 中必须通过配置关闭。
- 本 REQ 不实现 shadow 等价；shadow 在 compare scene 中必须通过配置关闭。
- 本 REQ 不实现 clear coat、transmission、subsurface、anisotropy。
- 本 REQ 不引入 bindless texture；固定上限 descriptor array 是 offline 约束。
- 本 REQ 不要求新增 editor UI。
- 本 REQ 不要求联网下载资产。
- 本 REQ 不允许为 Helmet、offline 或 realtime 维护双轨材质解释逻辑。

## 依赖

- `REQ-049-a`
- `REQ-054-a`
- `REQ-055-a`
- `openspec/specs/material-system/spec.md`
- `openspec/specs/material-asset-loader/spec.md`
- `openspec/specs/mesh-loading/spec.md`
- `openspec/specs/texture-loading/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`

## 后续工作

- `REQ-057-a` 在完整 PBR path tracing 中消费这些共享材质、纹理和 shader common。
- 后续 assets downloader 可在同一共享 loader 上扩展完整 glTF archive/cache conversion。
- 后续 IBL / shadow / GI 等价测试应作为独立 REQ，不混入本 REQ 的直接光等价目标。

## 实施状态

2026-06-14 复核关闭：本文档基于旧 `baseColorFactor` / `materialTag` / MaterialUBO-era PBR 合同，当前默认材质路线已迁移到 `schema: lxe.material.v2`、`bsdf.source`、Material v3 source-reflected storage 和 RenderPathGraph。

本 REQ 的旧合同不再作为 active 实施目标维护；后续 offline/realtime 材质纹理质量与一致性由 `REQ-073-*`、`REQ-073-h` 承接。
