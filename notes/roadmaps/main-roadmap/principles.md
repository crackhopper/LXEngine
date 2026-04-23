# AI-Native 引擎的核心原则

> 这是整套 roadmap 的**架构级宪法**。它不描述某个阶段做什么，而是规定"无论哪个阶段、无论选哪个具体实现，都必须满足的不变量"。每个 phase 都会回到这里对照。
>
> 阅读顺序建议：先读本文件一遍形成心智模型，再按 phase 顺序浏览。在具体 phase 遇到"本阶段如何契合原则 X"时回到这里查。

---

## P-1 确定性是架构级不变量

> **同样的输入序列，必须产出同样的状态。**

原因：agent 能否依赖引擎 = 引擎是否确定。不确定的引擎逼迫 agent 写防御式代码、做 retry loop、人工校对结果，整个协作成本指数爆炸。

**要求**：
- 所有写入路径都通过命令层（P-3），命令对状态的变更是纯函数
- 浮点 / 物理 / 随机数有显式的 seed 与可复现模式
- 时间推进通过 `Clock` 抽象，支持"以最高速度跑到某个时刻"和"锁定帧率"两种模式
- 异步操作（资产加载、AI 生成）完成后以**事件**形式进入状态流，而不是隐式副作用
- 有一个"是否确定"的开关：`--deterministic`，关闭非确定性源（线程调度、硬件计时器、系统熵）

**代价**：
- 热路径可能略慢
- 某些优化（自由的多线程更新）被约束

**收益**：replay / 时间旅行 / agent 自动化 / 可信单测 / 未来的网络同步 —— 全部从这一条免费派生。

---

## P-2 状态即事件流

> **当前状态是事件流的一个投影，不是事实本身。**

核心结构：

```
initial state ─ event ─▶ state_1 ─ event ─▶ state_2 ─ event ─▶ ... ─▶ current
                ▲                    ▲                    ▲
                └──── 都是一等对象，可序列化、可 replay、可审计
```

**为什么这是 AI-Native 的关键**：
- **Undo / Redo**：倒着折叠事件
- **Time travel**：折叠到任意历史点
- **Replay**：用同一串事件重现 bug
- **Diff broadcast**：编辑器 / agent / 远端客户端订阅事件流即可同步 UI
- **审计**：每个变更"谁做的、何时做的、为什么做" 全部保留
- **自我学习**：成功的事件流可以作为 demonstration 喂给 agent

**代价**：
- 所有写入必须走事件，散点 `obj.field = value` 必须消失
- 事件 schema 需要版本化（见 P-17 重构友好）

---

## P-3 三层 API：查询 / 命令 / 原语

引擎的公开面被切成三层，每一层有明确的责任和受众：

| 层次 | 是否只读 | 是否可逆 | 是否给 agent | 典型调用者 |
|------|---------|---------|-------------|-----------|
| **Query layer** | ✅ | n/a | ✅ 永远安全 | agent / 编辑器 / 工具 |
| **Command layer** | ❌ | ✅ 每个命令 | ✅ 首选写入方式 | agent / 编辑器 / 游戏脚本 |
| **Primitive layer** | ❌ | ❌ | ❌ 不暴露 | 引擎内部 / 热路径 |

**Query layer**：
- 签名类似 `query.xxx() -> T`
- 无副作用，任意时刻可调用、可并发
- 任何返回值都能被序列化为文本（见 P-7）

**Command layer**：
- 签名类似 `command.xxx(params) -> CommandResult`
- 一个命令 = 一组原子修改 + 一个反向命令 + 一份成本估算
- 失败时状态不变（事务性）
- 每条命令产生一条事件（P-2）

**Primitive layer**：
- 热路径：渲染主循环、物理 step、采样骨骼、合批 draw call
- 引擎内部用，不出现在 agent/脚本/编辑器可达的任何 API 里
- 性能敏感，不承担确定性 / 可逆 / 可观察的义务

**强制约束**：Phase 10 的 MCP tool 只暴露 Query + Command 层。**任何 primitive 泄漏到 agent 就是 bug**。

---

## P-4 单源能力清单（Capability Manifest）

> **一个引擎能力在代码里只声明一次，从这份声明派生出所有外部面。**

