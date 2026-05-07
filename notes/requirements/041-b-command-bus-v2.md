# REQ-041-b: 编辑器命令总线 v2 — 参数补全 + undo·redo + 多选 EditorState

> 拆分自 2026-05-06 整理：原 [REQ-040-a](finished/040-a-editor-command-bus.md) v1 把"参数补全 / undo·redo / 多选"显式留给 v2，本 REQ 收口这条 v2 路径。命令权限、throttling、MCP tool schema 自动从 brief 生成等更后置项见"后续工作"段。

## 背景

[REQ-040-a 命令总线](finished/040-a-editor-command-bus.md) 已落地：`verb arg1 arg2` 文本协议、handler 注册表、`{ ok, message, structured }` 返回、history 队列、verb 级 Tab 补全、ImGui 控制台面板、`EditorState` 单选。这些把"键盘 / 控制台 / gizmo 三种输入路径全部走命令总线"做出来了。

进入实战使用后会马上撞到三个 v1 显式延后的能力：

1. **参数补全**：在 `move /world/player/arm 1 0 0` 中敲到 `move /` 后想 Tab 出可选 path；v1 仅 verb 补全，参数还得手敲完整 path，对编辑器和 agent 都是慢路径
2. **undo / redo**：v1 的 history 字段已为此设计，但翻转回放逻辑没做。频繁拖 gizmo + 误操作时缺 Ctrl+Z / Ctrl+Y 是反人类
3. **多选**：编辑器复杂场景常需要"选中一组节点同时移动 / 改 layer / 删除"，v1 的 `EditorState::getSelected()` 只返回单节点

本 REQ 在 v1 的 dispatch / history / EditorState 框架上加这三件事，**不**重写 v1 命令协议、**不**破坏已注册命令的 brief / signature。

## 目标

1. 参数补全：`CompletionProvider` 接口让每个 handler 注册"在第 N 个参数 Tab 时返回候选列表"的回调；控制台 Tab 在 verb 之外也走这条
2. undo / redo：每个命令实现声明自身是否可逆 + 提供反向命令；`bus.undo()` 与 `bus.redo()` 翻转 history 游标；快捷键 Ctrl+Z / Ctrl+Y 与控制台 `undo` / `redo` 命令统一
3. 多选：`EditorState::select(...)` / `getSelected()` 升级到节点集合；`select` / `deselect` 命令支持多个 path 参数；`move /a /b 1 0 0` 等多目标语法（按 last-arg-is-numeric 启发式）
4. v1 已注册命令的语义保持兼容；对单选 / 单目标的 path 用法**不**回归

## 需求

### R1: 参数补全协议

`src/core/editor/command_bus.hpp` 扩展：

```cpp
struct CompletionContext {
  std::string  partialToken;          // 当前光标位置的不完整 token（可能为空）
  std::vector<std::string> precedingArgs;   // 前面已敲完的 args（不含 verb）
};

using CompletionProvider =
    std::function<std::vector<std::string>(const CompletionContext &)>;

class CommandBus {
 public:
  void registerCompleter(StringID verb, usize argIndex, CompletionProvider p);

  // 控制台调用：返回 token 候选 + 公共前缀（用于 Tab 补 token）
  CompletionResult complete(std::string_view line) const;
};
```

- 内置 completer：所有需要 `<path>` 参数的命令注册"按当前 scene root 递归扫节点 path"completer；调命令总线统一接 `Scene::listAllPaths()` 即可
- `<componentType>` 参数注册"已注册的 component 类型名"completer
- 控制台 Tab：先看光标位置在 verb 还是 arg；arg 走 `complete(line)` 路径

### R2: undo / redo

每个 handler 在注册时声明（v1 接口扩字段，老命令默认 `inverse = std::nullopt` 不可逆）：

```cpp
struct CommandBrief {
  StringID verb;
  std::string usage;
  std::string description;

  // 新：可选反向命令；nullptr 时该 verb 不可逆，unop history 跳过
  using InverseFn = std::function<std::optional<std::string>(
      const ParsedCommand &executed,
      const CommandResult &result)>;
  InverseFn inverse;
};
```

- v1 改状态的命令（`move / rotate / scale / set / add / remove`）补 inverse 实现；`select / list / get / cam preview` 等只读命令不补，标记不可逆
- `bus.undo()` 从 history 找最近一个可逆命令，调其 inverse 拿反向命令字符串，dispatch 后将原命令从 active 区移到 redo 区；`bus.redo()` 反向
- history 区分：`m_undoStack` / `m_redoStack`；任何新 dispatch 的可变命令清空 redo（标准编辑器语义）
- 控制台命令 `undo` / `redo` + 快捷键 Ctrl+Z / Ctrl+Y 一起注册，全部走 `bus.undo()` / `bus.redo()` 单点入口

### R3: 多选 EditorState

```cpp
class EditorState {
 public:
  void select(std::vector<SceneNodeSharedPtr> nodes);          // 整体替换
  void selectAdd(SceneNodeSharedPtr node);                     // 加入选区
  void selectRemove(SceneNodeSharedPtr node);                  // 移出选区
  void deselect();
  std::vector<SceneNodeSharedPtr> getSelected() const;         // 现状返集合
  std::optional<std::reference_wrapper<SceneNode>>
  getPrimarySelected() const;                                  // gizmo 锚点 = 最后加入项
};
```

