# REQ-041-f: 测试场景编辑 v1 — 节点 Rename / Duplicate 对齐 scene document

> 2026-05-11 重整：保留 Rename / Duplicate 这个方向，但旧文档过度绑定“component deep clone / clone() 虚函数 / component 模型 v2”。按当前代码审计，场景作者链路真正需要的是：对 primitive、camera、directional light 这些可保存节点，提供可靠的重命名与复制语义，并让 scene document / runtime / undo 一致。

## 背景

现在的场景作者主链路已经不只是运行时节点树，还包括：

- `SceneDocument` 的 `meshUri / materialUri / camera / directionalLight`
- `SceneRuntime` 里按节点维护的 directional light 关联
- `CommandBus` 的 undo / redo stash 机制

因此 Rename / Duplicate 若继续按“复制一堆 component 即可”去定义，会和当前 `lxe_editor` 的保存语义脱节。我们需要把它改成面向“可保存测试场景”的操作，而不是面向未来 component 架构实验的操作。

## 当前代码对照（2026-05-14）

| 能力 | 当前事实 | 对本 REQ 的影响 |
|---|---|---|
| Rename | Inspector 顶部 name 输入已通过 `set <path>.name <newName>` 改名 | R1 的命令基础已存在；仍要补 gameplay camera path 同步、冲突失败信息、Scene Tree 入口 |
| Scene Tree 菜单 | `SceneTreePanel` 右键菜单当前只有 `Remove` | R4 的 Rename / Duplicate 右键入口仍未实现 |
| Copy / Paste | 当前没有 `copy`、`paste_as_sibling` 或 editor 内部剪贴板命令 | R5 仍是新增命令能力 |
| 保存语义 | `SceneRuntime::captureSceneDocument()` 会捕获 runtime camera/directional light，并保留既有文档节点的 `meshUri/materialUri` | Duplicate 必须复制 document payload，而不是只复制 runtime node/component |
| 依赖数据 | node-level material override 还未实现 | R2 中复制 material override 的部分依赖 `041-e` 落地 |

## 目标

1. Scene Tree 右键菜单提供 Rename / Duplicate
2. Scene Tree 获得焦点时，支持 `Ctrl+C / Ctrl+V` 复制粘贴当前节点
3. 粘贴默认在当前选中节点的 parent 下生成一个兄弟节点
4. 复制结果保留 scene document 语义：primitive、camera、directional light、material override 都要跟过去
5. 操作结果必须能保存、撤销、重做

## 需求

### R1: Rename 保持路径与文档引用一致

- Scene Tree 节点右键菜单包含 `Rename`
- Inspector 顶部 name 编辑仍保留，两个入口走同一条命令总线路径
- Rename 底层继续走 `set <path>.name <newName>`

补齐约束：

- 若被改名节点正好是 gameplay camera path 指向的节点，scene document 中的 `gameplayCameraPath` 需要同步更新
- 若改名导致子树绝对路径变化，后续保存时必须以新路径为准
- 名字冲突时给出稳定的失败信息，不隐式覆盖兄弟节点

### R2: Duplicate 复制的是“可保存节点语义”，不是裸 runtime 外观

`Duplicate` / `Copy + Paste` 对以下内容做深复制：

- 节点 name / transform / visibilityMask
- `meshUri`
- `materialUri`
- node-level material overrides（如 `baseColor`）
- 当前 scene document 内的材质侧覆盖语义（若该节点引用的材质配置已被本 scene 定制）
- `camera` 负载
- `directionalLight` 负载
- 子节点树

要求：

- primitive 节点复制后仍是 primitive，而不是运行时展开后的不可保存 mesh
- directional light 复制后必须建立新的 light runtime 绑定，而不是复用旧节点的 light 指针
- camera 节点复制后保留当前投影参数

### R3: Duplicate / Paste 的命名与默认摆放

- 同父节点下使用 `<name>.copy`、`<name>.copy.001`、`<name>.copy.002` 递增
- 复制后默认选中新节点
- 对非空场景对象，复制出的节点默认在本地 `+X` 方向小幅偏移，避免与原节点完全重叠而“看似没复制成功”
- 若复制的是 camera，可同时对其线框代理做同样偏移

