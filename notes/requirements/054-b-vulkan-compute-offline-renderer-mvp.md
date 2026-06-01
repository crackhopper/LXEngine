# REQ-054-b: Vulkan Compute Offline Renderer MVP

> 2026-06-01 新增：本 REQ 实现 A 阶段最小闭环：用 Vulkan compute 离线渲染现有场景，完成 GPU buffer readback。正式 EXR/PNG 输出由 `REQ-055-a` 负责。当前仍在讨论中，未开始。

## 背景

用户明确希望第一版直接基于 Vulkan 后端，而不是 CPU path tracer。目标不是只得到图片，而是借离线 renderer 反推 backend 的 headless compute、storage buffer、readback、shader/pipeline 组织，从而提升引擎底层质量。

第一版不使用 Vulkan ray tracing pipeline。它先用 CPU 构建 BVH，上传到 GPU，由 compute shader 遍历 BVH 和 triangle buffer，完成 primary ray + 简单材质 + environment 的最小渲染闭环。

本 REQ 依赖 `REQ-054-a` 的 renderer foundation/realtime/offline 拆分。离线渲染实现必须落在 offline renderer 路径，不应继续塞进当前 realtime `VulkanRenderer` 大类。

## 目标

1. 新增 `lxe_offline_render` CLI。
2. Headless 创建 Vulkan device，不依赖 swapchain。
3. 从现有 `.scene.yaml` 生成 `OfflineSceneIR`。
4. 编译 `GpuScene` buffer 和 BVH buffer。
5. compute shader 写入 HDR accumulation/output buffer，并完成 CPU readback。
6. 复用当前 IBL 金属球场景作为 demo。

## 需求

### R1: `lxe_offline_render` CLI

新增命令行工具，位置固定为 `src/tools/lxe_offline_render/`：

```bash
lxe_offline_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --camera /game_cam \
  --width 1024 \
  --height 576 \
  --samples 1 \
  --out artifacts/offline/ibl_metal_sphere
```

要求：

- 支持 `--scene`、`--camera`、`--profile`、`--width`、`--height`、`--samples`、`--out`。
- 支持 `--seed`，默认固定为 `1`。
- 支持无窗口/headless 运行。
- 失败时输出明确错误，例如缺 scene、缺 camera、缺 mesh、unsupported material。
- `--out` 在本 REQ 中只用于指定 readback/debug dump 目标；正式 EXR/PNG 路径语义由 `REQ-055-a` 接管。
- 该工具属于 engine/tooling，不放在 `src/demos/`。
- 该工具不得依赖 `src/demos/lxe_editor/`；scene YAML 读取必须通过 `REQ-053-a` 下沉后的 `scene_io` / offline compiler 边界。
- 后续同类工具可按同一约定放在 `src/tools/`，例如 `lxe_asset_bake`、`lxe_scene_validate`、`lxe_image_compare`。

### R2: OfflineSceneIR

从 runtime scene 转换出 renderer-neutral IR。

模块位置要求：

| 层 | 模块 | 职责 |
|---|---|---|
| `src/core/offline/` | `offline_scene.*`、`offline_render_profile.*` | renderer-neutral IR 与 profile 数据结构 |
| `src/infra/offline/` | `offline_scene_compiler.*`、`offline_asset_resolver.*` | `scene_io` scene document / asset loader 到 IR 的转换 |
| `src/backend/vulkan/offline/` | `vulkan_offline_renderer.*`、`gpu_scene_builder.*`、`compute_bvh_builder.*` | Vulkan GpuScene、BVH、compute dispatch |

`OfflineSceneIR` 不应放在 `src/demos/lxe_editor/`。editor 可以使用它，但不能拥有它；CLI 也不能反向依赖 editor demo 目录。MVP 主路径是 `.scene.yaml -> scene_io document -> OfflineSceneIR`，不要求先构建 editor runtime `Scene`。

首版至少包含：

| 数据 | 要求 |
|---|---|
| camera | eye/target/up/fov/aspect/near/far |
| mesh | 保留 mesh 级几何数据，不在 IR 阶段强制展平成全局三角形 |
| instance | mesh 引用、material 引用、world transform、visibility 信息 |
| material | baseColor / metallic / roughness 常量 |
| environment | HDR texture 或 bake 后 skybox resource 的输入路径 |
| lights | 复用 scene YAML 的 `light:` 节点；MVP 至少编译 directional light |
| transform | node world transform 保留在 instance 上 |

