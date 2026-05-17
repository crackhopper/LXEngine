# REQ-044-b: Roadmap 支撑 — Engine CLI / MCP / Agent 入口

> 2026-05-17：本需求已从 active 队列移入 pending。Engine CLI / MCP / Agent 仍属于 Phase 10，但不进入 v0.1.1 active 队列。

## 背景

当前远程自动化主要围绕 `lxe_editor` API service 和外部 manager 工具展开。它能执行 editor command、读取 state、收集 event、录制和回放，但这还不是 Roadmap Phase 10 描述的 engine-level MCP / CLI / agent runtime。

Phase 10 要求无窗口 CLI、标准 MCP server、内置 agent runtime、权限与成本模型。为了让设计文档能清楚标注“当前已有 editor API，但 engine-level agent 尚未实现”，需要把最小入口拆成 active requirement。

## 目标

1. 定义 engine CLI 的最小可用模式。
2. 定义标准 MCP server 的首批 tools/resources。
3. 复用 CommandBus / state snapshot / event 模型。
4. 为权限、HITL 和成本模型留出扩展点。

## 需求

### R1: CLI 模式

新增或扩展 engine/editor 启动入口，至少支持：

| 模式 | 含义 |
|---|---|
| `--cli` | 无窗口执行单条命令或脚本 |
| `--chat` | 交互式 agent / command REPL 占位 |
| `--mcp` | 启动 MCP stdio server |
| `--headless` | 不创建窗口，仍可加载 project/scene |

首版可以先把 `--chat` 实现为 command REPL，不要求接入真实 LLM。

### R2: MCP tools/resources

首批 MCP surface 至少包含：

| 类型 | 名称 | 来源 |
|---|---|---|
| tool | `command.execute` | CommandBus |
| tool | `scene.save` | project/scene command |
| resource | `scene.state` | API state snapshot |
| resource | `project.state` | project summary |
| resource | `command.history` | CommandBus history |

### R3: 权限与确认占位

每个 tool 声明权限等级：

| 等级 | 含义 |
|---|---|
| `auto` | 可自动执行 |
| `notify` | 执行后通知 |
| `confirm` | 执行前确认 |
| `review` | 需要人工审查 |

首版可以只实现 schema 和默认拒绝策略；复杂 UI 确认可以后续扩展。

### R4: Agent runtime 占位

定义 agent runtime 接口，但首版不要求接入外部模型。接口至少能表达：

- 当前 conversation id。
- 可用 tools。
- 执行计划或 command list。
- 执行结果和错误。

### R5: 测试覆盖

覆盖：

- headless 启动后可以加载 scene 并执行 command。
- MCP `list_tools` 返回首批 tools。
- `command.execute` 能返回 structured result。
- 未授权 destructive command 被拒绝或要求 confirm。

## 修改范围

- engine / editor 启动入口
- CommandBus bridge
- MCP server 新模块
- `notes/tools/`
- `notes/design/editor-system/06-roadmap-boundaries.md`
- 相关 tests

## 边界与约束

- 本 REQ 不实现完整 LLM agent。
- 本 REQ 不引入云端托管。
- 本 REQ 不替代当前 `lxe_manager` 诊断链路。
- 本 REQ 不要求 Web Editor 完成。

## 依赖

- 当前 CommandBus
- 当前 scene/project state snapshot
- `REQ-044-a` 可作为 Web 通道参考，但不硬依赖

## 实施状态

Pending，未开始。当前仅作为 Phase 10 engine-level MCP / CLI / agent 的后续最小入口需求。
