# REQ-037-b: Camera 作为 component 接入 SceneNode — 让相机由 transform chain 驱动

> 本 REQ 是 [Phase 1.5 ImGui Editor MVP + 命令总线](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 的第 3b 步。原 REQ-037 在 2026-05-01 立项后被拆为两段：[REQ-037-a](037-a-component-model-foundation.md) 是 component 基础设施，本文是其上的第一个非 mesh-bearing 应用。

## 背景

`src/core/scene/camera.hpp:49-118` 中 `Camera` 当前**不是** `SceneNode`，位置 / 朝向以独立字段（`position` / `target` / `up`）持有，并独立于 scene 层级管理（`Scene::m_cameras` 是平铺 vector）。这套现状对编辑器有三个问题：

1. **不能 attach 到节点**：常见编辑器需求"相机挂在 player 节点下" / "相机跟随骨骼 socket"在当前 API 下做不到
2. **gizmo 不能直接用**：[REQ-041 ImGui Editor MVP](041-imgui-editor-mvp.md) 选中相机时希望出 TRS gizmo 拖拽，但 gizmo 期望节点是 `SceneNode`
3. **命令总线不一致**：`move <node>` 命令希望同样适用于 mesh 节点和 camera；当前 camera 不在 SceneNode 协议下，要走专门的 `cam` 子命令路径

把 Camera 重构为 `CameraComponent` 挂在 `SceneNode` 上，能让 mesh / camera / light（未来）共用同一套 transform / picking / 命令路径，所有"位置/朝向"由 transform chain 决定。

## 目标

1. 把 `Camera` 重构为 `CameraComponent : IComponent`；camera 数据 + 行为都迁到 component 上
2. 现有 Camera 公共 API（`getViewMatrix` / `getProjMatrix` / `setCullingMask` / `setTarget` / `setPosition` / `lookAt`）平移到 `CameraComponent`，行为等价
3. 旧的 `Camera::position / target / up` 字段以"orbit / freefly 控制器层"形式保留控制器内部状态；不污染 component 数据模型
4. `Scene::m_cameras` 仍是 cameras 注册表，但每个 camera 也是一个挂着 `CameraComponent` 的 SceneNode（默认 attach 到 root，可重 parent）

## 需求

### R1: `CameraComponent : IComponent`

`src/core/scene/components/camera_component.hpp`（新；目录由 [REQ-037-a R2](037-a-component-model-foundation.md#r2-三个一等-component) 引入）：

```cpp
class CameraComponent final : public IComponent {
 public:
  CameraComponent(CameraData::Param param);
  ComponentTypeId getTypeId() const override { return componentTypeId<CameraComponent>(); }

  // view / proj 推导：view 来自 owner SceneNode 的 worldTransform；proj 来自 m_data
  Mat4f getViewMatrix() const;
  Mat4f getProjMatrix(float aspect) const;

  // 兼容旧 API（行为：写回 owner 的 transform）
  void setPosition(const Vec3f &p);                              // → owner->setTranslation
  void lookAt(const Vec3f &eye, const Vec3f &target, const Vec3f &up);

  // m_data 直读直写
  void setCullingMask(u32 mask);
  u32 getCullingMask() const;
  void setTarget(std::optional<RenderTarget> target);
  bool matchesTarget(const RenderTarget &t) const;
  void setActive(bool a);
  bool isActive() const;

 private:
  CameraData::Param m_data;       // fov / near / far / projection / cullingMask / target
  bool m_active = true;           // [REQ-041 R6](041-imgui-editor-mvp.md) 方案 A 的字段
};
```

- `Camera` 类 **彻底删除**；所有原 `Camera*` 参数改为 `CameraComponent*`，或上游函数改接 `SceneNode*`，内部 `getComponent<CameraComponent>()` 取
- `m_data` 字段集与原 `Camera` 完全一致（fov / near / far / projection / cullingMask / target）；位置 / 朝向由 owner SceneNode 的 transform 提供

### R2: `getViewMatrix()` 由 owner SceneNode 推导

- view matrix = `inverse(owner->getWorldTransform())`，但要先把 scale 归一化（避免 view 矩阵被节点 scale 污染）
- 约定：camera 在 local space 看向 -Z，up 为 +Y（标准 OpenGL/Vulkan 视图惯例）
- 文档明确：camera 节点的 scale **不**影响 view 矩阵（编辑器 inspector 上设置 scale 不会改变投影）

### R3: 移除 `Camera::position / target / up`（迁到控制器）

- 旧 `setPosition(p)` API 内部行为：`owner()->setTranslation(p)`
- 旧 `lookAt(eye, target, up)` 内部行为：计算 view matrix → 取逆 → 提取 translation + rotation → 写入 `owner` 的 local transform（前提 camera 当前 attach 在 root）
- 控制器（`OrbitCameraController` / `FreeFlyCameraController`）继续每帧调 `cameraComp->setPosition / lookAt`，无需感知 component 内部
- 控制器 **不**直接访问 owner SceneNode；仍以 `CameraComponent` 公共 API 为接口

### R4: `Scene::m_cameras` 与场景层级的双重身份

- `Scene::addCamera(node)`：参数变成 `SceneNode*`（必须已挂 `CameraComponent`）；注册到 `m_cameras` + 将 node attach 到 scene root（如尚未 attach）
- `Scene::removeCamera(node)`：从 m_cameras 注销 + 如果 node 是 root 直接子节点则 detach
- 用户也可以手动 `cameraNode->setParent(some_other_node)` 让相机挂载到任意节点下；`m_cameras` 注册不变
- `Scene::findByPath(path)` 也能找到 camera 节点（因为它就是个 SceneNode）

### R5: `matchesTarget` / `cullingMask` 不变

- `m_data` 内的 `target` / `cullingMask` 字段保留
- `setTarget` / `setCullingMask` / `matchesTarget` 接口签名平移到 `CameraComponent`，语义不变
- 不要混淆"render target"与"camera 在场景里挂载的 parent"——前者是 `RenderTarget` 对象，后者是 SceneNode

### R6: 编辑器相机的初始挂载

- demo 启动时，编辑器创建 `SceneNode`（`name = "editor_cam"`）+ `addComponent<CameraComponent>(...)` + `Scene::addCamera(node)`，初始 attach 到 root
- 编辑器选中 camera node → inspector 显示 transform（来自 SceneNode）+ camera 字段（来自 CameraComponent）
- 用户也可以建一个 game camera node（`add camera game_camera` 命令），同样 attach 到 root

### R7: 控制器仍写 owner SceneNode 的 local transform

- `OrbitCameraController::update(cameraComp, input, dt)` 内部计算 eye/target/up 后调 `cameraComp.lookAt(...)` —— R3 实现已经把这一步写回 owner 的 SceneNode
- 这保证 [REQ-015](finished/015-orbit-camera-controller.md) / [REQ-016](finished/016-freefly-camera-controller.md) 完成的控制器代码 *逻辑* 无需改动，仅参数类型从 `Camera*` 改为 `CameraComponent*`

## 测试

- 创建 `SceneNode` + `addComponent<CameraComponent>(...)`，调 `setPosition({1,2,3})` → `getViewMatrix()` 等价于过去单字段 view
- `lookAt({0,0,5}, {0,0,0}, {0,1,0})` → view matrix 正确把 (0,0,5) 映射到 origin
- 把 `cameraNode->setParent(playerNode)` 后，移动 player → camera 跟随
- camera 节点 scale 设为 (2,2,2) → view matrix 与 scale=1 时**完全一致**（R2 约束）
- `Scene::findByPath("/camera_main")` 命中 camera 注册的 SceneNode；`getComponent<CameraComponent>()` 返回非空
- 旧 demo 代码（不改 *逻辑*）能继续跑：`scene_viewer` 启动时 camera 行为与改造前一致
- `OrbitCameraController` / `FreeFlyCameraController` 改造（仅参数类型替换）后画面与改造前一致

## 修改范围

- `src/core/scene/components/camera_component.{hpp,cpp}`（新；继承自 [REQ-037-a R1](037-a-component-model-foundation.md#r1-icomponent-基类) 的 `IComponent`）
- `src/core/scene/camera.hpp` / `.cpp`（**删除** `Camera` 类；`CameraData::Param` 等共用类型保留）
- `src/core/scene/scene.hpp` / `.cpp`（addCamera / removeCamera 同时维护 m_cameras 与 SceneNode 层级；参数类型改 `SceneNode*`）
- `src/core/frame_graph/render_queue.cpp`（按 owner node 取 `CameraComponent`，并按 `m_active` 过滤）
- `src/infra/camera/orbit_camera_controller.cpp` / `freefly_camera_controller.cpp`（参数类型 `Camera*` → `CameraComponent*`，逻辑不变）
- `src/demos/scene_viewer/main.cpp`（构造 camera 路径改为 SceneNode + addComponent）
- `src/test/integration/`（新增 camera-as-component 测试 + 现有 camera 测试参数迁移）
- `notes/source_analysis/src/core/scene/camera.md`（落地后更新）

## 边界与约束

- 不引入"camera component 子类型"分层（perspective / orthographic 仍由 `m_data.projection` 区分，不拆类）
- 不改 `CameraData::Param` 内字段（fov / near / far / projection / cullingMask / target 全部不变）
- 不改 `Scene::getSceneLevelResources(pass, target)` / `getCombinedCameraCullingMask(target)` 接口
- 不引入"camera scale 影响 view matrix"语义；scale 显式忽略（R2）
- 旧 `Camera::position / target / up` 字段彻底删除，**不**保留 `// deprecated` 兼容层

### REQ-042 兼容预留

本 REQ **完全不动** `CameraComponent::m_data.target` 字段与 `matchesTarget` 接口签名。[REQ-042 R6](042-render-target-desc-and-target.md) 后置到 Phase 1.5 完工后、Phase 1 REQ-103 之前实施时，会把 `m_data.target` 类型从 `std::optional<RenderTarget>` 升级为 `std::optional<RenderTargetDesc>`、把 `matchesTarget` 改为比 desc 形状兼容。本 REQ 的"camera-as-component"接入与 m_target 升级在数据模型上正交（一个管"位置/朝向 + 数据归属"，一个管"渲染到哪个 attachment 形状"），无字段冲突，无 ABI 变化的迁移负担。

## 依赖

- [REQ-037-a IComponent 基础设施](037-a-component-model-foundation.md) — `CameraComponent` 是其第一个非 mesh-bearing 应用，**硬前置**
- [REQ-035 Transform 组件](finished/035-transform-component.md) — Camera 通过 owner SceneNode 间接消费
- [REQ-036 路径查询](finished/036-scene-node-path-lookup.md) — `Scene::findByPath("/editor_cam")` 命中 camera 节点
- 现有 [REQ-026 Camera visibility layer mask](finished/026-camera-visibility-layer-mask.md) — culling mask 字段不变

## 后续工作

- [REQ-040 Editor 命令总线](040-editor-command-bus.md) — `move camera_main 1 0 0` 直接生效（走 SceneNode transform setter），不需要"camera"专门命令路径
- [REQ-041 ImGui Editor MVP](041-imgui-editor-mvp.md) — 选中 camera 出 TRS gizmo；camera frustum visualizer 从 owner.worldTransform 取 view 矩阵
- 未来 `LightComponent` 走同样的"持有 SceneNode + 注册表登记"模式（`Scene::addLight(node)` 同形态）

## 实施状态

待实施。Phase 1.5 第 3b 步。在 [REQ-037-a](037-a-component-model-foundation.md) 落地后开工（硬前置），与 [REQ-035](finished/035-transform-component.md) / [REQ-036](finished/036-scene-node-path-lookup.md) 一起为后续 picking / DebugDraw / 命令总线提供"camera 也是 SceneNode"的统一假设。
