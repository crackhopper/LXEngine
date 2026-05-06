# REQ-041-h: mesh 三角面级 picking — hit point / hit normal + CPU 侧 mesh 数据保留

> 拆分自 2026-05-06 整理：原 [REQ-038-a](finished/038-a-ray-aabb-picking-min.md) v1 取"AABB 命中级粒度"最小子集，把 mesh 三角面级 picking + hit point / hit normal 显式留给 v2。本 REQ 把 v2 收口，并把"CPU 侧 vertex / index 保留"作为同一笔实施的真前置一起做（不再让它在 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md) 上漂着）。

## 背景

[REQ-038-a](finished/038-a-ray-aabb-picking-min.md) 已经能让"点击视口选中节点"工作：ray vs `BoundingBox` slab test，遍历可见节点。这套对编辑器选中够用，但有两个明确的天花板：

1. **空心物体 / 长条物体 picking 不直观**：torus / 长条架子的 AABB 比物体本身大很多倍，光标在 AABB 内但不在 mesh 上时也会 select。RTR 章节实验和编辑器复杂场景里这个误差很烦
2. **没有 hit point / hit normal**：编辑器的"在表面放置 decal / asset"、"贴地放置"、视觉化 ray 命中点（DebugDraw）等场景都需要精确命中信息

要做三角面级 picking，CPU 必须能拿到 vertex 位置 + index。当前 [REQ-038-a R3](finished/038-a-ray-aabb-picking-min.md) 只让 mesh 加载时算 `BoundingBox` 落到 `Mesh::bounds`，没有保留三角面数据 — 顶点上传 GPU 后 CPU 侧就丢了。