一条命令 / 查询 / 组件类型 / 资产类型的"单次声明"会自动生成：

- MCP tool schema（供 agent 发现）
- TypeScript 类型定义（供脚本层类型检查）
- 编辑器 UI（供人类填表）
- CLI 帮助文本
- 文档页面
- 默认测试用例的 fixture

**不允许**："给这个命令加个参数，需要改 6 个文件"。加参数改一处，其他全部自动同步。

**实现思路（高度抽象）**：
- 源头可以是 C++ 的 constexpr 反射表 / 某种 IDL / 代码注释 / 外部 schema 文件
- 中间形态是 `CapabilityManifest`，是一棵带类型的树，每个叶子是一条能力
- 派生器（generator）由 CMake build step 驱动，把 manifest 吐成各种外部面

**为什么对 AI-Native 特别关键**：LLM 对"不一致的多份真相"极度不友好。一份是 spec、一份是代码、一份是文档、三份互相偏移时，agent 会不断生成"看起来对但其实不对"的代码。单源让这个问题消失。

---

## P-5 语义查询层

> **Agent 问的是"意图"，不是"结构"。**

基础查询（P-3 的 query layer）能按路径/名字取东西：

```
query.scene.getNode("world/player/weapon")
```

但真实的 agent 任务是：

- "找场景里所有会动的、不是 NPC 的、在玩家视野内的物体"
- "找这个角色身上所有带 `damage` 组件且 `owner == player` 的"
- "找所有材质引用了给定纹理的 mesh"

必须有一个 **语义查询**层：

```
query.select({
  type:    "node",
  where: [
    { has_component: "RigidBody" },
    { not: { has_tag: "npc" } },
    { in_frustum: { camera: "main" } }
  ],
  limit: 50,
  order_by: "distance_to:player"
})
```

设计要点：
- 支持按类型 / 组件 / 标签 / 空间 / 关系 / 属性值过滤
- 返回结果可以是 id 列表 / 结构化摘要 / 完整 dump（由调用方指定分辨率，见 P-7）
- 查询本身可被序列化为一个字符串/对象，作为命令参数或事件负载
- 查询语言不必复杂，能表达 80% 常见场景即可

**契合**：Phase 2 的 `dumpScene` 和命令层统一到这个查询语言下。Phase 10 的 agent 用它代替手写遍历。

---

## P-6 意图图（Intent Graph）

> **Agent 的工作不是一串 tool 调用，而是一棵树：目标 → 计划 → 步骤 → 原子操作。**

每一次 agent 接到用户请求，都会在引擎内创建一条 **Intent**：

```
Intent "给场景放一只木桶"
├── Plan 1: 生成贴图 → 生成网格 → 组装材质 → 实例化节点
│   ├── Step 1.1  generateTexture(prompt=...)         [成本: ~2c, ~6s]
│   │            └── Action: asset.generate_texture(...)
│   ├── Step 1.2  generateMesh(prompt=...)            [成本: ~15c, ~40s]
│   ├── Step 1.3  buildMaterial(...)                  [成本: ~0, ~0.1s]
│   └── Step 1.4  scene.instantiate(...)              [成本: ~0, <0.01s]
└── Plan 2: 从资产库找现成 wooden_barrel → instantiate  [成本: ~0, <0.01s]
```

**为什么是图而不是列表**：
- 允许备选 plan（成本 / 失败时的 fallback）
- 允许回滚到任意层级（不必撤销全部）
- 允许"部分成功"的报告："Plan 1 的贴图和材质已完成，网格生成失败"

**每个节点的元数据**：
- 成本估算（见 P-9）
- 前置条件（见 P-8 dry-run 预验证）
- 产生的事件清单
- 人工确认要求（见 P-13）

**实现轻量级**：不追求 AI planner 框架的复杂度。一个 `IntentNode` 带 `children` + 元数据就够。

---

## P-7 多分辨率观察 / 渐进披露

> **永远不要一次性把全部状态甩给 agent。**

agent 的上下文窗口很贵。引擎默认的观察都是"最小信息 + 可下钻"的 pagination 模型。

**观察的三层分辨率**：

- **Summary**（1–3 行）
  - "Scene: 127 nodes, 3 cameras, 12 lights, 4 active animations, last change 2s ago"
