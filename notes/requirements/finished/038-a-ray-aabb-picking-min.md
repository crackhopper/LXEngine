# REQ-038-a: ray-AABB picking 最小子集 — 编辑器视口点击选中

> 本 REQ 是 [Phase 1.5 ImGui Editor MVP + 命令总线](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 的第 4 步。在 roadmap 中以"REQ-209 AABB + 空间索引（最小子集）"前向声明 —— 完整 spatial index 推到 Phase 2 REQ-209 全量。
>
> 2026-05-06 拆分：原 `038-ray-aabb-picking-min.md` 即本档（v1，AABB 命中级粒度）。v2（mesh 三角面级 picking + hit point / hit normal + CPU 端 mesh 数据保留）移到 [REQ-038-b mesh 三角面级 picking](041-h-mesh-level-triangle-picking.md)。

## 背景

[REQ-041 ImGui Editor MVP](041-a-imgui-editor-mvp.md) 视口里点击需要选中目标节点，TRS gizmo 才能挂上去。当前代码里：

- `BoundingBox` 值类型已存在于 `src/core/math/bounds.hpp`（带 `merge / transformed / contains / intersects / getCenter / getSize / isValid`），`Mesh` 头文件 (`src/core/asset/mesh.hpp`) 已经把 `bounds.hpp` 包进来，但 `Mesh` 没有 bounds 字段、`SceneNode` 也没有 bounds 查询接口
- mesh 加载流程（GLTF / OBJ）没有 min/max 计算
- `src/core/math/` 没有 ray / ray-box 求交函数

完整的 spatial-index 加速结构（BVH / octree / loose octree）是 Phase 2 REQ-209 的范围，那里会带来一次性的设计与维护成本。本 REQ 只取最小子集：每 mesh 一个本地 `BoundingBox` + 暴力遍历 + ray-box slab test。10k 节点级别下完全够用，编辑器不会成为热路径。

## 目标

1. mesh 加载完成时一次性算出本地 `BoundingBox`（min/max in mesh local space），落到 `Mesh::bounds`
2. `SceneNode` 暴露 `getLocalBounds()` / `getWorldBounds()`，前者透过 `MeshComponent` 取，后者按当前 world transform 计算
3. `src/core/math/` 增加 `Ray { Vec3f origin, Vec3f direction }` 与 `intersectRayBox(ray, box) -> std::optional<float>`（返回首次命中 t 值）
4. `Scene::pick(ray, layerMask)` 暴力遍历命中候选，返回最近命中（节点用 `SceneNodeSharedPtr`，与 `Scene::m_renderables` 的所有权一致）

## 需求

### R1: 复用现有 `BoundingBox`，不新建 `AABB`

`src/core/math/bounds.hpp` 中的 `BoundingBox` 已经覆盖本 REQ 需要的全部能力（`merge(point) / merge(box) / transformed(Mat4f) / contains / intersects / getCenter / getSize / isValid`），且在默认构造时用 `±inf` 形成"空 box"语义、`isValid()` 在 `min > max` 时返回 false。

- **不**新建 `AABB` 类型、**不**新建 `src/core/math/aabb.hpp`
- 本 REQ 后续条款中"AABB"一词全部对应 `BoundingBox`
- 若后续发现缺方法（如 `static empty()` 显式工厂），应在原文件里就地补，而不是另起新类型

### R2: `Ray` 值类型 + 求交

`src/core/math/ray.hpp`（新文件，组织方式仿 `bounds.hpp`：值类型 + 同名空间内的自由函数）：

```cpp
struct Ray {
  Vec3f origin;
  Vec3f direction;            // 不强制单位长度
};

// Slab test (Williams et al. 2005)。命中时返回 ray.origin + t * ray.direction
// 的首次进入参数 t；t 的单位与 direction 长度相关。
// - ray 起点在盒内 → t = 0
// - 完全在 ray 反向 → nullopt
// - direction 任一分量为 0 → 走 ±inf 分支，不产生 NaN
// 当多条 ray 之间需要比较距离时，调用方负责把 direction 归一化。
std::optional<float> intersectRayBox(const Ray &ray, const BoundingBox &box);
```

- **不**单独引入 `intersect.hpp`：当前只有一个求交函数，与 `Ray` 同住一个文件更内聚；后续若出现 ray-tri / ray-sphere 再讨论拆分
- 调用方约定：`direction != 0`，零向量为契约违反（debug 断言即可）

### R3: Mesh 加载时计算本地 `BoundingBox`

修改 mesh 加载流程：

- `Mesh` (`src/core/asset/mesh.hpp`) 增加 `BoundingBox bounds`（公有字段，与 `vertexBuffer / indexBuffer` 同级；header 已 include `bounds.hpp`，零额外依赖）
- `src/infra/mesh_loader/gltf_mesh_loader.cpp` / `obj_mesh_loader.cpp` 在解析 vertex 位置属性的同一遍循环里 `bounds.merge(position)`，并通过 `Mesh::create(...)` 把 bounds 传进去（`create` 签名扩成 `create(VertexBufferSharedPtr, IndexBufferSharedPtr, BoundingBox)`，老调用点全部修正）
- 任何手写/程序化构造的 mesh（demos / 测试夹具）也走同一通道；**不**为编辑器单独多扫一次顶点

### R4: `SceneNode::getLocalBounds() / getWorldBounds()`

```cpp
class SceneNode {
 public:
  BoundingBox getLocalBounds() const;   // 透过 getComponent<MeshComponent>() 取 mesh.bounds；
                                        // 节点没有 MeshComponent 时返回默认构造（!isValid()）
  BoundingBox getWorldBounds() const;   // local bounds transformed by getWorldTransform()
};
```

- 命名向 `BoundingBox` 看齐（不是 `getLocalAABB`），避免在公共 API 上同时出现两种术语
- `getWorldBounds()` 不缓存：每次现算（worldTransform dirty 传播已经覆盖 transform 变化；bounds 是 transform 的纯派生）
- 节点无 `MeshComponent`（pure-transform / camera / light 节点）→ 返回 `!isValid()` 的 box → 自动从 picking 候选中剔除
- 走 `MeshComponent` 而不是 `node->getMesh()`：mesh 的所有权与生命周期由 [REQ-037-a 组件模型](finished/037-a-component-model-foundation.md) 收口在 component 上

### R5: `Scene::pick(...)`

```cpp
struct PickHit {
  SceneNodeSharedPtr node;        // 与 Scene::m_renderables 的 IRenderableSharedPtr 所有权一致；
                                  // 调用方拿到这一刻命中节点仍然存活，不依赖 Scene 内部生命期
  float distance;                 // ray.direction 长度单位下的 t（同 R2）
};

std::optional<PickHit>
Scene::pick(const Ray &ray,
            VisibilityLayerMask layerMask = VisibilityMask_All) const;
```

- **不**用裸指针：`SceneNode*` 违反 `openspec/specs/cpp-style-guide/spec.md` "No raw pointers for object references"
- `VisibilityLayerMask` 是项目现有的可见性掩码类型（见 `src/core/scene/object.hpp`），不在签名里裸出 `u32`
- 暴力遍历所有 `SceneNode`：
  - 跳过 `getLocalBounds().isValid() == false`（无 mesh）
  - 跳过 `(node->getVisibilityLayerMask() & layerMask) == 0`
  - `intersectRayBox(ray, node->getWorldBounds())` 命中时记录 (node, t)
- 返回 t 最小的命中
- v1 不做 mesh-level triangle test（box 命中即算命中）
- v1 不做"hit point 反推 world coords" / "hit normal" — 编辑器只需要"哪个节点被选了"

### R6: 屏幕坐标 → ray helper（编辑器用）

落到 [REQ-037-b](finished/037-b-camera-as-component.md) 引入的 `CameraComponent` 上（最自然的归属，因为它持有 view + proj 推导所需的全部状态）：

```cpp
class CameraComponent {
 public:
  Ray pickRay(Vec2f screenPixel, Vec2f viewportSize) const;
};
```

- 输入约定为 pixel 坐标 (0..viewportSize.x, 0..viewportSize.y)，函数内部转 NDC
- 输出 world-space Ray：perspective 时 `origin = camera world position`；orthographic 时 `origin` 在 near plane 对应像素位置
- direction 在 helper 内部归一化（让 `Scene::pick` 返回的 `distance` 有稳定的世界距离含义）
- 对 perspective / orthographic 两种 projection 都正确
- **顺序硬约束**：本节依赖 [REQ-037-b](finished/037-b-camera-as-component.md) 已落地。若 037-b 因任何原因晚于本 REQ，临时把 helper 挂在旧 `Camera` 上，037-b 落地时同步迁移

### R7: 测试覆盖

`src/test/integration/test_picking.cpp`（新）：

- `BoundingBox::transformed` 在 90° 旋转 + 非均匀 scale 下保守正确（包含原 8 角点；用现有实现，本 REQ 不重写）
- `intersectRayBox` 与轴对齐 box 的命中 / 错过 / 切线擦过都正确
- ray 起点在 box 内部时返回 t = 0（与 R2 契约一致，文档明确）
- Scene 含 3 个挂着 `MeshComponent` 的 SceneNode + 给定 ray，`pick` 返回最近命中（命中节点是 `SceneNodeSharedPtr`）
- 命中节点的 visibility mask 不在 layerMask 内 → 跳过

## 修改范围

- `src/core/math/ray.hpp`（新；`Ray` 值类型 + `intersectRayBox` 自由函数）
- `src/core/math/bounds.hpp`（如发现需要小幅扩展再就地补；不新建文件、不重写）
- `src/core/asset/mesh.hpp`（`Mesh` 增加 `BoundingBox bounds` + `Mesh::create` 签名扩展）
- `src/infra/mesh_loader/gltf_mesh_loader.cpp`（解析位置属性时累计 bounds）
- `src/infra/mesh_loader/obj_mesh_loader.cpp`（同上）
- `src/core/scene/object.hpp` / `.cpp`（`getLocalBounds` / `getWorldBounds`，前者透过 `MeshComponent`）
- `src/core/scene/scene.hpp` / `.cpp`（`pick` + `PickHit`）
- `src/core/scene/components/camera_component.hpp` / `.cpp`（`pickRay`；依赖 [REQ-037-b](finished/037-b-camera-as-component.md)）
- `src/test/integration/test_picking.cpp`（新）

## 边界与约束

- **不**做 BVH / octree / loose octree（Phase 2 REQ-209 全量）
- **不**做 mesh-level triangle picking（移到 [REQ-038-b](041-h-mesh-level-triangle-picking.md)，那里也带 CPU 端 vertex / index 保留）
- **不**做 frustum vs box（Phase 1 REQ-110 视锥剔除做这个）
- **不**做 hit point / hit normal 反推（移到 [REQ-038-b](041-h-mesh-level-triangle-picking.md)）
- **不**新增 `AABB` 类型，**不**新增 `src/core/math/aabb.hpp`：复用 `BoundingBox`
- `BoundingBox::transformed` 用 8 角点法（现有实现），**不**改写为 Arvo 优化方法
- 大规模场景（>10k 节点）线性扫描可能变慢；上 BVH 留 Phase 2

## 依赖

- [REQ-035 Transform 组件](finished/035-transform-component.md) — `getWorldTransform()` 用于 box 变换
- [REQ-037-a 组件模型基础](finished/037-a-component-model-foundation.md) — `MeshComponent` 是 `getLocalBounds()` 取 mesh 的唯一路径
- [REQ-037-b Camera 作为 component](finished/037-b-camera-as-component.md) — `pickRay` 落在 `CameraComponent`；本 REQ 落地次序排在 037-b 之后
- 现有 mesh 加载流程（GLTF / OBJ）
- 现有 `BoundingBox` (`src/core/math/bounds.hpp`) 与 `Vec3f` / `Mat4f` math

## 后续工作

- [REQ-040 Editor 命令总线](040-a-editor-command-bus.md) — `select <path>` 命令直接调 `findByPath`；视口点击 `select` 调 `Scene::pick`
- [REQ-041 ImGui Editor MVP](041-a-imgui-editor-mvp.md) — 视口点击事件 → `camera->getComponent<CameraComponent>()->pickRay(screen)` → `scene.pick(ray, layerMask)` → 选中节点
- Phase 2 REQ-209 全量：BVH / octree / 增量更新；本 REQ 的 `Scene::pick` API 保持不变，内部加速
- [REQ-038-b mesh 三角面级 picking](041-h-mesh-level-triangle-picking.md) — 把命中粒度从 AABB 收窄到三角面，并补 hit point + hit normal；硬依赖 REQ-209 已落地的空间索引（候选集已小，三角扫描才不爆 CPU）

## 实施状态

已实现并验证，通过归档条件。Phase 1.5 第 4 步已完成，实际落地顺序满足 [REQ-037-a](finished/037-a-component-model-foundation.md) / [REQ-037-b](finished/037-b-camera-as-component.md) 前置约束。

验证结论：

- `R1` / `R4` / `R5` / `R6` 与当前代码一致：`BoundingBox` 复用、`SceneNode` bounds 查询、`Scene::pick(...)`、`CameraComponent::pickRay(...)` 都已落地
- `R2` 已补齐文档约定的零方向 debug 契约：`intersectRayBox(...)` 现在对零方向 ray 做 debug `assert`
- `R7` 已补齐文档列出的变换覆盖：`test_picking.cpp` 现在验证 90° 旋转 + 非均匀 scale 下的 world bounds
- `R3` 存在一处已接受的实现漂移：OBJ / GLTF loader 负责计算并暴露 bounds，最终 `Mesh::create(...)` 的注入发生在消费这些 loader 的构建路径里，而不是由 loader 直接产出 `Mesh`；行为结果与本 REQ 目标一致，因此以代码事实为准记录

本次验证运行：

- `cmake --build build --target test_picking test_math test_scene_path_lookup test_scene_node_validation test_frame_graph -j4`
- `ctest --output-on-failure -R "test_picking|test_math|test_scene_path_lookup|test_scene_node_validation|test_frame_graph"`