[Phase 2 REQ-209](../roadmaps/main-roadmap/phase-2-foundation-layer.md#req-209--aabb--空间索引) 引入空间索引（BVH / octree / loose octree）后，picking 候选集会从"全部 SceneNode"缩成"AABB 与 ray 相交的少数节点"。在小候选集上跑三角面级 ray-tri test 才不会让编辑器主线程爆 CPU；这是本 REQ 排在 REQ-209 之后立项的硬原因。

## 目标

1. `Mesh` 携带 CPU 侧的"位置 + 索引"快照（不重复 vertex buffer 完整内容；只留 picking / 物理 / 高度查询会用的子集），由 mesh 加载流程一次性写入
2. `Scene::pick(...)` 行为升级为"AABB 候选 → 候选内逐三角面 ray-tri test → 返回最近命中"，外部 API 形态不变（仍返回 `std::optional<PickHit>`）
3. `PickHit` 增加 `Vec3f hitPoint` + `Vec3f hitNormal`，分别表示 world-space 命中位置与命中三角面的几何法线
4. 编辑器：DebugDraw 在选中命中时画一个小坐标轴在 hitPoint，让用户看见精度提升

## 需求

### R1: `Mesh` 携带 CPU 侧 picking 数据

`src/core/asset/mesh.hpp`：

```cpp
class Mesh {
 public:
  // 既有：vertexBuffer / indexBuffer / bounds
  std::vector<Vec3f> cpuPositions;     // 仅位置；不持纹理 / 法线 / tangent
  std::vector<u32>   cpuIndices;       // triangle list 已 unwrap；与 indexBuffer 等价但 CPU 侧
};
```

- `Mesh::create(...)` 签名扩成 `create(vb, ib, bounds, cpuPositions, cpuIndices)`；老调用点全量迁移
- 数据在 mesh 加载时一次性写入：GLTF / OBJ loader 在已经解析位置 + 索引的循环里直接复制一份；**不**多扫顶点
- 内存预算：CPU positions 是 `vector<Vec3f>` = 12 B / vertex；CPU indices 是 `vector<u32>` = 4 B / index。100k 顶点 + 300k 索引的中型 mesh ≈ 2.4 MB，可接受
- 程序化 / 手写 mesh（demos / 测试夹具）也走同一通道；不为编辑器单独多扫一次顶点

### R2: ray-triangle 求交

`src/core/math/ray.hpp` 扩展（继续与 ray + 求交同住一个文件，不拆 `intersect.hpp`）：

```cpp
struct RayTriHit {
  float t;                // 与 REQ-038-a R2 同语义：ray.direction 长度单位下的 t
  float u, v;             // 重心坐标 (w = 1 - u - v)
};

// Möller–Trumbore 算法。命中时返回 t 与重心坐标；未命中或在 ray 反向返回 nullopt
std::optional<RayTriHit>
intersectRayTriangle(const Ray &ray,
                     const Vec3f &p0, const Vec3f &p1, const Vec3f &p2);
```

- 选用 Möller–Trumbore（行业标准）：分支少、无除零陷阱、CPU 跑 mesh 三角扫描时常数因子合适
- back-face culling：默认双面命中（编辑器选中只关心是否命中，不需要朝向过滤）；预留 `bool cullBackFace = false` 参数留给后续物理 / 视觉射线用例

### R3: `Scene::pick(...)` 升级

```cpp
struct PickHit {
  SceneNodeSharedPtr node;        // 与 REQ-038-a 一致
  float distance;                 // ray.direction 长度单位下的 t（同 ray.hpp R2）
  Vec3f hitPoint;                 // world-space 命中位置
  Vec3f hitNormal;                // world-space 三角面几何法线（CCW 缠绕侧）；与 ray.direction 取负保证朝向相机
};

std::optional<PickHit>
Scene::pick(const Ray &ray,
            VisibilityLayerMask layerMask = VisibilityMask_All) const;
```

实现：

1. 用 [Phase 2 REQ-209](../roadmaps/main-roadmap/phase-2-foundation-layer.md#req-209--aabb--空间索引) 的空间索引拿到"AABB 与 ray 相交的候选节点集"
2. 候选集内每个节点：把 ray 变换到节点 local space（用 `worldTransform` 的逆变换 origin 与 direction）
3. 对每个 mesh 三角面调 `intersectRayTriangle(localRay, p0, p1, p2)`；记录最小 t
4. 把命中三角面的 (p0, p1, p2) 与重心坐标变换回 world space → `hitPoint`；几何法线 = `normalize(cross(p1-p0, p2-p0))` 经 `worldTransform` 的法线变换 → `hitNormal`
5. 跨节点取最小 distance 的命中

边界：

- 节点无 `MeshComponent` 或 mesh 没有 `cpuPositions`（不期望，但容错）→ 候选集中跳过该节点；fallback 不退回 AABB 命中（避免精度天花板出现"有的节点三角面、有的节点 AABB"的不一致）
- mesh 有 cpuPositions 但 cpuIndices 为空（point cloud / line list）→ 当前 REQ 不支持，跳过；line / point 拓扑的 picking 等真实需求出现再立项

### R4: 编辑器 / 命令总线集成

- [REQ-041-a 编辑器视口](041-a-imgui-editor-mvp.md) 的命中视觉化：选中后 `DebugDraw::axis(hitPoint, axisLen)` 一行画一个小坐标轴；`DebugDraw::wireBox(hit.node->getWorldBounds())` 仍画选中线框（保持 037-a 视觉）
- 命令总线 `pick <screenX> <screenY>` 在 [REQ-040-a](040-a-editor-command-bus.md) 的基础上扩展返回 `structured` 字段含 `{ path, hitPoint: [x,y,z], hitNormal: [x,y,z] }`，供 MCP 客户端 / agent 消费

### R5: 测试覆盖

`src/test/integration/test_picking_v2.cpp`（新）：

- 一个 torus mesh：ray 穿过 torus 中心的"洞" → v1 (REQ-038-a) 的 AABB 测试会命中，v2 三角面测试 **不**命中（这是 v2 的核心收益）
- ray 正命中 torus 实体部分时，`hitPoint` 在 mesh 表面上（与 mesh 顶点距离 < 容差）
- `hitNormal` 与命中三角面的几何法线一致（取 CCW 缠绕一侧），且面向 ray.direction 反向
- mesh 经过非均匀 scale + 旋转 transform 时 ray-tri 仍正确（验证 local-space ray 反变换路径）
- 多 mesh 沿 ray 排列，pick 返回最近一面（不是最近一个 AABB 候选节点）

## 修改范围

- `src/core/asset/mesh.hpp` / `.cpp`（加 `cpuPositions / cpuIndices` 字段 + `Mesh::create` 签名扩展）
- `src/infra/mesh_loader/gltf_mesh_loader.cpp` / `obj_mesh_loader.cpp`（写入 cpu 数据）
- `src/core/math/ray.hpp`（加 `RayTriHit` + `intersectRayTriangle`）
- `src/core/scene/scene.hpp` / `.cpp`（`PickHit` 加 `hitPoint / hitNormal`；`pick` 走候选集 + 三角面）
- `src/core/editor/viewport_overlay.cpp`（`DebugDraw::axis(hit.hitPoint)`）
- `src/core/editor/commands/pick.cpp`（structured 输出 hitPoint / hitNormal）
- `src/test/integration/test_picking_v2.cpp`（新）

## 边界与约束

- v2 **不**做：mesh skinning 后顶点的 picking（蒙皮 mesh 的 picking 用 bind pose 顶点；这个差异在选中编辑足够好，动画穿模 picking 留给后续）
- v2 **不**做：non-triangle 拓扑（line / point list）的 picking
- v2 **不**做：subset 命中（"命中第几个 submesh / draw call"）；v1 已经退到节点级，v2 仍只回节点 + 命中点，不细到 submesh
- v2 **不**做：runtime 修改 `cpuPositions / cpuIndices` 的同步（mesh 是不可变资产；运行时改 mesh 不在本 REQ 设想内）
- 内存：始终保留 CPU 侧 vertex 位置 + index 是有意识的代价。如果未来内存紧张，可以加配置开关让"标注为不参与 picking"的 mesh 不留 CPU 数据

## 依赖

- [REQ-038-a ray-AABB picking 最小子集](finished/038-a-ray-aabb-picking-min.md) — `BoundingBox` / `Ray` / `intersectRayBox` / 候选遍历框架已就位
- [Phase 2 REQ-209 AABB + 空间索引](../roadmaps/main-roadmap/phase-2-foundation-layer.md#req-209--aabb--空间索引) — **硬前置**。没有空间索引把候选集缩小，三角面级扫描在 1k+ 节点场景必爆 CPU
- [REQ-037-a 组件模型基础](finished/037-a-component-model-foundation.md) — 通过 `MeshComponent` 取 mesh 数据
- [REQ-039-a DebugDraw](039-a-debug-draw-subsystem.md) — `axis` API 用于可视化命中点
- [REQ-041-a 编辑器 MVP](041-a-imgui-editor-mvp.md) — 编辑器视口承接 hit visualizer

## 后续工作

- 蒙皮 mesh 的 picking（按 GPU skinning 后顶点）：等动画路径稳定后再立项
- "拖拽 asset 到表面" / "decal 贴地"：直接消费本 REQ 的 hitPoint / hitNormal
- mesh 的 CPU 侧数据未来若被序列化路径复用（mesh asset round-trip），把 `cpuPositions / cpuIndices` 标记为 mesh 的常驻 CPU view，让 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md) 直接读

## 实施状态

待实施。立项窗口：[Phase 2 REQ-209](../roadmaps/main-roadmap/phase-2-foundation-layer.md#req-209--aabb--空间索引) 已落地后开工。Phase 1.5 编辑器在 [REQ-038-a](finished/038-a-ray-aabb-picking-min.md) 的 AABB picking 上已经够用，本 REQ 是精度升级，不阻塞 Phase 1 / Phase 2 主线。