- **Outline**（结构，不含数值细节）
  - 节点层级 + 类型标签，不含 transform 数值 / UBO 内容
- **Full**（完整 dump）
  - 所有字段、所有矩阵、所有 UBO 字节

**规则**：
- Agent 默认只能看到 Summary
- 明确指定 path 时可以 Outline 或 Full 这一个节点
- 没有"dumpAll" 这种按钮。大规模 full dump 必须带 filter / pagination

**配套机制**：
- 每个 dump 返回一个 `continuation_token`，agent 可以 "show me the next batch"
- Summary 里自带 "drill-down hint"：告诉 agent 下一步可以问什么

**强制约束**：Phase 10 的 MCP tool 设计时必须验证"agent 按最小信息原则能否完成典型任务"。如果不行，就切 dump 粒度。

---

## P-8 Dry-run / 影子状态

> **命令在提交前可以被预演，返回预测的 diff。**

Agent 的正确工作流是：

```
1. 查询当前状态
2. Dry-run 目标命令 → 拿到预测 diff
3. 检查预测 diff 是否符合意图
4. 真正提交
```

而不是：

```
1. 瞎猜参数
2. 提交
3. 查询新状态
4. 检查是不是错了
5. 撤销
6. 重试
```

**接口形态（抽象）**：

```
command.run(params)        → CommandResult     // 真正执行
command.preview(params)    → PredictedDiff     // 不提交，返回将发生的事件
```

**实现策略**：在一份"影子状态"上跑命令，记录所有变更，然后丢弃影子。通过 copy-on-write 或事件收集两种方式都可以。

**为什么关键**：让 agent 可以安全地探索，尤其是昂贵或破坏性操作。Dry-run + 成本模型（P-9）+ 人工确认（P-13）三个一起形成 agent 的"谨慎之手"。

---

## P-9 成本模型是一等公民

> **每个命令 / 查询 / 生成操作都携带三维度的成本估算。**

三个维度：

| 维度 | 示例 | 预算来源 |
|------|------|---------|
| **Tokens** | 完整 dump 一个 500 节点场景要多少字符 | agent 的上下文预算 |
| **Time** | 生成一个贴图要 6 秒 | 实时性要求 |
| **Money / Compute** | 调 text-to-3D 要 0.15 USD | 用户预算 |

**每个命令声明自己的估算函数**：

```
command.foo.estimateCost(params) → {tokens, timeMs, usd}
```

**引擎维护每个 session 的预算**：

```
session.budget = {
  tokens_total: 100_000,  tokens_used: 34_200,
  usd_total:    1.00,     usd_used:    0.42,
  time_ms_total: 60_000,  time_ms_used: 12_300
}
```

**Agent 规划**：拿到 intent graph（P-6）后，它需要在预算下选择最优 plan。超出预算时要么选便宜的替代、要么问人。

**引擎侧强制**：真正 `run()` 前检查预算，不足时直接拒绝；超出"警告阈值"要求人工确认（P-13）。

---

## P-10 资产血统 / Provenance

> **每份资产都记得自己怎么来的。**

特别是 AI 生成的资产：

```
asset.meta.provenance = {
  kind: "generated",
  generator: "text_to_mesh",
  input: { prompt: "...", refs: [...] },
  model: { provider: "<abstract>", version: "..." },
  created_at: "...",
  cost: { usd: 0.15, time_ms: 42000 },
  reproducible: true,   // 给定相同输入能否复现
  edits: [              // 后续人工编辑链
    { at: "...", author: "user", description: "uv 重新拓扑" }
  ]
}
```

**用处**：
- 可溯源：这个丑贴图是哪个 prompt 生的，改 prompt 重新跑
- 可复现：重装机器后能重生一份 byte-equivalent 的资产（或至少语义等价）
- 可审计：发布游戏时知道哪些资产来自哪个模型
- 可清理：`assets.remove_by_provenance({cost_gte: 0.10, unused: true})`

**通用原则**：不只是 AI 资产，手工导入的资产也记录导入源（文件路径 / mtime / 导入设置）。资产没有"凭空存在"的。

---

## P-11 时间旅行查询

