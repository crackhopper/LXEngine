# Variable Rate Shading + Specialization Constants 技术调研

> 状态：**LX 完全空白 / 优先级低 — Phase 1 主路径完成后再考虑**
> 最后更新:2026-04-28
> 目的:记录 VRS（Variable Rate Shading）和 Specialization Constants 这两个 *相对独立的工程优化* 在 LX 路线里的位置。两者都是性能 tuning 工具,不涉及架构改动。
>
> 参考材料:*Mastering Graphics Programming with Vulkan* · Chapter 9（Implementing Variable Rate Shading）。

## 这个目录是什么 / 为什么轻量

跟前面 5 个调研（multi-threading / frame-graph / async-compute / gpu-driven-rendering / shadows）相比,本调研体量小:

- VRS 是 *单点性能优化*（fragment shader 工作量减少）,跟架构无关
- Specialization constants 是 *基础工程能力*,扩展 shader system 即可
- 都不引入新的 pipeline 类型 / 同步原语 / 跨调研依赖

**所以本调研只 4 篇文档**（README + 3 章）,远少于其它调研的 7 篇。

## 优先级判断

**LX 当前不应该做这两个**。理由:

- VRS 优化的是 *已有 fragment shading 工作量*,但 LX 现在没有重 shader（PBR 还没完整、deferred 还没做、多光源 shader 还没建）
- Spec constants 用于 *shader 变体管理*,但 LX 当前 shader 数量少,#define 路径够用
- 这两个的收益 *依赖前置工作完成* —— shader 多了、材质多了、性能瓶颈出现了,引入才有意义

工程上的位置:**Phase 1 主路径完成后的 *性能 tuning 阶段* 才考虑**。

## 建议阅读顺序

| # | 文档 | 讲什么 |
|---|------|------|
| 01 | [VRS 基础](01-VRS基础.md) | shading rate 概念、三种集成方式、Sobel 边缘检测 |
| 02 | [Specialization Constants](02-specialization-constants.md) | SPIR-V 编译期常量、跟 #define / push constant 对比、shader 反射扩展 |
| 03 | [LX 演进与触发条件](03-LX演进与触发条件.md) | gap 分析 + 候选 REQ + 何时触发 |

## TL;DR

- **VRS** = fragment shader 执行频率可变。1×1 默认、2×2 减 75% shading。三种集成:per-draw / per-primitive / image attachment。基于 Sobel edge detection 决定 rate
- **Specialization constants** = SPIR-V 编译期常量但 *pipeline 创建时填值*。比 #define 灵活、比 push constant 高效
- **跟前面调研的耦合**:弱。VRS 跟 frame-graph attachment 模型有点关系;spec constants 跟 shader system 有关系;其它无强耦合
- **何时做**:Phase 1 主路径（PBR / shadow / 多光源 / clustered lighting）完成后,作为性能 tuning 阶段引入

## 业界形态速查

| 维度 | LX 现状 | Ch9 推荐 |
|------|---------|--------|
| Fragment shading rate 控制 | 无（1×1 默认） | 1×1 / 2×1 / 1×2 / 2×2 可变 |
| Per-tile rate 决策 | 无 | Sobel edge detection |
| Shader specialization | 无（用 #define） | `VK_KHR_fragment_shading_rate` |
| Subgroup size 自适应 | 无 | spec constant 跟硬件 query 联动 |

## 跟其它调研的关系

```
其它五个调研（架构层）          本调研（工程优化层）
─────────────────              ──────────────────
multi-threading / frame-graph / 解耦
async-compute / gpu-driven /   
shadows                        
   ↓                             ↓
   提供基础设施                   只在主架构稳定后才有意义
   优先级 高                      优先级 低
```

本调研 *不影响* 其它调研的任何决策。是 *叠加层* 不是 *依赖层*。

## 何时触发本调研立项

**条件全部满足时才做**:

- Phase 1 PBR / Shadow / Clustered Lighting 主路径已完成
- Shader 数量 > 30,#define 变体管理变得繁琐
- Profiling 显示 fragment shading 是 hotspot,VRS 有可观收益
- 硬件兼容性可控（VRS 需要 Pascal+ / RDNA 2+）

任一条件不满足,就 *暂不立项*,等条件到再开。

## 参考

- *Mastering Graphics Programming with Vulkan* · Chapter 9（Implementing Variable Rate Shading）
- LX 已有调研:本调研 *不依赖任何前置调研*,是独立优化层