- v1 `getSelected() -> SceneNode &` 升级为返集合；调用方迁移：编辑器 inspector 仍只显示 `getPrimarySelected()`；gizmo 在 `getPrimarySelected()` 的 transform 上挂，但拖拽时把 delta 应用到所有 selected
- 命令总线 `select <p1> <p2> ...` 支持任意数量 path；`select` 不传参 = `deselect()`
- 控制台 `move /a /b 1 0 0` 解析：从尾部贪心吃 numeric token 作 args，剩下 leading token 作多 path（启发式 — v2 显式约定）；如不便 LLM 解析，提供等价的 `select /a /b ; move 1 0 0` 串行式语法（`;` 分号在 [REQ-040-a R7](finished/040-a-editor-command-bus.md) `dispatchScript` 已天然支持）

### R4: 测试覆盖

`src/test/integration/test_command_bus_v2.cpp`（新）：

- 注册一个 path completer，`complete("move /wo")` 返回 `/world` / `/world/player` 等候选；公共前缀 `/world`
- `move /a 1 0 0` 后 `bus.undo()`：节点 a 的 translation 回到原值；`bus.redo()` 再次到 (1,0,0)
- `select /a /b /c` 后 `EditorState::getSelected().size() == 3`；`getPrimarySelected()` = node c
- 多目标 `move /a /b 1 0 0`：a 与 b 的 translation 各自 += (1,0,0)
- v1 单参数命令（`select /a`、`move /a 1 0 0`）行为不变（回归测试以 040-a 已有用例为准）

## 修改范围

- `src/core/editor/command_bus.hpp` / `.cpp`（`CompletionProvider` + `complete()` + `undo` / `redo` 栈）
- `src/core/editor/editor_state.hpp` / `.cpp`（升级到节点集合）
- `src/core/editor/commands/move.cpp` / `rotate.cpp` / `scale.cpp` / `set.cpp` / `add.cpp` / `remove.cpp`（补 inverse 实现）
- `src/core/editor/commands/select.cpp`（多 path 解析）
- `src/core/editor/console_panel.cpp`（Tab 走 `complete()`；Ctrl+Z / Ctrl+Y 接 undo / redo）
- `src/core/editor/inspector_panel.cpp`（按 `getPrimarySelected()` 渲染；gizmo 应用到所有 selected）
- `src/test/integration/test_command_bus_v2.cpp`（新）

## 边界与约束

- v2 **不**做命令权限 / 角色（每个命令仍可被任何 caller 调）；权限留 [Phase 6 gameplay](../roadmaps/main-roadmap/phase-6-gameplay-layer.md) 引入脚本边界后再立项
- v2 **不**做命令 throttling / rate limit；MCP server 自身做 caller 端速率控制即可，命令总线层保持纯
- v2 **不**做 MCP tool schema 自动从 brief 生成；Phase 1.6 MCP shim 仍手写 tool 注册（命令数 < 30，写得起）；自动从 brief 生成留到 brief 字段稳定后再立项
- v2 **不**做命令脚本文件加载（`source <file>`）；脚本作为 asset 类型登记需要 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md)，那时再立项
- inverse 命令的"等效性"：本 REQ 选 *字符串等价*（执行 inverse 后状态 == 执行前；不要求 inverse 与原命令在内部数据流上一一对称）
- undo 不跨 scene 切换：scene reload / 切换时 history / undoStack / redoStack 全部清空（避免上一场景命令在新场景执行）

## 依赖

- [REQ-040-a 编辑器命令总线](finished/040-a-editor-command-bus.md) — dispatch / history / `CommandBrief` 协议已就位
- [REQ-041-a ImGui Editor MVP](041-a-imgui-editor-mvp.md) — 控制台 Tab 补全 / 快捷键路由
- [REQ-036 路径查询](finished/036-scene-node-path-lookup.md) — path completer 用 `Scene::listAllPaths()`
- [REQ-035 Transform 组件](finished/035-transform-component.md) — undo 反向 transform 命令依赖 setter

## 后续工作

- [REQ-041-b 编辑器 v2 polish](041-c-editor-multi-select.md) — 把 undo / redo 接到 ImGui 工具栏 + 状态栏；多选的视觉化（高亮多个节点 + gizmo 锚点提示）由 041-b 接管
- 命令权限 / 角色：[Phase 6 gameplay](../roadmaps/main-roadmap/phase-6-gameplay-layer.md) 引入脚本边界后立项
- 命令 throttling / rate limit：等真出现 agent 风暴时再立项；命令总线本身保持纯
- MCP tool schema 自动从 brief 生成：等 Phase 1.6 MCP shim 落地一段时间，确认 brief 字段够稳后立项
- 命令脚本文件加载（`source <file>`） + 脚本作为 asset 类型：等 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md) 落地后立项

## 实施状态

2026-05-07 收口完成：

- 依赖已满足：`REQ-040-a`、`REQ-041-a`、`REQ-036`、`REQ-035` 均已落地
- 已实现：
  - `CommandBus` 已支持参数补全注册、`complete()`、`undo()` / `redo()`、undo/redo 栈
  - `EditorState` 已升级为多选集合，`getPrimarySelected()` 已提供主选锚点
  - `select` / `move` / `rotate` / `scale` / `set` / `cam fov` / `preview` / `undo` / `redo` / `add` / `remove` 已接入 v2 语义
  - `add` 已补 `<componentType>` completer；`get` / `set` 已共享字段路径补全
  - `ViewportOverlay` gizmo 在多选拖拽时以 primary selection 为锚点，把同一 delta 应用到全部 selected，并以 multi-target / script 形式统一提交命令总线
  - `src/test/integration/test_command_bus_v2.cpp` 与 `src/test/integration/test_viewport_overlay.cpp` 已覆盖路径/组件补全、add/remove undo·redo、多选 gizmo delta 提交
- 结论：REQ-041-b 已满足归档前验证条件，可继续执行 finish-req / archive。
