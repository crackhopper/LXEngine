# Phase 1 · 渲染深度 + Web 后端

> **目标**：画面从“PBR tutorial 风格”推进到“现代引擎风格”（PBR + shadow + IBL + HDR + 后期），并让同一份渲染代码能同时在桌面 Vulkan 与浏览器 WebGPU/WebGL2 里跑。
>
> **依赖**：现状即可启动（`FrameGraph` / `RenderQueue` / `PipelineCache` / `MaterialInstance` 已就位）。
>
> **可交付**：
> - `demo_pbr_shadow_ibl`（桌面 Vulkan）— 立方体 + 地面，方向光实时阴影 + IBL + bloom + ACES tone map
> - `demo_pbr_web`（浏览器 WebGPU / WebGL2）— 同一场景，URL 一开即见

## 当前实施状态（2026-04-24）

**部分开工**。多 pass 骨架 + 通用材质路径已就位；渲染深度与 Web 后端均未开始。

| 条目 | 状态 | 备注 / 位置 |
|------|------|-------------|
| 多 pass 骨架（`FrameGraph` / `RenderQueue` / `PipelineCache`） | ✅ | `src/core/frame_graph/*` + `src/backend/vulkan/details/pipelines/pipeline_cache.*` |
| Pass 常量 `Pass_Forward` / `Pass_Deferred` / `Pass_Shadow` | ✅ | `src/core/frame_graph/pass.hpp`（`Forward` 活跃；`Shadow` 预留） |
| Camera visibility layer / culling mask | ✅ | `Camera::cullingMask` + `SceneNode::visibilityLayerMask`，`RenderQueue::buildFromScene` 交集过滤 |
| 基础 scalar table 收口（默认 `usize` / `u32`，少量协议 alias 保留） | ✅ | `src/core/platform/types.hpp` |
| 通用 `.material` YAML loader + 非 BlinnPhong 示例 | ✅ | `loadGenericMaterial` + `assets/materials/pbr_gold.material` + `assets/shaders/glsl/pbr.{vert,frag}` |
| HDR scene color target / tone map pass | ❌ | REQ-101 / REQ-102 |
| PBR 完整管线（IBL ambient + shadow 接收 + 多光源循环 + 可选贴图齐全） | ⚠️ 最小版已存在 | REQ-118；`assets/shaders/glsl/pbr.{vert,frag}` + `assets/materials/pbr_gold.material` |
| G-Buffer / 延迟渲染路径 | ❌ | REQ-119；`Pass_Deferred` 已在 `pass.hpp` 预留 |
| Shadow map（单 map） | ❌ | REQ-103 |
| CSM | ❌ | REQ-104 |
| Environment map loader + IBL prefilter + scene-level IBL 资源挂接 | ❌ | REQ-105 / REQ-106 |
| Bloom | ❌ | REQ-107 |
| FXAA | ❌ | REQ-108 |
| `PointLight` + `SpotLight` + 统一多光源合同 | ❌ | REQ-109 |
| Frustum culling | ❌ | REQ-110 |
| Renderer 接口审计 | ❌ | REQ-111 |
| WebGPU 后端 | ❌ | REQ-112 |
| Shader 跨后端 IR | ❌ | REQ-113 |
| PipelineCache 适配多后端 | ❌ | REQ-114 |
| Web 构建目标（WASM） | ❌ | REQ-115 |
| WebGL2 降级 | ❌ | REQ-116（可选） |
| Headless 渲染 + PNG readback | ❌ | REQ-117 |

## 优先路径（2026-05-01 更新）

> **2026-05-01**：本 phase 现在排在 [Phase 1.5 ImGui Editor MVP](phase-1.5-imgui-editor-mvp.md) 之后开工。优先级按"编辑器有更多可玩内容 + 视觉反馈最强"重排：先做多光源数据类型（让编辑器 visualizer 一次写完衰减球 / spot cone），再做 shadow（视觉反馈最强），最后 PBR / IBL / G-Buffer 伴随 RTR 阅读渐进推进。

当前阶段优先级顺序：

1. **多光源 + point/spot**（REQ-109）— `PointLight` / `SpotLight` 数据类型 + 统一 GPU light set；让 Phase 1.5 编辑器的衰减球 / spot cone visualizer 一次写完
2. **Shadow + CSM**（REQ-103 + REQ-104）— shadow map + PCF + 4 级 cascade；视觉反馈最强，配合编辑器移光交互能立刻看出阴影变化
3. **PBR 完整管线**（REQ-118）— 伴随 RTR 阅读渐进推进；当前 `pbr.{vert,frag}` + `pbr_gold.material` 只是最小 forward PBR，需补 IBL ambient / shadow 接收 / 多光源循环 / 完整贴图集
4. **IBL**（REQ-105 + REQ-106）— 环境贴图 + prefilter + BRDF LUT，并把 scene-level IBL 资源接入 `Scene`
5. **G-Buffer / 延迟渲染**（REQ-119）— 首个 `Pass_Deferred` 管线，同一 scene 可切 forward / deferred
6. 其他渲染深度条目（HDR / tone map / bloom / FXAA / 视锥剔除）与 Web 后端按需继续

## 范围与边界

**做**：

