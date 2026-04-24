# AI-Native 引擎核心原则（宪法）

> 跨阶段架构级不变量。任何 phase 文档与原则冲突时以本文件为准。

## P-1 确定性是架构级不变量

**同样的输入序列产出同样的状态。**

- 写入只走命令层（P-3），命令变更是纯函数
- 物理 / 随机数 / 浮点有显式 seed 与可复现模式
- 时间推进通过 `Clock` 抽象（已落地 `src/core/time/clock.*`）
- 异步操作通过事件进入状态流，不是隐式副作用
- `--deterministic` 开关关闭非确定性源

代价：热路径略慢。收益：replay / 时间旅行 / agent 自动化 / 可信单测 / 未来网络同步免费派生。

## P-2 状态即事件流

**当前状态是事件流的投影。**

```
initial ─event─▶ s_1 ─event─▶ s_2 ─event─▶ ... ─▶ current
```

- Undo / Redo / Time travel / Replay / Diff broadcast / 审计 / 学习均由事件流派生
- 写入必须走事件，散点 `obj.field = value` 消失
- 事件 schema 版本化（P-15）

## P-3 三层 API：查询 / 命令 / 原语

| 层次 | 只读 | 可逆 | 暴露给 agent | 调用者 |
|------|------|------|--------------|--------|
| **Query** | ✅ | n/a | ✅ | agent / 编辑器 / 工具 |
| **Command** | ❌ | ✅ | ✅ | agent / 编辑器 / 游戏脚本 |
| **Primitive** | ❌ | ❌ | ❌ | 引擎内部热路径 |

- Query：`query.xxx() -> T`，无副作用
- Command：`command.xxx(params) -> CommandResult`，原子修改 + 反向命令 + 成本估算；失败时状态不变
- Primitive：渲染主循环 / 物理 step / 合批 draw；**primitive 泄漏到 agent 即是 bug**

MCP tools 只暴露 Query + Command。

## P-4 单源能力清单

**一个能力只声明一次，派生所有外部面：**MCP schema / TypeScript 类型 / 编辑器 UI / CLI 帮助 / 文档页 / 测试 fixture。

- 中间形态是 `CapabilityManifest`（带类型的树）
- 由 CMake build step 驱动 generator
- LLM 对“多份不一致真相”极不友好；单源让这个问题消失

## P-5 语义查询层

**Agent 问意图，不问结构。**

```
query.select({
  type: "node",
  where: [
    { has_component: "RigidBody" },
    { not: { has_tag: "npc" } },
    { in_frustum: { camera: "main" } }
  ],
  limit: 50,
  order_by: "distance_to:player"
})
```

- 按类型 / 组件 / 标签 / 空间 / 关系 / 属性值过滤
- 查询可序列化，作命令参数 / 事件负载

## P-6 Intent Graph

**Agent 工作是一棵树：目标 → 计划 → 步骤 → 原子操作。**

```
Intent "给场景放一只木桶"
├── Plan 1: 生成贴图 → 生成网格 → 组装材质 → 实例化
│   ├── Step 1.1  generateTexture(...)    [~2c, ~6s]
│   ├── Step 1.2  generateMesh(...)        [~15c, ~40s]
│   ├── Step 1.3  buildMaterial(...)       [~0, ~0.1s]
│   └── Step 1.4  scene.instantiate(...)   [~0, <0.01s]
└── Plan 2: 从资产库找 wooden_barrel → instantiate
```

每个节点带：成本估算（P-9）/ 前置条件 / 产生事件清单 / 人工确认要求（P-13）。

## P-7 多分辨率观察

**默认最小信息，可下钻。**

- **Summary**（1–3 行）："Scene: 127 nodes, 3 cameras, 12 lights"
- **Outline**（结构无数值）：节点层级 + 类型标签
- **Full**（完整 dump）：所有字段

大规模 dump 必须带 filter / pagination；每个 dump 返回 `continuation_token`；Summary 自带 drill-down hint。

## P-8 Dry-run / 影子状态

**命令在提交前可预演。**

```
command.run(params)     → CommandResult
command.preview(params) → PredictedDiff
```

实现：copy-on-write 影子 / 事件收集。Agent 正确流程：查询 → dry-run → 检查 diff → 提交。

## P-9 成本模型是一等公民

每个命令 / 查询声明三维度估算：

| 维度 | 示例 | 预算来源 |
|------|------|---------|
| **Tokens** | 500 节点 dump 多少字符 | agent 上下文 |
| **Time** | 生成贴图 6 秒 | 实时性 |
| **Money** | text-to-3D $0.15 | 用户预算 |

`command.foo.estimateCost(params) → {tokens, timeMs, usd}`。超预算拒绝或要求人工确认。

## P-10 资产血统 / Provenance

**每份资产记得自己从哪来。**

```
asset.meta.provenance = {
  kind: "generated",
  generator: "text_to_mesh",
  input: { prompt: "...", refs: [...] },
  model: { provider: "...", version: "..." },
  created_at: "...",
  cost: { usd: 0.15, time_ms: 42000 },
  reproducible: true,
  edits: [{ at: "...", author: "user", description: "uv 重新拓扑" }]
}
```

手工导入资产也记录导入源（文件路径 / mtime / 导入设置）。

## P-11 时间旅行查询

```
query.at(event_id=42).scene.getNode("player")
query.at(time="5s_ago").assets.list()
query.diff(from=10, to=30)
```

