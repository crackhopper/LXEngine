# Ray Tracing 技术调研

> 状态：**LX 完全空白 / Phase 1 主路径 + temporal 子系统就位后立项**
> 最后更新：2026-04-28
> 目的：把 *Mastering Graphics Programming with Vulkan* 的 4 个 RT 章节（Ch12 基础 / Ch13 shadow / Ch14 DDGI / Ch15 reflection）合并成一个调研。4 章共享 *AS + RT pipeline / ray query + SBT* 整套底盘，3 个特性（shadow / GI / reflection）是消费方，分章建调研会重复 80% 内容。
>
> 参考材料：*Mastering Graphics Programming with Vulkan* · Chapter 12 (Getting Started with Ray Tracing) + Chapter 13 (Revisiting Shadows with Ray Tracing) + Chapter 14 (Adding Dynamic Diffuse Global Illumination with Ray Tracing) + Chapter 15 (Adding Reflections with Ray Tracing)。

## 这个目录是什么

LX 当前完全没有 RT 能力。本调研覆盖 *现代渲染管线* 里 hardware-accelerated ray tracing 的 *基础设施 + 三大消费特性*：

- **AS（Acceleration Structure）**：BLAS / TLAS 数据结构 + 两步构建
- **调用范式**：RT pipeline（SBT-driven）vs ray query（任意 shader 内调用），何时用哪个
- **RT Shadow**（Ch13）：ray query 替代 shadow map，4-pass adaptive sampling + 时空滤波
- **RT DDGI**（Ch14）：light probe + irradiance volume + octahedral 编码 + Chebyshev 防漏，动态 GI
- **RT Reflection**（Ch15）：1 ray/pixel + GGX VNDF + importance sampling lights + SVGF 3-pass denoiser

工业引擎（UE5 Lumen / Frostbite / RDR2 / Cyberpunk RTX）里这些是 *硬件 RT 子系统的标配*，本调研按这个视角组织。

## 建议阅读顺序

| # | 文档 | 讲什么 |
|---|------|------|
| 01 | [Ray Tracing 范式](01-ray-tracing-范式.md) | hardware RT 跟 raster 的根本差异 + BVH 思想 + 6 个 shader stage 总览 |
| 02 | [Acceleration Structure](02-acceleration-structure.md) | BLAS / TLAS 数据结构 + 两步构建 + scratch buffer + 跨帧更新策略 |
| 03 | [RT 管线与 Ray Query](03-RT管线与ray-query.md) | RT pipeline + SBT + payload **vs** ray query；何时用哪个 |
| 04 | [RT Shadow](04-RT-shadow.md) | Ch13：朴素版 + RT Gems advanced 4-pass（variance-driven sample count + Poisson + 时空滤波） |
| 05 | [RT DDGI](05-RT-DDGI.md) | Ch14：light probe + spherical Fibonacci + octahedral + Chebyshev 防漏 + probe offset |
| 06 | [RT Reflection 与 SVGF](06-RT-reflection与SVGF.md) | Ch15：GGX VNDF 采样 + importance sampling lights + SVGF 3-pass denoiser |
| 07 | [LX 当前状态对照](07-LX当前状态对照.md) | 综合 gap 表 + 跟前面 7 个调研的耦合 |
| 08 | [演进路径](08-演进路径.md) | 候选 REQ-A..H 切分 + 时序 + 风险 + Web 兼容性策略 |

## TL;DR

- **RT 共享底盘**：AS（BLAS+TLAS BVH 树）+ RT pipeline（SBT 绑表 + raygen/closest-hit/miss/any-hit/intersection/callable 6 个 shader stage）+ ray query（任意 shader 内同步求交，无 SBT）
- **关键架构选择**：*ray query* 用于简单 binary 可见性（shadow / AO），*RT pipeline* 用于需要 hit shader 算材质的特性（GI / reflection）— 两者必须 *并存*，互补不互替
- **三个消费特性**：
  - **Shadow**（Ch13）：ray query 4-pass，每光源 adaptive 1-N rays，硬阴影 / 软阴影 / 多光源都 native 支持
  - **DDGI**（Ch14）：3D probe 网格 + octahedral 编码 + 极慢 hysteresis（h=0.97），动态 diffuse GI
  - **Reflection**（Ch15）：1 ray/pixel + GGX VNDF + SVGF 3-pass denoiser（temporal accum + variance + 5× wavelet）
- **共享 denoiser 框架**：SVGF 是 LX 处理 RT 噪声的通用 denoiser（不止 reflection 用，将来 RT AO / RT 透射也用）
- **跟前面调研的耦合**：消费 frame-graph（多 pass + 跨帧资源）+ multi-threading/08（AS retire）+ temporal-techniques REQ-A（motion vector / history / blue noise）+ shadows（共存 / fallback）+ gpu-driven-rendering（cluster lighting + bindless texture from RT shader）+ async-compute（AS build 走 compute queue）
- **硬件 / 兼容性风险**：RT 仅 RTX 20+ / RDNA 2+ / Arc 支持；**WebGPU 当前无 RT**——跟 Phase 1 跨平台目标 *直接冲突*，需要双路径 + 条件编译

