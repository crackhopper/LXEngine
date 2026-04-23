# Phase 5 · 物理

> **目标**：引入碰撞、刚体、射线查询、角色控制器；让场景里的物体"能碰到东西"。
>
> **依赖**：Phase 2（Transform + Clock 的 fixedUpdate + 命令层）。和 Phase 4 独立，可并行。
>
> **可交付**：`demo_physics_pong` — 一个盒子里有若干球在重力 + 弹性碰撞下运动，射线点击爆炸。

## 范围与边界

**做**：
- 接入一个**成熟的第三方物理引擎**作为底层
- 刚体组件（静态 / 动态 / kinematic）
- 碰撞形状：box / sphere / capsule / convex hull / triangle mesh
- 射线查询 / sweep / overlap 查询
- 碰撞事件（enter / stay / exit）
- 角色控制器（capsule + slide）
- 物理调试绘制
- 触发器（trigger volume，不产生 force）
- **确定性物理模式**：支持固定 seed + 固定顺序的可重放配置

**不做**：
- 布料 / 流体
- 软体
- 载具（vehicle）
- 物理驱动的动画（ragdoll 先跳过）
- 自写物理引擎

---

## 前置条件

- Phase 2：`Transform` + `Clock` 的 fixed step + 命令层
- 构建系统能接入外部 CMake / 源码依赖

---

## 工作分解

### REQ-501 · 物理接口抽象 + 第三方引擎接入

- `core/physics/physics_world.hpp` 定义纯接口 `IPhysicsWorld`（core 侧看到的抽象）
- `infra/physics/<vendor>_world.{hpp,cpp}` 实现接口 —— 具体物理引擎是可替换的实现细节
- 所有对第三方类型的引用**只**出现在 infra 侧；core 层看到的都是中立接口
- 物理世界的生命周期、步进、事件订阅都通过接口完成

**选型参考（可替换）**：成熟的开源 C++ 物理引擎中任选 —— 评估标准是：C++ 17/20 兼容、CMake 友好、确定性支持、活跃维护、License 友好。核心实现绑定 1 个，接口允许未来换。

**验收**：空物理世界能创建、步进、销毁，不泄漏；切换实现时 core 代码无变动。

### REQ-502 · 碰撞形状

```cpp
class CollisionShape {
public:
    enum class Kind { Box, Sphere, Capsule, ConvexHull, TriangleMesh };
    // ...
};

BoxShape        // half-extents
SphereShape     // radius
CapsuleShape    // radius + half-height
ConvexHullShape // points
TriangleMeshShape // 来自 Mesh（静态）
```

- 是 asset：能按 GUID 引用，能复用
- 从 `Mesh` 自动生成 convex hull（V-HACD 之类复杂算法先不做，直接 Jolt 的 `ConvexHullShapeSettings`）

**验收**：能构造每种形状并插入物理世界。

### REQ-503 · RigidBody 组件

```cpp
enum class BodyType { Static, Kinematic, Dynamic };

class RigidBody {
public:
    BodyType type;
    std::shared_ptr<CollisionShape> shape;
    float mass;
    float friction;
    float restitution;
    bool  isTrigger;

    Vec3f getLinearVelocity() const;
    void  setLinearVelocity(Vec3f v);
    void  addForce(Vec3f f);
    void  addImpulse(Vec3f i);
};
```

- 挂在 `SceneNode` 上，与 `Transform` 同步
- Dynamic：物理世界驱动 Transform
- Kinematic：Transform 驱动物理世界（角色控制器、平台）
- Static：只提供碰撞，不被移动
- Trigger：不产生 force，只发 event

**验收**：Dynamic 立方体从高处落下，在静态地面上反弹停下。

### REQ-504 · 物理步进 + Transform 同步

在 `Engine::onFixedUpdate` 里：

```cpp
void onFixedUpdate(float dt) override {
    // 1. Kinematic 和 Dynamic 的 Transform → 物理世界
    physicsWorld.syncTransformsToBodies();
    // 2. Jolt step
    physicsWorld.step(dt);
    // 3. Dynamic body 的结果 → SceneNode Transform
    physicsWorld.syncBodiesToTransforms();
}
```

- 两侧同步时要尊重 dirty 标记，避免无意义传递
- fixedUpdate 之间的渲染帧需要插值（`alpha = accumulator / fixedDt`），否则物理物体在高刷新率下会抖动。实现一个 `TransformSmoother` 组件

**验收**：物理物体在 144Hz 渲染下看起来平滑。

### REQ-505 · 射线 / 查询 API

```cpp
struct RaycastHit {
    SceneNode* node;
    Vec3f      point;
    Vec3f      normal;
    float      distance;
};

class IPhysicsWorld {
    std::optional<RaycastHit> raycast(Vec3f origin, Vec3f dir, float maxDist) const;
    std::vector<RaycastHit>   raycastAll(Vec3f origin, Vec3f dir, float maxDist) const;
    std::vector<SceneNode*>   overlapSphere(Vec3f center, float radius) const;
    // sweep capsule 等按需加
};
```

- 支持 layer mask：只和指定 layer 交互
- 结果里的 `SceneNode*` 通过 Jolt 的 `userData` 回指