首版不支持 skeleton/skinning。

OfflineSceneIR 必须保留 mesh + instance transform 的关系。MVP 可以在
`GpuSceneBuilder` 阶段把 instance 展平为 world-space triangle / BVH buffer，
但不能让 `OfflineSceneIR` 一开始就丢失 mesh、instance、material 之间的语义。
这样后续支持实例化、材质槽、bake target 和 editor 对比时仍有清晰的数据边界。

### R2.1: OfflineMaterialIR

离线 renderer 不直接消费 realtime `MaterialInstance`、pipeline key、descriptor set
或 shader variant。通用材质输入必须先转换成 renderer-neutral 的
`OfflineMaterialIR`。

第一版 `OfflineMaterialIR` 至少包含：

| 字段 | 含义 |
|---|---|
| `materialModel` | `pbr-metallic-roughness` |
| `baseColor` | 线性 base color 常量 |
| `albedoTextureRef` | 可选 albedo/baseColor 纹理引用 |
| `metallic` | metallic 常量 |
| `roughness` | roughness 常量 |
| `emissive` | emissive 常量 |
| `textureRefs` | 除 albedo 外的纹理由 `REQ-056-a` 扩展 |

要求：

- `.material` / `MaterialInstance` 是输入来源，不是 offline shader 的直接执行表示。
- realtime renderer 和 offline renderer 可以从同一份材质资产编译出不同执行表示。
- `REQ-054-b` 只要求常量 PBR 参数 + 简单 albedo/baseColor 纹理。
- normal / metallicRoughness / AO / emissive 等完整纹理支持进入 `REQ-056-a`。
- unsupported material model 必须有诊断或明确 fallback。

### R3: GpuScene buffer layout

定义 compute shader 可消费的数据：

- triangle buffer
- vertex buffer 或 packed triangle buffer
- material buffer
- instance/object table
- BVH node buffer
- camera uniform
- output `RGBA32F` storage image/buffer
- MVP albedo texture descriptor array

要求：

- buffer layout 在 C++ 和 GLSL 之间有稳定合同。
- 使用 `std430` 或明确对齐规则。
- 测试覆盖 CPU side struct size/alignment 与 shader 预期。
- `GpuSceneBuilder` 可以为了 MVP 将 `OfflineSceneIR` 的 mesh instance 展平到
  triangle/BVH buffer，但必须保留 object/material id，方便 debug AOV 和后续
  bake 输出定位。
- albedo 纹理使用固定小上限 descriptor array，例如 32 或 64 张 texture。
- material buffer 存 `albedoTextureIndex`；无纹理使用 sentinel。
- UV 缺失时禁用 albedo texture 并输出 warning。
- MVP 不引入 bindless。

### R4: CPU BVH 构建，GPU BVH 遍历

第一版由 CPU 构建 BVH。

要求：

- 支持 triangle AABB。
- BVH node 上传 GPU。
- compute shader 遍历 BVH 找最近 hit。
- 没有 hit 时采样 environment。
- 数据模型预留 BLAS/TLAS 风格边界：
  - mesh-local triangle acceleration structure 对应未来 BLAS。
  - instance-level acceleration structure 对应未来 TLAS。
  - MVP 实现可以先把所有 instance 展平成一个 global triangle BVH。

首版 BVH 可以是简单 SAH 以外的中点分割或 Morton 排序，优先正确性和数据合同。
接口命名和模块边界不应把单层 global BVH 固化为唯一长期模型；后续 Vulkan
hardware ray tracing backend 应能复用 mesh/instance 边界。

MVP 明确不实现双层 BLAS/TLAS。首版只要求 CPU 构建 global triangle BVH 并上传 GPU；
模块和接口名称必须保留未来 BLAS/TLAS 扩展空间。

### R5: MVP lighting and shading

第一版 shading 做最小闭环，但不能把光照简化到只能显示 skybox。离线渲染器
是 ray tracing 路线，必须从第一版就表达直接光与环境光的区别。