> **查询可以指定时间点："状态在 event N 时是什么样？"**

引擎维护足够的事件历史 + 快照，支持：

```
query.at(event_id=42).scene.getNode("player")
query.at(time="5s_ago").assets.list()
query.diff(from=event_id=10, to=event_id=30)
```

**三种实现策略任选（或混合）**：
- **全量快照 + 增量事件**：每 N 个事件存一份快照，查询时从最近快照 forward replay
- **反向命令栈**：每个命令都有反向版本，查询历史时反向执行
- **Copy-on-write 历史**：持久化数据结构天然支持版本查询

**典型用途**：
- 调试："这个 bug 是哪条命令引入的？" → 二分历史事件直到复现
- Agent 回滚：intent graph（P-6）的某个分支失败，回滚到 plan 开始前
- Diff 可视化：编辑器显示"上次 save 以来改了什么"

---

## P-12 错误即教学

> **错误不是"出了问题"，是"给 agent/人类的一次学习机会"。**

所有错误对象必须具备：

```
EngineError = {
  code:        "E_MISSING_ASSET",
  path:        "scene/player/arm/weapon.material",   // 上下文路径
  message:     "Material reference is null",         // 人类可读
  reason:      "Asset guid xxx was unloaded at ...",  // 诊断信息
  fix_hint:    "Call assets.load('xxx') or reassign the material via command.node.setMaterial",
  related:     ["command.node.setMaterial", "query.assets.find"],
  severity:    "error",
  recoverable: true,
  agent_tip:   "This is usually caused by loading a scene file whose referenced assets were moved. Try `assets.scan()` first."
}
```

**规则**：
- 永远不裸抛 `"something is null"` 这种错误
- 错误必须可 JSON 序列化 → 可以作为 agent 下一轮 prompt 的上下文
- 错误包含 `related` 字段指向"你下一步该调什么 API"
- 错误有 `agent_tip` 字段，专门写给 LLM 看的简短建议

**为什么这么讲究**：agent 80% 的自我改进来自错误反馈。错误写得好，agent 一两轮就能恢复；错误写得糊，agent 会无限打转。

---

## P-13 Human-in-the-loop 是类型级契约

> **哪些操作需要人工确认，不应该靠约定，应该被类型系统标注。**

每个命令声明自己的确认级别：

| 级别 | 含义 | 触发条件 |
|------|------|---------|
| **auto** | 无需确认 | 读、查询、纯计算 |
| **notify** | 执行后通知 | 小修改、可逆 |
| **confirm** | 执行前要人工点"是" | 大量修改、昂贵、外部写入 |
| **review** | 执行前要人类审阅 diff | 破坏性、资产删除、git 操作 |

**引擎强制**：在交互模式下，`confirm` / `review` 级别的命令被 agent 调用时会停下来等人工。headless 模式下，未显式授权的命令被拒绝。

**给 agent 的好处**：
- 清楚什么时候该 ask、什么时候该 act
- 不必写"请问我能..."的 meta 对话
- 类型系统级别保证了"agent 不可能绕过确认" —— 不是靠它"记得"

**配套**：支持"按次授权" / "本 session 不再问" / "白名单列表" 三种确认策略。

---

## P-14 能力发现优于能力配置

> **Agent 运行时问引擎"你能做什么？"，而不是从配置里读一个固定列表。**

反面：每次引擎加了一个新命令，要去改 `allowed_tools.json`，要去改 agent 的 system prompt，要去改 MCP 配置。

正面：引擎暴露一个 `capability.list()` query，返回当前可用的所有 Query / Command / Generator 的清单 + schema + 描述 + 使用示例。Agent 在 session 开始时调一次，之后靠这个清单规划。

**每个能力自描述的字段**：

```
{
  id: "scene.translateNode",
  category: "scene",
  description: "Move a node by a delta vector in world space",
  when_to_use: "When the user asks to move/nudge/displace an object",
  examples: [
    {prompt: "move player to (0,0,0)",
     call: {path:"player", delta:[...]}}
  ],
  schema_in:  {...},
  schema_out: {...},
  confirmation: "notify",
  cost_estimator: "<function ref>"
}
```

