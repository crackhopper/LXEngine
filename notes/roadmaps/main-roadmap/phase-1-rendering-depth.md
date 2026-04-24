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

## 优先路径（2026-04-24 更新）

当前阶段优先级顺序：

1. **PBR 管线落地**（REQ-118）— 当前 `pbr.{vert,frag}` + `pbr_gold.material` 只是最小 forward PBR，缺 IBL ambient / shadow 接收 / 多光源循环
2. **IBL**（REQ-105 + REQ-106）— 环境贴图 + prefilter + BRDF LUT，并把 scene-level IBL 资源接入 `Scene`
3. **G-Buffer / 延迟渲染**（REQ-119）— 首个 `Pass_Deferred` 管线，同一 scene 可切 forward / deferred
4. 其他渲染深度条目（shadow / CSM / bloom / FXAA / 视锥剔除 / 多光源）与 Web 后端按需继续

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

当前聚焦顺序（前三个里程碑优先交付）：

| M | 条件 | demo |
|---|------|------|
| **M1.α · PBR + IBL 闭环** | REQ-101 + REQ-102 + REQ-105 + REQ-106 + REQ-118 | 完整 PBR 画面（IBL ambient + shadow 接收 + 多光源占位） |
| **M1.β · G-Buffer / 延迟** | REQ-119 | 同一场景切 forward / deferred |
| M1.γ · 方向光阴影 | REQ-103 | shadow map + PCF |
| M1.δ · 多光源 + CSM + 剔除 | REQ-104 + REQ-109 + REQ-110 | `demo_pbr_shadow_ibl` 完整画面 |
| M1.ε · Bloom + FXAA | REQ-107 + REQ-108 | 完整后期链路 |
| M1.ζ · Dawn 接入 | REQ-111 + REQ-112 + REQ-113 + REQ-114 | `demo_pbr_dawn` 桌面 WebGPU |
| M1.η · Web 可打开 | REQ-115 | `demo_pbr_web` 在 Chrome / Edge 打开 |
| M1.θ · Headless + PNG readback | REQ-117 | `engine-cli render` 渲染 PNG |
| M1.ι · WebGL2 兜底 | REQ-116（可选） | 无 WebGPU 浏览器打开 demo 仍可见 |

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
