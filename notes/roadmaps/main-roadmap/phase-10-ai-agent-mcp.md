# Phase 10 · Agent / MCP / CLI

> 目标：把已有 CommandBus、HTTP/WebSocket API、recording 和 manager debug 能力，收敛成正式的 engine CLI / MCP / agent 入口。

## 当前入口

[REQ-044-b](../../requirements/pending/044-b-engine-cli-mcp-agent-entry.md) 是本 phase 的 pending 最小需求。它不要求一次实现完整 agent runtime，但要先把 CLI、MCP server、capability discovery 的边界定下来。它不进入 0.2.0-pre 基线。

## 当前基础

| 已有 | 还缺 |
|---|---|
| CommandBus | 用户级 engine CLI |
| HTTP command API | MCP tools schema |
| WebSocket event stream | capability manifest |
| Recording controller | 权限 / HITL / cost model |
| lxe_manager MCP debug | 引擎自身正式 MCP server |

## 实施顺序

| 顺序 | 主题 |
|---|---|
| 1 | `engine-cli` headless 入口 |
| 2 | Query/Command manifest |
| 3 | MCP stdio transport |
| 4 | MCP websocket transport |
| 5 | permission + HITL |
| 6 | cost estimate |
| 7 | local agent runtime |
| 8 | eval harness |

## 与 Phase 1 的关系

Phase 1 不等 Phase 10。Shadow/G-Buffer 先在 editor 和 tests 中跑通；Phase 10 后再把 render graph、pipeline、scene、asset 状态暴露给 agent。

## 继续阅读

- [REQ-044-b](../../requirements/pending/044-b-engine-cli-mcp-agent-entry.md)
- [AI-Native 原则](principles.md)
