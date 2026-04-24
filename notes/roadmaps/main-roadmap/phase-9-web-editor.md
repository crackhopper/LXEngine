# Phase 9 · Web 编辑器

> **目标**：编辑器从 ImGui 搬到**浏览器** — 用 Phase 8 Vue UI 容器写，通过 WebSocket 与引擎通信。人类开发者与 AI agent 共享同一可视化面。
>
> **依赖**：Phase 1（WebGPU 后端 + Web 构建）+ Phase 2（内省 API）+ Phase 8（Vue UI 容器）。
>
> **可交付**：
> - `editor.html` — 浏览器打开即可编辑当前运行中的场景
> - 场景树 / Inspector / 资产面板 / 控制台

## 当前实施状态（2026-04-24）

**未开工**。除 ImGui overlay（开发者 debug）外没有任何编辑器基础设施。

| 条目 | 状态 |
|------|------|
| 编辑器壳（Vue app） | ❌ REQ-901 |
| WebSocket IPC | ❌ REQ-902 |
| 场景树面板 | ❌ REQ-903 |
| Inspector | ❌ REQ-904 |
| 资产面板 | ❌ REQ-905 |
| 控制台 / REPL | ❌ REQ-906 |
| 历史 / Undo | ❌ REQ-907 |

## 范围与边界

**做**：基于 Phase 8 Vue 容器的编辑器壳 / WebSocket 双向 IPC（命令下发 + 事件订阅） / 场景树 + Inspector + 资产浏览器 / TS 脚本编辑与热加载 / 命令历史 + Undo。

**不做**：内嵌 VSCode / Monaco 级 IDE（可外链） / Scene graph 可视化节点编辑 / 性能剖析 UI（Phase 12 做）。

## 前置条件

- Phase 2 `dumpScene` / `describe` 是 Inspector 数据源
- Phase 8 Vue 容器是 UI 技术栈
- 命令 / 事件总线（Phase 6 REQ-604 或引擎核心事件流）

## 工作分解

### REQ-901 · 编辑器壳

- Vue app，入口 `editor.html`
- 布局：top bar / left dock（scene tree）/ center（游戏画面）/ right dock（inspector）/ bottom（console）

**验收**：浏览器打开 `editor.html` 看到固定 layout（内容占位）。

### REQ-902 · WebSocket IPC

- 协议：JSON message `{ id, type, payload }`
- 双向：client → engine 发命令；engine → client 推事件
- 引擎侧复用 Phase 10 MCP server 的命令总线（P-19）

**验收**：编辑器连引擎，发 `query.scene.summary` 收到响应。

### REQ-903 · 场景树面板

- 显示 `Scene` 节点层级 + 类型标签
- 拖放 reparent / 右键菜单（删除 / 复制 / 重命名）
- 选中节点 → 联动 Inspector

**验收**：场景树实时反映节点增删改。

### REQ-904 · Inspector

- Transform / 组件字段自动生成表单
- 字段 schema 来自 [P-4 Capability Manifest](principles.md#p-4-单源能力清单)
- 修改 → dispatch command，受 HITL 级别（P-13）影响

**验收**：修改 Transform 位置 → 场景画面即时更新。

### REQ-905 · 资产面板

- 浏览 `AssetRegistry`（Phase 3）
- 缩略图 / 搜索 / 过滤 / 拖入场景树

**验收**：资产面板能定位到任意已注册 asset。

### REQ-906 · 控制台 / REPL

- 文本框输入 TS 片段，在当前 scene 执行
- 结构化日志流输出
- 绑定 Phase 10 agent chat

**验收**：控制台输入 `scene.select("player").translate(1,0,0)` 立即看位移。

### REQ-907 · 历史 / Undo

- 命令总线记录每个 command 的 inverse
- Ctrl+Z / Ctrl+Y
- 历史带时间戳 + 作者（人 / agent / 脚本）

**验收**：Undo 30 步后场景完全恢复。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M9.1 · 壳 + IPC | REQ-901 + REQ-902 | 浏览器连引擎 |
| M9.2 · 场景 + Inspector | REQ-903 + REQ-904 | 改 Transform 即时生效 |
| M9.3 · 资产 + 控制台 | REQ-905 + REQ-906 | 拖资产 / REPL 改场景 |
| M9.4 · Undo | REQ-907 | Ctrl+Z 恢复 |

## 风险 / 未知

- **WebSocket 安全**：本地开发无鉴权；公网部署需要 token。
- **延迟**：IPC 往返 + reactivity 延迟需 < 50ms。
- **引擎 CPU 占用**：双向 IPC 每帧推场景 diff 成本高 —— 按事件触发推而非每帧。
- **字段 schema 一致性**：inspector 表单字段来自 manifest，manifest 变化 UI 自适配。

## 与 AI-Native 原则契合

- [P-3 三层 API](principles.md#p-3-三层-api查询--命令--原语) + [P-19 命令总线](principles.md#p-19-双向命令总线)：编辑器与 agent 走同一命令路径。
- [P-4 Capability Manifest](principles.md#p-4-单源能力清单)：inspector 表单自动生成。
- [P-7 多分辨率](principles.md#p-7-多分辨率观察)：场景树 outline → 节点 detail 的下钻。
- [P-13 HITL](principles.md#p-13-hitl-类型级契约)：编辑器是 confirm / review 级命令的 UI 面。

## 与现有架构契合

- 当前 ImGui overlay 与 Web 编辑器并存：ImGui 是本地 dev tool；Web 编辑器是主编辑器。
- `SceneNode` / `Scene` / `MaterialInstance` 的可观察字段由 Phase 2 `dumpScene` / `describe` 暴露。

## 下一步

[Phase 10 MCP + Agent](phase-10-ai-agent-mcp.md)：agent 可直接在编辑器控制台 chat 面发起任务。