首版光照：

- primary ray
- miss ray 采样 environment / skybox
- 至少支持一个 scene `light.kind: Directional`，作为太阳或主光源
- hit point 对 directional light 做直接光评估
- directional light 必须支持 hard shadow ray：从 hit point 沿光线方向查询 BVH
  occlusion，被遮挡时 direct light contribution 为 0
- offline MVP 不使用 realtime `shadowStrength` 混合阴影；`shadowStrength` 只作为 realtime shadow 参数保留。
- offline MVP 忽略 `shadowDistance`、`shadowCascadeCount` 等 realtime shadow-map 参数。
- directional light 默认参与 offline shadow ray，不新增 `castsShadow` 字段。
- 常量 baseColor / metallic / roughness
- 可选 albedo/baseColor 纹理采样，用于影响 baseColor
- direct light 使用简化 Cook-Torrance：Lambert diffuse + GGX specular
- BRDF 参数语义与 realtime PBR 保持一致：baseColor / metallic / roughness
- 金属反射方向采样 environment
- diffuse 可使用简单 lambert 或 environment 近似
- 不要求多 bounce
- 不要求 normal / ORM / AO / emissive 纹理

对 environment 的解释：

- skybox/environment 可视为已经预先渲染或采集得到的远场纹理。
- MVP 可以把 environment 用作 miss ray 背景与环境反射输入。
- IBL 是 realtime 的简化模型；offline renderer 后续在 `REQ-057-a` 中应逐步走向显式直接/间接光路径积分，而不是只依赖 IBL 近似。

MVP sampling 边界：

- `samples` 支持保留，但只用于 camera jitter anti-aliasing 和未来接口兼容。
- `maxDepth` 固定为 1。
- direct light hard shadow 使用确定性单 shadow ray。
- 金属反射按反射方向确定性采样 environment。
- roughness 可影响 GGX specular 强度/形状近似，但不要求 stochastic glossy lobe sampling。
- 真正随机路径采样、多 bounce、variance 收敛和 glossy importance sampling 进入 `REQ-057-a`。

目标是当前 IBL 金属球或高质量 assets-downloader demo 能通过 readback buffer 验证一张可读参考结果。

### R6: Backend 组织要求

离线 renderer 不能把所有 Vulkan 逻辑塞进 CLI main。
离线 renderer 也不能继续堆进 realtime `VulkanRenderer` 主实现。

建议模块：

| 模块 | 职责 |
|---|---|
| `OfflineRenderJob` | 输入参数、执行状态、输出路径 |
| `OfflineSceneCompiler` | scene_io scene document -> OfflineSceneIR |
| `GpuSceneBuilder` | OfflineSceneIR -> GPU buffers |
| `ComputeBvhBuilder` | CPU BVH -> GPU BVH buffer；MVP 可实现 global triangle BVH，但接口预留 BLAS/TLAS |
| `VulkanOfflineRenderer` | headless Vulkan compute dispatch，基于 `REQ-054-a` 的 shared foundation |
| `OfflineReadback` | 将 compute 输出 buffer / storage image read back 到 CPU linear float buffer；正式文件写出交给 `REQ-055-a` |

### R7: 测试覆盖

覆盖：

- CLI 参数解析。
- `ibl_metal_sphere.scene.yaml` 能编译成 OfflineSceneIR。
- builtin sphere/plane 能转换为 triangle 数据。
- OfflineSceneIR 保留 mesh + instance + material 关系。
- albedo/baseColor 纹理能进入 OfflineMaterialIR 并影响 GPU 输出颜色。
- GpuSceneBuilder 能把 instance transform 正确应用到 BVH/triangle buffer。
- BVH hit 测试可命中中心球。
- headless compute dispatch 后 output buffer 非空。
- readback buffer 的尺寸、格式和有限数值范围可验证。
- 中心像素或指定 probe pixel 能命中 MVP 球体，颜色/深度在合理范围内。
- deterministic seed 下重复运行 readback 结果稳定。
- 本 REQ 不要求 image similarity 测试；EXR/PNG 文件和 preview 测试进入 `REQ-055-a`。
- 失败 scene 给出可诊断错误。

