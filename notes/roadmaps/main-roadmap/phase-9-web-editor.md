# Phase 9 · Web Editor

> 目标：在浏览器中提供编辑器 shell，通过 IPC 操作运行中的引擎，而不是复制一套引擎逻辑。

## 当前入口

[REQ-044-a](../../requirements/pending/044-a-web-editor-ipc-and-shell.md) 是本 phase 的 pending 最小需求：先定义 Web Editor shell 与 IPC 合同，不要求完整替代 ImGui editor。它不进入 v0.1.1 active 队列。

## 实施顺序

| 顺序 | 主题 |
|---|---|
| 1 | Web shell：project/scene 面板骨架 |
| 2 | IPC：连接 `lxe_editor` 或 headless engine |
| 3 | scene tree / inspector 复用 CommandBus |
| 4 | viewport stream / screenshot / picking bridge |
| 5 | asset browser 连接 AssetRegistry |
| 6 | extension registry |

## 与现有 editor 的关系

| ImGui editor | Web editor |
|---|---|
| 当前主力工具 | 长期目标 |
| 本地 C++ UI | 浏览器 shell |
| 直接访问 runtime 对象 | 通过 Command/Query/API |
| 适合快速调试 | 适合 AI-native、远程、插件化 |

## 继续阅读

- [REQ-044-a](../../requirements/pending/044-a-web-editor-ipc-and-shell.md)
- [Phase 10 · Agent / MCP / CLI](phase-10-ai-agent-mcp.md)
