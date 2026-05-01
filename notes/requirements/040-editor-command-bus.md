# REQ-040: Editor 命令总线 — 文本协议 + 注册表 + 历史 + 控制台后端

> 本 REQ 是 [Phase 1.5 ImGui Editor MVP + 命令总线](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 的第 6 步。在 roadmap 中以"REQ-151 Editor 命令总线"前向声明。

## 背景

用户在 Phase 1.5 设计讨论中明确：

> "编辑器启动的时候，带一个内置的 command line（这块可能要提前开发），所有编辑器里的操作，都可以敲命令来完成。所以我们不需要文件窗口，我敲命令即可。此外这个输入流，也提供一个 mcp 访问能力，方便外部 AI 控制。"

这意味着所有编辑器操作（gizmo 拖拽、面板按钮、菜单项）最终都化为**文本命令**，由命令总线分发。这套设计：

1. **MCP 友好**：未来 Phase 1.6 接 MCP shim 时只需把命令总线的 `dispatch(text)` 暴露成一个 MCP tool（如 `dispatch_command`）即可，几乎零额外工作
2. **可记录可重放**：所有交互在控制台留下一条文本日志；支持后续 undo / 回归测试 / agent 训练数据
3. **跟 P-19 双向命令总线**正好对齐：编辑器 / Web / agent / 脚本最终竞争同一套命令空间

本 REQ 不做 MCP shim（推到 Phase 1.6），但**接口形态必须为 MCP 暴露做好准备**。

## 目标

1. 提供 `CommandBus`：注册 handlers，按文本分发，返回 `CommandResult`
2. 文本协议简单可解析：`verb arg1 arg2 ...`，空白分隔，引号包围多词字符串
3. 内置命令集：`select / deselect / move / rotate / scale / add / remove / list / set / get / cam / preview`
4. 提供 ImGui 控制台面板（输入栏 + 历史 + 输出区）
5. `CommandResult` 可序列化为 JSON（为 Phase 1.6 MCP 暴露铺路）

## 需求

### R1: `CommandBus` 数据结构

`src/core/editor/command_bus.hpp`（新）：

```cpp
struct CommandResult {
  bool ok = false;
  std::string message;                          // 人类可读
  std::string structured;                       // JSON payload（可空）
};

using CommandHandler = std::function<CommandResult(std::vector<std::string> args)>;

class CommandBus {
 public:
  void registerHandler(std::string verb, std::string brief, CommandHandler handler);
  void unregisterHandler(const std::string &verb);

  CommandResult dispatch(const std::string &line);

  std::vector<std::string> listVerbs() const;
  std::string brief(const std::string &verb) const;

  // 历史（仅命令文本 + 结果 ok / message；不存指针）
  struct HistoryEntry {
    std::string line;
    CommandResult result;
    u64 timestampMs;
  };
  const std::vector<HistoryEntry>& history() const;

 private:
  std::unordered_map<std::string, CommandHandler> m_handlers;
  std::unordered_map<std::string, std::string> m_briefs;
  std::vector<HistoryEntry> m_history;
};
```

- `dispatch` 内部：tokenize → 查 handler → 调用 → 结果入历史 → 返回
- 未知 verb：返回 `{ ok: false, message: "unknown command: <verb>" }`
- handler 抛异常：catch 后返回 `{ ok: false, message: "exception: ..." }`，不让异常逸出

### R2: 文本协议

- 默认空白分隔（空格 / tab）
- 双引号包围 → 视为一个 token（支持 `"node with spaces"`）
- 反斜杠转义 `\"` / `\\` / `\n`
- 不支持注释（`#` / `//`）
- 不支持多行（一条命令一行；多命令脚本由 caller 拆行）

### R3: 内置命令集（v1 必须）

每个命令的 verb / brief / 行为：

| verb | brief | 行为 |
|---|---|---|
| `help` | `help [verb]` | 列出所有 verb 或单个 verb 的详细说明 |
| `select` | `select <path>` | 选中节点；写选中状态到 `EditorState` |
| `deselect` | `deselect` | 取消选中 |
| `move` | `move <path> <x> <y> <z>` | `findByPath` + `setTranslation` |
| `rotate` | `rotate <path> <rx-deg> <ry-deg> <rz-deg>` | XYZ 欧拉角 → quaternion → `setRotation` |
| `scale` | `scale <path> <sx> <sy> <sz>` | `setScale`，单参数 `<s>` 时三轴均匀 |
| `add` | `add (mesh \| light \| camera) <name> [args...]` | 新节点 attach 到当前选中节点（或 root） |
| `remove` | `remove <path>` | 从 scene 删节点（含子树） |
| `list` | `list (nodes \| cameras \| lights)` | 文本树或表格 |
| `set` | `set <path>.<field> <value>` | 通用 setter（dot-path：`set /camera_main.fov 75`） |
| `get` | `get <path>.<field>` | 通用 getter |
| `cam` | `cam (look-at \| reset \| fov ...)` | 编辑器相机控制 |
| `preview` | `preview (on \| off \| toggle)` | F 键全屏切换游戏相机预览 |

- 内置命令的实现按主题拆分：`src/core/editor/commands/transform_cmds.cpp` / `scene_cmds.cpp` / `camera_cmds.cpp` / `light_cmds.cpp` / `view_cmds.cpp`
- 启动时统一注册：`registerBuiltinCommands(CommandBus&, EditorState&, Scene&)`

### R4: `CommandResult.structured` JSON

每个 handler 返回 `structured` 时遵循：

- 空字符串：表示无结构化结果（仅 message）
- JSON object（或 array）：可被外部 MCP 客户端直接 parse
- 例：`get /camera_main.fov` 的 result：
  ```json
  { "ok": true, "message": "fov = 60", "structured": "{\"value\": 60.0}" }
  ```
- 例：`list nodes` 的 result：
  ```json
  { "ok": true, "message": "(tree text)", "structured": "{\"tree\": [{\"path\": \"/world\", ...}]}" }
  ```
- v1 不强制每个命令都填 structured；只在"会被 agent 消费"的命令上填（`get` / `list` 等）

### R5: ImGui 控制台面板

`src/core/editor/console_panel.hpp` / `.cpp`（新）：

- 一个 ImGui 窗口（拖动 / 关闭）
- 上半 area：滚动输出区，列出 history 中每条 `< line` + `> result.message`
- 下半 area：input text + 回车提交
- 上下方向键浏览历史（命令）
- Tab 触发自动补全（基于 `CommandBus::listVerbs()`）—— v1 仅 verb 补全，参数补全 v2
- 右上角"clear"按钮清空显示但不清 history（history 用于 undo / agent 训练）

### R6: `EditorState` 选中状态

`src/core/editor/editor_state.hpp` / `.cpp`（新）：

```cpp
class EditorState {
 public:
  void select(SceneNode* node);
  void deselect();
  SceneNode* getSelected() const;
};
```

- `select` 命令调用本接口
- gizmo 与 inspector 都查这里
- v1 单选；多选留 v2

### R7: 命令脚本入口（agent 友好）

```cpp
class CommandBus {
 public:
  std::vector<CommandResult> dispatchScript(std::string_view multiLineText);
};
```

- 拆行 + 跳过空行 + 跳过 `#` 起首注释行
- 任一命令失败时**不**短路（继续跑后续命令；返回每条结果）
- 这是 Phase 1.6 MCP shim 的主入口形态

### R8: 安全约束

- 命令不直接执行任意文件系统 I/O（`add mesh /etc/passwd` 不能读 root）
- `add mesh <path>` 的 path 必须经过 `assets/` 目录策略校验（沿用 [REQ-010](finished/010-test-assets-and-layout.md) / [REQ-011](finished/011-gltf-pbr-loader.md) 的 asset 路径解析）
- 不引入 `exec` / `eval` / shell 命令
- 命令不直接 `assert` 退出进程；非法输入仅返回 `{ ok: false }`

## 测试

- 注册一个 echo handler，dispatch 后历史里有这条记录
- 解析器正确处理引号 / 转义：`select "node with spaces"` 拆出 1 个 token
- 内置 `move /world/cube 1 0 0` 后节点 translation 等于 (1, 0, 0)
- `get /camera_main.fov` 返回当前 fov 值，structured 含合法 JSON
- `dispatchScript("select /a\nmove /a 1 0 0\nlist nodes")` 返回 3 条结果，第一条命中节点
- `help move` 返回 brief
- 未知 verb 返回 ok=false 不抛异常
- handler 内 throw → ok=false + message 含 `exception:`

## 修改范围

- `src/core/editor/command_bus.hpp` / `.cpp`（新）
- `src/core/editor/editor_state.hpp` / `.cpp`（新）
- `src/core/editor/console_panel.hpp` / `.cpp`（新；依赖 ImGui）
- `src/core/editor/commands/`（新目录，按主题拆 5 个 .cpp）
- `src/demos/scene_viewer/main.cpp`（启动时构造 bus + 注册 builtins + 加载 console panel）
- `src/test/integration/test_command_bus.cpp`（新）

## 边界与约束

- 协议**简单 verb arg**，**不**用 lisp / s-expr / json-rpc 输入（输出可 json，输入保持人类友好）
- v1 **不**做参数补全（只 verb 补全）
- v1 **不**做命令权限 / 角色（每个命令都可被任何 caller 调）；权限留 Phase 6+ 收口
- v1 **不**做 undo / redo（history 字段已预留，逻辑 v2 实现）
- v1 **不**做命令 throttling / rate limit
- v1 **不**做 MCP 暴露（推到 Phase 1.6，本 REQ 接口形态对齐 MCP 即可）
- 命令实现可能依赖具体子系统（scene / camera / debug_draw），但 `CommandBus` 本身保持纯
- 命令返回 `structured` 是 best-effort，不保证 schema 稳定性（agent 应基于 message 兜底）

## 依赖

- [REQ-035 Transform 组件](finished/035-transform-component.md) — `move/rotate/scale` 直接调 setter
- [REQ-036 路径查询](finished/036-scene-node-path-lookup.md) — 所有 `<path>` 参数靠 `findByPath`
- [REQ-037-b Camera 作为 component](037-b-camera-as-component.md) — `move camera_main` / `cam ...` 命令统一接口
- [REQ-038 picking](038-ray-aabb-picking-min.md) — 视口点击 → 内部生成 `select <path>` 命令
- [REQ-017](finished/017-imgui-overlay.md) ImGui overlay — 控制台面板的渲染

## 后续工作

- [REQ-041 ImGui Editor MVP](041-imgui-editor-mvp.md) — 接入控制台面板；gizmo 拖拽结束后发 `move/rotate/scale` 命令
- **Phase 1.6 MCP shim**：起一个 stdio JSON-RPC server，暴露 `dispatch_command(line: string) -> { ok, message, structured }` 工具；server 内部就是调 `bus.dispatch(line)`，~50 LOC
- v2：参数补全 / undo redo / 命令权限 / MCP tool schema 自动从 brief 生成
- v3：脚本文件加载（`source <file>`）+ 命令脚本作为 asset 类型登记进 [Phase 3 资产管线](../roadmaps/main-roadmap/phase-3-asset-pipeline.md)

## 实施状态

待实施。Phase 1.5 第 6 步。在 [REQ-035](finished/035-transform-component.md) / [REQ-036](finished/036-scene-node-path-lookup.md) / [REQ-037-a](037-a-component-model-foundation.md) / [REQ-037-b](037-b-camera-as-component.md) / [REQ-038](038-ray-aabb-picking-min.md) 落地后开工（每个内置命令依赖其中至少一个）。
