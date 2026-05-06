# REQ-041-g: Component 模型 v2 — 同类型多 component + enable / disable

> 拆分自 2026-05-06 整理：原 [REQ-037-a](finished/037-a-component-model-foundation.md) v1 把"多 mesh / multi-material / 同类型多份 / enable·disable / 依赖声明"等成熟度工作显式留给 v2。评审后再次拆分：本 REQ 只做"同节点同类型多份 + enable·disable"两件有真实驱动的事；依赖声明（`ComponentTraits<T>`）移到 [REQ-041-j](041-j-component-dependency-declaration.md)（等 [Phase 5 物理](../roadmaps/main-roadmap/phase-5-physics.md) 出现真实 require 链时再立项）。**不**含 component 序列化 / 反射（等 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md) 引入统一 reflection 后再立项）。

## 背景

[REQ-037-a 组件模型基础](finished/037-a-component-model-foundation.md) 在 Phase 1.5 完成了 `IComponent` 基础设施 + Mesh / Material / Skeleton / Camera 转 component。v1 一次 `addComponent<T>` 就 assert 唯一性，调用方负责正确顺序，没有 enable / disable。这套约束在 Phase 1.5 编辑器 MVP 与 RTR 章节实验阶段够用。

随着实战推进会很快遇到两类需求（依赖顺序声明的需求等到 Phase 5 物理才出现，因此移到 [REQ-041-j](041-j-component-dependency-declaration.md)）：

1. **同节点多 mesh / 多 material**：一个 prop 节点包两段 mesh（车身 + 轮毂）走不同材质；当前 v1 必须拆成两个子节点，破坏"逻辑物体 = 一个节点"的直觉
2. **临时禁用一个 component 而不丢配置**：调试时希望"先关掉 collider 看渲染"，重新打开时不需要重建状态；当前只能 remove + 后续重新 addComponent，丢配置

本 REQ 在 037-a 的 `IComponent` / `addComponent / getComponent / removeComponent` 模板族基础上，把上述两件事一次落地。

## 目标

1. 同节点同类型多 component：`addComponent<MeshComponent>(...)` 可以挂多份，`getComponents<T>()`（复数）返回所有；`getComponent<T>()`（单数）保留为"取第一份"的便捷形式
2. enable / disable：每个 `IComponent` 增加 `bool enabled` 字段；renderable / pass / picking / DebugDraw 等遍历点统一跳过 `!enabled` 的 component
3. 不破坏 v1 单 component 路径的 ABI / API（`getComponent<T>()` 单数仍可用；多份场景显式调 `getComponents<T>()`）

## 需求

### R1: 同节点同类型多 component

`SceneNode::addComponent<T>(args...)` 改为允许同类型多次调用。底层存储仍是 `std::vector<std::unique_ptr<IComponent>>`，按插入顺序保留。新增：

```cpp
template <typename T>
std::vector<std::reference_wrapper<T>> getComponents();      // 全部 T 类型 component

template <typename T>
std::vector<std::reference_wrapper<const T>> getComponents() const;
```

- `getComponent<T>()`（单数）保留：返回第一份命中（若无返回 `std::nullopt`）；编辑器 inspector / 渲染主路径继续用单数
- `removeComponent<T>()` 在多份场景需要新增 `removeComponentAt<T>(usize index)` 与 `removeAllComponents<T>()`，避免"只删第一份"的二义性
- `Mesh / Material / Skeleton component` 的 renderable 主路径升级：单 mesh 走旧路径（`getComponents<MeshComponent>().size() == 1`），多 mesh 时遍历所有 `MeshComponent` 并各自配对相同顺序的 `MaterialComponent`（多 material 必须显式按 index 对齐；缺一时跳过该 mesh，并打 warning）

### R2: `IComponent::enabled`

```cpp
class IComponent {
 public:
  bool isEnabled() const { return m_enabled; }
  void setEnabled(bool enabled);

 private:
  bool m_enabled = true;
};
```

- `setEnabled` 在状态翻转时调一个 `onEnabledChanged(bool)` 钩子（virtual no-op 默认实现），让 `MaterialComponent` 等需要 install / uninstall 监听器的实现可以正确响应
- 渲染主路径：`SceneNode::getRenderingDataForPass` 跳过 `!isEnabled()` 的 `MeshComponent / MaterialComponent`
- 命令总线：`set <path>:<componentType>.enabled true|false` 由 [REQ-040-b](041-b-command-bus-v2.md) 的 setter dispatch 自动覆盖

