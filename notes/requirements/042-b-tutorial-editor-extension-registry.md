# REQ-042-b: 教程支撑 — Editor toolbar 与 command 扩展注册入口

## 背景

当前 `lxe_editor` 已经采用 command-first 设计：toolbar、Inspector、API 和 MCP 诊断通道最终复用 `CommandBus`。这适合教学“按钮只是遥控器，命令总线才是线路”。

但新增 toolbar 按钮和 command 仍需要直接改 `UiOverlay`、`builtin_commands`、补全、测试和 API 状态。教程可以教当前手工扩展路径，但更适合新人学习的是稳定的扩展注册入口。

## 目标

1. 为教程提供可复用的 command 注册和 toolbar action 注册模型。
2. 让 toolbar 按钮通过 command schema 创建，而不是直接改 editor state。
3. 让补全、undo/redo、HTTP/WebSocket/MCP 复用同一份 command metadata。

## 需求

### R1: Command metadata

每个 command 声明：

| 字段 | 含义 |
|---|---|
| `verb` | 命令名字 |
| `summary` | 一句话说明 |
| `args` | 参数 schema |
| `undoable` | 是否支持 undo |
| `source` | builtin / extension |

### R2: Toolbar action metadata

toolbar action 声明：

| 字段 | 含义 |
|---|---|
| `id` | 稳定 action id |
| `label` | UI 显示名 |
| `icon` | 可选图标名 |
| `commandTemplate` | 点击时 dispatch 的命令模板 |
| `group` | toolbar 分组 |

### R3: Builtin commands 迁移到 metadata

现有核心命令继续保留 C++ handler，但补齐 metadata，使教程能通过同一接口解释命令、补全和 toolbar。

### R4: API 暴露 command / toolbar schema

HTTP/WebSocket/MCP 诊断通道可以查询 command 列表和 toolbar action 列表。

### R5: 测试覆盖

覆盖：

- metadata 与 handler 注册一致。
- toolbar action 点击只 dispatch command。
- command completion 使用 metadata。
- API 查询能看到新增 command。

## 修改范围

- `src/core/editor/command_bus.*`
- `src/core/editor/commands/builtin_commands.*`
- `src/demos/lxe_editor/ui_overlay.*`
- `src/demos/lxe_editor/lxe_editor_api_service.*`
- 相关 tests

## 边界与约束

- 本 REQ 不引入脚本插件系统。
- 本 REQ 不要求动态加载外部 binary。
- toolbar layout 仍由现有 ImGui UI 承载。

## 依赖

- `REQ-040-a`
- `REQ-041-b`
- `REQ-041-d`

## 后续工作

- command 权限 / HITL。
- 外部 extension package。

## 实施状态

未开始。当前仅作为教程中“未来顺滑工作流”的支撑需求。
