# Phase 1 · 渲染深度 + Web 后端

> **目标**：让引擎能输出一张"看上去像现代引擎"的画面（PBR + 阴影 + 环境光 + HDR + 后期），**并且同一份渲染代码能同时在桌面 Vulkan 和 Web 浏览器里跑**。
>
> **依赖**：现状即可启动（`FrameGraph` / `PipelineCache` / `MaterialInstance` 已就位）。
>
> **可交付**：
> - `demo_pbr_shadow_ibl`（桌面 Vulkan）— 一个立方体 + 一块地面，方向光实时阴影 + IBL + bloom + ACES tone map
> - `demo_pbr_web`（浏览器 WebGPU/WebGL2）— 同一场景，在 Chrome 里跑，URL 一开就能看

## 范围与边界

**做**：
- 多 pass 渲染：`Pass_Shadow` + `Pass_Forward` + `Pass_PostProcess`
- Directional light 的 shadow map（先 single-map，后升级到 CSM）
- 环境贴图加载 + IBL 预过滤（diffuse + specular + BRDF LUT）
- 独立的 HDR scene color 目标 → tone map + gamma pass
- Bloom（downsample / upsample / blend）
- FXAA（TAA 留给以后）
- 视锥剔除
- 点光 + 聚光类型
- **WebGPU 后端（P1 高优）** — 通过 [Dawn](https://dawn.googlesource.com/dawn) 原生 C++ 支持
- **WebGL2 fallback** — 通过 Emscripten 构建到 WASM 的路径
- **Shader 跨平台编译** — GLSL → SPIR-V → WGSL（用 [Tint](https://dawn.googlesource.com/tint)）或直接写 WGSL

**不做**：
- GI（VXGI / DDGI / lightmap 烘焙）
- 体积雾
- SSAO / SSR
- 延迟渲染（forward 够用）
- 虚拟纹理 / 可变率着色
- Metal / D3D12 原生后端（Dawn 内部会处理，但引擎不直接写）

---

## 前置条件

- `FrameGraph` 能容纳多 pass（已满足）
- `RenderTarget` 有 `colorFormat` + `depthFormat`（已满足）
- `Pass_Shadow` 已存在于 `ResourcePassFlag` 枚举里（已满足）
- HDR 颜色格式（`R16G16B16A16_SFLOAT`）需要加入 `ImageFormat` 枚举
- `Renderer` 抽象基类已就位（已满足，当前只有 `VulkanRenderer` 一个实现）

---

## 工作分解

### REQ-101 · HDR Scene Color Target

在 `ImageFormat` 里加一个 HDR 变体，并允许 `FramePass.target` 使用它。

- 新增 `ImageFormat::RGBA16F`，在 backend 的 `toVkFormat` 里映射到 `VK_FORMAT_R16G16B16A16_SFLOAT`
- `VulkanRenderer::initScene` 能根据 pass 类型创建 HDR 离屏目标而非 swapchain
- 新增一个 `OffscreenColorTarget` 辅助类型，封装 (image, view, framebuffer) 生命周期

**验收**：单个 pass 能 render-to-texture，结果可以作为下一个 pass 的 sampler 输入。

### REQ-102 · PostProcess Pass 架构

提出一个通用的"全屏三角形 pass"模板。

- 新增 `FullscreenPass` 辅助：无顶点 buffer、`gl_VertexIndex` 生成三角形、一个 sampler 输入、一个颜色输出
- `core/frame_graph/pass.hpp` 增加 `Pass_PostProcess` StringID
- 全屏 pass 的 pipeline 绕过 vertex layout（`pipelineKey` 的 meshSig 用 `StringID{}`）
- 新增 `infra/post_process/tonemap_pass.hpp`：第一个落地的后期 pass，Reinhard + ACES 两种可选

**验收**：从 HDR 目标采样 → tone map → 输出到 swapchain，画面不变暗不变亮。

### REQ-103 · Shadow Pass + Depth-only Pipeline

- `Pass_Shadow` 的 framegraph 节点：只有 depth attachment，no color
- 单张 2048×2048 阴影贴图先跑通（后续再升级 CSM）
- 需要一个 **depth-only 版本**的 PBR 材质 shader（只跑 vertex stage）
- `RenderQueue::buildFromScene` 扫描所有 `renderable->supportsPass(Pass_Shadow)` 的物体
- 在 forward pass 的 fragment shader 里采样 shadow map，做 PCF 3×3

**验收**：立方体旁边的地面上能看到方块形阴影，光源旋转时阴影跟着动。

### REQ-104 · Cascaded Shadow Maps

在 REQ-103 之后升级。

- 4 个 cascade，split 方式：logarithmic + uniform 混合
- 每帧重算 cascade 投影矩阵（view frustum split → 包围球 → light space）
- `LightUBO` 扩展为 `DirectionalLightUBO_CSM`，含 4 组 VP 矩阵 + 4 个 split 距离
- Fragment shader 根据 view depth 选 cascade，做 PCF 采样 + cascade 边界渐变

**验收**：场景拉远拉近时，阴影细节不过度模糊也不闪烁。

### REQ-105 · Environment Map Loader

- 支持 HDR equirectangular 图片（`.hdr` / `.exr` via stb_image）
- 离线或启动时把 equirect 转换为 cubemap（6 面 render-to-cube）
- 新 resource 类型：`CubemapResource : IGpuResource`
- `getBindingName() == "EnvironmentMap"`

**验收**：天空盒能显示加载的 HDR，方向光方向与环境贴图主亮度方向一致。

### REQ-106 · IBL Prefilter

- **Diffuse irradiance**：对 cubemap 做 cosine-weighted 半球卷积，得到一张低分辨率 irradiance cube（通常 32×32）
- **Specular prefilter**：按 roughness 逐 mip 做 GGX 重要性采样，得到 mip chain（128×128 / 64×64 / ... / 1×1）
- **BRDF LUT**：一张 512×512 2D 纹理，预算 `scale + bias`（Schlick-GGX split sum）
- 预过滤结果可以缓存到磁盘，避免每次启动重算

在 `pbr_cube.frag` 里加 ambient 项：
```glsl
vec3 F    = fresnelSchlickRoughness(max(dot(N,V),0.0), F0, roughness);
vec3 kS   = F;
vec3 kD   = (1.0 - kS) * (1.0 - metallic);
vec3 irrd = texture(irradianceMap, N).rgb;
vec3 prefiltered = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
vec2 brdf = texture(brdfLUT, vec2(max(dot(N,V),0.0), roughness)).rg;
vec3 ambient = (kD * irrd * albedo + prefiltered * (F * brdf.x + brdf.y)) * ao;
```

**验收**：关闭方向光后物体不是漆黑，背光面有环境反光。

### REQ-107 · Bloom Pass

- 16-bit HDR scene color → bright pass（阈值过滤高亮区域）
- 5 级 mip 下采样（box filter 或 13-tap）
- 5 级上采样 + 加法混合
- 最终与 scene color 相加，注入 tone map pass 前面

**验收**：高光物体边缘有柔和光晕，不溢出到整个画面。

### REQ-108 · FXAA Pass

- 单 pass 后处理，标准 luminance edge detection + blend
- 放在 tone map 后（LDR 阶段），避免 HDR 值导致的过亮边缘

**验收**：高对比边缘锯齿明显减少，无明显模糊。

### REQ-109 · Point Light + Spot Light

- `PointLight : LightBase` + `PointLightUBO`
- `SpotLight : LightBase` + `SpotLightUBO`（含 inner/outer cone angle）
- Forward pass shader 支持多光源循环（硬上限 N=8 起步）
- 光源按距离排序后取前 N 个

**验收**：场景放 3 盏点光 + 1 盏聚光 + 1 盏方向光，亮度合计正确。

### REQ-110 · Frustum Culling

- 在 `RenderQueue::buildFromScene` 里加入 AABB × 视锥测试
- `Mesh` 增加 `boundingBox` 字段（加载 OBJ / GLTF 时计算）
- `RenderableSubMesh` 根据 world transform 得到 world AABB（依赖 Phase 2 的 transform 系统；也可以先用"无 transform 的 identity world AABB"占位）

**验收**：把摄像机转 180°，draw call 数降到 0。

---

## 跨平台后端工作分解

本节是 AI-Native 版本 roadmap 新增的内容。Vulkan 不能满足"编辑器在浏览器里跑"、"LLM 在 sandbox 里试运行生成代码"这两类需求，必须补上 **Web-capable** 的渲染后端。

契合 [P-20 渲染与模拟可分离](principles.md#p-20-渲染与模拟的可分离)：本节的所有工作同时为 headless / render-to-texture / simulation-only 三种运行模式打基础。

### REQ-111 · Renderer 接口抽象的审计与收敛

现在的 `Renderer` 基类只有一个 native 实现。要让第二个后端可以平级挂入，必须把它真正当接口用：

- 审计基类虚方法签名是否泄露了第一方后端特有类型
- 把这类泄露抽到 `core/rhi/` 的中立类型（`ICommandBuffer` / `IGpuImage` / `IGpuBuffer` 等纯接口）
- 原生后端继续存在，只是从"唯一实现"变成"若干实现之一"

**验收**：通过接口调用原生后端，调用点对"背后跑的是什么"完全无感知。

### REQ-112 · 引入 Web-capable 渲染后端

**能力要求**：
- 同一份 C++ 代码在桌面原生环境和浏览器 WASM 环境都能跑
- 桌面原生环境底层可以 route 到 Vulkan/Metal/D3D12，浏览器里 route 到 WebGPU
- Shader / 资源 / pipeline 描述在两个环境下语义一致
- 不引入第二套 API（"我们有一个 native path 和一个 web path" 是反模式）

**选型参考（可替换）**：
- 基于 WebGPU 的 C++ 实现库（能同时产出原生和 WASM 二进制）
- 或者自己写一层 `WebGpuRenderer : Renderer` 直接封装浏览器 WebGPU API，native 则保留 Vulkan 路径

关键是**接口不变**，后端可替换。

**验收**：在两个目标环境下运行同一个 demo 场景，画面像素级一致（允许 tone map / swapchain 色空间带来的可解释差异）。

### REQ-113 · Shader 的跨后端 IR

不同 GPU 后端的 shader 语言不同（SPIR-V / WGSL / MSL / HLSL / GLSL）。**应当维护一份中立的 shader 源表达**，通过编译管线产生各后端所需的字节码。

- `ShaderCompiler` 接口扩展一个"目标后端"参数
- 内部走"源码 → 中间表示 → 后端字节码"的两步编译
- `ShaderReflector` 的输出结构与后端无关 —— 同一个 shader 在所有后端反射结果**结构化等价**（binding 名、type、offset 一致）

**选型参考**：使用现有的 SPIR-V 作为 IR + 标准工具链转 WGSL/MSL/HLSL；或接入其他 cross-compiler。

**验收**：同一个 shader 的反射结果在两个后端下 JSON 比对完全一致。

### REQ-114 · PipelineCache 适配多后端

- `PipelineBuildDesc` 已经是后端中立的结构（现状），只需要为每个后端新增一条 `from BuildInfo → native pipeline handle` 的实现
- 缓存的 key（`PipelineKey`）与后端无关，不同后端共享同一份 key 空间
- 新后端的预编译流水线和现有 `PipelineCache::preload` 共用入口

**验收**：同一个场景在两个后端都能命中预编译 pipeline cache。

### REQ-115 · Web 构建目标

- 新增一条 toolchain 构建路径，产出 `engine.wasm` + glue code + 静态 HTML 入口
- 运行时自动把 GPU 调用 route 到浏览器原生 API
- 资源加载路径的同步 I/O 假设必须被消除（浏览器没有同步文件读）—— 改成 async / 预加载到虚拟文件系统

**选型参考**：基于 Emscripten 的 CMake toolchain；或其他 C++ → WASM 工具链。

**验收**：启动一个本地 HTTP server，浏览器打开入口 URL 看到同一个 demo 场景。

### REQ-116 · 降级渲染路径（可选）

考虑到部分目标浏览器 / 机器上 WebGPU 尚未成熟，保留一条**能力子集更小、兼容面更广**的降级后端：

- 走类似 OpenGL ES 3 级别的能力
- 缺失的能力（compute / storage texture / HDR）触发 post-process 链降级
- 只作为 fallback，不是主路径

**验收**：在 WebGPU 不可用的浏览器里仍能打开 demo，画面可能简化但不崩。

### REQ-117 · Headless 渲染能力

契合 [P-20](principles.md#p-20-渲染与模拟的可分离)：后端必须能在**没有窗口**的环境初始化。

- `Renderer` 增加 `Mode::Headless`
- Headless 模式下仍能完成 initScene / upload / draw，只是 present 替换为 readback
- 提供 `readback() → bytes` 接口，把当前 color target 导成图片格式
- CLI 入口：`engine-cli render --scene <source> --out <image>`（为 Phase 10 做铺垫）
- 输出图像同时带一份**语义描述**（画面主体、主要颜色、检测到的物体 —— 由 Phase 10 的 agent 附带生成），契合 [P-16 多模态](principles.md#p-16-文本优先--文本唯一)

**验收**：命令行渲染一张场景图，与窗口里的画面像素一致；输出语义描述给 agent 能用来验证。

---

## 里程碑

### M1.1 · HDR + Tone Map Pass 跑通

- REQ-101 + REQ-102 完成
- demo：旋转立方体的画面经过一个独立 tone map pass，效果和现状一致

### M1.2 · 方向光阴影可见

- REQ-103 完成
- demo：立方体 + 地面，单张 shadow map

### M1.3 · IBL 环境光

- REQ-105 + REQ-106 完成
- demo：关闭方向光仍有环境反射

### M1.4 · Bloom + FXAA

- REQ-107 + REQ-108 完成
- demo：带光晕 + 抗锯齿的完整后期链路

### M1.5 · 多光源 + 剔除 + CSM

- REQ-104 + REQ-109 + REQ-110 完成
- demo：`demo_pbr_shadow_ibl` exe，完整的"好看画面"

### M1.6 · Dawn 接入（桌面 native WebGPU）

- REQ-111 + REQ-112 + REQ-113 + REQ-114 完成
- demo：`demo_webgpu_triangle` / `demo_pbr_dawn`，桌面下走 WebGPU 路径

### M1.7 · Web 可打开

- REQ-115 完成
- demo：`demo_pbr_web` 在 Chrome / Edge 里能打开

### M1.8 · Headless + PNG readback

- REQ-117 完成
- demo：`engine-cli render` 命令把场景渲成 PNG —— Phase 10 的 agent demo 会直接复用这个命令

### M1.9 · WebGL2 兜底（可选）

- REQ-116 完成
- demo：在 Firefox 或不支持 WebGPU 的 Chrome 旧版打开 demo 能看到降级画面

---

## 风险 / 未知

- **IBL 预过滤性能**：GGX 重要性采样 mip chain 若在启动期跑 + 没优化会非常慢（秒级）。解决：缓存到磁盘，契合 [P-10 Provenance](principles.md#p-10-资产血统--provenance) 作为"生成型资产"登记。
- **Shadow map 漏光 / 彼得潘**：depth bias 调参痛点。先用固定 bias + slope-scale bias 搞定 90% 场景。
- **PostProcess 链和 FrameGraph 的关系**：当前 `FrameGraph` 以 `FramePass` 为节点，post-process 天然适合这个抽象，但"一个 pass 对应一个 RenderQueue" 的假设在 post-process 上不成立。可能需要引入第二种 pass 形态（fullscreen pass）不走 `buildFromScene`。
- **HDR 格式兼容性**：部分 iGPU 对 16-bit 浮点格式支持差。降级策略待定。
- **新后端库的体量与编译时间**：任何 Web-capable GPU 抽象库都可能带来数分钟级的首次编译开销。解决：固定版本 + 构建缓存。
- **跨后端反射差异**：各后端 shader cross-compiler 的反射 API 不统一。隔离在 `ShaderReflector` 内部，暴露给上层的是一份统一的反射结构。
- **WebGPU / Vulkan 的 binding 抽象差异**：bind group 与 descriptor set 概念相近但细节不同。反射驱动的绑定在两边都需要验证过。
- **Web 环境下的 async I/O**：浏览器没有同步文件读，所有"启动时读文件"的 loader 都需要改成异步或预加载到虚拟文件系统。

---

## 与 AI-Native 原则的契合

- [P-1 确定性](principles.md#p-1-确定性是架构级不变量)：跨后端像素一致是渲染侧确定性的基础验收。
- [P-4 Capability Manifest](principles.md#p-4-单源能力清单capability-manifest)：shader 的反射结果 + pass 描述加入 capability manifest，供 agent 查询"引擎现在支持哪些渲染能力"。
- [P-7 多分辨率观察](principles.md#p-7-多分辨率观察--渐进披露)：渲染统计有 summary (FPS + draw call 总数) / outline (每个 pass 一行) / full (每个 item 的 pipeline key) 三级。
- [P-16 多模态](principles.md#p-16-文本优先--文本唯一)：`rendering.screenshot` 是 agent 的视觉通道，输出同时带语义描述。
- [P-20 渲染与模拟可分离](principles.md#p-20-渲染与模拟的可分离)：headless 模式是 eval harness 的前置条件。

---

## 与现有架构的契合

- `FrameGraph::addPass` 已经支持任意 `FramePass`。新增 pass 只需要新增 `Pass_*` StringID 并在 backend 创建对应 render pass object。
- `PipelineCache::preload` 走 `collectAllPipelineBuildDescs()` 路径，新 pass 的 pipeline 同样会被扫描预构建。
- `RenderQueue` 的 pass / target 过滤正好适配 shadow pass（只有支持 shadow 的 renderable 才会进 queue）。
- 反射驱动材质系统让后处理 shader 的 binding 表自动生成，不需要手写。
- `IGpuResource::getBindingName()` 让环境贴图 / BRDF LUT 这类资源通过命名自动绑定。

---

## 下一步

完成本阶段后可以进入 [Phase 2](phase-2-foundation-layer.md)（若还没做）或 [Phase 3](phase-3-asset-pipeline.md)。IBL 资源的磁盘缓存为 Phase 3 的资产管线提供了一个早期 driver。