**收益**：
- 新增能力 = 新增一条，零配置改动
- 减能力 = 删一条
- 版本升级时客户端感知新能力
- Agent 能主动询问 "有没有哪个 tool 可以 ...?" 并得到语义搜索结果

---

## P-15 重构友好 / 版本化的一切

> **引擎会大改。命令 / 事件 / 资产 / 场景格式都必须支持透明迁移。**

规则：
- 每个事件带 `schema_version`
- 每个资产元数据带 `schema_version`
- 每个场景文件带 `engine_version`
- 版本升级时提供一条迁移函数：`migrate_v3_to_v4(event) → event`
- 加载老版本数据时自动迁移；保存时用最新版本
- 老版本迁移失败 → 错误对象带 `fix_hint: "run capability migrations.repair"`

**为什么这对 AI-Native 更重要**：
- Agent 生成的资产可能在几个月后仍然要能加载
- 事件 replay（P-2 / P-11）在跨版本时需要迁移链
- `capability.list()` 的输出会变，agent 要学会处理旧学的能力消失

**自动化工具**：引擎自带 `migrations` capability，agent 可以调用它执行升级而不需要人工介入。

---

## P-16 文本优先 ≠ 文本唯一

> **Agent 的主要感官是文本，但它有一整套辅助感官。**

| 模态 | 典型用途 | 调用方式 |
|------|---------|---------|
| 结构化文本 | 场景理解、组件状态查询 | `query.*` / `dump*`（P-7 多分辨率） |
| 图像（截图） | 验证视觉效果、UI 布局 | `rendering.screenshot(camera, resolution)` |
| Profiler trace | 性能诊断 | `dev.profile(duration)` |
| 日志流 | 运行时事件追踪 | `dev.logs(since, filter)` |
| 音频 | 调试音效 / 节奏 | `audio.dump_active_voices()` |
| Diff | 理解一次变更 | `query.diff(events_range)` |
| 事件重放 | 理解 bug 如何发生 | `dev.replay(events, speed)` |

**原则**：每种模态都是文本/结构化的补充。`screenshot` 返回的 PNG 同时带一份 `description`（语义标签 + 像素直方图），让 agent 不看图也能用。

**不允许**：某个能力只能通过"看图"才能验证。必须永远有一条文本 fallback 路径。

---

## P-17 Eval harness 是内建设施

> **没有 eval 的 AI 系统不可维护。**

引擎从 Phase 10 起自带一个 `eval` capability。它的职责：

- 维护一个**挑战集（benchmark suite）**：一组 "给定初始场景 + 指令 + 预期结果" 的测试用例
- 运行 agent 尝试解决每个挑战，记录：
  - 是否达成目标
  - 使用的 token / 时间 / 钱
  - tool 调用步数
  - 遇到的错误数
- 产出**分数卡**：按 agent / 模型 / 引擎版本横切
- CI 跑一遍完整挑战集，任何质量回归都阻断发布

**挑战集的范围**：
- 简单：单命令可解（"把 player 移到原点"）
- 中等：多命令组合（"给所有 enemy 降 10 血"）
- 复杂：多阶段 intent graph（"做一个能赢的小游戏关卡，包括障碍物和胜利条件"）
- 越狱：故意引导 agent 做危险操作，看它是否拒绝

**这个 benchmark 反向驱动引擎的进化**：发现某类任务老失败？说明对应 capability 缺失或 API 不够语义化。eval 的失败就是下一个 issue。

---

## P-18 沙箱友好的进程模型

> **引擎能在一次性的沙箱里跑，agent 的实验不会污染 host 状态。**

特点：
- 引擎可以用"只读 + 可写 overlay"的文件系统启动
- 所有可变状态都在 `session_root/` 下，删除目录 = 重置
- 没有依赖 "全局状态 / 环境变量 / 用户目录写入" 的隐藏副作用
- 启动时间足够短（秒级），允许 agent 每次实验都 fresh start

**配套**：每个 agent session 可以选 "persistent"（继承上次状态）或 "ephemeral"（每次全新）。前者便于长任务，后者便于实验。

**为什么**：
- Agent 的错误探索应该可以被轻易回滚
- 同一台机器能起多个 agent session 平行跑（Phase 17 eval 需要）
- 远程 / 云上部署友好

---