- PBR metallic-roughness 管线升级（基于现有 `pbr.{vert,frag}` + `pbr_gold.material`）
- 多 pass 渲染：`Pass_Shadow` + `Pass_Forward` + `Pass_Deferred` + `Pass_PostProcess`
- Directional light shadow map（先 single-map，后 CSM）
- 环境贴图加载 + IBL 预过滤（diffuse / specular / BRDF LUT）
- 独立 HDR scene color 目标 → tone map + gamma
- **G-Buffer / 延迟渲染路径**（与 forward 并存，scene-level 切换）
- Bloom（downsample / upsample / blend）
- FXAA（TAA 后置）
- 视锥剔除
- `PointLight` + `SpotLight` + 固定上限多光源合同
- **WebGPU 后端** — 通过 [Dawn](https://dawn.googlesource.com/dawn) 原生 C++
- **WebGL2 fallback** — 通过 Emscripten 构建到 WASM
- **Shader 跨平台编译** — GLSL → SPIR-V → WGSL（[Tint](https://dawn.googlesource.com/tint) 或直接写 WGSL）

**不做**：

- GI（VXGI / DDGI / lightmap 烘焙）
- 体积雾
- SSAO / SSR
- 虚拟纹理 / 可变率着色
- Metal / D3D12 原生后端（Dawn 内部处理，引擎不直接写）

## 前置条件

- `FrameGraph` 容纳多 pass ✅
- `RenderTarget` 有 `colorFormat` + `depthFormat` ✅
- `Pass_Shadow` 已在 `pass.hpp` ✅
- HDR 颜色格式（`R16G16B16A16_SFLOAT`）需加入 `ImageFormat`
- `Renderer` 抽象已就位 ✅（当前仅 `VulkanRenderer` 一个实现）

## 工作分解

### REQ-101 · HDR Scene Color Target

- `ImageFormat` 新增 `RGBA16F`，backend `toVkFormat` 映射 `VK_FORMAT_R16G16B16A16_SFLOAT`
- `VulkanRenderer::initScene` 能按 pass 类型创建 HDR 离屏目标而非 swapchain
- 新增 `OffscreenColorTarget` 辅助封装 (image / view / framebuffer)

**验收**：单 pass render-to-texture，结果作下一 pass sampler 输入。

### REQ-102 · PostProcess Pass 架构

- `FullscreenPass` 辅助：无顶点 buffer、`gl_VertexIndex` 生成三角形、一个 sampler 输入、一个颜色输出
- `src/core/frame_graph/pass.hpp` 加 `Pass_PostProcess` StringID
- 全屏 pass pipeline 绕过 vertex layout（`pipelineKey` 的 meshSig 用 `StringID{}`）
- `src/infra/post_process/tonemap_pass.*`：Reinhard + ACES 两种

**验收**：HDR 目标采样 → tone map → 输出 swapchain，亮度不异常。

### REQ-103 · Shadow Pass + Depth-only Pipeline

- `Pass_Shadow` framegraph 节点：仅 depth attachment
- 单 2048×2048 shadow map 先跑通
- depth-only PBR 材质 shader（只跑 vertex stage）
- `RenderQueue::buildFromScene` 扫描 `supportsPass(Pass_Shadow)` 物体
- Forward fragment 采样 shadow map，3×3 PCF

**验收**：地面上有方块形阴影；光源旋转时阴影跟动。

### REQ-104 · Cascaded Shadow Maps

- 4 级 cascade，split 方式 logarithmic + uniform 混合
- 每帧重算 cascade 投影矩阵（view frustum split → 包围球 → light space）
- `DirectionalLightData` 扩展为 `DirectionalLightData_CSM`，含 4 组 VP + 4 split 距离
- Fragment 按 view depth 选 cascade，PCF + cascade 边界渐变

**验收**：拉远拉近时阴影细节不糊不闪。

### REQ-105 · Environment Map Loader + Scene-level IBL 资源

- HDR equirectangular（`.hdr` / `.exr` via stb_image）
- 启动或离线转 cubemap（6 面 render-to-cube）
- 新 resource 类型 `CubemapResource : IGpuResource`
- `Scene` 持有一组 IBL scene-level 资源（environment cubemap / diffuse irradiance / specular prefilter / BRDF LUT）
- `RenderQueue` / descriptor 组装将其作为 scene-level inputs 追加到目标 pass
- IBL 不是 `LightBase` 子类；与 `DirectionalLight` / 局部光源并列

**验收**：天空盒显示加载的 HDR；方向光方向与环境贴图主亮度方向一致。

### REQ-106 · IBL Prefilter

- **Diffuse irradiance**：cosine-weighted 半球卷积，32×32 irradiance cube
- **Specular prefilter**：按 roughness 逐 mip GGX 重要性采样，128 → 1 的 mip chain
- **BRDF LUT**：512×512 2D，Schlick-GGX split sum
- 预过滤结果可缓存磁盘

Forward PBR fragment 加 ambient 项：

```glsl
vec3 F    = fresnelSchlickRoughness(max(dot(N,V),0.0), F0, roughness);
vec3 kS   = F;
vec3 kD   = (1.0 - kS) * (1.0 - metallic);
vec3 irrd = texture(irradianceMap, N).rgb;
vec3 prefiltered = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
vec2 brdf = texture(brdfLUT, vec2(max(dot(N,V),0.0), roughness)).rg;
vec3 ambient = (kD * irrd * albedo + prefiltered * (F * brdf.x + brdf.y)) * ao;
```

**验收**：关方向光后物体非漆黑，背光面有环境反光。

### REQ-107 · Bloom Pass

- HDR scene color → bright pass（阈值过滤）
- 5 级 mip 下采样 + 5 级上采样加法混合
- 与 scene color 相加注入 tone map 之前

**验收**：高光边缘柔光晕；不溢出整幅画面。

### REQ-108 · FXAA Pass

- 单 pass 后处理，luminance edge detection + blend
- 放在 tone map 之后（LDR 阶段）

**验收**：高对比边缘锯齿明显减少。

### REQ-109 · PointLight + SpotLight + 统一多光源合同

- `PointLight : LightBase` + `PointLightData`（`position` / `color/intensity` / `range` 或等价衰减）
- `SpotLight : LightBase` + `SpotLightData`（`position` / `direction` / `color/intensity` / `innerCone` / `outerCone`）
- `LightBase::getUBO()` / `supportsPass(pass)` 合同不变
- `Scene::addLight(...)` / `getSceneLevelResources(pass, target)` 不为新类型增加特殊接口
- 统一 GPU light set：scene-level 资源装配阶段把命中 pass 的 light objects 投影成固定上限（`N=8` 起步）GPU 结构
- 超出上限规则写死：方向光优先保留；其余按距离裁剪
- **多光源数量变化不改变 `PipelineKey`**；只有 shader 是否启用多光源合同才影响 pipeline
- Forward shader 支持多光源循环

**验收**：3 点光 + 1 聚光 + 1 方向光，亮度合计正确；超过 N 时裁剪稳定。

### REQ-118 · PBR 完整管线

当前 `assets/shaders/glsl/pbr.{vert,frag}` + `assets/materials/pbr_gold.material` 已落地最小 forward PBR（BRDF + directional light）。本 REQ 把它补齐成可产出“现代引擎画面”的完整 PBR：

- **IBL ambient 项**：接入 REQ-105 / REQ-106 产出的 environment cubemap / irradiance cube / specular prefilter / BRDF LUT，作为 `pbr.frag` 的 `ambient` 项
- **Shadow 接收**：采样 REQ-103 / REQ-104 的 shadow map，作为 PBR direct lighting 的遮蔽项
- **多光源循环**：对接 REQ-109 统一多光源合同（方向光 + N 个局部光）
- **完整贴图集**：albedo / normal / metallicRoughness / occlusion / emissive；variant 由 `.material` 文件驱动
- **数值稳定性**：Schlick F0 / GGX NDF / Smith G 的 edge-case 审查
- 保留 `pbr_gold.material` 作为 sanity 基线；新增至少一个 textured 真实示例（例如 `pbr_scifi_floor.material`）

**验收**：单个 PBR 材质在 directional light + IBL + 多 local light + shadow 下画面符合 reference（与 glTF sample viewer / Filament viewer 参考对齐）。

### REQ-119 · G-Buffer / 延迟渲染路径

`Pass_Deferred` 已在 `src/core/frame_graph/pass.hpp` 预留。本 REQ 把它实化为可选渲染路径，与 forward 并存。

- **G-Buffer layout**（首版，可调）：
  - RT0 `RGBA8` — base color（rgb）+ AO（a）
  - RT1 `RGBA8` — world normal（rg 八面体编码）+ roughness / metallic（ba）
  - RT2 `RGBA16F` — emissive（rgb）+ reserved
  - Depth 单独 attachment
- **Geometry pass**：Scene renderable 走 `Pass_Deferred` 把几何 + 材质属性写进 G-Buffer；descriptor 沿用 REQ-031 保留名字集 + 材质 canonical binding
- **Lighting pass**：fullscreen pass（复用 REQ-102 `FullscreenPass`）采样 G-Buffer，计算 PBR + IBL + shadow，写入 HDR scene color
- **Scene-level 切换**：`Scene::setRenderPath(Forward | Deferred)`，`SceneNode::supportsPass(Pass_Deferred)` 基于 material template 的 `Pass_Deferred` 定义
- **PipelineKey 稳定性**：forward / deferred 共享 `MaterialTemplate`，但 pass 不同 → `PipelineKey` 自然区分；scene-level 切换不改变身份
- **过渡物体**：透明 / 非 PBR（如 UI overlay）仍走 forward

**验收**：`demo_pbr_shadow_ibl` 按 scene 切 forward / deferred，画面一致、performance 有差别；G-Buffer 可通过 debug 视图检视。

### REQ-110 · Frustum Culling

- `RenderQueue::buildFromScene` 加入 AABB × 视锥测试
- `Mesh` 加 `boundingBox`（加载 OBJ / GLTF 时计算）
- `SceneNode` 按 world transform 得出 world AABB（依赖 Phase 2 transform；前期可用 identity world AABB 占位）

**验收**：相机转 180° 时 draw call 数降到 0。

### Frame Graph 演进（REQ-042 + 候选 REQ-A..G）

当前 `src/core/frame_graph/FrameGraph` 的名字与业界 *frame graph* 重叠，但实际上只承担了"持有 `vector<FramePass>` + 按调用顺序透传 target + 跨 pass `PipelineKey` 去重"。它 **不感知资源依赖、不构边、不做拓扑排序、不自动插 barrier、不做 attachment aliasing、不数据驱动** —— 大致只对应业界 frame graph 在 *parse 完但还没 compile* 的最早期状态。

[REQ-042](../../requirements/042-render-target-desc-and-target.md) 把 `RenderTarget` 拆为 `RenderTargetDesc` + `RenderTarget`，是后续 frame graph 演进的 *字段层前置*：attachment 形状要先变得可比、可哈希、能进 `PipelineKey`，才有资格作为资源边的载体。

完整的演进路径（候选 REQ 编号，正式立项时按 `notes/requirements/` 实际可用号顺延）：

| REQ | 内容 | 风险 |
|-----|------|------|
| A | `FrameGraphResource` 数据结构 + 跨 pass 资源命名（代码 API） | 低 |
| B | `compile()` v1：构边 + 拓扑排序 + cycle 检测 | 低 |
| C | 自动 renderpass / framebuffer 创建 | 中 |
| **D** | **自动 barrier 推导** | **高（风险点 #1）** |
| E | JSON parser + schema | 中 |
| **F** | **attachment aliasing** | **高（风险点 #2）** |
| G | runtime register_pass 策略热替换 | 低 |

**触发条件**：当 [REQ-119](#req-119--g-buffer--延迟渲染路径) 延迟渲染 / [REQ-103](#req-103--shadow-pass--depth-only-pipeline) shadow pass 立项后，跨 pass 资源流转开始变多，REQ-A 应紧随其后立项。在那之前 LX 仍可在"单 forward pass"形态下手工跑，不需要立刻补 frame graph。

**详细调研**：[notes/roadmaps/research/frame-graph/](../research/frame-graph/README.md)（6 篇 + 入口索引）覆盖：完整 frame graph 形态、JSON 资源类型、`FrameGraphResource / FrameGraphNode` 字段拆解、compile 三大算法、LX 字段级 gap、REQ-A..G 切分与里程碑。

### Async Compute（候选 REQ-A..F）

LX 当前 `VulkanRenderer` 只用 graphics queue，没有 compute pipeline 路径、没有 dispatch 命令封装、没有跨 queue 同步机制。完整 async compute 能力需要分两族 6 个 REQ 引入：

- **基础设施族（A / B / F）**：compute queue + pipeline 基础设施 / queue ownership transfer 工具 / 硬件 fallback；不依赖 frame graph，可早做
- **集成族（C / D / E）**：FramePass 队列亲和性 / 跨 queue 自动 barrier 推导（**风险点 #1**）/ 实战 demo；与 frame graph 演进协同

**关键时序**：

- async-compute REQ-C 与 frame-graph REQ-A（FramePass 数据结构扩展）共改 `FramePass`，建议同步推进
- async-compute REQ-D（跨 queue 自动 barrier）必须等 frame-graph REQ-D（自动 barrier 推导）完成才能做

**触发条件**：

- 当前 Phase 1 PBR / shadow / IBL / bloom 都是 graphics-only，**没有强触发**
- 强触发条件主要在 [Phase 5 物理](phase-5-physics.md)（GPU 物理 / 粒子）或独立性能优化场景（GPU culling / compute bloom）

**已有基础**：[multi-threading/08](../research/multi-threading/08-Timeline与资源退休模型.md) 已经设计了 `IGpuTimeline` 接口并预留 `computeTimeline` 字段，async compute 落地时直接消费这套同步原语，不需要重新发明。

**详细调研**：[notes/roadmaps/research/async-compute/](../research/async-compute/README.md)（6 篇 + 入口索引）覆盖：GPU 利用率模型、多 queue 机制、compute shader 基础、LX 字段级 gap、与 frame graph 的耦合时序、REQ-A..F 切分与风险定位。

### GPU-Driven Rendering + Clustered Lighting（候选 REQ-A..H）

LX 当前是纯 CPU-driven 渲染：CPU 逐 mesh 决策 + 提交 draw call、单光源、单 forward pass、无 G-buffer、无 GPU culling、无 indirect drawing。要走向现代渲染管线（Unreal 5 Nanite / Frostbite / Doom Eternal 风格），需要一组组合性变更。

这块工作的核心引用是 *Mastering Graphics Programming with Vulkan* Chapter 6（GPU-Driven Rendering）和 Chapter 7（Clustered Deferred Rendering），两章紧密相关，**合并到一个调研里**。

完整的演进路径（候选 REQ 编号，正式立项时按 `notes/requirements/` 实际可用号顺延）：

| REQ | 内容 | 风险 |
|-----|------|------|
| A | Meshlet 资产 pipeline（meshoptimizer 接入） | 低 |
| B | Indirect draw + storage buffer 基础 | 低 |
| C | G-Buffer + Multi-RT + dynamic rendering | 中 |
| D | Compute culling 基础（frustum + back-face） | 中 |
| **E** | **Hi-Z + 两阶段 occlusion culling** | **高（风险点 #1）** |
| F | Clustered lighting（Activision 1D bin + 2D tile） | 中 |
| G | Mesh shader pipeline + 实战 | 中 |
| H | Path selection + fallback | 低 |

**REQ 跟现有 Phase 1 条目的关系**：

| 现有 REQ | 跟本调研关系 |
|---------|-------------|
| [REQ-119 G-Buffer / 延迟渲染](#req-119--g-buffer--延迟渲染路径) | 直接由候选 REQ-C + REQ-F 实施 |
| [REQ-109 多光源合同](#req-109--pointlight--spotlight--统一多光源合同) | "N=8 上限"由候选 REQ-F 替换为 *无上限 clustered lighting* |
| [REQ-110 Frustum Culling](#req-110--frustum-culling) | CPU 路径由候选 REQ-D 替换为 GPU compute culling |

**触发条件**：

- REQ-119 / REQ-109 / REQ-110 任一立项 → 强触发
- 性能瓶颈分析显示 draw call CPU 时间或 shading 时间是 hotspot → 强触发
- 引入大场景或复杂光照（>30 光源） → 中触发

**关键时序**：

- 候选 REQ-C 依赖 [`REQ-042`](../../requirements/042-render-target-desc-and-target.md)（RenderTarget 拆分）
- 候选 REQ-E 依赖 frame-graph 候选 REQ-D（barrier 推导） + multi-threading/08 backend 实现
- 候选 REQ-D 依赖 async-compute 候选 REQ-A（compute pipeline）

**详细调研**：[notes/roadmaps/research/gpu-driven-rendering/](../research/gpu-driven-rendering/README.md)（6 篇 + 入口索引）覆盖：GPU-driven 范式、meshlet 资产层、culling pipeline（含 Hi-Z 两阶段 + mesh shader vs compute）、clustered deferred lighting、LX 字段级 gap、REQ-A..H 切分与跟前面三个调研的耦合时序。

### Shadows + Sparse Resources（候选 REQ-A..G）

LX 当前完全无阴影渲染。本节涵盖 [REQ-103 Shadow pass](#req-103--shadow-pass--depth-only-pipeline) / [REQ-104 CSM](#req-104--cascaded-shadow-maps) 的实施细节，以及让 256 光源同时投影成为可能的 *sparse residency* 基础能力。

参考：*Mastering Graphics Programming with Vulkan* Chapter 8（Adding Shadows Using Mesh Shaders）。

完整的演进路径（候选 REQ 编号，正式立项时按 `notes/requirements/` 实际可用号顺延）：

| REQ | 内容 | 风险 |
|-----|------|------|
| A | Cubemap + cubemap array 基础设施 | 低 |
| B | Directional shadow map（基础阴影入门） | 低 |
| C | Point light cubemap shadow（少光源） | 中 |
| D | Sparse residency 基础设施（跨主题，独立可做） | 中 |
| E | Per-light importance metric（cluster-based） | 低 |
| **F** | **Multi-light sparse-bound shadow（256 光源）** | **高（风险点 #1，核心整合）** |
| G | Shadow filtering (PCF) + CSM | 中 |

**REQ 跟现有 Phase 1 条目的关系**：

| 现有 REQ | 跟本调研关系 |
|---------|-------------|
| [REQ-103 Shadow pass](#req-103--shadow-pass--depth-only-pipeline) | 直接由候选 REQ-A + REQ-B 实施 |
| [REQ-104 CSM](#req-104--cascaded-shadow-maps) | 由候选 REQ-G 实施 |
| [REQ-109 多光源](#req-109--pointlight--spotlight--统一多光源合同) | 256 光源 shadow 由候选 REQ-C/D/E/F 提供 |
| [REQ-118 PBR 完整管线](#req-118--pbr-完整管线) | shadow 接收依赖 REQ-B 完成 |

**触发条件**：

- REQ-103 立项 → 触发 A + B（基础阴影入门）
- REQ-109 立项 + 决定走多光源 shadow → 触发 C + D + E + F（核心整合）
- 其它项目（streaming texture / virtual geometry）需要 sparse → 触发 D 单独立项

**关键时序**：

- 候选 REQ-A / REQ-B 独立可做（不依赖前面调研）
- 候选 REQ-D 依赖 multi-threading/08 timeline backend 实现
- 候选 REQ-E 依赖 gpu-driven-rendering 候选 REQ-F（clustered lighting 提供 cluster 数据结构）
- **候选 REQ-F**（核心整合）依赖：REQ-C + REQ-D + REQ-E + gpu-driven REQ-G（mesh shader）+ frame-graph 候选 REQ-D（barrier 推导）+ async-compute 候选 REQ-A（compute pipeline）

**详细调研**：[notes/roadmaps/research/shadows/](../research/shadows/README.md)（6 篇 + 入口索引）覆盖：shadow 技术全景 / cubemap shadow 数据结构 / mesh shader 4 步阴影流水线 / Vulkan sparse residency 完整模型（page pool / vkQueueBindSparse）/ 跨调研耦合（5 个前置）/ REQ-A..G 切分。**Sparse resources 单独占一篇**，因为是跨主题的 Vulkan 基础能力（未来 streaming texture / virtual geometry 也用）。

### 后期性能优化（VRS + Specialization Constants，候选 REQ-A..B，**优先级低**）

> 这一节是 *Phase 1 主路径完成后* 的性能 tuning 工具，**不阻塞任何其它工作**。在主架构（PBR / shadow / 多光源 / GPU-driven / clustered lighting）稳定前不立项。

**Variable Rate Shading (VRS)**：让 fragment shader 执行频率可变（1×1 / 2×2），通过 image attachment 控制每 tile 的 rate。基于 Sobel 边缘检测决定哪些区域用低速率。典型收益：fragment shading 时间 -30% ~ -50%，画质几乎无损。

**Specialization Constants**：SPIR-V 编译期常量但 *pipeline 创建时填值*。比 #define 灵活、比 push constant 高效。典型用例：subgroup size 自适应（NV=32 / AMD=64）、shader 静态变体管理。

候选 REQ：

| REQ | 内容 | 风险 |
|-----|------|------|
| A | Specialization constants 反射 + pipeline 集成 | 低 |
| B | Variable Rate Shading（image attachment 路径） | 低 - 中 |

**触发条件**：

- REQ-A：shader 数 > 30，#define 变体管理变痛点
- REQ-B：[REQ-119 G-Buffer](#req-119--g-buffer--延迟渲染路径) 已完成 + profiling 显示 fragment shading > 30% frame time

**关键定位**：本节工作 *不依赖任何其它调研*，是 *叠加层* 不是 *依赖层*。但 *依赖前置工作完成才有收益* —— 主架构没就位时引入是过早优化。

**详细调研**：[notes/roadmaps/research/variable-rate-shading/](../research/variable-rate-shading/README.md)（轻量 4 篇）覆盖：VRS 概念 + 三种集成方式 + Sobel 算法 / specialization constants 机制 + 跟 #define 对比 + shader 反射扩展 / LX gap + 候选 REQ + 触发条件。

### Temporal 子系统（Volumetric Fog + TAA，候选 REQ-A..F，**Phase 1 主路径完成后立项**）

> 这一节覆盖 *现代渲染管线* 的 temporal 范式，是 LX 当前完全空白的能力。本节是 *消费方*，依赖 frame-graph / multi-threading/08 / clustered lighting / cubemap shadow 全部就位才能完整跑通。**基础设施 REQ-A 是唯一硬前置**，REQ-B/C 早做能先拿 TAA baseline。

**Temporal 共享基础设施**：camera jitter（Halton sequence sub-pixel offset）+ motion vector buffer（R16G16，1080p ≈ 8 MB，camera + per-vertex 双源）+ history texture 双 buffer（跨帧资源 + RetirePoint）+ blue noise + golden ratio 时间偏移。所有 temporal 特性（volumetric fog / TAA / 未来的 SSR / SSAO / DoF）共享这套底盘。

**Volumetric Fog**（Wronski 2014）：把视锥切 froxel（128³ 或 160×90×64，~36 MB）+ 5 步 compute pipeline（injection / scattering / spatial filter / temporal filter / integration）+ Beer-Lambert 应用到场景；*scattering 步骤直接复用 clustered lighting bins/tiles + cubemap shadow*；典型 1080p ~1.5ms。视觉效果：god rays / 雾飘动。

**TAA**：6 步算法骨架 + 5 大改进（closest-depth 3×3 / Catmull-Rom history / 邻域 moments / variance clip + clamp / 动态 resolve weight）+ blurriness 缓解（sharpen post-process / 负 mip bias / unjitter UVs）。*TAA 跟 deferred 完美兼容*（MSAA 不适合 deferred），且 *跟 volumetric fog 协同*（fog 激进 dithering + TAA 跨帧累积消除）。

候选 REQ：

| REQ | 内容 | 风险 |
|-----|------|------|
| A | Temporal 共享基础设施（jitter / motion vec / history / blue noise） | 中 |
| B | TAA 最简版（4 行骨架 baseline） | 低 |
| C | TAA 5 大改进（防 ghosting + 防 blurry） | 中 |
| D | Volumetric Fog（完整 5 步 pipeline） | 高 |
| E | TAA + Fog 协同（dithering 自然消除） | 低 |
| F | TAA corner case（disocclusion / 半透明 / specular 单独路径） | 中 |

**前置依赖**：

- REQ-A：frame-graph 候选 REQ-A（FramePass）+ multi-threading/08 IGpuTimeline + RetirePoint
- REQ-D：REQ-A + frame-graph 候选 REQ-D（barrier 推导）+ gpu-driven-rendering 候选 REQ-F（clustered lighting）+ shadows 候选 REQ-F（cubemap shadow）
- REQ-E：REQ-C + REQ-D
- REQ-B/C/F：仅依赖 REQ-A，跟 REQ-D 完全并行

**触发条件**：

- 强触发：Phase 1 主路径（PBR / shadow / clustered lighting）完成 → 立 REQ-A + REQ-B
- 强触发：测出 fragment temporal aliasing（树叶闪烁、铁丝网鬼影）严重 → 立 REQ-A + REQ-B + REQ-C
- 强触发：引入室内场景 / 大空间 → 立 REQ-A + REQ-D（god rays 是室内必需）
- 中触发：引入 SSR / SSAO / DoF — 共享 REQ-A，一次铺好多个特性受益

**关键定位**：本节是 *最下游消费方*。Phase 1 路线图本来 `不做` 雾 + 只规划了 FXAA（REQ-108），TAA + 体积雾的引入意味着把视觉表现层推进到 *现代引擎水准*。REQ-D 的上游依赖最重（三个调研同时就位），REQ-B/C 可以更早穿插。

**详细调研**：[notes/roadmaps/research/temporal-techniques/](../research/temporal-techniques/README.md)（6 篇 + 入口索引）覆盖：temporal 范式（jitter / motion vec / history / blue noise）+ volumetric fog 原理（scattering / extinction / phase function / froxel）+ 5 步 compute pipeline + TAA 6 步算法 + 5 大改进 / LX gap + 候选 REQ + 跟前面 5 个调研的耦合。

### Ray Tracing 子系统（Ch12-15 全，候选 REQ-A..H，**desktop-only 加分项**）

> 这一节覆盖 *硬件 RT* 整套能力：AS + ray query / RT pipeline + 三大消费特性（shadow / GI / reflection）。**LX 当前完全空白；Phase 1 主路径完成后才考虑立项；WebGPU 无 RT，必须双路径 fallback**。本节是 *最下游消费方 + 最大复杂度引入者*——必须 frame-graph / multi-threading/08 / temporal / cluster / bindless / shadows 全部就位才能完整跑通。

**RT 共享底盘**：BLAS（mesh 静态几何）+ TLAS（instance + transform）两层 acceleration structure；buffer device address（RT 强依赖）；6 个新 shader stage（raygen / miss / closest-hit / any-hit / intersection / callable）；shader binding table (SBT) 像虚函数表 vtable，决定 ray 命中事件调哪个 shader。

**双调用范式**：
- **Ray query**（fragment / compute shader 内同步求交，无 SBT）— 简单可见性（shadow / AO），4 行代码
- **RT pipeline**（独立 raygen + 完整 SBT + 递归）— 复杂 hit shader（GI / reflection），LX 引入完整 RT 路径的真正动机

**三个消费特性**：
- **RT Shadow**（Ch13）：朴素版 ray query 4 行代码 + RT Gems advanced 4-pass（adaptive sample count + Poisson disk + 时空滤波 + 3D texture history per-light）；几何精度 + 256 光源 + 软阴影 native 支持
- **DDGI**（Ch14）：3D probe 网格 + spherical Fibonacci ray + octahedral 编码 + Chebyshev 防漏 + 极慢 hysteresis（h=0.97）；动态 diffuse GI，~1.5 ms RTX 30
- **RT Reflection + SVGF**（Ch15）：1 ray/pixel + GGX VNDF + importance sampling lights + SVGF 3-pass 通用 denoiser（temporal accumulation + variance + 5× A-trous wavelet，mesh_id + depth_dd + normal + luminance 四重权重）；SVGF 是 LX 处理所有 RT 噪声的 *通用 denoiser 框架*

**G-buffer 新通道**（SVGF 必需）：
- `mesh_id`（u32 per-pixel，跨帧稳定）— 跟 REQ-119 G-buffer 整合
- `depth_normal_dd`（屏幕空间 ddx/ddy）— 跟 REQ-119 G-buffer 整合

候选 REQ：

| REQ | 内容 | 风险 |
|-----|------|------|
| A | RT 基础设施（4 个 Vulkan 扩展 + BLAS/TLAS + scratch + 双路径开关） | 中 |
| B | Ray query 路径（fragment / compute shader 内调用） | 低 |
| C | 朴素 RT shadow（lighting shader 加 4 行 ray query） | 低 |
| D | RT pipeline + SBT + hit shader 框架（buffer device address + bindless from RT） | 高 |
| E | DDGI 完整 5 步 pipeline | 高 |
| F | RT reflection + SVGF 3-pass 通用 denoiser | 高 |
| G | Advanced RT shadow（升级 REQ-C 到 4-pass adaptive） | 中 |
| H | 双路径 fallback + 跨平台收口（Web / 移动端走 raster） | 低-中 |

**前置依赖**：

- REQ-A：frame-graph 候选 REQ-A + multi-threading/08 IGpuTimeline + RetirePoint
- REQ-D：REQ-A + frame-graph 候选 REQ-D（barrier 推导处理 RT access mask）+ bindless texture
- REQ-E：REQ-D + gpu-driven-rendering 候选 REQ-F（cluster lighting，hit shader 算 direct lighting 必需）+ bindless
- REQ-F：REQ-D + REQ-E（验证 RT pipeline 1 遍后再做）+ G-buffer mesh_id / depth_dd 通道
- REQ-G：REQ-C + REQ-F（SVGF 框架可作 reference）+ gpu-driven REQ-F（cluster lighting）
- REQ-H：REQ-A..G 全部 + frame-graph 主路径

**触发条件**：

- 强触发：Phase 1 主路径（PBR / shadow / cluster lighting / G-buffer / bindless）+ temporal-techniques REQ-A 全部就位
- 强触发：项目转向 desktop-only 或明确放弃 Web → RT 进入 P0
- 中触发：视觉品质要求 AAA 级（动态 GI / 屏幕外反射 / 多光源 soft shadow）
- 中触发：shadow map 的 acne / peter-panning / 多光源支持成为画质瓶颈
- **反触发**：当前 Phase 1 路线图 *把 Web 当作 P0*（REQ-112 WebGPU + REQ-115 WASM + REQ-116 WebGL2），所以 RT 调研 *所有* REQ 都应延后到 Phase 1 主路径稳定后再考虑

**关键定位**：本节是 *最下游消费方 + 最大复杂度引入者*。Phase 1 路线图本来 `不做` GI；引入 RT 意味着把视觉表现层推进到 *AAA 引擎水准*。但 RT 跟 Web 兼容性 *直接冲突*——必须 *双路径设计*（REQ-A 起就埋坑，REQ-H 收口）：RT 是 *desktop-only 加分项*，不是 *跨平台基础*。

**详细调研**：[notes/roadmaps/research/ray-tracing/](../research/ray-tracing/README.md)（8 篇 + 入口索引）覆盖：RT 范式（BVH / 6 shader stage / vs raster）+ AS 数据结构（BLAS / TLAS 两步构建 + 跨帧策略 + compaction）+ RT pipeline / ray query 双路径 + RT shadow（朴素 + advanced）+ DDGI 完整算法 + RT reflection + SVGF 通用 denoiser / LX gap + 候选 REQ + 跟前面 7 个调研的耦合 + Web 兼容性策略。

## 跨平台后端工作分解

契合 [P-20 渲染/模拟可分离](principles.md#p-20-渲染与模拟可分离)。

### REQ-111 · Renderer 接口审计

- 审计 `Renderer` 基类虚方法签名是否泄露 Vulkan 特有类型
- 把泄露抽到 `core/rhi/` 的中立类型（`ICommandBuffer` / `IGpuImage` / `IGpuBuffer` 等）
- 原生 Vulkan 继续存在，从“唯一实现”变成“实现之一”

**验收**：通过接口调用原生后端时，调用点对“背后跑什么”完全无感。

### REQ-112 · Web-capable 渲染后端

**能力要求**：

- 同一份 C++ 代码在桌面原生 + 浏览器 WASM 都能跑
- 桌面 native 可 route 到 Vulkan / Metal / D3D12；浏览器 route 到 WebGPU
- Shader / 资源 / pipeline 描述两环境语义一致
- 不引入第二套 API（“一个 native path + 一个 web path”是反模式）

**选型参考（可替换）**：基于 WebGPU 的 C++ 实现库（能同时产原生 + WASM 二进制）；或自写 `WebGpuRenderer : Renderer`。接口不变，后端可替换。

**验收**：两目标环境运行同一 demo 场景，画面像素级一致（允许 tone map / swapchain 色空间可解释差异）。

### REQ-113 · Shader 跨后端 IR

- `ShaderCompiler` 接口扩展“目标后端”参数
- 内部走“源码 → IR → 后端字节码”两步编译
- `ShaderReflector` 输出与后端无关 —— 同一 shader 在所有后端反射结果**结构化等价**（binding 名 / type / offset 一致）

**选型参考**：SPIR-V 作 IR + 标准工具链转 WGSL/MSL/HLSL；或其他 cross-compiler。

**验收**：同一 shader 在两后端下反射 JSON 比对一致。

### REQ-114 · PipelineCache 多后端

- `PipelineBuildDesc` 已是后端中立结构；各后端各自实现 `from BuildInfo → native pipeline handle`
- 缓存 key（`PipelineKey`）与后端无关，共享 key 空间
- 新后端预编译与现有 `PipelineCache::preload` 共用入口

**验收**：同一场景在两后端都命中预编译 pipeline cache。

### REQ-115 · Web 构建目标

- 新增 toolchain 产出 `engine.wasm` + glue + 静态 HTML 入口
- 运行时自动把 GPU 调用 route 到浏览器原生 API
- 消除同步 I/O 假设（浏览器没有同步文件读）— 改 async / 预加载到虚拟文件系统

**选型参考**：Emscripten CMake toolchain 或其他 C++ → WASM 工具链。

**验收**：启动本地 HTTP server，浏览器打开入口 URL 看到同一 demo。

### REQ-116 · 降级渲染路径（可选）

- 走类似 OpenGL ES 3 能力子集
- 缺失能力（compute / storage texture / HDR）触发 post-process 链降级
- 仅作 fallback

**验收**：WebGPU 不可用的浏览器仍能打开 demo（画面可简化不崩）。

### REQ-117 · Headless 渲染能力

契合 [P-20](principles.md#p-20-渲染与模拟可分离)：后端必须能在**无窗口**环境初始化。

- `Renderer` 增加 `Mode::Headless`
- Headless 下仍完成 `initScene` / `uploadData` / `draw`，present 替换为 readback
- 提供 `readback() → bytes`，把当前 color target 导成图片
- CLI 入口：`engine-cli render --scene <source> --out <image>`（Phase 10 复用）
- 输出同时带语义描述，契合 [P-16 多模态](principles.md#p-16-文本优先--文本唯一)

**验收**：命令行渲染 PNG，与窗口画面像素一致；语义描述可供 agent 使用。

## 里程碑

当前聚焦顺序（按 2026-05-01 重排，前三个里程碑优先交付）：

| M | 条件 | demo |
|---|------|------|
| **M1.α · 多光源（point + spot）** | REQ-109 | 编辑器里 3 点光 + 1 聚光 + 1 方向光，亮度合计正确；衰减球 / spot cone visualizer 接通 |
| **M1.β · 方向光 shadow + CSM** | REQ-101 + REQ-102 + REQ-103 + REQ-104 | HDR scene color + tone map + shadow map + 3×3 PCF + 4 级 cascade |
| **M1.γ · PBR 完整管线** | REQ-118 | IBL ambient + shadow 接收 + 多光源循环 + 完整贴图集（伴随 RTR 阅读渐进推进） |
| M1.δ · IBL 闭环 | REQ-105 + REQ-106 | environment cubemap + diffuse irradiance + specular prefilter + BRDF LUT |
| M1.ε · G-Buffer / 延迟 | REQ-119 | 同一场景切 forward / deferred |
| M1.ζ · Bloom + FXAA + 视锥剔除 | REQ-107 + REQ-108 + REQ-110 | 完整后期链路 + 相机转 180° draw call 数降到 0 |
| M1.η · Dawn 接入 | REQ-111 + REQ-112 + REQ-113 + REQ-114 | `demo_pbr_dawn` 桌面 WebGPU |
| M1.θ · Web 可打开 | REQ-115 | `demo_pbr_web` 在 Chrome / Edge 打开 |
| M1.ι · Headless + PNG readback | REQ-117 | `engine-cli render` 渲染 PNG |
| M1.κ · WebGL2 兜底 | REQ-116（可选） | 无 WebGPU 浏览器打开 demo 仍可见 |

## 风险 / 未知

- **IBL 预过滤性能**：GGX 重要性采样 mip chain 启动期跑 + 没优化会秒级。解决：缓存磁盘 + 作为 [P-10](principles.md#p-10-资产血统--provenance) 的“生成型资产”登记。
- **Shadow map 漏光 / 彼得潘**：固定 bias + slope-scale bias 覆盖 90% 场景。
- **PostProcess 与 FrameGraph 关系**：`FrameGraph` 以 `FramePass` 为节点；“一 pass 一 RenderQueue” 假设在 post-process 上不成立。可能引入第二种 pass 形态（fullscreen pass）不走 `buildFromScene`。
- **HDR 格式兼容性**：部分 iGPU 对 16-bit 浮点支持差。降级待定。
- **新后端库体量与编译时间**：Web-capable GPU 抽象库首次编译可能数分钟。解决：固定版本 + 构建缓存。
- **跨后端反射差异**：各后端 shader cross-compiler 反射 API 不统一。隔离在 `ShaderReflector` 内部。
- **WebGPU / Vulkan binding 抽象差异**：bind group vs descriptor set。反射驱动的绑定需两边验证。
- **Web 环境下的 async I/O**：“启动时读文件”的 loader 需改异步或预加载。

## 与 AI-Native 原则契合

- [P-1 确定性](principles.md#p-1-确定性是架构级不变量)：跨后端像素一致是渲染侧确定性的基础验收。
- [P-4 单源能力清单](principles.md#p-4-单源能力清单)：shader 反射 + pass 描述加入 capability manifest，供 agent 查询。
- [P-7 多分辨率](principles.md#p-7-多分辨率观察)：渲染统计 summary (FPS / draw calls) / outline (每 pass 一行) / full (每 item 的 pipeline key)。
- [P-16 多模态](principles.md#p-16-文本优先--文本唯一)：`rendering.screenshot` 是 agent 视觉通道，同时带语义描述。
- [P-20 渲染与模拟可分](principles.md#p-20-渲染与模拟可分离)：headless 模式是 eval harness 前置。

## 与现有架构契合

- `FrameGraph::addPass` 支持任意 `FramePass`；新增 pass 只需新增 `Pass_*` StringID 并在 backend 创建对应 render pass object。
- `PipelineCache::preload` 走 `collectAllPipelineBuildDescs` 路径；新 pass 的 pipeline 一并扫描预构建。
- `RenderQueue` 的 pass / target / visibility mask 过滤适配 shadow pass（只有支持 shadow 的 renderable 才会进 queue）。
- 反射驱动材质系统让后处理 shader 的 binding 表自动生成。
- `IGpuResource::getBindingName()` 让环境贴图 / BRDF LUT 等资源通过命名自动绑定。

## 下一步

本 phase 后进入 [Phase 2](phase-2-foundation-layer.md)（若未做）或 [Phase 3](phase-3-asset-pipeline.md)。IBL 资源的磁盘缓存是 Phase 3 资产管线的早期 driver。
