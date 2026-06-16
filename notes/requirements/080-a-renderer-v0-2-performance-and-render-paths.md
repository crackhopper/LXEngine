# REQ-080-a: Renderer v0.2 Performance And Render Path Expansion

> 2026-06-16 新增：`REQ-080` 阶段再次做架构治理和帧率优化，并补齐 Deferred、Forward+ 等 render path 能力。完成到本阶段后，可以把 LXEngine v0.2.0 的游戏引擎渲染器视为初步完善。

## 背景

`REQ-073` 到 `REQ-079` 分别处理 PBR/IBL 当前闭环、BMW/package/load、OfflineRT 质量、治理、3DGS、异步计算和 Temporal。到 `REQ-080` 时，renderer 应该从“功能能跑”进入“多 render path 可选择、性能可观察、复杂场景可交互”的阶段。

本 REQ 是 `REQ-080` 的阶段总入口。具体非 mesh render path 架构扩展由 `REQ-076-c` 继续承接。

## 目标

1. 复核 Forward / Deferred render path 的默认质量和性能。
2. 引入 Forward+ 或等价 tiled/clustered light culling 路线。
3. 为多 render path 建立统一配置、profile 和 smoke。
4. 清理 `REQ-073` 到 `REQ-079` 中保留的临时性能 fallback。
5. 给 v0.2.0 renderer 建立性能基线和最低可用标准。

## 非目标

- 不重新定义 Material v3 合同。
- 不替代 `REQ-077` 的 3DGS 管线。
- 不替代 `REQ-079` 的 TAA。
- 不把所有性能优化塞进一个需求；本 REQ 负责阶段入口和第一批 render path 扩展。

## 需求

### R1: Render Path Profiles

SHALL 定义 renderer profile：

- Forward。
- Deferred。
- Forward+ 或等价 tiled/clustered path。
- Debug / validation profile。

每个 profile SHALL 指向明确 RenderPathGraph asset、feature set 和 smoke scene。

### R2: Performance Baseline

SHALL 建立最低性能观测：

- frame time breakdown。
- draw / dispatch count。
- render target memory。
- pipeline/cache hit 情况。
- GPU upload / transient resource 分配统计。

### R3: Temporary Path Cleanup

SHALL 审计前序阶段遗留的临时路径：

- 手写 feature UBO。
- render path name heuristic。
- placeholder resource。
- debug-only material / shader fallback。

默认 profile 不得依赖这些路径。

## 测试

- Forward / Deferred / Forward+ profile smoke。
- BMW M6 或同等级复杂场景的 frame time report。
- rg audit 覆盖主要临时路径 token。

## 修改范围

- render path graph assets。
- renderer profile / scene output profile。
- Vulkan render path execution。
- performance diagnostics。
- notes / source analysis。

## 实施状态

未实施。
