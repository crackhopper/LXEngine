# Phase 5 · 物理

> **目标**：引入碰撞 / 刚体 / 射线 / 角色控制器。场景内物体“能碰到东西”。
>
> **依赖**：Phase 2（`Transform` + `Clock` fixedUpdate + 命令层）。与 Phase 4 独立，可并行。
>
> **可交付**：
> - `demo_physics_box_pile` — 一堆盒子从空中掉下碰撞堆叠
> - `demo_character_controller` — 角色用 CCT 沿地面行走 / 上台阶 / 跳跃

## 当前实施状态（2026-04-24）

**未开工**。无任何物理基础设施。

| 条目 | 状态 |
|------|------|
| 刚体 / 碰撞 / 射线 | ❌ REQ-501 / REQ-503 |
| 碰撞层 / 掩码 | ❌ REQ-502 |
| 角色控制器（CCT） | ❌ REQ-504 |
| Joint / 约束 | ❌ REQ-505 |
| Ragdoll 过渡 | ❌ REQ-506 |

## 范围与边界

**做**：刚体（static / dynamic / kinematic） / 基础碰撞形状（sphere / box / capsule / convex / mesh） / 射线 + overlap 查询 / 碰撞层掩码 / capsule CCT / Joint 约束 / 动画 → 物理 ragdoll 过渡。

**不做**：流体 / 软体 / 布料 / 载具 / GPU 加速物理。

## 前置条件

- Phase 2 `Transform` + `Clock` fixedUpdate
- Phase 3 collision shape / mesh 资产加载

## 工作分解

### REQ-501 · 物理引擎接入

选型参考（可替换）：Jolt / Bullet / PhysX。默认推荐 [Jolt](https://github.com/jrouwe/JoltPhysics)（现代 C++ / 高性能 / MIT）。

- 抽象 `IPhysicsWorld` + `IRigidBody`，实现放 `src/infra/physics/`
- `PhysicsSystem` 每 fixedUpdate 推一步

**验收**：`PhysicsWorld::addRigidBody + step(dt)` 让盒子自由下落。

### REQ-502 · 碰撞层 + 掩码

- `CollisionLayer`（位掩码）
- `CollisionMask` 决定“我能碰到谁”
- 与 [P-5 语义查询](principles.md#p-5-语义查询层) 对齐，agent 按层过滤

**验收**：player 与 enemy 不互穿，但都能碰到地面。

### REQ-503 · 射线 / Overlap 查询

- `scene.queryRay(origin, dir, maxDist, mask) → hit`
- `scene.queryBox(bounds, mask) → nodes`
- 数据源与 Phase 2 REQ-209 空间索引共用或独立（选型决定）

**验收**：屏幕点击 → 相机发射射线 → 被点中物体高亮。

### REQ-504 · 角色控制器

- Capsule-based CCT
- Ground snap / step offset / 斜坡 slide

**验收**：WASD 操作角色沿地面行走；上 0.3m 台阶；45° 以上斜坡不爬。

### REQ-505 · Joint / 约束

- 基本 joint：fixed / hinge / slider / point-to-point
- 暴露给 Phase 6 脚本层

**验收**：hinge 接两个盒子能摆动。

### REQ-506 · Ragdoll 过渡

- 动画 pose 映射到骨骼链物理形体
- 动画 ↔ 物理开关
- 物理 → 动画混回

**验收**：按 K 角色变 ragdoll 倒下；再按 K 恢复动画。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M5.1 · 引擎接入 | REQ-501 | 盒子自由下落 |
| M5.2 · 层 + 查询 | REQ-502 + REQ-503 | 射线命中正确物体 |
| M5.3 · 角色控制器 | REQ-504 | WASD 上斜坡 |
| M5.4 · Joint + Ragdoll | REQ-505 + REQ-506 | 角色 ragdoll 倒下再站起 |

## 风险 / 未知

- **线程模型**：物理库通常自带并行调度，与主循环对齐需显式 step 边界。
- **确定性**：契合 [P-1](principles.md#p-1-确定性是架构级不变量) 需选支持 deterministic mode 的库（Jolt 支持）。
- **库体量**：Jolt 编译 ≈ 10MB；首次集成耗时。
- **碰撞形状与美术形状分离**：美术 mesh 不能直接当 collider，需要 import pipeline 支持 collision mesh。

## 与 AI-Native 原则契合

- [P-1 确定性](principles.md#p-1-确定性是架构级不变量)：物理步长 + seed 是 replay 的基础。
- [P-5 语义查询](principles.md#p-5-语义查询层)：碰撞层是 filter 的一等公民。
- [P-8 Dry-run](principles.md#p-8-dry-run--影子状态)：spawn 物体前 dry-run 能预测碰撞与穿透。

## 与现有架构契合

- `SceneNode` 持 `Transform`（Phase 2），物理结果写回 Transform，其余子系统自动看到。
- `Clock` 已有 smoothedDeltaTime；fixedUpdate 将在 Phase 2 REQ-206 加入 `EngineLoop`。

## 下一步

[Phase 6 Gameplay](phase-6-gameplay-layer.md)：碰撞事件由 TS 脚本消费。
