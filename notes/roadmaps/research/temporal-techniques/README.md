# Temporal Techniques 技术调研

> 状态：**LX 完全空白 / 待 Phase 1 主路径完成后立项**
> 最后更新:2026-04-28
> 目的:把 *Volumetric Fog*（Ch10）和 *Temporal Anti-Aliasing*（Ch11）合并成一个调研。两章共享 60%+ 的 *temporal infrastructure*（camera jitter / motion vectors / history reprojection / blue noise jitter），分开调研会重复。
>
> 参考材料:*Mastering Graphics Programming with Vulkan* · Chapter 10（Adding Volumetric Fog）+ Chapter 11（Temporal Anti-Aliasing）。

## 这个目录是什么

LX 当前完全没有 temporal 渲染能力。本调研覆盖 *现代渲染管线* 里几乎所有 temporal-based 技术的核心机制:

- **Volumetric Fog**(Ch10):用 froxel 3D 纹理 + 5 步 compute pipeline 实现 real-time 体积雾
- **TAA**(Ch11):用 camera jitter + history reprojection + constraint 实现高质量 anti-aliasing
- **共享基础设施**:motion vector / previous-frame view-projection / history texture 双 buffer / blue noise 动画化

工业引擎(UE / Frostbite / RDR2)里这些是 *单一 temporal 子系统*,被多个特性消费(volumetric fog / TAA / SSR / SSAO / DoF)。本调研按这个视角组织。

## 建议阅读顺序

| # | 文档 | 讲什么 |
|---|------|------|
| 01 | [Temporal 范式](01-temporal-范式.md) | camera jitter + motion vector + history reprojection 通用框架;blue noise + golden ratio 抖动 |
| 02 | [Volumetric Fog 原理](02-volumetric-fog-原理.md) | volumetric rendering 物理基础(scattering/extinction/transmittance/phase function)+ froxel 数据结构 |
| 03 | [Volumetric Fog Pipeline](03-volumetric-fog-pipeline.md) | 5 步 compute pipeline(injection / scattering / spatial filter / temporal filter / integration)+ 应用到场景 |
| 04 | [TAA](04-TAA.md) | TAA 算法(jitter / reproject / constrain / resolve)+ 5 大改进点 + blurriness 缓解 |
| 05 | [LX 当前状态对照](05-LX当前状态对照.md) | 综合 gap 表 + 跟前面 5 个调研的耦合 |
| 06 | [演进路径](06-演进路径.md) | 候选 REQ-A..F 切分 + 时序 + 风险 |

## TL;DR

- **Temporal techniques** = 跨多帧累积样本的渲染范式。共享 3 个核心机制:**camera jitter**(per-frame 偏移)、**motion vectors**(per-pixel 重投影)、**history texture**(上一帧结果作为输入)
- **Volumetric fog**:把视锥切 froxel(64×64×128)+ 5 步 compute pipeline 算 scattering/transmittance + 应用到场景(Beer-Lambert)
- **TAA**:Halton jitter camera + reproject history + variance clip 防 ghost + 1:9 blend
- **跨主题协同**:fog 的 dithering 配合 TAA 自然消除 → 画质大幅提升,这是为什么两章必须放一起
- **推进路径**:6 个候选 REQ 分两族 — *temporal 基础设施族*(A/B/C)+ *特性族*(D/E/F);A/B/C 早做,D/E 等前面调研就位
- **跟前面调研的耦合**:消费 frame-graph(多 pass + 跨帧资源)、clustered lighting(volumetric scattering 用 cluster)、shadows(雾受 shadow 影响)、async-compute(compute pipeline)、multi-threading/08(history texture retire)

## 业界形态速查

| 维度 | LX 现状 | Ch10+Ch11 推荐 |
|------|---------|----------------|
| Camera jitter | 无 | Halton sequence + sub-pixel offset |
| Motion vectors | 无 | R16G16 buffer + per-pixel + per-vertex 双源 |
| History texture | 无 | 双 buffer 切换,跨帧 retire |
| Volumetric fog | 无 | froxel + 5 步 compute |
| TAA | 无 | jitter + reproject + variance clip + resolve |
| Phase function | 无 | Henyey-Greenstein |
| Blue noise jitter | 无 | golden ratio 时间偏移 |
| Sharpen post-process | 无 | luminance-based 高通 |

## 跟其它调研的关系

```
multi-threading/08      frame-graph             clustered (gpu-driven)    shadows               temporal-techniques
─────────────           ──────────────          ─────────────────         ─────────             ──────────────────
RetirePoint             跨帧资源 +              cluster 数据结构          cubemap shadow          消费上面四者 +
   ↓                    多 pass 调度               ↓                          ↓                  自己的:
   被本调研               ↓                       被本调研                    被本调研             - camera jitter
   消费                  被本调研                  消费(volumetric            消费(雾受             - history reprojection
                         消费                       scattering)              shadow 影响)         - motion vector
                                                                                                  - froxel 数据结构
```

**关键点**:本调研是 *消费方*。架构层面不引入新东西,但把前面的能力组合成 *现代视觉特性*(雾 + AA)。

## 何时触发本调研立项

强触发:

- Phase 1 主路径(PBR / shadow / clustered lighting)完成 → 触发 A + E (TAA)
- 引入大场景 / 室内场景 → 触发 D (volumetric fog) 增强氛围
- 测出 fragment 闪烁(temporal aliasing)严重影响画质 → 强触发 TAA

中触发:

- 视觉品质升级阶段
- 引入半透明粒子(雾跟粒子协同)

弱触发:纯学习目的。

## 参考

- *Mastering Graphics Programming with Vulkan* · Chapter 10(Adding Volumetric Fog)+ Chapter 11(Temporal Anti-Aliasing)
- Bart Wronski, *Volumetric Fog*, SIGGRAPH 2014 — froxel 算法的原始 paper
- Brian Karis, *High Quality Temporal Supersampling*, SIGGRAPH 2014 — TAA 的现代实践
- *Physically Based Sky, Atmosphere and Cloud Rendering*(Hillaire) — phase function + 大气
- Red Dead Redemption 2 SIGGRAPH presentation — fog + clouds + ray marching 协同
- LX 已有调研:
  - [`gpu-driven-rendering/`](../gpu-driven-rendering/README.md) — clustered lighting 提供 cluster 数据
  - [`shadows/`](../shadows/README.md) — cubemap shadow 提供 fog shadow 受体
  - [`frame-graph/`](../frame-graph/README.md) — 多 pass + 跨帧资源
  - [`multi-threading/08`](../multi-threading/08-Timeline与资源退休模型.md) — history retire 模型
