# Phase 10 · MCP + Agent + CLI

> **目标**：把引擎升级成 **AI-Native** 运行时 — 默认启 MCP server，内置可对话 agent，CLI 支持交互模式。人类或外部 agent 均可通过自然语言驱动引擎。
>
> **依赖**：Phase 2（内省 API）+ Phase 3（资产 GUID）+ Phase 6（TS 绑定 / 命令总线）+ Phase 9（编辑器的命令路径）。
>
> **可交付**：
> - `engine-cli --chat` — 启动后可对话，agent 可读 / 改场景
> - `engine-cli --mcp` — 启动 MCP server（stdio + WebSocket）
> - Claude Code / Codex 等外部 agent 能作客户端接入

## 当前实施状态（2026-04-24）

**未开工**。引擎当前 `src/main.cpp` 仅负责 `expSetEnvVK()` + 入口提示，不是 CLI 也不是 MCP server。

| 条目 | 状态 |
|------|------|
| MCP server（stdio + WebSocket） | ❌ REQ-1001 |
| Capability manifest 暴露 | ❌ REQ-1002 |
| 内置 agent runtime | ❌ REQ-1003 |
| CLI 交互模式 | ❌ REQ-1004 |
| Skill 扩展机制 | ❌ REQ-1005 |
| 权限 / 沙箱 | ❌ REQ-1006 |
| 成本 + HITL 强制 | ❌ REQ-1007 |

## 范围与边界

**做**：MCP server（stdio + WebSocket transport） / 把 Query + Command 暴露为 MCP tools（派生自 [P-4](principles.md#p-4-单源能力清单)） / 内置 agent runtime（长驻 conversation） / CLI 交互模式 + 管道模式 / Skill 扩展（类 `.claude/skills/` 约定） / 权限模型（白名单 / 本 session 授权 / 每次确认） / 成本预算强制（P-9）+ HITL 强制（P-13）。

**不做**：云端 agent 托管（本地 first） / agent 训练 / fine-tune（使用现成模型） / 多 agent 协作 orchestration（首版单 agent）。

## 前置条件

- Phase 2 `dumpScene` / `describe` 是 MCP tool 数据源
- Phase 3 AssetRegistry（资产相关 tool 需要 GUID）
- Phase 6 命令总线（MCP tool 调用走命令总线）
- Phase 9 编辑器（可选：agent 命令回显到编辑器）

## 工作分解

### REQ-1001 · MCP server

- 支持 stdio + WebSocket 两种 transport
- MCP 协议：tools / resources / prompts
- 复用命令总线（[P-19](principles.md#p-19-双向命令总线)）

**验收**：Claude Code 通过 stdio 接入，`list_tools` 返回引擎 tool 列表。

### REQ-1002 · Capability manifest 暴露

- MCP tool schema 派生自 [P-4 capability manifest](principles.md#p-4-单源能力清单)
- 新增能力 → 新增 manifest 条目 → MCP 自动感知

**验收**：加一个 query，不改 MCP server 代码，客户端能列到新 tool。

### REQ-1003 · 内置 agent runtime

- 长驻 conversation（历史 + session state）
- 支持多家模型 backend（OpenAI / Anthropic / 本地）
- 产出结构化响应（修改场景 / 生成资产 / 运行测试）
- 支持 intent graph（[P-6](principles.md#p-6-intent-graph)）

**验收**：“把 player 向左移 2 米” → agent 产生 intent graph → 调命令 → 场景更新。

### REQ-1004 · CLI 交互模式

- `engine-cli --chat` REPL：
  ```
  > move player 2m to the left
  [agent] I'll translate player by (-2, 0, 0). Confirm? [y/N]
  > y
  [ok] done. new position: (-1, 0, 3)
  ```
- 管道模式：`echo "dump scene" | engine-cli`
- 纯 headless（[P-20](principles.md#p-20-渲染与模拟可分离)）

**验收**：无窗口环境启动 CLI chat，交互正常。

### REQ-1005 · Skill 扩展

- 类 Claude Code `.claude/skills/` 目录约定
- Skill = 一组 prompt + tool 调用示例
- Agent session 启动时扫描 → 融合进 system prompt

**验收**：加一个 `build-level.skill.md`，agent 能按 skill 步骤执行多步任务。

### REQ-1006 · 权限 / 沙箱

- 工具分级：`auto` / `notify` / `confirm` / `review`（[P-13](principles.md#p-13-hitl-类型级契约)）
- 首次使用需人工确认；支持“本 session 不再问”或“白名单”
- Headless 下未显式授权即拒绝

**验收**：破坏性命令首次被拒绝；授权后可执行。

### REQ-1007 · 成本 + HITL 强制

- 每 tool 声明 `estimateCost(params) → {tokens, timeMs, usd}`（[P-9](principles.md#p-9-成本模型是一等公民)）
- Session 级预算记账；超预算拒绝
- 超警告阈值要求人工确认

**验收**：限额 $0.10 下跑大任务，超出时被拒绝。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M10.1 · MCP server | REQ-1001 + REQ-1002 | Claude Code 接入 `list_tools` |
| M10.2 · 内置 agent | REQ-1003 + REQ-1004 | `engine-cli --chat` 交互 |
| M10.3 · Skill + 权限 | REQ-1005 + REQ-1006 | 外部 skill 驱动多步任务 |
| M10.4 · 成本 / HITL | REQ-1007 | 预算 + 确认强制 |

## 风险 / 未知

- **MCP server 进程模型**：stdio 是 peer-of-claude-code；WebSocket 是 peer-of-browser-editor。共享实现但 transport 不同。
- **模型后端抽象**：OpenAI / Anthropic SDK 接口不同。抽象 `IChatModel` 统一。
- **Agent 幻觉 / 不一致**：依赖 [P-4 manifest](principles.md#p-4-单源能力清单) 消除；工具 schema 精确 → agent 失败率大幅降低。
- **安全**：skill 可能夹带危险指令；首次执行一律要求 HITL。

## 与 AI-Native 原则契合

本 phase 直接落地 [P-3](principles.md#p-3-三层-api查询--命令--原语) / [P-4](principles.md#p-4-单源能力清单) / [P-6](principles.md#p-6-intent-graph) / [P-9](principles.md#p-9-成本模型是一等公民) / [P-13](principles.md#p-13-hitl-类型级契约) / [P-14](principles.md#p-14-能力发现优于能力配置) / [P-19](principles.md#p-19-双向命令总线) / [P-20](principles.md#p-20-渲染与模拟可分离)。

## 与现有架构契合

- 引擎当前有 `expSetEnvVK()` 入口；CLI 在其之前完成命令行解析，其后进入相应模式（headless / chat / mcp / window）。
- `EngineLoop` 已支持 update hook；agent 的 tool 调用作 `CommandResult` 注入。

## 下一步

- [Phase 11 AI 资产生成](phase-11-ai-asset-generation.md)：MCP tool 扩展贴图 / 3D / 动画生成。
- [P-17 eval harness](principles.md#p-17-eval-harness-内建)：agent 挑战集从本 phase 开始持续扩充。
