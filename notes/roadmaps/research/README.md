# 技术预研 (research/)

> 目录名：`research/`（原 `核心技术演进调研/`，已改为英文以对齐其他路径规范）。
>
> 本目录用于记录未来可能引入、但**当前阶段不实施**的核心渲染/引擎技术调研。与 `phase-*` 阶段文档不同，这里的内容**不进入路线图时间线**，仅作为技术储备与参考。

## 用途

- 记录业界主流引擎的做法与演进方向
- 为 LX Engine 架构选型提供事前参考，避免日后重复调研
- 标注"何时考虑引入"的触发条件，而不是"什么时候一定要做"

## 当前收录

| 主题 | 文件 | 简述 | 调研日期 |
|------|------|------|---------|
| Bindless Texture | [bindless-texture/README.md](bindless-texture/README.md) | bindless 资源绑定在现代引擎的采用情况、对 pipeline 数量的影响、ubershader/permutation 策略、nonuniform 深入、业界对比、LX 演进路径（6 篇 + 入口索引） | 2026-04-23 |
| Pipeline Cache | [pipeline-cache/README.md](pipeline-cache/README.md) | 应用层对象缓存 vs 驱动层 `VkPipelineCache` 两层关系、LX 当前实现、若接入底层机制的演进路径 | 2026-04-23 |
| Multi-threading | [multi-threading/README.md](multi-threading/README.md) | task-based vs fiber-based 选型、enkiTS + pinned task 异步 I/O 模式、LX 当前单线程现状、分阶段演进路径 | 2026-04-23 |
| Frame Graph | [frame-graph/README.md](frame-graph/README.md) | 完整 frame graph 形态（数据驱动 + DAG + 自动 barrier + aliasing）vs LX 当前最初阶骨架的差距、REQ-035..041 推进路径、风险点定位（6 篇 + 入口索引） | 2026-04-27 |
| Async Compute | [async-compute/README.md](async-compute/README.md) | 多 queue 调度 + compute pipeline + ownership transfer 的业界形态 vs LX 完全空白的差距；与 multi-threading/08 timeline 模型 + frame-graph REQ-038 barrier 推导的耦合时序；REQ-A..F 切分（6 篇 + 入口索引） | 2026-04-27 |
| GPU-Driven Rendering + Clustered Lighting | [gpu-driven-rendering/README.md](gpu-driven-rendering/README.md) | Ch6 (GPU-driven) + Ch7 (Clustered Deferred) 合并调研：meshlet + 两阶段 Hi-Z + indirect drawing + mesh shader vs compute path + G-buffer + Activision 1D bin / 2D tile light culling；REQ-A..H 切分；是 multi-threading / frame-graph / async-compute 三个前置调研的 *消费方*（6 篇 + 入口索引） | 2026-04-27 |
| Shadows + Sparse Resources | [shadows/README.md](shadows/README.md) | Ch8 调研：shadow mapping 全景 + cubemap array + layered rendering + mesh shader 4 步阴影流水线 + Vulkan sparse residency（page pool / vkQueueBindSparse / 跨主题基础能力）+ per-light importance metric；REQ-A..G 切分；最下游消费方，依赖 multi-threading / frame-graph / async-compute / gpu-driven-rendering 全部就位（6 篇 + 入口索引） | 2026-04-28 |
| Variable Rate Shading + Specialization Constants | [variable-rate-shading/README.md](variable-rate-shading/README.md) | Ch9 调研（轻量 4 篇）：VRS 三种集成方式（per-draw / per-primitive / image attachment）+ Sobel edge detection + SPIR-V specialization constants（编译期常量延迟绑定）+ shader system 反射扩展；2 个候选 REQ；**优先级低，Phase 1 主路径完成后再考虑**（独立优化层，不阻塞其它调研） | 2026-04-28 |
| Temporal Techniques (Volumetric Fog + TAA) | [temporal-techniques/README.md](temporal-techniques/README.md) | Ch10 + Ch11 合并调研：温度共享基础设施（camera jitter / Halton / motion vector / history texture / blue noise + golden ratio）+ Wronski 2014 froxel 5 步 compute pipeline（injection / scattering / spatial / temporal / integration）+ TAA 6 步算法 + 5 大改进（closest-depth / Catmull-Rom / variance clip / dynamic resolve / sharpen）；REQ-A..F 切分；最下游消费方，依赖 frame-graph / multi-threading/08 / clustered lighting / cubemap shadow / async-compute 全部就位（6 篇 + 入口索引） | 2026-04-28 |
| Ray Tracing (Ch12-15 全) | [ray-tracing/README.md](ray-tracing/README.md) | Ch12 + Ch13 + Ch14 + Ch15 合并调研：硬件 RT 范式 + BLAS/TLAS 两步构建 + RT pipeline (SBT) **vs** ray query 双路径 + RT shadow（Ch13 朴素 + RT Gems 4-pass adaptive）+ DDGI（Ch14 light probe + octahedral + Chebyshev 防漏）+ RT reflection（Ch15 GGX VNDF + SVGF 3-pass 通用 denoiser）；REQ-A..H 切分；**最大复杂度调研，desktop-only 加分项**（WebGPU 无 RT，跟 Phase 1 跨平台目标冲突，需双路径 fallback）；最下游消费方，依赖前面 7 个调研全部就位（8 篇 + 入口索引） | 2026-04-28 |

## 写入规范

新增一篇技术调研时，文件头建议包含：

```
> 状态：仅记录 / 已立项 / 已废弃
> 调研日期：YYYY-MM-DD
> 目的：一句话说明为什么记录此技术
```

正文推荐结构：

1. **业界采用情况** —— 哪些引擎在用，成熟度如何
2. **对现有架构的影响** —— 如果引入，需要改动哪些子系统
3. **替代方案对比** —— 至少列出 2 种备选路线
4. **对 LX Engine 的演进建议** —— 分步走的增量引入路径
5. **风险提示** —— 性能、兼容性、平台支持等
6. **参考资料**