**验收**：鼠标点击屏幕 → 相机出射 ray → 命中的物体高亮。

### REQ-506 · 碰撞事件

- Jolt 的 `ContactListener` 封装成引擎事件：
  - `OnContactAdded`
  - `OnContactPersisted`
  - `OnContactRemoved`
- 事件派发到挂在 SceneNode 上的组件（依赖 Phase 6 的组件生命周期，先用回调队列占位）

**验收**：两个球碰撞时触发一次 "Contact" 事件。

### REQ-507 · 触发器（Trigger）

- Body 的 `isTrigger = true` 时不参与碰撞响应，只发 overlap 事件
- 典型用例：区域检测、拾取点、道具刷新点

**验收**：角色穿过 trigger 时 enter/exit 事件准确。

### REQ-508 · 角色控制器

```cpp
class CharacterController {
public:
    float radius;
    float height;
    float stepHeight;
    float maxSlope;  // 度

    void move(Vec3f motion);   // 一帧内的位移请求
    bool isGrounded() const;
    Vec3f getPosition() const;
};
```

- 使用底层物理引擎提供的"虚拟角色"机制（非刚体驱动，手动控制）
- slide along walls / step up stairs
- 接地检测

**验收**：WASD 控制 capsule 在带楼梯的场景里行走不卡墙。

### REQ-509 · Debug Draw

- 物理引擎的调试渲染接口封装成引擎统一的 `DebugDraw` 接口
- 背后走一个独立的 `Pass_DebugDraw`：线段 / 三角形 pipeline
- `DebugDraw::line(a, b, color)` / `box(...)` / `sphere(...)`
- 所有物理形状可一键 toggle 显示

**验收**：按 F3 显示所有碰撞 shape 的 wireframe。

### REQ-510 · Layer 系统

- 物理 layer 是 bitmask，层间配置允许 / 禁止碰撞
- 典型 layer：`Default / Player / Enemy / Projectile / Trigger / UI`
- 在 `IPhysicsWorld` 构造时传入 layer 矩阵

**验收**：Projectile 不和自己层碰撞，但和 Player / Enemy 碰撞。

---

## 里程碑

### M5.1 · Jolt 接入 + 刚体下落

- REQ-501 + REQ-502 + REQ-503 + REQ-504 完成
- demo：立方体在重力下落地并停止

### M5.2 · 查询 + 事件

- REQ-505 + REQ-506 + REQ-507 完成
- demo：鼠标点击物体 / trigger 区域进入事件

### M5.3 · 角色控制器 + Debug

- REQ-508 + REQ-509 + REQ-510 完成
- demo：`demo_physics_pong`，可操控角色在带楼梯的场景里移动

---

## 风险 / 未知

- **第三方引擎的 runtime 布局**：物理引擎通常自带 allocator / job system，与引擎自己的未来 job system 可能重叠。规则：先用它的，别自己造。
- **Transform 同步的性能**：每帧全量同步 dynamic body 数量多时成为热点。按 dirty 过滤。
- **物理和动画的边界**：角色根骨骼由物理还是动画驱动？规定：`CharacterController` 的位置 = 动画 root motion + 物理 slide，两者在 gameplay 层合并。
- **确定性配置的开关**：大部分物理引擎默认是"追求性能"而非"追求确定性"。需要显式启确定性模式（单线程 / 固定求解器迭代次数 / 固定浮点语义），付出 10–30% 性能代价。与 [P-1](principles.md#p-1-确定性是架构级不变量) 的 `--deterministic` 开关联动。
- **编译时间**：任何成熟物理引擎都会显著增加构建时间，需要固定版本 + 构建缓存。

---

## 与现有架构的契合

- 物理世界是 `IPhysicsWorld` 接口 + 具体实现分离，走 core↔infra 两层，符合 `cpp-style-guide` R5。
- `Clock::fixedStepsToRun()`（Phase 2）直接为物理 step 服务。
- Transform 的 dirty 机制让物理 ↔ 渲染的矩阵同步高效。
- `SceneNode` 持有 `RigidBody` 与持有 `AnimationPlayer` 平级，没有特殊待遇。

---

## 与 AI-Native 原则的契合

| 原则 | 本阶段如何落实 |
|------|--------------|
| [P-1 确定性](principles.md#p-1-确定性是架构级不变量) | 显式确定性模式开关，给 agent 可重放物理世界 |
| [P-5 语义查询](principles.md#p-5-语义查询层) | `raycast` / `overlap` 是 agent 查询空间关系的关键工具 |
| [P-19 命令总线](principles.md#p-19-bi-directional-命令总线) | `applyForce` / `setVelocity` 走命令层，可 dry-run / 可撤销 |
| [P-20 渲染/模拟可分离](principles.md#p-20-渲染与模拟的可分离) | 物理纯模拟层，headless eval 不需要 GPU |

---

## 下一步

Phase 5 完成后 + Phase 4 也已经完成 → 进入 [Phase 6 Gameplay 层](phase-6-gameplay-layer.md)。这里才是"引擎给游戏代码用"真正的接口。
