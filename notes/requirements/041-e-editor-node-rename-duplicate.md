# REQ-041-e: 节点 Rename / Duplicate — 右键菜单 + 控制台命令 + Ctrl+D 快捷键

> 拆分自 2026-05-06：原 `041-b-editor-polish-v2.md` 早期 R5 段独立成档。Rename / Duplicate 是编辑器最高频的两个右键菜单项，也是 Ctrl+D 快捷键的目标命令；它们的实现需要 deep clone（component 级）+ 命令 inverse 注册。

## 背景

[REQ-041-a v1](041-a-imgui-editor-mvp.md) 的 scene tree 面板右键菜单只暴露了 Remove；Rename / Duplicate 留到 v2。这两个命令的实现各有独立顾虑：

- **Rename**：发 `set <path>.name <newname>` 即可，命令本身简单；难点是 path 重名与子节点 path 同步（设父节点改名后，所有子节点的"绝对 path 字符串"都变了；命令总线需要稳定 path → node 的映射，[REQ-036 路径查询](finished/036-scene-node-path-lookup.md) 已就位）
- **Duplicate**：需要 deep clone 节点 + 所有 component；component 列表由 [REQ-037-a 组件模型基础](finished/037-a-component-model-foundation.md) 提供；deep clone 协议需要每个 component 提供 `clone()` virtual

本 REQ 把两者一起做：相邻、同样小、共享场景树右键菜单实现。

## 目标

1. `IComponent::clone() const -> std::unique_ptr<IComponent>` 协议；每个具体 component（Mesh / Material / Skeleton / Camera）实现自己的 clone
2. `duplicate <path>` 命令：deep clone 节点 + 所有 component；新节点名按 `<originalName>.copy` / `.copy.001` 自动追加序号；inverse = `remove <newPath>`
3. scene tree 面板右键菜单加 Rename / Duplicate（Remove 已在 v1）
4. Ctrl+D 快捷键 → `duplicate <getPrimarySelected().path>`；多选时对每个 selected 节点都发一次

## 需求

### R1: `IComponent::clone()`

```cpp
class IComponent {
 public:
  virtual std::unique_ptr<IComponent> clone() const = 0;
};

// 例：MeshComponent
std::unique_ptr<IComponent> MeshComponent::clone() const {
  return std::make_unique<MeshComponent>(m_mesh);    // mesh 资源 shared，不深拷
}
```

- mesh / material / skeleton / camera 资源句柄是 `SharedPtr` → 拷贝指针即可；**不**深拷资源数据（duplicate 是节点级，不是资产级）
- 每个 component 自带的简单状态（如 [REQ-041-g](041-g-component-v2-multi-and-enable.md) 的 enabled 字段）按值拷贝
- 如 component 持有 listener / pass binding 等"挂在 owner 上的回调"，`clone()` 在新 owner attach 时由 `attachTo` 重新 install（与 `addComponent` 现有路径一致）

### R2: `duplicate` 命令

`src/core/editor/commands/duplicate.cpp`（新）：

```cpp
SceneNodeSharedPtr clone = std::make_shared<SceneNode>(uniqueName);
for (const auto &component : original->listComponents()) {
  clone->addExistingComponent(component.get().clone());
}
clone->setLocalTransform(original->getLocalTransform());
clone->setParent(original->getParent());
return clone;
```

- 名字策略：`uniqueName` = 在父 children 列表中尝试 `<original>.copy` / `<original>.copy.001` / `<original>.copy.002`，第一个不冲突即用
- inverse 命令：`remove <newPath>`（注册到 [REQ-041-b R2](041-b-command-bus-v2.md) 的 inverse 协议）
- 子节点：递归 duplicate（v1 选 *深拷整棵子树*；不带子树会让 prop 复用价值减半）
- structured 输出：`{ "newPath": "/world/cube.copy" }`，方便 agent 链接后续命令

### R3: scene tree 面板右键菜单

