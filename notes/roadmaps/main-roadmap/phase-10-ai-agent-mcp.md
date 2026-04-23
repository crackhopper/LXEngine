# Phase 10 · MCP + Agent + CLI

> **目标**：把引擎升级成一个 **AI-Native** 运行时 —— 默认启 MCP server，内置一个可对话的 agent，CLI 支持交互模式。人类或外部 agent 都可以通过自然语言驱动引擎。
>
> **依赖**：Phase 2（文本内省 + 命令层 + 事件流）、Phase 3（资产管线 + provenance）、Phase 6（TS 脚本 + 组件反射）、Phase 9（编辑器的 RPC 通道）。
>
> **可交付**：
> - `engine-cli --chat` —— CLI 进入交互对话模式，能通过自然语言操作引擎
> - `engine-cli --mcp-stdio` —— 对标准 MCP 客户端（如各主流 agent 框架）暴露 tool 列表
> - `engine-cli --mcp-ws --port <port>` —— MCP-over-WebSocket，配合 Phase 9 编辑器里的 chat panel
>
> **本阶段落实的原则**：[P-3 三层 API](principles.md#p-3-三层-api查询--命令--原语) · [P-4 Capability Manifest](principles.md#p-4-单源能力清单capability-manifest) · [P-6 Intent Graph](principles.md#p-6-意图图intent-graph) · [P-8 Dry-run](principles.md#p-8-dry-run--影子状态) · [P-9 成本模型](principles.md#p-9-成本模型是一等公民) · [P-13 HITL 契约](principles.md#p-13-human-in-the-loop-是类型级契约) · [P-14 能力发现](principles.md#p-14-能力发现优于能力配置) · [P-17 Eval Harness](principles.md#p-17-eval-harness-是内建设施) · [P-18 沙箱进程](principles.md#p-18-沙箱友好的进程模型)

## 为什么这是 AI-Native 的核心

前 9 个 phase 打造的引擎已经非常接近"传统小型引擎"：有渲染、基础层、资产、动画、物理、gameplay、音频、UI、编辑器。但如果 agent 不能直接用这些能力，它仍然只是一个"被 AI 辅助开发的"引擎，而不是**AI-Native**的引擎。

AI-Native 意味着：

1. **引擎本身就会对话**。不是"通过 VSCode 的 Claude 插件来操作引擎"，而是引擎进程自己持有对话上下文，接收自然语言指令。
2. **Agent 有技能包**。引擎把自己的能力封装成 "skills"（命名的命令 + schema + description），agent 动态选择调用，不是靠人类手写 prompt 告诉它能做什么。
3. **协议标准化**。走 MCP 协议而不是自创 RPC，这样外部 agent 能无缝接入 —— 今天是 Claude Code，明天是任何新出现的 agent 框架都行。
4. **全模态输入输出**。能读文本 dump（Phase 2）、能执行命令（Phase 2 REQ-213）、能写脚本（Phase 6）、能生成资产（Phase 11）、能看截图（Phase 1 REQ-117 的 headless 渲染）。

---

## 范围与边界

**做**：
- MCP server 实现（stdio + WebSocket 两种 transport）
- Skill registry —— 把引擎命令包装成 MCP tools
- 引擎内置 agent runtime（本地对话循环）
- 外部模型适配（OpenAI / Anthropic / 本地 Ollama / LiteLLM）
- CLI 交互模式（REPL-style 对话）
- 技能扩展机制（类似 `.claude/skills/`）
- 沙箱 + 权限（agent 不能 `rm -rf`）
- 对话持久化 + 多 session
- 工具调用轨迹（trace）可观测

**不做**：
- 训练自己的模型
- 多 agent 协作框架（swarm / crew）
- 完整的 Cloud 服务
- 私有模型 fine-tune 流水线

---

## 前置条件

- Phase 2 已提供 `describe()` / `dumpScene` / `invokeCommand` 全套文本接口
- Phase 6 已能让 agent 写 TS 脚本
- Phase 9 的 WebSocket 服务器已经实现 —— 本阶段复用同一个 server，只是加 MCP 协议

---

## 工作分解

### REQ-1001 · MCP Server 实现

[Model Context Protocol](https://modelcontextprotocol.io/) 是一个开放的 agent 接入协议。引擎作为 server 对外暴露 tools / resources / prompts 三类原语。

- MCP 服务器实现封装在 infra 层
- 支持三大原语：
  - **Tools**：引擎命令层（Phase 2 REQ-213）的**自动映射** —— 一条命令 = 一个 tool
  - **Resources**：引擎内文本资源（场景 dump / 资产清单 / 日志 / 性能指标 / profile trace）
  - **Prompts**：预置 prompt 模板（典型任务的 starter）
- Transport 两种：
  - stdio（用于 subprocess 形式接入主流 agent 框架）
  - WebSocket（用于 Phase 9 编辑器的内置 chat）

**关键**：MCP server **不是**单独实现一套 RPC 映射。它就是 Phase 2 命令总线的一个 transport adapter，消息协议换成 MCP 的 JSON-RPC。契合 [P-19 命令总线](principles.md#p-19-bi-directional-命令总线)。

**验收**：通过任意主流 MCP 客户端连接引擎，能发现 tool 列表、能调用命令、能订阅资源变化。

### REQ-1002 · Skill Registry & Capability Manifest

契合 [P-4 单源清单](principles.md#p-4-单源能力清单capability-manifest) + [P-14 能力发现](principles.md#p-14-能力发现优于能力配置)。

"Skill" = 一组相关的命令 / 查询 + 描述 + 示例，按领域分组。

**核心规则**：
- 新增一条命令 → 自动归入对应 skill 分组 → 自动出现在 manifest → 自动成为 MCP tool，**无需改任何配置**
- 每个 skill 带 `name` / `description` / `when_to_use` / `examples` 四元组，LLM 用它决定何时调用
- 每个 tool 的 schema 由单源派生
- 提供 `capability.list()` / `capability.search(query)` 作为 agent 查询能力的入口

**典型 skill 分组**（非冻结清单，可增减）：
- `scene` — 场景树 / transform / 组件挂载 / 语义查询
- `assets` — 加载 / 卸载 / 导入 / provenance 查询 / 生成（Phase 11 追加）
- `gameplay` — prefab 实例化 / 脚本绑定 / 事件发布
- `rendering` — 截图 / 相机设置 / framegraph 描述
- `physics` — raycast / overlap / force
- `io` — save / load / export
- `dev` — profile / hot-reload / trace / 事件流查询
- `eval` — 自测 / 回归检查（REQ-1010）
- `migrations` — schema 迁移 / 版本升级（对接 Phase 3 REQ-311）

**验收**：
- 通过 MCP 客户端连接后能看到所有 skill 分组
- `capability.list()` 返回的 tool 数 = 引擎命令层注册的命令数
- 新增一条命令后，客户端下次握手自动看到新 tool

### REQ-1003 · 引擎内置 Agent Runtime

**关键决策**：引擎 **本身** 持有一个 agent 对话循环。不是只做被动 server 等外部调用。

`AgentRuntime` 的职责：
- 维护对话历史
- 维护当前 intent graph（见 REQ-1011）
- 对接一个 **IModelProvider** 接口
- 执行 agent loop：user message → model → text or tool_use → execute → 反馈 → repeat
- 订阅接口供前端显示 streaming 输出 + tool call + tool result + error
- 可观测：`dumpConversation` / `dumpIntent` / `getTrace`

**内部规则**：
- Agent 调用的 tool 直接走命令层（本地），不走 MCP 协议层的 JSON-RPC（避免自己 loopback）
- 相同的 tool 通过 MCP transport 也对外部 agent 暴露

**验收**：engine-cli 启动后给它一句话指令能让 agent 自动选择命令、执行、返回结果。

### REQ-1004 · Model Provider 适配

定义 `IModelProvider` 接口，抽象"给 provider 发一串消息 + tool schema，拿回文本或 tool_use"。

**要求**：
- 接口不绑定任一具体厂商
- 同时支持同步调用和 streaming
- 支持函数/tool 调用（现代 LLM 普遍支持）
- 可扩展：本地模型 / 商用 API / 网关聚合

**选型参考**：至少接入 1 个商用 API provider + 1 条本地推理路径 + 1 个聚合网关（让切换模型成本最低）。具体产品不在 roadmap 中冻结。

**配置**：通过 config 文件切换 provider + model；API key 从环境变量读取。

**验收**：切换 provider 配置后，agent 能从不同模型获得响应，其余调用点无感知。

### REQ-1005 · CLI 交互模式

```bash
$ engine-cli --scene assets/scenes/level1.json --chat
Engine loaded: 127 nodes, 34 assets
Agent ready (claude-opus-4-6). Type 'help' for commands, or just tell me what you want.

> 当前场景里有多少个带物理的物体？
Thinking...
Calling scene.findByComponent({"type":"RigidBody"})
Result: 12 nodes.
场景里有 12 个带物理的物体。它们大部分是 "environment/*" 下的静态碰撞体，
还有 3 个动态刚体：player、crate_01、crate_02。

> 把其中一个 crate 移到 player 旁边
Calling scene.findByName({"pattern":"crate_*"})
Calling scene.getTransform({"path":"player"})
Calling scene.setTransform({"path":"environment/crate_01",
                            "position":[1.2, 0, -0.3]})
✓ 已把 crate_01 放到 player 右侧。

> 渲染一张截图给我看
Calling rendering.takeScreenshot({"width":800,"height":600})
✓ Saved to cache/screenshots/2026-05-01-143022.png
(图片已输出到终端，若终端不支持 iTerm2 / Kitty 内联图片协议，打开 cache 文件查看)
```

- Readline 风格的输入，支持历史记录（上下箭头）
- `help` / `history` / `skills` / `session save xxx` 等 meta command
- Ctrl+C 中断当前 agent turn（不退出）
- Ctrl+D 或 `exit` 退出
- 对话历史可保存到 `~/.engine-cli/sessions/*.json`

**验收**：端到端跑通一个 5-轮对话的会话。

### REQ-1006 · Headless 服务模式

除了交互模式，还需要一个 "engine 作为 agent 的被动服务" 模式：

```bash
# 选项 A：stdio MCP（给 Claude Code 用）
engine-cli --mcp-stdio

# 选项 B：WebSocket MCP（给 Phase 9 编辑器用）
engine-cli --mcp-ws --port 9000

# 选项 C：HTTP MCP（给自建的 agent 框架用）
engine-cli --mcp-http --port 9001
```

headless 模式下，引擎不创建窗口但所有非 present 的管线都能跑（通过 Phase 1 REQ-117 的 headless renderer）。

**验收**：
- Claude Code 接 stdio 模式能操作引擎
- curl 调 HTTP 模式的 tool endpoint 能生效

### REQ-1007 · Skill 扩展机制

类似 `.claude/skills/` 的做法 —— 让人类 / agent 可以**动态**新增技能：

```
./engine-skills/
├── optimize-scene/
│   ├── skill.md       # description + instructions
│   ├── logic.ts       # TS 实现，调其他 skills
│   └── examples.md
├── generate-level/
│   ├── skill.md
│   └── logic.ts
```

- `skill.md` 前言带 YAML frontmatter：`name / description / when_to_use`
- `logic.ts` 用 Phase 6 的 TS runtime 执行，能调 engine API 和其他 skills
- Agent 根据 `when_to_use` 描述自动识别调用时机

这一机制让引擎能力**自扩展**：一个复杂任务如果 agent 经常做，人类（或 agent 自己）可以把它沉淀为一个新 skill。

**验收**：把一个自定义 skill 扔到目录后，`engine-cli skills list` 能看到。

### REQ-1008 · 权限 / 沙箱 / HITL 契约

契合 [P-13 HITL 契约](principles.md#p-13-human-in-the-loop-是类型级契约) + [P-18 沙箱进程](principles.md#p-18-沙箱友好的进程模型)：

Agent 驱动引擎是强能力，必须有边界：

**能力标签**：
- 每个命令在声明时带一组 `requires` 标签：`fs_write` / `net` / `scene_mutate` / `asset_generate` / `run_tests` / `spend_budget` 等
- 启动引擎时传入能力白名单：`--allow fs_write,scene_mutate`
- 运行时命令调用如果缺少对应标签，直接被拒绝（返回结构化错误 [P-12](principles.md#p-12-错误即教学)）

**HITL 级别**（[P-13](principles.md#p-13-human-in-the-loop-是类型级契约) 的 4 档）：
- `auto` / `notify` / `confirm` / `review`
- 交互模式下 `confirm` 和 `review` 会停住 agent 等人工
- Headless 模式下未显式授权的 confirm/review 命令自动拒绝
- 支持"按次授权" / "本 session 内不再问" / "永久白名单" 三种确认策略

**沙箱**（[P-18](principles.md#p-18-沙箱友好的进程模型)）：
- 引擎可在"只读 + 可写 overlay"的文件系统启动
- 所有可变状态在 `session_root/` 下，删除目录 = 重置
- 没有环境变量 / 用户目录写入的隐藏副作用
- 快速冷启动，允许 agent 每次实验都全新开始

**验收**：
- 没有 `scene_mutate` 标签的 agent 被拒绝调用 `scene.*` 的写入
- `review` 级别的命令在 headless 未授权时被拒绝
- 删掉 session_root 目录能彻底重置引擎状态

### REQ-1011 · Intent Graph 追踪

契合 [P-6 Intent Graph](principles.md#p-6-意图图intent-graph)：

Agent 每次对话不是孤立的 tool 调用，而是追求一个 **Intent**。引擎内部维护一棵意图树：

```
Intent (用户输入原文)
├── Plan (候选执行方案)
│   ├── Step (具体行动)
│   │   └── Action (命令调用)
│   └── Step
└── Plan (备选方案)
```

每个节点带元数据：
- `status`: `pending / running / done / failed / cancelled`
- `cost_estimate` / `cost_actual`（[P-9](principles.md#p-9-成本模型是一等公民)）
- `preconditions`（[P-8 Dry-run](principles.md#p-8-dry-run--影子状态) 预验证结果）
- `produced_events`（对应事件流中的 event ids）
- `confirmation_required`（[P-13](principles.md#p-13-human-in-the-loop-是类型级契约)）

**Intent 的作用**：
- 失败回滚：某一 step 失败时可以回滚到 plan 开始前
- 可视化：编辑器显示 agent 当前在做什么
- 审计：知道每条事件属于哪个 intent
- 学习：成功的 intent 可以沉淀为新 skill（REQ-1007）

**验收**：agent 执行一个多步任务 → 查询 intent graph → 能看到完整的 goal → plan → step → action 树 → 每个节点都带元数据。

### REQ-1012 · Budget 强制

契合 [P-9 成本模型](principles.md#p-9-成本模型是一等公民)：

引擎维护每个 session 的预算：

```
Budget {
    tokens:  { total, used, warn_threshold }
    time_ms: { total, used, warn_threshold }
    usd:     { total, used, warn_threshold }
}
```

**规则**：
- 每个命令声明自己的 `estimateCost(params)`
- Agent 调用 tool 前引擎自动检查：`current_used + estimate > warn_threshold` → 要求 [P-13](principles.md#p-13-human-in-the-loop-是类型级契约) 的 confirm
- 超过 `total` 直接拒绝
- Session 结束时输出实际消耗 vs 估算的偏差，持久化到 trace

**验收**：设一个 `usd.total = 0.5` 的预算，跑一个需要超预算的任务 → agent 被拒绝 → 错误提示含"需要更多预算"。

### REQ-1009 · 对话轨迹 Observability

每次 agent 运行都产生一条 trace：

```json
{
  "session": "a1b2c3",
  "started_at": "2026-05-01T14:30:22Z",
  "messages": [
    {"role": "user", "content": "把 crate 移到 player 旁边"},
    {"role": "assistant", "tool_calls": [
      {"tool": "scene.findByName", "params": {"pattern":"crate_*"}},
      {"tool": "scene.getTransform", "params": {"path":"player"}},
      {"tool": "scene.setTransform", "params": {...}}
    ]},
    {"role": "assistant", "content": "已把 crate_01 放到 player 右侧。"}
  ],
  "tokens": {"input": 2430, "output": 128},
  "cost_usd": 0.018,
  "duration_ms": 3200
}
```

- Trace 存在 `~/.engine-cli/traces/`
- CLI 命令 `engine-cli traces list` / `traces show <id>` 可以回看
- 集成到 Phase 9 控制台面板里展示

**验收**：每个 session 产生的 trace 和对话内容一致。

### REQ-1010 · Eval Harness

契合 [P-17 Eval Harness](principles.md#p-17-eval-harness-是内建设施)：

把"agent 能否正确用 skills"变成可测试的目标。引擎自带一个 **benchmark suite**：

**挑战集的结构**：
- 每个挑战 = 初始场景 + 自然语言指令 + 预期结果 + 评分器
- 评分器是一个纯函数：`(final_state) → score`，可以检查场景结构 / 事件流 / 特定字段值
- 挑战按难度分级：
  - **Tier 1 单命令**：`"把 player 移到原点"`
  - **Tier 2 多命令组合**：`"给所有 enemy 降 10 血"`
  - **Tier 3 多阶段 intent**：`"做一个能赢的关卡，包括障碍物和胜利条件"`
  - **Tier 4 越狱测试**：故意引导 agent 做危险操作，验证它拒绝
- 每跑完一次产生 score card：成功率 / 平均 token / 平均时间 / 平均 USD / 平均步数

**运行模式**：
- CI 每次 commit 触发
- 本地 `engine-cli eval --suite tier1` 快速跑
- 结果按 (引擎版本, agent 模型, skill 集) 三维切片，追踪质量变化

**反向驱动**：eval 失败的那些任务是**下一个 issue 的输入** —— 如果 tier 2 的"把 enemy 批量改血"成功率 < 80%，说明需要新增一个 `scene.batch_modify` 类型的 skill。

**验收**：CI 里能跑 tier 1 + tier 2 完整挑战集，产出 score card；tier 1 成功率 > 95%。

---

## 里程碑

### M10.1 · MCP Server 骨架 + Skills

- REQ-1001 + REQ-1002 完成
- demo：Claude Code 连上能看到引擎 tools，能调用 `scene.dumpTree`

### M10.2 · Agent Runtime + Provider

- REQ-1003 + REQ-1004 完成
- demo：本地 agent 循环能运行，调 Anthropic API 完成一个 5 步对话

### M10.3 · CLI 交互模式

- REQ-1005 + REQ-1006 完成
- demo：`engine-cli --chat` 可用的完整 REPL

### M10.4 · 技能扩展 + 权限 + 可观测

- REQ-1007 + REQ-1008 + REQ-1009 完成
- demo：自定义 skill 插入 + 权限控制 + trace 回看

### M10.5 · Intent Graph + Budget + Eval Harness

- REQ-1010 + REQ-1011 + REQ-1012 完成
- demo：跑一个 tier 1/tier 2 挑战集，输出 score card；intent graph 可视化；预算拒绝生效

---

## 风险 / 未知

- **Token 消耗**：复杂任务动辄上万 token，成本会升。解决：默认多分辨率 dump（[P-7](principles.md#p-7-多分辨率观察--渐进披露)） + 提供商侧的 prompt caching + 按事件流 cache 只在变化时重读。
- **LLM 漏做步骤 / 虚构 tool**：LLM 会虚构不存在的 tool 名或 param。解决：MCP 协议强校验 schema + 不存在的 tool 返回结构化错误（[P-12](principles.md#p-12-错误即教学)）+ 重试建议。
- **长对话的上下文膨胀**：超过模型 context window 后失忆。解决：滚动摘要 + 关键 dump 用"引用"而不是"内联"。
- **与 Web 编辑器的状态同步**：agent 改场景，编辑器 viewport 需要感知。解决：事件流广播（[P-19](principles.md#p-19-bi-directional-命令总线)）。
- **Sandbox 逃逸**：agent 写脚本可能试图调非白名单 API。解决：脚本 runtime 只暴露引擎命令层，不暴露文件系统 / 网络 / OS API。
- **Eval 的非确定性**：LLM 输出不确定，测试可能翻车。解决：`temperature=0` + 固定 seed + 评分器比对**工具调用步骤和最终状态**而不是比对自然语言输出。

---

## 与现有架构的契合

- **命令 API 的三处合流**：Phase 2 的命令 = Phase 9 的编辑器消息 = Phase 10 的 MCP tool。加一个能力只改一处，契合 [P-19 命令总线](principles.md#p-19-bi-directional-命令总线)。
- **单源 schema**：组件反射 + 命令声明被自动吐成 JSON Schema 喂给 LLM，契合 [P-4](principles.md#p-4-单源能力清单capability-manifest)。
- **文本内省的完整复用**：agent 能读的一切都是 Phase 2 REQ-210 的多分辨率 dump。
- **RPC 通道复用**：Phase 9 的 server 同时处理编辑器协议和 MCP 协议，通过消息 `method` 前缀区分。
- **脚本 runtime 的复用**：Phase 6 的 TS 运行时既跑游戏脚本、也跑 skill extension、也跑 agent eval 断言。

---

## 下一步

Agent 能驱动引擎、能写脚本、能读状态，还差最后一块：**能生产资产**。见 [Phase 11 · AI 资产生成](phase-11-ai-asset-generation.md)。