### R4: UI 与快捷键入口

- Scene Tree 右键菜单：`Rename / Duplicate / Remove`
- Scene Tree 获得焦点时：
  - `Ctrl+C`：复制当前 primary selection 到 editor 内部剪贴板
  - `Ctrl+V`：把剪贴板中的节点粘贴到**当前选中节点的 parent** 下，生成一个新的兄弟节点
- 若当前选中节点没有 parent（例如 scene root），则 `Ctrl+V` 不执行，并给出稳定提示
- `Ctrl+C / Ctrl+V` 仅在 Scene Tree 拥有焦点时响应；Inspector、Console、其他面板聚焦时不抢快捷键
- `Ctrl+D` 不再作为本 REQ 的默认快捷键入口
- 多选复制不在本 REQ 范围；v1 先把“单节点复制 + 子树复制”做稳
- editor 内部剪贴板只要求支持本次会话内复制粘贴；不做系统剪贴板集成

### R5: 命令总线与保存路径

- 引入明确命令：
  - `copy <path>`
  - `paste_as_sibling <targetPath>`
- `paste_as_sibling <targetPath>` 的语义是：读取当前内部剪贴板内容，在 `targetPath` 所在节点的 parent 下复制一个兄弟节点
- 成功后 structured 结果至少带：

```json
{ "path": "/world/cube.copy" }
```

- 操作必须进入现有 undo / redo history
- 保存场景时，duplicate 产物要完整出现在 scene document 中，并可在 reload 后恢复
- 右键菜单里的 `Duplicate` 可以作为 `copy + paste_as_sibling` 的一键封装，但底层语义要与快捷键一致

### R6: 测试覆盖

至少补以下测试：

- duplicate 一个 primitive 节点后，新节点保留 `meshUri/materialUri`，并能保存后再加载
- duplicate 一个 directional light 节点后，新旧节点有各自独立的 light runtime 绑定
- duplicate 一个 camera 节点后，`camera` 负载完整保留
- rename gameplay camera 节点后，`gameplayCameraPath` 同步更新
- Scene Tree 聚焦时 `Ctrl+C / Ctrl+V` 生效；其他面板聚焦时不生效
- `Ctrl+V` 会把复制内容粘贴为“当前选中节点的 sibling”，而不是 child
- duplicate / rename 都能被 undo / redo 正确回滚

## 修改范围

- `src/core/editor/scene_tree_panel.cpp`
- `src/core/editor/commands/builtin_commands.cpp`
- `src/core/editor/inspector_panel.cpp`
- `src/demos/lxe_editor/scene_document.{hpp,cpp}`
- `src/demos/lxe_editor/scene_runtime.cpp`
- `src/test/integration/test_scene_tree_panel.cpp`
- `src/test/integration/test_command_bus.cpp`
- `src/test/integration/test_scene_document.cpp`
- `src/test/integration/test_scene_runtime.cpp`

## 边界与约束

- 本 REQ 不做 component 模型 v2，也不引入 `IComponent::clone()`
- 本 REQ 不做多选 duplicate、跨父节点 paste、剪贴板系统
- duplicate 的保存语义以 `lxe_editor` 当前 scene document 为准；不追求成为通用引擎级复制框架
- 本 REQ 的粘贴目标固定为“当前选中节点的 parent 下”；不做“粘贴为子节点 / 粘贴到任意节点 / 跨 scene 粘贴”

## 依赖

- [REQ-041-d](041-d-scene-authoring-toolbar-palette.md)
- [REQ-041-e](041-e-scene-authoring-inspector-material-and-visibility.md)
- [REQ-041-b](finished/041-b-command-bus-v2.md)

## 后续工作

- 若单节点 duplicate 路径稳定，再讨论多选 duplicate / copy-paste
- 若未来 `lxe_editor` 的场景作者能力需要被其他入口复用，再评估是否下沉成通用 scene authoring service

## 实施状态

待实施。当前只有 Inspector Rename 与 Scene Tree Remove；Duplicate、Copy/Paste、Scene Tree Rename 入口、gameplay camera path 同步和 document payload 深复制仍未实现。优先级低于 `041-d` / `041-e`，但仍保留在 active 队列中，因为它直接提升“批量搭测试场景”的速度。