- 点选节点右键 → 弹出菜单：`Rename / Duplicate / Remove`
- Rename：弹一个内联输入框（或 ImGui modal），用户改完发 `set <path>.name <newname>`
- Duplicate：发 `duplicate <path>`；多选时遍历选区每个节点发一次
- Remove：v1 已有，沿用

### R4: Ctrl+D 快捷键

- 全局快捷键 Ctrl+D → 调用 `EditorState::getSelected()` 拿到选区
- 选区为空 → 不响应（不弹错）
- 选区单节点 → `dispatch("duplicate <path>")`
- 选区多节点 → 按 [REQ-041-b R3](041-b-command-bus-v2.md) 多目标命令机制（`duplicate /a /b /c`）一次 dispatch 完成

### R5: 测试覆盖

`src/test/integration/test_editor_node_rename_duplicate.cpp`（新）：

- `duplicate /cube` → 场景里多出 `/cube.copy`，与 `/cube` 的 mesh / material / transform / 子节点结构一致
- 同节点连续 duplicate 3 次 → 名字 `<n>.copy` / `<n>.copy.001` / `<n>.copy.002`
- `set /cube.name foo` → `/cube` 节点 `getName() == "foo"`；其子节点 path 变为 `/foo/...`
- Ctrl+D 在选中 `/cube` 时与 `duplicate /cube` 等价
- duplicate 后 `bus.undo()` 把新节点 remove 掉（inverse 路径验证）

## 修改范围

- `src/core/scene/component.hpp`（加 `IComponent::clone() = 0`）
- `src/core/scene/components/mesh_component.cpp` / `material_component.cpp` / `skeleton_component.cpp` / `camera_component.cpp`（实现 clone）
- `src/core/editor/commands/duplicate.cpp`（新）
- `src/core/editor/commands/set.cpp`（在 v1 基础上承接 `.name` 子键，已包含在 [REQ-040-a R3](040-a-editor-command-bus.md) `set` 命令的语义里——本 REQ 仅复核）
- `src/core/editor/scene_tree_panel.cpp`（右键菜单 Rename / Duplicate / Remove）
- `src/core/editor/keymap.cpp`（注册 Ctrl+D）
- `src/test/integration/test_editor_node_rename_duplicate.cpp`（新）

## 边界与约束

- duplicate 子树时**深拷**整棵子树（节点 + component）；mesh / material / skeleton 资源仍 shared
- duplicate **不**做"批量参数化"（如"duplicate 后立刻平移 1m"）；多步操作走命令脚本
- Rename **不**做 scene tree 内联编辑控件复杂化（仅一个 input + Enter 提交）；自动补全 / 历史名字记忆等留 v3
- 重名容器策略：`.copy` / `.copy.001` / `.copy.002` 上限 999；超限抛 logic_error（实战中不会触发）
- 跨父节点 paste（不仅 duplicate 同父）：本 REQ **不**做；留 v3，等剪贴板抽象上来一起做

## 依赖

- [REQ-041-a ImGui Editor MVP](041-a-imgui-editor-mvp.md) — scene tree 面板 + keymap
- [REQ-041-b 命令总线 v2](041-b-command-bus-v2.md) — `duplicate` 命令的 inverse 注册 + 多目标命令机制
- [REQ-036 路径查询](finished/036-scene-node-path-lookup.md) — `<path>` 解析
- [REQ-037-a 组件模型基础](finished/037-a-component-model-foundation.md) — `listComponents` + 新 component 挂入路径

## 后续工作

- 跨父节点 paste / 剪贴板抽象：留 v3
- Rename 时自动补全（按已有节点名）：留 v3
- 组件级 duplicate（"复制一个 component 到另一个节点"）：等真实需求出现再立项

## 实施状态

待实施。立项窗口：[REQ-041-b 命令总线 v2](finished/041-b-command-bus-v2.md) 落地后开工。本 REQ 与 [REQ-041-c](finished/041-c-editor-multi-select.md) / [REQ-041-d](041-d-editor-undo-redo-ui.md) 互不依赖，可并行推进。
