# REQ-037-a: IComponent 基础设施 + Mesh / Material / Skeleton 转 component

> 本 REQ 是 [Phase 1.5 ImGui Editor MVP + 命令总线](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 的第 3a 步。原 REQ-037 在 2026-05-01 立项后被拆为两段：本文是架构层（component 模型），[REQ-037-b](037-b-camera-as-component.md) 是 Camera 接入。

## 背景

`src/core/scene/object.hpp:88-178` 中 `SceneNode` 当前以专属字段直接持有可渲染对象的三块数据：

```cpp
MeshSharedPtr m_mesh;
MaterialInstanceSharedPtr m_materialInstance;
std::optional<SkeletonSharedPtr> m_skeleton;
```

并暴露三个对应 getter（`getMesh / getMaterialInstance / getSkeleton`）+ setter，`object.cpp:315-444` 内的 renderable 验证 / `RenderingItem` 装配 / pipeline 信息构造直接读这三个字段。这套现状对 Phase 1.5 / 后续阶段有三个问题：

1. **结构上是双轨制**：`SceneNode` 的"携带数据"只能是 mesh + material + skeleton 三种；想让 Camera / Light / future（particle emitter / collider 等）也接入 SceneNode 必须给每种类型新增一组字段或一组旁路接口，没有统一形态。[REQ-037-b](037-b-camera-as-component.md) 让 Camera 走 SceneNode 路径时立刻撞上这个边界。
2. **调用面僵化**：`object.cpp` 内 `getRenderingDataForPass / supportsPass / collectAllBuildInfos` 都直接消费 `m_mesh` / `m_materialInstance` / `m_skeleton`；新增"挂载对象"必须改这段。
3. **编辑器 inspector 不易统一**：[REQ-041 ImGui Editor MVP](041-imgui-editor-mvp.md) 的 inspector 想"显示当前节点上挂的所有 component"，没有统一 component 列表就只能枚举专属字段。

把"SceneNode 持有的可渲染数据"重构为 component 集合，能让 mesh / material / skeleton / camera / light（未来）共用同一接口、同一调用面、同一 inspector 渲染逻辑。

## 目标

1. 引入轻量 `IComponent` 基类（virtual destructor + type id），不引入 ECS framework
2. 把 `MeshSharedPtr` / `MaterialInstanceSharedPtr` / `SkeletonSharedPtr` 三个 SceneNode 字段重构为 `MeshComponent` / `MaterialComponent` / `SkeletonComponent`，作为本 REQ 的三个一等 component
3. `SceneNode` 持有 `std::vector<std::unique_ptr<IComponent>>`，提供 `getComponent<T>() / addComponent<T>(...) / removeComponent<T>()`
4. 删除 `SceneNode::getMesh / getMaterialInstance / getSkeleton / setMesh / setMaterialInstance / setSkeleton`；所有调用点改走 `getComponent<MeshComponent>()->getMesh()` 风格
5. `object.cpp` 内 renderable 路径（`getRenderingDataForPass` / `supportsPass` / `collectAllBuildInfos`）改为通过 component 取数据，不直接访问字段

## 非目标

- 本 REQ **不**把 Camera / Light 转成 component（Camera 在 [REQ-037-b](037-b-camera-as-component.md)；Light 留后续）
- 本 REQ **不**把 `Transform` 转成 component（Transform 在 [REQ-035](finished/035-transform-component.md) 已收口为 SceneNode 上的内置字段；保持原样）
- 本 REQ **不**引入 ECS（archetypes / systems / queries）；本质是把 SceneNode 的"专属字段"重构为"按类型查表的 component 集合"
- 本 REQ **不**引入 component 间事件 / 订阅 / 信号
- 本 REQ **不**引入 component 序列化 / 反射框架（落地仍是手写 setter / getter）

## 需求

### R1: `IComponent` 基类

`src/core/scene/component.hpp`（新文件）：

```cpp
class SceneNode;

using ComponentTypeId = std::size_t;

class IComponent {
 public:
  virtual ~IComponent() = default;

  // 类型 id：编译期常量，用于 getComponent<T> 查表
  virtual ComponentTypeId getTypeId() const = 0;

  // owning node 反向指针，组件方法需要时可以访问其他 component / SceneNode 状态
  void attachTo(SceneNode *owner) { m_owner = owner; }
  SceneNode *owner() const { return m_owner; }

  IComponent(const IComponent &) = delete;
  IComponent &operator=(const IComponent &) = delete;

 protected:
  IComponent() = default;

 private:
  SceneNode *m_owner = nullptr;
};

template <typename T>
ComponentTypeId componentTypeId() {
  static const ComponentTypeId id = nextComponentTypeId();   // monotonic atomic 计数
  return id;
}
```

- `ComponentTypeId` 用 monotonic 计数器（线程安全的 `std::atomic<size_t>` 自增）。**不**用 `typeid(T).hash_code()`，避免跨平台 hash 不稳定
- 组件不可拷贝、不可移动；编辑器 / 命令总线只通过 `SceneNode` 接口操作，不复制
- 组件方法可以通过 `owner()->getComponent<...>()` 访问同节点其他组件（如 `MaterialComponent` 在 pipeline 信息里需要 `MeshComponent` 的 vertex layout）

### R2: 三个一等 component

均位于 `src/core/scene/components/`：

```cpp
// components/mesh_component.hpp
class MeshComponent final : public IComponent {
 public:
  explicit MeshComponent(MeshSharedPtr mesh);
  ComponentTypeId getTypeId() const override { return componentTypeId<MeshComponent>(); }

  const MeshSharedPtr &getMesh() const { return m_mesh; }
  void setMesh(MeshSharedPtr mesh);

 private:
  MeshSharedPtr m_mesh;
};

// components/material_component.hpp
class MaterialComponent final : public IComponent {
 public:
  explicit MaterialComponent(MaterialInstanceSharedPtr material);
  ~MaterialComponent() override;
  ComponentTypeId getTypeId() const override { return componentTypeId<MaterialComponent>(); }

  const MaterialInstanceSharedPtr &getMaterialInstance() const { return m_material; }
  void setMaterialInstance(MaterialInstanceSharedPtr material);

 private:
  MaterialInstanceSharedPtr m_material;
  u64 m_passListenerId = 0;        // 原 SceneNode::m_materialPassListenerId 搬到这里
};

// components/skeleton_component.hpp
class SkeletonComponent final : public IComponent {
 public:
  explicit SkeletonComponent(SkeletonSharedPtr skeleton);
  ComponentTypeId getTypeId() const override { return componentTypeId<SkeletonComponent>(); }

  const SkeletonSharedPtr &getSkeleton() const { return m_skeleton; }
  void setSkeleton(SkeletonSharedPtr skeleton);

 private:
  SkeletonSharedPtr m_skeleton;
};
```

- 三个 component 的内部数据格式与原 `m_mesh` / `m_materialInstance` / `m_skeleton` 完全一致；本 REQ **不**改 `Mesh` / `MaterialInstance` / `Skeleton` 自己的 API
- `MaterialComponent` 持有原 `m_materialPassListenerId`，把"material pass 状态变更 → scene revalidate"的钩子从 `SceneNode` 内部搬到 component 内
- `SkeletonComponent` 直接持有 `SkeletonSharedPtr`（**不**保留 `optional`）：节点上"没有骨骼"等价于"没挂 SkeletonComponent"，不需要 optional 兜底

### R3: `SceneNode` component 容器接口

修改 `src/core/scene/object.hpp` / `.cpp`：

```cpp
class SceneNode {
 public:
  template <typename T, typename... Args>
  T *addComponent(Args &&...args);              // 同类型已存在 → assert（v1）

  template <typename T>
  T *getComponent() const;                       // 不命中返回 nullptr

  template <typename T>
  bool removeComponent();                        // 不命中返回 false

  // 编辑器 inspector 用：列出本节点所有 component
  std::vector<IComponent *> listComponents() const;

 private:
  std::vector<std::unique_ptr<IComponent>> m_components;
};
```

- v1 约定：**一个 SceneNode 同类型 component 仅 1 份**（多 mesh / multi-material 留 v2）；`addComponent<T>` 在已存在时 assert
- 内部存储用 `std::vector` + 线性扫描查找，不用 unordered_map：v1 单节点 component 数 < 10，线性查找比 hash 更快
- `addComponent<T>` 内部对新建 component 调 `attachTo(this)`
- `removeComponent<T>` 析构 component（释放 listener / GPU 资源句柄等）

### R4: 删除旧字段 + getter / setter

- `SceneNode::m_mesh` / `m_materialInstance` / `m_skeleton` 字段 **删除**
- `SceneNode::getMesh / getMaterialInstance / getSkeleton / setMesh / setMaterialInstance / setSkeleton` 公共方法 **删除**
- `SceneNode::m_materialPassListenerId` 字段连同 install / uninstall 逻辑搬到 `MaterialComponent`
- 不保留任何 `// deprecated, use getComponent<...>` 兼容层；v1 一次性硬迁移

### R5: 调用点全量迁移

`src/` 下所有读写 mesh / material / skeleton 的位置都改：

| 当前 | 迁移后 |
|---|---|
| `node->getMesh()` | `auto *mc = node->getComponent<MeshComponent>(); mc ? mc->getMesh() : nullptr` 或一行 helper `meshOf(node)` |
| `node->setMesh(m)` | 已存在 mesh component → `getComponent<MeshComponent>()->setMesh(m)`；否则 `addComponent<MeshComponent>(m)` |
| `node->getMaterialInstance()` | `node->getComponent<MaterialComponent>()->getMaterialInstance()` |
| `node->getSkeleton()` | `auto *sc = node->getComponent<SkeletonComponent>(); sc ? sc->getSkeleton() : nullptr` |

`src/core/scene/object.cpp` 内的 `getRenderingDataForPass` / `supportsPass` / `collectAllBuildInfos` / `getVertexBuffer` / `getIndexBuffer` 等所有方法都按 R5 改写。重构后这些方法的核心结构基本不变，只是把 `m_mesh` 替换为 `getComponent<MeshComponent>()` 的查找。

构造函数 `SceneNode(name, mesh, material, skeleton)` 改为 `SceneNode(name)` + 链式 `addComponent`。所有 demo / test / scene 构造路径一并迁移。

### R6: `IRenderable` / `IDescriptorContributor` 等 mixin 关系

- `SceneNode` 继续 `: public IRenderable, public IDescriptorContributor`（不改继承链）
- `IRenderable::getRenderingDataForPass` 实现内部走 component 查找，对外行为不变
- 如果现有逻辑里"无 mesh 节点也能跑过 supportsPass"已是常态（pure-transform 节点），R3 / R4 重构后仍然 — `getComponent<MeshComponent>()` 返回 nullptr 即视为不可渲染

### R7: 测试

- 创建 `SceneNode`，`addComponent<MeshComponent>(m)` 后 `getComponent<MeshComponent>()->getMesh() == m`
- 同节点重复 `addComponent<MeshComponent>` → assert（debug 构建）
- `removeComponent<MeshComponent>()` 后 `getComponent<MeshComponent>() == nullptr`
- `listComponents()` 返回的指针顺序与 `addComponent` 调用顺序一致
- 完整 demo 场景（cube + plane + light + camera）渲染结果与重构前像素一致（用 `demo_scene_viewer` 的现有 golden image / smoke test）
- `MaterialComponent` 析构时正确移除 `materialPassListener`（无悬空回调）

## 修改范围

- `src/core/scene/component.hpp` / `.cpp`（新；`IComponent` + `componentTypeId<T>()`）
- `src/core/scene/components/mesh_component.{hpp,cpp}`（新）
- `src/core/scene/components/material_component.{hpp,cpp}`（新；含 pass listener 逻辑）
- `src/core/scene/components/skeleton_component.{hpp,cpp}`（新）
- `src/core/scene/object.hpp` / `.cpp`（删字段 / 删旧 getter / 加 component 容器与模板方法 / 重写 renderable 路径）
- `src/core/scene/scene.cpp`（构造 SceneNode 的位置一并迁移）
- `src/infra/mesh/`、`src/infra/material/` 中所有构造 SceneNode 的 helper（如 GLTF loader 产出的节点装配）
- `src/demos/scene_viewer/main.cpp`（构造 cube / plane / light 的代码改为 `addComponent` 链式风格）
- `src/test/integration/` 中所有 setup SceneNode 的测试一并迁移
- `notes/source_analysis/src/core/scene/scene.md`（落地后更新）

## 边界与约束

- v1 **不**允许同类型 component 多份（`MeshComponent` 仅 1 份 / 节点）；多 mesh 留 v2
- v1 **不**引入 component 之间的依赖声明（如"MaterialComponent 必须在 MeshComponent 之后 add"）；调用方负责正确顺序
- v1 **不**引入 component enable / disable（要禁用就 remove）
- v1 **不**做 component 序列化 / 反射；编辑器 inspector 在 [REQ-041](041-imgui-editor-mvp.md) 内手写每个 component 的 UI
- v1 **不**做 `getComponent<T>()` 性能优化（线性扫描 < 10 项足够；BVH / hash 留 v2）
- 跨 DLL 的 `componentTypeId<T>()` 一致性：本仓库目前是单 binary，v1 不考虑跨动态库 type id 同步；未来如有需要切换为 `StringID` 即可

## 依赖

- 现有 `SceneNode` parent / child / dirty 传播（`src/core/scene/object.hpp:122-176`）— 不变
- 现有 `Transform` 字段（[REQ-035](finished/035-transform-component.md) 已落地）— 留在 `SceneNode` 上不动
- 现有 `Mesh` / `MaterialInstance` / `Skeleton` 内部 API — 不变（仅持有方从 SceneNode 改为 Component）

## 后续工作

- [REQ-037-b Camera 作为 component](037-b-camera-as-component.md) — 第一个非 mesh-bearing component 应用，验证基础设施
- 未来 `LightComponent`（DirectionalLight / PointLight / SpotLight 走同模型）
- 未来 `ColliderComponent` / `RigidBodyComponent`（[Phase 5 物理](../roadmaps/main-roadmap/phase-5-physics.md)）
- v2：同节点同类型多 component；component 间订阅；component 序列化

## 实施状态

待实施。Phase 1.5 第 3a 步。在 [REQ-035](finished/035-transform-component.md) / [REQ-036](finished/036-scene-node-path-lookup.md) 落地后开工，是 [REQ-037-b](037-b-camera-as-component.md) 的硬前置。