## P-19 Bi-directional 命令总线

> **编辑器 / CLI / Agent / 游戏脚本都共享同一个命令总线。**

这是之前几个原则（P-3 / P-4 / P-14）的综合后果：

```
              ┌──────────────┐
              │  命令总线     │
              │  + 事件流     │
              └──┬──┬──┬──┬──┘
                 │  │  │  │
          ┌──────┘  │  │  └─────┐
          ▼         ▼  ▼         ▼
        人类      Agent  游戏脚本  外部工具
      （编辑器）  (MCP)   (TS)    (CLI)
```

**意味着**：
- 任何客户端的一次操作会被所有其他客户端"看到"（通过事件订阅）
- "Agent 改一下" 和 "人类在编辑器里点一下" 走同一条路径
- 新增一条命令，所有客户端自动可用，不需要在每个客户端里重写

**不允许**的反面设计：
- 编辑器有独占的"只有 GUI 能做"的操作
- Agent 有独占的"只有 MCP 能做"的 tool
- 游戏脚本有独占的运行时 API

任何这样的独占都是架构 bug。

---

## P-20 渲染与模拟的可分离

> **"画出来"和"模拟出来"必须可以分开。**

具体要求：
- 有 `headless` 模式：不创建窗口，不画画，但所有模拟正常跑
- 有 `render to texture` 模式：画到内存，截图 / 生成视频
- 有 `simulation-only` 模式：只跑物理 / 动画 / gameplay，不走 GPU
- 这三种模式下 `dump` / `query` 的语义完全一致

**为什么**：
- Agent eval（P-17）通常在 headless 跑（快、便宜、可并发）
- Agent 截图验证时走 render to texture
- 复杂 bug 复现时可能只需要 simulation，不需要 GPU
- 服务器端 AI 训练 / 评估不依赖 GPU

**契合**：这一条贯穿 Phase 1 的 Web 后端决策（backend 要可以不带 window 初始化）、Phase 9 的编辑器（本质上也是"远程 + 可选 GPU"）、Phase 17 eval。

---

## 原则之间的依赖

```
P-1 确定性 ──────────┐
                    ├─→ P-2 事件流 ─→ P-11 时间旅行 ─→ P-17 eval
P-3 三层 API ────────┘                              ↑
       │                                            │
       └─→ P-4 Capability Manifest ─→ P-14 能力发现  │
             ↓                            ↑          │
             └─→ P-5 语义查询 ─────────────┘          │
                                                     │
P-6 Intent Graph ─→ P-8 Dry-run ─→ P-13 HITL ────────┤
                         ↓                            │
                         └─→ P-9 成本模型             │
                                                     │
P-10 Provenance ──────────────────────────────────→  │
                                                     │
P-7 多分辨率 ─────────────────────────────────────→  │
                                                     │
P-12 错误即教学 ──────────────────────────────────→ │
                                                     │
P-15 版本化 ──────────────────────────────────────→ │
                                                     │
P-16 多模态 ─→ P-20 渲染/模拟可分 ──────────────→ ─┤
                                                     │
P-18 沙箱进程 ────────────────────────────────────→  │
                                                     │
P-19 命令总线 ←── 由 P-3 + P-4 产生自然结果 ─────→   │
                                                     ▼
                                      整套 roadmap 的架构验收
```

**读法**：上层原则是下层的前置条件。删掉上层意味着下层垮掉。

---

## 这份文档和 phase 的关系

- 每个 phase 的工作分解里，具体任务应当**对照原则 P-X** 做一次契合检查
- 每个 phase 的退出准则应当包含至少一条"本阶段如何落实了 P-X"
- 架构评审（人类或 agent 发起）应当**回到本文件**，而不是回到某个 phase 的细节

---

## 和"不做具体选型"的关系

本文件只描述**架构约束**，不描述"用哪个库 / 哪个算法 / 哪个协议"。所有 phase 文件遵守同一个原则：**优先描述能力和接口，把具体实现放在"选型参考"的小框里**，当作可替换的参考，不当成架构。

这样做的代价：读者需要一次抽象提升才能落地。
收益：重构时 roadmap 不失效。"把物理引擎换掉"不会让本文件任何一句话过时。

← [README](README.md)