### R3: Inspector / 命令总线集成

- inspector 面板在每个 component 区块上头加一个 enabled checkbox，改动发 `set <path>:<componentType>.enabled <value>` 命令
- `list components <path>` 命令在 [REQ-040-a](040-a-editor-command-bus.md) 的基础上扩展为列出所有 component（多 instance 显示 index）+ enabled 状态

### R4: 测试覆盖

`src/test/integration/test_component_v2.cpp`（新）：

- 同节点 `addComponent<MeshComponent>(a)` + `addComponent<MeshComponent>(b)` 后 `getComponents<MeshComponent>().size() == 2`，且 `getComponent<MeshComponent>()` 返回第一份
- `setEnabled(false)` 的 mesh component 不参与 `getRenderingDataForPass` 输出
- v1 单 component 调用路径（`addComponent` + `getComponent` 单数）行为完全不变（回归测试以 037-a 已有用例为准）

## 修改范围

- `src/core/scene/component.hpp` / `.cpp`（加 `m_enabled` + `setEnabled` + `onEnabledChanged` 钩子）
- `src/core/scene/object.hpp` / `.cpp`（`addComponent` 容许同类型多份；新增 `getComponents<T>()` / `removeComponentAt<T>` / `removeAllComponents<T>`）
- `src/core/scene/components/material_component.cpp`（`onEnabledChanged` 时 install / uninstall 现有 pass listener）
- `src/core/editor/inspector_panel.cpp`（每个 component 区块顶部 checkbox）
- `src/core/editor/commands/list.cpp` / `set.cpp`（`list components` 扩展；`set` 支持 enabled 子键）
- `src/test/integration/test_component_v2.cpp`（新）

## 边界与约束

- v2 **不**做 component 序列化 / 反射；那条线在 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md) 引入统一 reflection 后单独立项
- v2 **不**做 `getComponent<T>()` 性能优化：单 instance 路径仍是 < 10 元素线性扫描；多 instance 也只在 component 数 > 32 时才考虑加索引（不在本 REQ 范围）
- v2 **不**改变 `componentTypeId<T>()` 的实现（仍是地址 token；跨 DLL 一致性留到真出现 dynamic library 时再切 `StringID`）
- enabled 不是一种"半失效"信号：disabled component 不参与渲染 / picking / DebugDraw，但仍占内存且 listener 已 uninstall；想完全释放仍走 `removeComponent`

## 依赖

- [REQ-037-a 组件模型基础](finished/037-a-component-model-foundation.md) — 已落地；本 REQ 在它之上加成熟度
- [REQ-040-a 编辑器命令总线](040-a-editor-command-bus.md) — `set / add / list` 命令的 dispatch 框架
- [REQ-041-a ImGui Editor MVP](041-a-imgui-editor-mvp.md) — inspector 面板，本 REQ 加 enabled checkbox

## 后续工作

- [REQ-041-j component 依赖声明](041-j-component-dependency-declaration.md) — `ComponentTraits<T>` 的 Requires / Before / After，等 Phase 5 物理出现真实 require 链时立项
- component 序列化 / 反射 / asset round-trip：[Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md) 引入统一 reflection 后立项
- multi-mesh 路径稳定后，把 GLTF loader 中"多 primitive 拆成多子节点"的兼容代码改成"一个节点挂多份 MeshComponent"
- `LightComponent` / `ColliderComponent` / `RigidBodyComponent`（[Phase 5 物理](../roadmaps/main-roadmap/phase-5-physics.md)）落地时直接用 [REQ-041-j](041-j-component-dependency-declaration.md) 的 traits 描述依赖

## 实施状态

待实施。在 Phase 1.5（[REQ-037-a](finished/037-a-component-model-foundation.md) ~ [REQ-041-a](041-a-imgui-editor-mvp.md) + [REQ-042](042-render-target-desc-and-target.md)）全部落地、且至少一笔 Phase 1 渲染深度（如 PBR 完整管线）已暴露 multi-mesh / multi-material 真实需求后再立项。
