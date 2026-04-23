# REQ-028: IBL 环境光资源与场景接入

## 背景

Phase 1 roadmap 已经把 Environment Map Loader 和 IBL Prefilter 列为 REQ-105 / REQ-106，但当前活跃 requirement 列表里还没有一个可以被概念层直接引用的“IBL 环境光资源如何进入 scene”文档。

这使得光源系统和资产系统都只能说“未来会有 IBL”，却没有一个稳定 requirement 可挂接。

## 目标

1. 把环境贴图、irradiance、prefilter、BRDF LUT 收敛成一组 scene-level 资源概念。
2. 明确这些资源如何从资产进入 scene。
3. 让概念层可以稳定标注“IBL 尚未实现，但需求已立”。

## 需求

### R1: 定义一组最小 IBL 资源

至少覆盖：

- environment cubemap
- diffuse irradiance cubemap
- specular prefilter cubemap
- BRDF LUT

### R2: IBL 资源必须有 scene-level 挂接点

- `Scene` 或等价 scene-level 容器需要能持有当前生效的一组 IBL 资源。
- queue / descriptor 组装时，需要能把这些资源作为 scene-level inputs 追加到对应 pass。

### R3: 资产入口与运行时入口要分开说明

- 环境贴图 loader 和预过滤过程属于资产 / 生成阶段。
- 最终进入 scene 的是已经可绑定的运行时资源集合。

### R4: 概念层要明确 IBL 不等于普通 light object

- IBL 属于环境光资源，不是 `LightBase` 的一个子类。
- 它和 `DirectionalLight` / `SpotLight` 并列存在于“光照系统”中，但运行时形态不同。

### R5: roadmap 依赖需要在文档里钉住

- 本 REQ 依赖 `REQ-105` 和 `REQ-106` 的资源加载与预过滤能力。
- 本 REQ 负责 scene-level 接入与文档归口，不重复定义底层预过滤算法。

## 修改范围

- `notes/concepts/assets/`
- `notes/concepts/light/`
- `notes/subsystems/scene.md`
- 未来相关 runtime resource / scene-level descriptor 路径

## 依赖

- `notes/roadmaps/main-roadmap/phase-1-rendering-depth.md` 中的 `REQ-105` / `REQ-106`
- [`REQ-010`](finished/010-test-assets-and-layout.md) 的 HDR 资产输入

## 实施状态

2026-04-16 核查结果：未开始。

- 代码里还没有 IBL scene-level resource
- notes 侧也仍明确标注 IBL 尚未正式接入 scene

本次核查后，剩余工作统一并入 [`REQ-034`](034-remaining-validated-backlog.md)。
