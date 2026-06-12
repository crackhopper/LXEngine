# REQ-074-f: BMW M6 Package Load Performance Comparison

> 2026-06-12 新增：本 REQ 对 `REQ-074-c/d/e` 的 package 和 pipeline cache 工作做性能验收。目标是给出 BMW M6 场景 source load 与 package restore 的时间对比，并证明 package + pipeline cache 确实改善加载路径。

## 背景

Package 和 pipeline cache 的目标是场景加载速度，而不是仅让格式 round-trip。BMW M6 是当前最重要的大场景验收对象，必须用它测量 source parse、package restore、GPU upload/pipeline preload 的时间差异。

## 目标

1. 建立 BMW M6 source load timing profile。
2. 建立 BMW M6 package restore timing profile。
3. 分离 CPU restore、texture payload restore、GPU upload、pipeline cache import/preload 等阶段。
4. 输出可重复的对比报告。

## 需求

### R1: Timing Instrumentation

This REQ SHALL add the synchronous timing instrumentation needed for the benchmark. It does not require the async loading task/UI system. Load path SHALL report:

- source scene parse time。
- material parse / projection time。
- mesh/geometry load time。
- texture decode/compression or package payload restore time。
- SceneResourceTable construction/restore time。
- RenderPathGraph validation/FrameGraph compile time。
- GPU upload time。
- pipeline cache import/preload/build time, including realtime raster pipelines and OfflineRT compute pipeline if the benchmark profile requests offline validation。
- total time to first valid frame or validation render。

### R2: BMW M6 Benchmark

Benchmark SHALL run:

```text
BMW M6 source scene load
BMW M6 package restore without backend cache
BMW M6 package restore with compatible Vulkan pipeline cache
```

Each run should record environment/build metadata and repeat count.

### R3: Report

Report SHALL include:

- timing table。
- speedup ratio。
- cache hit/miss/preload count。
- package size。
- texture payload size。
- diagnostics for fallback or unsupported features。

### R4: Correctness Gate

Performance comparison must not pass if package restore output is invalid.

Minimum correctness:

- SceneResourceTable root hash matches source parse。
- validation render output non-black。
- RenderPathGraph and material counts match。

## 测试

### T1: Timing Smoke

Small scene emits all required timing phases.

### T2: BMW M6 Benchmark Command

Command or test fixture runs BMW M6 source/package/cache comparison and writes report.

### T3: Correctness Before Timing

Corrupt package or cache mismatch is reported as correctness/fallback diagnostic, not hidden inside performance result.

## 修改范围

- load/profile timing utilities
- package restore CLI or test entry
- BMW M6 validation fixtures
- report generation

## 边界与约束

- 不要求 fixed target speedup number until baseline is measured。
- 不实现 editor loading UI。
- 不 include offline/realtime image equivalence; `REQ-075-a` handles that。

## 依赖

- `REQ-074-d`: CPU package restore。
- `REQ-074-e`: GPU pipeline cache restore。
- `REQ-073-e`: OfflineRT config hard cut and smoke。

## 后续工作

- `REQ-074-g`: post-package hard cut and cleanup。

## 实施状态

未实施。