### R8: Demo 1 - MVP 基准场景

本 REQ 必须提供第一档 demo，用于验证最小闭环。

场景形态：

- 复用当前 IBL 金属球/地面测试场景，或从它派生一个 offline MVP scene。
- HDR skybox / environment。
- 至少一个 directional light，代表太阳或主光源。
- 常量 PBR 金属材质。
- 不依赖外部大型资产下载。

输出：

- GPU 端 HDR output/accumulation buffer。
- CPU 端 readback linear float buffer。
- 可选 debug dump，用于开发期检查；正式 EXR/PNG 输出进入 `REQ-055-a`。
- CLI 命令可复现。
- 使用 `mvp` profile，建议 1024x576 / samples=4 / maxDepth=1。

验收重点：

- camera、mesh、instance transform、BVH、directional light、environment、compute dispatch、readback 全链路跑通。
- 金属球能看到环境/skybox 反射。
- direct light 与 environment 的开关或参数变化能影响结果。
- directional light hard shadow 能在地面或物体之间产生可见遮挡关系。
- readback buffer 应 deterministic，允许有少量采样噪声；低噪声 progressive reference 留到 `REQ-057-a`。
- shader 随机数使用 `hash(seed, pixel, sample)` 或等价稳定规则，重复运行同一 profile 得到相同输出。

### R9: MVP scene support matrix

首版明确支持：

| 能力 | 范围 |
|---|---|
| geometry | static triangle mesh |
| builtin primitives | sphere / plane；MVP 必须支持 |
| glTF | 可选支持 static mesh；仅在现有 loader 可直接提供 triangle/UV/material slot 且能在 headless/offline 工具链复用时启用 |
| camera | 单个指定 camera |
| light | directional light |
| environment | HDR skybox/environment |
| transform | node world transform |
| material | 单材质或简单材质槽，常量 PBR + albedo texture |

首版明确不支持：

- skeleton/skinning
- animation
- transparent material
- alpha test
- 同时渲染多个 camera
- point / spot / area light
- instancing 优化；IR 保留 instance 语义，但 MVP 可在 GpuScene 阶段展平
- complex material graph

glTF 边界：

- Demo 1 不依赖 glTF；builtin sphere/plane 是 MVP 必需路径。
- 如果 glTF loader 当前无法在 headless/offline 工具链复用，不阻塞 `REQ-054-b`。
- unsupported glTF 场景必须给出明确诊断。
- 高质量 glTF/PBR 验收进入 `REQ-056-a` / `REQ-057-a`。

遇到 unsupported scene feature 时必须输出 warning 或 error。规则：

- 会导致明显错误图像的能力使用 error。
- 可以安全忽略且不影响 MVP demo 的能力使用 warning。

## 修改范围

- `src/tools/lxe_offline_render/`
- `src/core/offline/`
- `src/infra/offline/`
- `src/backend/vulkan/offline/`
- `src/core/scene/`
- `src/core/math/`
- `src/infra/mesh_loader/`
- `src/infra/texture_loader/`
- `src/backend/vulkan/`
- `assets/shaders/glsl/`
- `src/test/integration/`
- CMake

## 边界与约束

- 本 REQ 不实现 EXR/PNG writer；正式可查看图片交给 `REQ-055-a`。
- 本 REQ 只实现简单 albedo/baseColor 纹理；完整纹理进入 `REQ-056-a`。
- 本 REQ 不实现多 bounce path tracing；进入 `REQ-057-a`。
- 本 REQ 不使用 Vulkan hardware ray tracing pipeline。
- 本 REQ 不接入 editor UI。
- 本 REQ 不实现 bindless texture。

## 依赖

- `REQ-052-a`
- `REQ-053-a`
- `REQ-053-b`
- `REQ-054-a`
- 当前 Vulkan backend / shader compilation / scene runtime

## 后续工作

- `REQ-055-a`：把本 REQ 的 readback linear float buffer 写成 EXR + PNG，并交付第一张正式可查看的离线渲染图。
- `REQ-056-a`：纹理材质支持。
- `REQ-057-a`：完整 PBR path tracing reference。

## 实施状态

讨论中，未开始。