实现：全量快照 + 增量事件 / 反向命令栈 / copy-on-write 历史。用途：二分历史定位 bug；intent graph 回滚；编辑器 diff 可视化。

## P-12 错误即教学

```
EngineError = {
  code:        "E_MISSING_ASSET",
  path:        "scene/player/arm/weapon.material",
  message:     "Material reference is null",
  reason:      "Asset guid xxx was unloaded at ...",
  fix_hint:    "Call assets.load('xxx') or reassign via command.node.setMaterial",
  related:     ["command.node.setMaterial", "query.assets.find"],
  severity:    "error",
  recoverable: true,
  agent_tip:   "Usually caused by loading a scene whose referenced assets were moved. Try `assets.scan()` first."
}
```

永不裸抛 `"something is null"`。错误可 JSON 序列化，可作 agent 下一轮 prompt 上下文。

## P-13 HITL 类型级契约

| 级别 | 含义 | 触发 |
|------|------|------|
| **auto** | 无需确认 | 读 / 查询 / 纯计算 |
| **notify** | 执行后通知 | 小改动、可逆 |
| **confirm** | 执行前点“是” | 大量修改、昂贵、外部写 |
| **review** | 执行前人类 review diff | 破坏性、资产删除、git |

交互模式：`confirm` / `review` 停等人。Headless 模式：未显式授权即拒绝。

## P-14 能力发现优于能力配置

**运行时问“你能做什么”。**

`capability.list()` 返回所有 Query / Command / Generator 的 schema / 描述 / 示例 / 成本估算器。Agent session 启动时调一次。新增能力 = 新增一条，零配置改动。

## P-15 重构友好 / 版本化

- 每个事件 / 资产元数据 / 场景文件带 `schema_version` / `engine_version`
- 版本升级提供 `migrate_vN_to_vN1(event) → event`
- 加载老版本自动迁移；保存用最新版本
- 迁移失败 → 错误对象带 `fix_hint: "run capability migrations.repair"`

## P-16 文本优先 ≠ 文本唯一

| 模态 | 用途 | 调用 |
|------|------|------|
| 结构化文本 | 场景 / 组件查询 | `query.*` / `dump*` |
| 截图 | 视觉验证 | `rendering.screenshot(...)` |
| Profiler trace | 性能诊断 | `dev.profile(...)` |
| 日志流 | 运行时事件 | `dev.logs(...)` |
| 音频 | 声效 / 节奏 | `audio.dump_active_voices()` |
| Diff | 一次变更 | `query.diff(events_range)` |
| 事件重放 | bug 再现 | `dev.replay(events, speed)` |

`screenshot` 返回 PNG 同时带 `description`。不允许“只能看图才能验证”的能力。

## P-17 Eval harness 内建

- 维护挑战集（初始场景 + 指令 + 预期结果）
- 运行 agent，记录目标达成 / token / 时间 / 钱 / tool 步数 / 错误数
- 产出按 agent / 模型 / 引擎版本横切的分数卡
- CI 跑完整挑战集，质量回归阻断发布

范围：简单（单命令）/ 中等（多命令）/ 复杂（多阶段 intent graph）/ 越狱。Eval 失败即下一个 issue。

## P-18 沙箱友好的进程模型

- 只读 + 可写 overlay 启动
- 可变状态都在 `session_root/` 下，删目录即重置
- 不依赖全局状态 / 环境变量 / 用户目录写入
- 秒级启动，`persistent` vs `ephemeral` session 二选一

## P-19 双向命令总线

```
         ┌──────────────┐
         │  命令总线     │
         │  + 事件流     │
         └──┬──┬──┬──┬──┘
            │  │  │  │
     ┌──────┘  │  │  └──────┐
     ▼         ▼  ▼         ▼
   人类      Agent  游戏脚本  外部工具
 （编辑器）  (MCP)   (TS)    (CLI)
```

任何客户端的操作被其他所有客户端看到（事件订阅）。编辑器独占操作 / agent 独占 tool / 脚本独占运行时 API 均是架构 bug。

## P-20 渲染与模拟可分离

- **Headless**：不创建窗口 / 不画 / 模拟正常跑
- **Render to texture**：画到内存，截图 / 视频
- **Simulation-only**：只跑物理 / 动画 / gameplay

三种模式下 `dump` / `query` 语义完全一致。Eval 通常 headless；agent 截图验证走 render to texture。

## 原则依赖关系

```
P-1 确定性 ──────────┐
                    ├─→ P-2 事件流 ─→ P-11 时间旅行 ─→ P-17 eval
P-3 三层 API ────────┘
       │
       └─→ P-4 能力清单 ─→ P-14 能力发现
              ↓              ↑
              └─→ P-5 语义查询─┘

P-6 Intent Graph ─→ P-8 Dry-run ─→ P-13 HITL
                         ↓
                         └─→ P-9 成本模型

P-7 多分辨率 / P-10 Provenance / P-12 错误即教学
P-15 版本化 / P-16 多模态 / P-18 沙箱进程
P-19 命令总线 / P-20 渲染/模拟可分
```

上层是下层前置条件。

## 与“不锁定选型”的关系

本文件只描述**架构约束**，不规定具体库 / 算法 / 协议。phase 文档同样优先描述能力与接口，把具体实现放在“选型参考”内当可替换参考。重构时 roadmap 不失效。
