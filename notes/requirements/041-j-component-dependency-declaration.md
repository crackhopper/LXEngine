# REQ-041-j: Component 依赖声明 — `ComponentTraits<T>` Requires / Before / After

> 拆分自 2026-05-06 整理：原 [REQ-041-g](041-g-component-v2-multi-and-enable.md) 早期版本含"同节点多份 + enable·disable + 依赖声明"三件事；评审后把依赖声明作为最后置项独立成档（前两项有 multi-mesh / debug 临时禁用的真实驱动；依赖声明等到 [Phase 5 物理](../roadmaps/main-roadmap/phase-5-physics.md) 出现 RigidBody / Collider 真实 require 链时再立项）。本 REQ 在 041-* 家族里编号靠后（`-j`），表示其立项窗口在多数 v2 子项之后。

## 背景

[REQ-037-a 组件模型基础](finished/037-a-component-model-foundation.md) + [REQ-041-g component v2](041-g-component-v2-multi-and-enable.md) 把 `IComponent` / multi-instance / enable·disable 都做完后，`addComponent<T>` 的语义是"加一份就行"，调用方靠记忆保证顺序（如未来 `RigidBodyComponent` 必须在 `ColliderComponent` 之后才能正确取碰撞数据）。这条隐式契约一旦多人 / agent 调用就会出错；本 REQ 把它显式化。

[Phase 5 物理](../roadmaps/main-roadmap/phase-5-physics.md) 立项前，LX 现有 component（Mesh / Material / Skeleton / Camera）相互间没有顺序依赖；本 REQ 是为了"出现真实 require 链时不需要再发明一套机制"。

## 目标

1. 提供编译期 `ComponentTraits<T>` 模板，让 component 类型可声明 `Requires` / `Before` / `After` 关系
2. `addComponent<T>` 在挂入容器前按 traits 校验，违反时返回 `std::nullopt` 并不挂入
3. 命令总线 `add` 命令转译 nullopt 为可读错误（含 missing requires 列表），写进 `structured` 字段
4. 现有 v1 + 041-g 已有 component 默认零 traits，不强制补关系

## 需求

### R1: `ComponentTraits<T>` 模板

```cpp
template <typename T>
struct ComponentTraits {
  using Requires = std::tuple<>;          // 同节点必须先存在
  using Before   = std::tuple<>;          // 必须排在它们之前
  using After    = std::tuple<>;          // 必须排在它们之后
};

// 例（Phase 5 物理立项时落地）：
template <>
struct ComponentTraits<RigidBodyComponent> {
  using Requires = std::tuple<TransformComponent, ColliderComponent>;
  using After    = std::tuple<ColliderComponent>;
};
```

- 默认特化空 tuple；不强制为现有 Mesh / Material / Skeleton / Camera 补关系（避免 037-a 落地后再回头加约束）
- 类型集合用 `std::tuple` 而非 `type_list` 自定义模板，标准库覆盖即可

### R2: `addComponent<T>` 校验

- 挂入前校验：`Requires` 全在；插入位置满足 `Before` / `After` 的偏序
- 校验失败：返回 `std::nullopt`，**不**挂入；走当前 logger 打 warning 含具体原因（哪个 require 缺失 / 哪个顺序冲突）
- 校验通过：行为与 [REQ-041-g](041-g-component-v2-multi-and-enable.md) 一致（多份允许，按插入顺序）

### R3: 命令总线集成

- [REQ-040-a](040-a-editor-command-bus.md) 的 `add <path> <componentType> [args...]` 在 `addComponent` 返回 nullopt 时回 `{ ok: false }`，`structured` 字段含：

```json
{ "missing_requires": ["TransformComponent"], "order_violation": null }
```

- inspector 在 component 添加按钮上 hover 时显示该 component 的 traits 摘要（"requires: A, B"），让 UI 调用方提前看到约束

### R4: 测试覆盖

`src/test/integration/test_component_traits.cpp`（新）：

- 配 `ComponentTraits<B>::Requires = std::tuple<A>`；先 `addComponent<B>` → 返 nullopt + warning；先 A 后 B → 成功
- 配 `ComponentTraits<C>::After = std::tuple<A>`；A 在尾、B 在前先 add，再 add C → 成功
- 现有 Mesh / Material 无 traits 配置：组合 add 顺序不受任何约束（回归测试）
- 命令总线 `add /node BogusComp` → `ok=false`，`structured.missing_requires` 列出 BogusComp 的所有 requires

## 修改范围

- `src/core/scene/component.hpp`（加 `ComponentTraits<T>` 默认模板）
- `src/core/scene/object.hpp` / `.cpp`（`addComponent` 走 traits 校验）
- `src/core/editor/commands/add.cpp`（接 nullopt + structured）
- `src/core/editor/inspector_panel.cpp`（traits 摘要 hover）
- `src/test/integration/test_component_traits.cpp`（新）

## 边界与约束

- **不**做运行时 ComponentTraits 注册 / 修改：traits 是编译期描述
- **不**做"自动按 traits 重排已挂 component"：traits 仅校验 `addComponent` 时刻；想调整顺序仍走 `removeComponent` + 重新 add
- **不**为现有 v1 + 041-g component 强制补关系；只在新 component（Phase 5 物理首发）显式声明 traits
- 跨 DLL：不在本 REQ 范围（沿用 037-a 的边界——单 binary 假设）

## 依赖

- [REQ-037-a 组件模型基础](finished/037-a-component-model-foundation.md) — `IComponent` / `addComponent` 模板
- [REQ-041-g component v2](041-g-component-v2-multi-and-enable.md) — multi-instance / enabled 字段（traits 与 multi-instance 共享 `addComponent` 校验路径）
- [REQ-040-a 编辑器命令总线](040-a-editor-command-bus.md) — `add` 命令的 structured 输出

## 后续工作

- 为 [Phase 5 物理](../roadmaps/main-roadmap/phase-5-physics.md) 首批 component（`ColliderComponent` / `RigidBodyComponent`）填 traits，验证 require 链
- 跨 DLL traits 一致性：等真出现 dynamic library 边界时再立项

## 实施状态

待实施。立项窗口：[Phase 5 物理](../roadmaps/main-roadmap/phase-5-physics.md) 立项 + 出现真实 require 链时一起做（在那之前没有任何 component 需要声明依赖，立项过早会做无用功）。
