# Phase 5 · Physics

> 目标：给场景增加碰撞、刚体、射线查询和角色控制器。首版走 CPU 物理，不把 GPU/async compute 作为前置。

## 决策

| 问题 | 决策 |
|---|---|
| 先 CPU 还是 GPU 物理 | 先 CPU |
| 是否要求 async compute | 不要求 |
| 是否自研物理 | 不优先，自研会拖慢主线 |
| 推荐集成方向 | Jolt / Bullet 这类成熟库，外层包 `IPhysicsWorld` |

## 实施顺序

| 顺序 | 主题 |
|---|---|
| 1 | `IPhysicsWorld` / `RigidBodyComponent` |
| 2 | collider shape：box/sphere/capsule/mesh |
| 3 | fixed step 接入 Phase 2 time model |
| 4 | ray / overlap query |
| 5 | collision layers/masks |
| 6 | capsule character controller |
| 7 | animation ragdoll bridge |

## 与 Task-based 并行的关系

CPU 物理库可能内部有 task scheduler，但这不是 Phase 1 task-based render pass 并行的前置。GPU 物理、粒子或 compute cloth 才会强触发 async compute。

## 继续阅读

- [Phase 2 · Foundation Layer](phase-2-foundation-layer.md)
- [Async Compute 调研](../research/async-compute/README.md)
