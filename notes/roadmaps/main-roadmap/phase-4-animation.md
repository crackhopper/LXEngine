# Phase 4 · Animation

> 目标：从已有 `Skeleton` / skinning shader 入口，推进到 animation clip、player 和状态机。

## 当前基础

| 已有 | 缺口 |
|---|---|
| `Skeleton` / `SkeletonUBO` | `AnimationClip` 资产 |
| `USE_SKINNING` shader variant | clip 采样与 bone pose 更新 |
| `SkeletonComponent` | `AnimationPlayer` 组件 |
| scene component 模型 | 状态机、混合、root motion |

## 实施顺序

| 顺序 | 主题 |
|---|---|
| 1 | glTF animation channel → `AnimationClip` |
| 2 | `AnimationPlayer` 每帧采样并写 `SkeletonUBO` |
| 3 | idle/walk/run 状态机 |
| 4 | clip blend 与 bone mask |
| 5 | root motion |

## 前置与后置

Phase 4 需要 Phase 3 资产管线来稳定加载 animation clip；不阻塞 Phase 1 shadow/G-Buffer。它会被 Phase 6 gameplay 和 Phase 11 AI 角色生成消费。

## 继续阅读

- [Skeleton 子系统](../../subsystems/skeleton.md)
- [Phase 6 · Gameplay](phase-6-gameplay-layer.md)
- [Phase 11 · AI Asset Generation](phase-11-ai-asset-generation.md)
