# REQ-036: 场景节点路径查询 — `Scene::findByPath("/world/player/arm")`

> 本 REQ 是 [Phase 1.5 ImGui Editor MVP + 命令总线](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 的第 2 步。在 roadmap 中以"REQ-202 场景节点路径查询"前向声明。

## 背景

`SceneNode` 已支持 parent/child 层级（`src/core/scene/object.hpp:88-178`），但 `Scene` 没有任何"按名字 / 路径定位节点"的能力。命令总线（[REQ-040](040-editor-command-bus.md)）需要把 `move <node-name> 1 0 0` 中的 `<node-name>` 解析成 `SceneNode*`；ImGui 编辑器（[REQ-041](041-imgui-editor-mvp.md)）的 scene tree 也需要"路径 → 节点"的稳定句柄方便选中状态在跨帧/跨命令间一致。

文件系统风格的路径（`/world/player/arm`）是最容易被人类与 agent 同时使用的格式：跟操作系统路径、glob 习惯都对齐。

## 目标

1. 给 `SceneNode` 加一个 `name` 字段（如尚无）
2. 给 `Scene` 加 `findByPath(path)` API，路径是斜杠分隔的字符串
3. 给 `SceneNode` 加 `getPath()` API，返回从 root 到自身的字符串路径
4. 重名时给出确定性规则（首匹配 + 警告），不抛错

## 需求

### R1: `SceneNode::name` 字段

- `std::string m_name`（字符串 — 节点名是命令 / UI 显示用，不进 PipelineKey，不需要 intern）
- 默认构造为空字符串
- `setName(std::string)` / `getName() const` 公共 API
- 名字允许 ASCII 字母数字 + `_` + `-`，禁止包含 `/`（路径分隔符）；setter 检测到非法字符 `assert` + 替换为 `_`
- 保留 v2 扩展为 `StringID` 的可能性（不在本 REQ 强制）

### R2: `SceneNode::getPath() const -> std::string`

- 路径形如 `/world/player/arm`
- 起始 `/` 表示 scene synthetic root
- 自身 path = parent.getPath() + "/" + name
- `Scene::findByPath("/")` 返回 scene synthetic root；真实 top-level 节点（无 parent）若 name 为 `world`，则 path 为 `/world`
- 中间任一祖先节点 name 为空 → path 中相应位置使用临时占位 `<unnamed-node-0xADDR>`（仅用于排错；编辑器会在生成 scene tree 时给所有节点强制赋名）

### R3: `Scene::findByPath(const std::string &path) const -> SceneNode*`

- 输入：`"/world/player/arm"`
- 行为：从 scene root 向下走每段，按 child name 严格匹配
- 返回：命中的 `SceneNode*`，未命中返回 `nullptr`
- 不引入 wildcard / regex（v1 严格匹配；wildcard 留 v2）
- 路径解析空段（`//`、起始多 `/`）按一段空名处理；不静默吞掉
- 输入不以 `/` 起始 → 视为相对 scene root 的路径，等价于 `/` + 输入

### R4: 重名规则

- 同一 parent 下允许 ≥ 2 个子节点同名（不强制唯一性）
- `findByPath` 命中第一个匹配（按 child 列表插入顺序）
- 若 root 注册时检测到同 parent 下重名，仅 `LOG_WARN`，不阻止注册
- 编辑器命令 `add` 在 R4 重名场景下自动追加序号（`arm` → `arm.001` → `arm.002`），由 [REQ-040](040-editor-command-bus.md) 的命令实现负责，不在本 REQ 强制

### R5: `SceneNode` parent 改变时的 path 缓存

- 不引入 path 缓存：每次 `getPath()` 现算
- 编辑器 / 命令调用 `setParent` 后，下一次 `getPath()` 自然反映新位置；无显式失效协议

### R6: `Scene::dumpTree() const -> std::string`

为 [REQ-040](040-editor-command-bus.md) 中 `list nodes` 命令准备一个文本树渲染：

```
/
├── world
│   ├── player
│   │   └── arm
│   └── ground
└── camera_main
```

- 输出格式：每行 `<前缀> <name>`；前缀使用 box-drawing chars
- 与 path 查询互逆：`dumpTree` 列出的每条 path 都能用 `findByPath` 找回
- 不带 transform 信息（dump 仅结构）

## 测试

- `Scene::findByPath("/")` 返回 root
- 添加 `world / player / arm` 三层后，`findByPath("/world/player/arm")` 命中 arm 节点
- `findByPath("/world/missing")` 返回 nullptr
- arm 节点的 `getPath()` == `"/world/player/arm"`
- 把 arm 重 parent 到 world 下：`getPath()` 变为 `"/world/arm"`，`findByPath("/world/player/arm")` 返回 nullptr
- 同 parent 下两个名为 `enemy` 的子节点 → `findByPath("/enemies/enemy")` 命中第一个；LOG_WARN 输出
- `Scene::dumpTree()` 文本输出对每行去除前缀后能用 `findByPath` 反查到节点

## 修改范围

- `src/core/scene/object.hpp` / `.cpp`（`m_name` 字段、`setName/getName`、`getPath`）
- `src/core/scene/scene.hpp` / `.cpp`（`findByPath`、`dumpTree`）
- `src/test/integration/test_scene_path_lookup.cpp`（新）
- `notes/source_analysis/src/core/scene/scene.md`（落地后更新）

## 边界与约束

- **不**引入 path-based event listener（不接受"`/world/player/*` 触发回调"这种）
- **不**做 path 内置语法（不接受 `..` / `.` / `[index]`）
- **不**改变 SceneNode parent/child 数据结构
- **不**引入 GUID（asset 系统的 GUID 是 Phase 3 范围）；name 仅供编辑器显示与命令总线寻址
- name 唯一性**不**由本 REQ 保证；命令实现可加序号，但 `findByPath` 始终首匹配
- 大场景下（>10k 节点）`findByPath` 是 O(depth × siblings)，无加速结构。可接受，编辑器节点数远小于 10k

## 依赖

- 现有 `SceneNode` parent/child API
- 若 Scene 当前没有显式 root，本 REQ 同步引入 synthetic root，仅服务路径查询与 `dumpTree()`

## 后续工作

- [REQ-040 Editor 命令总线](040-editor-command-bus.md) — `select / move / rotate / scale / set / get` 命令的 `<node>` 参数都通过 `findByPath` 解析
- [REQ-041 ImGui Editor MVP](041-imgui-editor-mvp.md) — scene tree 面板用 path 作为选中节点的稳定句柄；inspector 显示当前 path
- 未来 Phase 2 `dumpScene` (roadmap REQ-207) 可基于 `dumpTree` + 节点 transform / 组件信息扩展
- v2 wildcard / regex / `[index]` 语法（如有真实需要再加）

## 实施状态

已实施并验证完成。实现采用 scene-internal synthetic root，因此文档里的 root 语义以 `Scene::findByPath("/")` 为准，真实 top-level 节点 path 为 `/<name>`。

核验摘要：

- R1：完成。`SceneNode` 新增 `m_name`、`setName/getName`
- R2：完成。`getPath()` 返回 rooted path；空名段使用 `<unnamed-node-0xADDR>` 占位
- R3：完成。`Scene::findByPath(...)` 支持绝对路径、root-relative 简写、空段保留
- R4：完成。duplicate sibling 名字首匹配，显式命名重复输出 `WARN`
- R5：完成。未引入 path cache，reparent 后下一次 `getPath()` 即反映新路径
- R6：完成。`Scene::dumpTree()` 输出 box-drawing 结构树，导出路径可回查

验证：

- `ninja test_scene_path_lookup test_scene_node_validation`
- `./src/test/test_scene_path_lookup`
- `./src/test/test_scene_node_validation`
- `ctest --output-on-failure -R "test_scene_path_lookup|test_scene_node_validation"`