## 业界形态速查

| 维度 | LX 现状 | Ch12-15 推荐 |
|------|---------|----------------|
| AS 支持 | 无 | BLAS（mesh）+ TLAS（instance） + 两步构建 |
| Ray query 路径 | 无 | `VK_KHR_ray_query` 扩展 + `rayQueryEXT` GLSL |
| RT pipeline 路径 | 无 | `VK_KHR_ray_tracing_pipeline` + SBT + 6 个 shader stage |
| Buffer device address | 无 | `VK_KHR_buffer_device_address`（RT 强依赖） |
| Bindless from RT shader | 无 | hit shader 必须按 mesh index 动态采贴图 |
| Shadow path | shadow map（Ch8） | ray query + adaptive sampling + 时空滤波 |
| GI path | 无 | DDGI probe + octahedral + Chebyshev |
| Reflection path | 无 | 1 ray + GGX VNDF + SVGF |
| Denoiser 框架 | 无 | SVGF（mesh_id + depth_dd + normal + variance 四重权重） |
| 新 G-buffer 通道 | 无 | `mesh_id`（u32）+ `depth_normal_dd`（screen-space ddx/ddy） |

## 跟其它调研的关系

```
multi-threading/08      frame-graph              clustered (gpu-driven)    shadows               temporal-techniques     ray-tracing
─────────────           ──────────────          ─────────────────         ─────────             ──────────────────       ─────────────
RetirePoint             跨帧资源 +              cluster bins/tiles          cubemap shadow         camera jitter +         消费上面五者 +
   ↓                    多 pass 调度               ↓                          ↓                    motion vector +         自己的:
   被本调研               ↓                       被本调研                    被本调研               history texture          - BLAS / TLAS
   消费                  被本调研                  消费(importance              消费(共存 /            ↓                       - SBT / RT pipeline
   (AS retire)           消费                       sampling lights /           fallback,             被本调研                  - ray query
                                                   hit shader 直接光)         非 RT GPU)             消费(motion vector +     - SVGF denoiser
                                                                                                     history reuse)            - DDGI probe 网格
```

**关键点**：本调研是 *最下游消费方*。架构层面引入 *AS + SBT* 两个新概念，但其余基础设施（frame-graph / temporal / cluster）全靠前面调研。

## 何时触发本调研立项

**强触发**：

- Phase 1 主路径（PBR / shadow / cluster lighting / G-buffer）+ temporal-techniques REQ-A（motion vector / history）就位 → 立 RT REQ-A + REQ-B（AS + ray query 路径）
- 视觉品质要求 AAA 级动态 GI（lightmap 烘焙不接受）→ 立 DDGI（REQ-D）
- 需要 *不依赖屏幕的反射*（开放世界、室内大场景） → 立 RT Reflection（REQ-E）

**中触发**：

- shadow map 的 acne / peter-panning / 多光源支持成为画质瓶颈 → RT shadow 替换
- 引入 path tracing / 离线 reference 渲染需求 → RT pipeline 必需

**弱触发**：纯学习 / 探索目的。

**反触发（需谨慎评估）**：

- LX 强调 Web 兼容性 — WebGPU 无 RT，必须 *双路径设计*；如果 Web 是 P0 目标，RT 应延后或仅作 *desktop-only optional* 特性
- 移动端目标 — 移动 GPU 几乎无 hardware RT 支持

## 参考

- *Mastering Graphics Programming with Vulkan* · Chapter 12-15
- Vulkan spec · `VK_KHR_acceleration_structure` / `VK_KHR_ray_tracing_pipeline` / `VK_KHR_ray_query`
- *Ray Tracing Gems* — Importance Sampling of Many Lights on the GPU + Ray Traced Shadows
- *Ray Tracing Gems II* — DDGI deep dive
- Heitz, *Sampling the GGX Distribution of Visible Normals* (2018) — VNDF 采样原始论文
- Schied et al., *Spatiotemporal Variance-Guided Filtering* (2017) — SVGF 原始论文
- Majercik et al., *Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields* (NVIDIA) — DDGI 原始论文
- *Hash Functions for GPU Rendering* — PCG / 工业级 GPU random
- Cyberpunk 2077 RTX Overdrive / UE5 Lumen 技术 talk
- LX 已有调研：
  - [`temporal-techniques/`](../temporal-techniques/README.md) — motion vector / history / blue noise 共享底盘
  - [`shadows/`](../shadows/README.md) — cubemap shadow（共存 fallback）
  - [`gpu-driven-rendering/`](../gpu-driven-rendering/README.md) — cluster lighting（importance sampling 支撑）+ bindless
  - [`frame-graph/`](../frame-graph/README.md) — 多 pass + 跨帧资源
  - [`async-compute/`](../async-compute/README.md) — AS build 候选 async
  - [`multi-threading/08`](../multi-threading/08-Timeline与资源退休模型.md) — AS retire 模型
