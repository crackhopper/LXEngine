# REQ-079-a: Temporal Anti-Aliasing Foundation

> 2026-06-16 新增：`REQ-079` 阶段关注 Temporal 技术的引入，首先以 TAA 为切入点。它排在 `REQ-078` 异步计算优化之后，避免在基础 render path、资源同步和帧间状态管理尚未稳定时引入历史帧依赖。

## 背景

TAA 不是一个单独 shader 就能完成的后处理。它需要稳定的 motion vector、jittered projection、history buffer、camera cut / resize / exposure 变化处理，以及与 tone mapping、debug dump、offline/realtime 对比的清晰边界。

当前 LXEngine 仍在推进 RenderPathGraph、IBL、OfflineRT、3DGS 和 async execution。Temporal 技术应在这些路径稳定后作为独立阶段引入，避免把历史帧状态塞进 backend 临时变量。

## 目标

1. 定义 temporal frame state：jitter、history texture、frame index、reset reason。
2. 在 RenderPathGraph 中声明 TAA pass 和 history resource。
3. 为 Forward / Deferred 路径生成 motion vector 或明确 unsupported diagnostic。
4. 实现第一版 TAA resolve。
5. 建立 resize、camera cut、scene reload、debug dump 下的 history reset 规则。

## 非目标

- 不实现 DLSS / FSR / XeSS。
- 不做复杂 temporal denoising。
- 不要求 OfflineRT 使用 TAA。
- 不把 TAA 作为 postprocess shader name heuristic 注入。

## 需求

### R1: Temporal State Contract

Temporal state SHALL 是明确的 scene/render runtime 资源，而不是 backend 隐式全局变量。

最低字段：

- frame index。
- jitter offset。
- history color resource。
- history valid flag。
- reset reason。

### R2: Graph-Owned TAA Pass

RenderPathGraph SHALL 声明 TAA pass：

- sources: current color, depth, motion vectors, temporal state。
- target: resolved HDR/LDR color，具体位置由设计决定。
- feature: `feature.temporalAa`。

### R3: Reset Diagnostics

resize、camera cut、scene reload、render path 切换和 history format mismatch SHALL 输出可诊断 reset reason。

## 测试

- graph parser / resource registry 覆盖 temporal resources。
- shader reflection 覆盖 TAA pass binding。
- smoke 测试验证连续帧 history 被使用，resize 后 history reset。

## 修改范围

- RenderPathGraph assets。
- temporal render feature。
- camera jitter / motion vector 相关路径。
- Vulkan render target / history resource 管理。
- editor debug dump 和 realtime profile 输出。

## 实施状态

未实施。
