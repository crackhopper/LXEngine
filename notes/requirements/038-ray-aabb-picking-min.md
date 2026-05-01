# REQ-038: ray-AABB picking 最小子集 — 编辑器视口点击选中

> 本 REQ 是 [Phase 1.5 ImGui Editor MVP + 命令总线](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 的第 4 步。在 roadmap 中以"REQ-209 AABB + 空间索引（最小子集）"前向声明 —— 完整 spatial index 推到 Phase 2 REQ-209 全量。

## 背景

[REQ-041 ImGui Editor MVP](041-imgui-editor-mvp.md) 视口里点击需要选中目标节点，TRS gizmo 才能挂上去。当前代码里：

- `SceneNode` 没有任何 AABB 字段
- mesh 加载流程（GLTF / OBJ）没有 min/max 计算
- `src/core/math/` 可能没有 ray / ray-AABB 求交函数

完整的 spatial-index 加速结构（BVH / octree / loose octree）是 Phase 2 REQ-209 的范围，那里会带来一次性的设计与维护成本。本 REQ 只取最小子集：每 mesh 一个本地 AABB + 暴力遍历 + ray-AABB slab test。10k 节点级别下完全够用，编辑器不会成为热路径。

## 目标

1. mesh 加载完成时一次性算出本地 AABB（min/max in mesh local space）
2. `SceneNode` 暴露 `getLocalAABB()` 与 `getWorldAABB()`，后者按当前 world transform 计算
3. `src/core/math/` 增加 `Ray { Vec3f origin, Vec3f direction }` 与 `intersectRayAABB(ray, aabb) -> std::optional<float>`（返回首次命中 t 值）
4. `Scene::pick(ray, layerMask) -> SceneNode*` 暴力遍历命中候选，返回最近命中

## 需求

### R1: `AABB` 值类型

`src/core/math/aabb.hpp`（新文件）：

```cpp
struct AABB {
  Vec3f min{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
  Vec3f max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

  bool isValid() const;       // min ≤ max 全部维度
  void expand(Vec3f p);
  void expand(const AABB &other);
  AABB transformed(const Mat4f &m) const;   // 8 角变换 + 重新包围（保守）
  Vec3f center() const;
  Vec3f extent() const;       // (max - min) / 2

  static AABB empty();        // 默认构造
};
```

- `transformed(m)` 用 8 个角点变换 + 重新求 min/max（不优化为 Arvo's method；保守过估，编辑器够用）

### R2: `Ray` 值类型 + 求交

`src/core/math/intersect.hpp`（新文件）：

```cpp
struct Ray {
  Vec3f origin;
  Vec3f direction;            // 不要求归一化，但函数返回 t 与 direction 长度相关；建议归一化
};

std::optional<float> intersectRayAABB(const Ray &ray, const AABB &aabb);
```

- 标准 slab test（Williams et al. 2005）
- 命中时返回首次进入 t；未命中或完全在 ray 后方返回 nullopt
- direction 中某轴为 0 时正确处理（用 `1.0f / 0.0f → +/-inf` 或显式分支，确保不出 NaN）

### R3: Mesh 加载时计算本地 AABB

修改 mesh 加载流程：

- `src/infra/mesh/` 中 GLTF / OBJ loader 在解析 vertex 数据后，扫一遍位置属性算 min/max
- `Mesh` 类（`src/core/mesh/...` 或加载产物）增加 `AABB getBounds() const` 接口
- 对于 mesh 数据来源不一的情况（`src/infra/mesh/primitives/` 内置立方体 / 球体等），它们的构造函数也直接填入 AABB
- AABB 计算与 vertex layout 解析在同一个 pass 里完成；**不**为编辑器单独多扫一次

### R4: `SceneNode::getLocalAABB() / getWorldAABB()`

```cpp
class SceneNode {
 public:
  AABB getLocalAABB() const;        // mesh.getBounds()；mesh = nullptr 返回 empty
  AABB getWorldAABB() const;        // local AABB transformed by getWorldTransform()
};
```

- `getWorldAABB()` 不缓存：每次现算（dirty 传播链已经覆盖 transform 变化；AABB 是 transform 的派生）
- 节点无 mesh（pure-transform / camera / light 节点）→ 返回 empty AABB → 自动从 picking 候选中剔除

### R5: `Scene::pick(...)`

```cpp
struct PickHit {
  SceneNode* node;
  float distance;
};

std::optional<PickHit> Scene::pick(const Ray &ray, u32 layerMask = 0xffffffffu) const;
```

- 暴力遍历所有 `SceneNode`：
  - 跳过 `getLocalAABB().isValid() == false`（无 mesh）
  - 跳过 `(node.visibilityLayerMask & layerMask) == 0`
  - `intersectRayAABB(ray, node.getWorldAABB())` 命中时记录 (node, t)
- 返回 t 最小的命中
- v1 不做 mesh-level triangle test（AABB 命中即算命中）
- v1 不做"hit point 反推 world coords" / "hit normal" — 编辑器只需要"哪个节点被选了"

### R6: 屏幕坐标 → ray helper（编辑器用）

`src/core/scene/camera.hpp` 加 helper（最自然的归属，因为它要 view + proj 矩阵）：

```cpp
class Camera {
 public:
  Ray pickRay(Vec2f screenUv, Vec2f viewportSize) const;
};
```

- 输入屏幕 NDC 或 pixel：约定用 (0..viewportSize.x, 0..viewportSize.y) pixel 坐标，函数内部转 NDC
- 输出 world-space Ray，origin 在 camera 位置（perspective）或 near plane 上对应位置（orthographic）
- 对 perspective / orthographic 两种 projection 都正确

### R7: 测试覆盖

`src/test/integration/test_picking.cpp`（新）：

- AABB transformation 在 90° 旋转 + 非均匀 scale 下保守正确（包含原 8 角点）
- ray 与轴对齐 AABB 的命中 / 错过 / 切线擦过都正确
- ray 起点在 AABB 内部时返回 t = 0（按需选择该约定，文档明确）
- Scene 含 3 个网格 + 给定 ray，pick 返回最近命中
- 命中节点的 visibility mask 不在 layerMask 内 → 跳过

## 修改范围

- `src/core/math/aabb.hpp` / `.cpp`（新）
- `src/core/math/intersect.hpp` / `.cpp`（新）
- `src/infra/mesh/` GLTF / OBJ loader（加 AABB 计算）
- `src/infra/mesh/primitives/`（内置 mesh 构造时填 AABB）
- `src/core/mesh/` mesh 类（增加 `getBounds`，若已存在则连通）
- `src/core/scene/object.hpp` / `.cpp`（getLocalAABB / getWorldAABB）
- `src/core/scene/scene.hpp` / `.cpp`（pick）
- `src/core/scene/camera.hpp` / `.cpp`（pickRay）
- `src/test/integration/test_picking.cpp`（新）

## 边界与约束

- **不**做 BVH / octree / loose octree（Phase 2 REQ-209 全量）
- **不**做 mesh-level triangle picking（v2 加，需要把 vertex / index 数据保留到 CPU 侧或 readback）
- **不**做 frustum vs AABB（Phase 1 REQ-110 视锥剔除做这个）
- **不**做 hit point / hit normal 反推
- AABB transformation 用 8 角点法，**不**用 Arvo 优化方法
- 大规模场景（>10k 节点）线性扫描可能变慢；上 BVH 留 Phase 2

## 依赖

- [REQ-035 Transform 组件](finished/035-transform-component.md) — `getWorldTransform()` 用于 AABB 变换
- 现有 mesh 加载流程（GLTF / OBJ）
- 现有 `Vec3f` / `Mat4f` math

## 后续工作

- [REQ-040 Editor 命令总线](040-editor-command-bus.md) — `select <path>` 命令直接调 `findByPath`；视口点击 `select` 调 `Scene::pick`
- [REQ-041 ImGui Editor MVP](041-imgui-editor-mvp.md) — 视口点击事件 → `camera.pickRay(screen)` → `scene.pick(ray, layerMask)` → 选中节点
- Phase 2 REQ-209 全量：BVH / octree / 增量更新；本 REQ 的 `Scene::pick` API 保持不变，内部加速
- v2 mesh-level triangle picking + hit point + hit normal

## 实施状态

待实施。Phase 1.5 第 4 步。可与 [REQ-035](finished/035-transform-component.md) / [REQ-036](finished/036-scene-node-path-lookup.md) 并行（无字段冲突）；早一点落地能让 [REQ-039 DebugDraw](039-debug-draw-subsystem.md) 在 picking 命中时画选中线框做视觉验证。
