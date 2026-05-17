# Phase 6 · Gameplay Layer

> 目标：让游戏逻辑脱离引擎源码，进入 TypeScript 脚本和组件生命周期。

## 当前基础

v0.1.0 已有 C++ component 模型和 command bus，但还没有 gameplay lifecycle。Phase 6 要补的是“游戏逻辑运行在哪里”。

## 实施顺序

| 顺序 | 主题 |
|---|---|
| 1 | Gameplay component lifecycle：awake/start/update/destroy |
| 2 | 事件总线：scene、input、physics、animation 事件 |
| 3 | TypeScript runtime 选型与嵌入 |
| 4 | 引擎 Query/Command binding 生成 |
| 5 | script asset hot reload |
| 6 | demo gameplay：Pong 或 third-person controller |

## 前置

| 前置 | 原因 |
|---|---|
| Phase 2 action/time | gameplay update 和输入 |
| Phase 3 asset registry | script 作为资产 |
| Phase 5 physics | collision events |
| Phase 10 capability manifest | binding 和 agent 共享 schema |

## 继续阅读

- [Phase 10 · Agent / MCP / CLI](phase-10-ai-agent-mcp.md)
- [AI-Native 原则](principles.md)
