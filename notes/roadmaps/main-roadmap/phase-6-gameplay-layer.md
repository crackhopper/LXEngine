# Phase 6 · Gameplay (TypeScript)

> **目标**：给游戏逻辑正式栖息地 — TypeScript 脚本层 + 组件生命周期 + 事件总线。游戏程序员不改引擎源码就能写游戏；LLM 能生成 TS 脚本，引擎直接加载运行。
>
> **依赖**：Phase 2（`Transform` + 层级 + 内省）+ Phase 3（脚本资产加载）+ Phase 4（动画状态机 hook）+ Phase 5（物理事件）。
>
> **可交付**：
> - `demo_ts_pong` — Pong 小游戏，逻辑全部 TS
> - LLM 生成一段 TS → 运行时热加载 → 场景新增“旋转立方体 + 点击变色”组件

## 当前实施状态（2026-04-24）

**未开工**。没有脚本运行时，没有组件生命周期，没有事件总线。

| 条目 | 状态 |
|------|------|
| TS runtime 接入 | ❌ REQ-601 |
| Component 生命周期 | ❌ REQ-602 |
| 系统调度 | ❌ REQ-603 |
| 事件总线 | ❌ REQ-604 |
| 脚本 → 引擎 bindings | ❌ REQ-605 |
| 脚本热重载 | ❌ REQ-606 |

## 范围与边界

**做**：TypeScript runtime（QuickJS / V8 embed） / Component 生命周期（awake / start / update / destroy） / 系统调度 / 事件总线 / TS → 引擎 bindings（派生自 [P-4 Capability Manifest](principles.md#p-4-单源能力清单)） / 脚本热重载。

**不做**：除 TS 之外的脚本语言 / 可视化脚本（BluePrint / 节点图）/ 独立调试器 UI（Phase 9 编辑器内做）。

## 前置条件

- Phase 2 Transform / 层级 / 命令层（脚本写入走命令总线）
- Phase 3 脚本资产作为一等 asset（GUID / 热重载）
- Phase 4 动画状态机条件回调
- Phase 5 物理事件（collision / overlap）

## 工作分解

### REQ-601 · TS runtime 接入

选型参考（可替换）：

- **[V8](https://v8.dev/)**：性能最佳，体积大（~20MB），编译复杂
- **[QuickJS](https://bellard.org/quickjs/)**：小巧（~1MB），启动快，性能中等；浏览器嵌入友好
- **[Duktape](https://duktape.org/)**：ES5，适合极简嵌入

首版推荐 QuickJS：对 Web build 友好、体量可控。抽象 `IScriptRuntime`，实现放 `src/infra/script/`。

**验收**：加载一段 `hello.ts`（经 tsc 编译成 JS），输出 `Hello`。

### REQ-602 · Component 生命周期

```ts
class PlayerController extends Component {
  onAwake()    { /* ... */ }
  onStart()    { /* ... */ }
  onUpdate(dt) { /* ... */ }
  onDestroy()  { /* ... */ }
}
```

- 组件实例挂在 `SceneNode` 上
- 按固定顺序调用生命周期钩子

**验收**：组件的 `onUpdate(dt)` 每帧被调用；dt 与 `Clock` 一致。

### REQ-603 · 系统调度

- phase 组织每帧 pipeline：`input → script update → animation → physics (fixed) → render`
- 每 phase 允许多个 system 注册
- System 声明依赖 phase，调度器拓扑排序

**验收**：注册 3 个 system，按声明顺序执行。

### REQ-604 · 事件总线

```ts
scene.on("collision", (a, b) => { /* ... */ });
scene.emit("player_died", { cause: "fire" });
```

- Typed event
- 订阅 / 取消 / 一次性订阅（`once`）
- 契合 [P-2 事件流](principles.md#p-2-状态即事件流)：系统事件同步写入事件流

**验收**：物理 collision 事件能被 TS 脚本接收。

### REQ-605 · 脚本 → 引擎 bindings

- TS 类型 / 引擎 Query + Command 由 [P-4 Capability Manifest](principles.md#p-4-单源能力清单) 派生
- `capability.list()` 同时供 MCP 与 TS bindings
- 脚本不直接触达 `core/` 类型；只触达 bindings

**验收**：`scene.getNode("player").transform.localPosition.x += 1` 能生效且 TS 类型检查通过。

### REQ-606 · 脚本热重载

- `.ts` 改动 → 重编 → 替换组件实例
- 组件实例状态通过 `serialize()` / `deserialize()` 保存，热更不丢失

**验收**：scene 运行中改 `.ts` 保存，行为下一帧生效，不重启。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M6.1 · TS runtime | REQ-601 | `hello.ts` 执行 |
| M6.2 · 组件 + 系统 | REQ-602 + REQ-603 | 组件每帧 update |
| M6.3 · 事件 + bindings | REQ-604 + REQ-605 | TS 通过 binding 改场景 |
| M6.4 · 热重载 | REQ-606 | 脚本热更生效 |

## 风险 / 未知

- **TS → JS 编译路径**：首版用外部 `tsc --watch` + JS 热加载，避免嵌入编译器。
- **跨 runtime 对象生命周期**：JS GC vs C++ shared_ptr。bindings 用 handle 而非直接持有 C++ 对象。
- **性能**：QuickJS 比 V8 慢 5–10×。热路径（渲染 / 物理 / 动画）仍留在 C++。
- **调试**：source map + 嵌入 inspector 归 Phase 9 编辑器。

## 与 AI-Native 原则契合

- [P-3 三层 API](principles.md#p-3-三层-api查询--命令--原语)：脚本只触达 Query + Command。
- [P-4 Capability Manifest](principles.md#p-4-单源能力清单)：TS 类型 / MCP schema 同源。
- [P-14 能力发现](principles.md#p-14-能力发现优于能力配置)：脚本启动时 `capability.list()` 列可用 API。
- [P-19 命令总线](principles.md#p-19-双向命令总线)：TS / agent / 编辑器共享同一命令总线。

## 与现有架构契合

- `SceneNode` 作组件挂载点；`Transform`（Phase 2）是脚本最常触达的数据。
- `EngineLoop::setUpdateHook`（已落地）是脚本 update 阶段的自然接入点。

## 下一步

- [Phase 9 Web 编辑器](phase-9-web-editor.md)：脚本调试 UI + inspector。
- [Phase 10 MCP + Agent](phase-10-ai-agent-mcp.md)：TS bindings 与 MCP schema 共用 capability 源。
