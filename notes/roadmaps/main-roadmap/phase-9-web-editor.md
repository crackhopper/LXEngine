# Phase 9 · Web 编辑器

> **目标**：把编辑器从 ImGui 搬到**浏览器**里 —— 用 Phase 8 的 Vue UI 容器写，和引擎通过 WebSocket 通信。人类开发者和 AI agent 共享同一个可视化面。
>
> **依赖**：Phase 1（Web 后端 / WebGPU demo 能在浏览器里跑）、Phase 2（文本内省 / 命令 API）、Phase 3（资产管线）、Phase 6（组件反射）、Phase 8（Vue UI 容器）。
>
> **可交付**：`editor/index.html` —— 一个浏览器打开的编辑器。左边场景树，中间 3D viewport（浏览器 WebGPU），右边 inspector，底部资产浏览器 + 控制台。

## 为什么走 Web 而不是 ImGui

原 roadmap 里编辑器是 ImGui-based（和 runtime 进程绑定）。AI-Native 版本改成 Web，原因：

1. **AI 能读懂** —— 编辑器 UI 是 `.vue` + `.ts` 文件，LLM 可以直接修改扩展
2. **AI 能看到** —— agent 可以用 headless browser 截图编辑器界面，用来做端到端测试
3. **AI 能在 sandbox 里跑** —— LLM 生成的编辑器插件无需重新编译 C++ 就能热加载
4. **进程解耦** —— 编辑器前端和引擎后端用 WebSocket 通信，可以同时连多个客户端（一个人类浏览器 + 一个 agent WebSocket 客户端）
5. **统一技术栈** —— 和 Phase 8 的游戏内 UI 共用 Vue / TypeScript / WebGPU，减少维护面

代价：
- 前端从零写（没有 ImGui 开箱即用的好处）
- WebSocket 协议设计
- 浏览器和引擎进程的生命周期协调

## 范围与边界

**做**：
- 编辑器前端（SPA，Vue 3 风格）
- 引擎侧 WebSocket 服务器（+ MCP 协议复用 —— 见 Phase 10）
- 场景层级面板 + inspector + viewport + 资产浏览器 + 控制台
- Gizmo（translate / rotate / scale）
- Play / Pause / Step 按钮
- Undo / Redo（10 步）
- 热重载 hook
- 与 engine-cli 的联动（启动 CLI 自动起编辑器服务器）
- Profiler / Tracy / GPU timestamp 集成（从原 Phase 8 继承）

**不做**：
- 节点图 / 蓝图 —— 超出小型引擎
- 独立的 Electron 打包（直接开浏览器就够）
- 多人协同编辑 —— 基础设施预留，功能不做

---

## 前置条件

- Phase 1：WebGPU 后端能在浏览器里跑一个场景
- Phase 2：`dumpScene` / `dumpResource` / `invokeCommand` 都能跑
- Phase 8：Vue UI 容器 + SFC 编译链成熟（编辑器前端就用它写）

---

## 工作分解

### REQ-901 · 双向 RPC 通道

引擎侧启一个长连接 RPC 服务：

```
engine-cli serve --port <port> --with-editor
```

- 协议：JSON over WebSocket（或等价的双向帧协议）
- 消息三类：`request` / `response` / `event`（订阅推送）
- **协议上层 = Phase 2 REQ-213 的命令层**：消息 `method` 是命令名，`params` 是命令参数。这与 Phase 10 MCP 的 tool 调用是同一套

**选型参考**：成熟的 C++ WebSocket 服务端库中任选；不自己造。

**验收**：浏览器建立连接后发送一条命令能收到结果；订阅的事件能被推送到所有连接的客户端。

### REQ-902 · 协议 = Phase 2 命令 + Phase 10 MCP

关键设计决策：**不为编辑器单独设计 RPC 协议**。编辑器用的每一条消息都是 Phase 2 REQ-213 的命令之一，或者未来 Phase 10 的 MCP tool。这样保证：

- 编辑器能做的事 = agent 能做的事（权限对称）
- 加一条编辑器功能 = 加一条命令（自动同时暴露给 agent）
- MCP 协议和编辑器协议合并，不重复维护

消息示例：

```json
// client → engine
{"id": 1, "method": "scene.dumpTree", "params": {"depth": -1}}

// engine → client
{"id": 1, "result": "Scene\n├── ...\n"}

// engine → client (push)
{"event": "scene.changed", "params": {"nodePath": "player"}}
```

**验收**：所有 Phase 2 的命令都能通过 WebSocket 触发。

### REQ-903 · 编辑器前端骨架

新建 `editor/` 目录，用标准前端工程组织：

- 一份 SPA：dockspace 布局 + 各 panel 组件
- 前端工具链用**主流 web 构建器 + 开发服务器**（任选）
- 前端用**完整** Vue（不是 Phase 8 的子集 —— 编辑器跑在真实浏览器里）
- 编辑器构建独立于游戏 UI 编译链
- Panel 列表：HierarchyPanel / InspectorPanel / ViewportPanel / AssetBrowserPanel / ConsolePanel
- 一个 `engine-client` 模块封装双向 RPC + 类型安全调用

**验收**：开发服务器起来后浏览器能看到空的 dockspace。

### REQ-904 · Hierarchy Panel

- 通过 WebSocket 调 `scene.dumpTree` 获得场景树
- 用一棵可展开的树组件渲染
- 点击节点 → 本地状态设为 selected + emit event
- 右键菜单：添加子节点 / 删除 / 复制 / 重命名
- 拖拽改父子关系

前端订阅 `scene.changed` 事件，收到时增量刷新。

**验收**：场景树显示与 CLI dump 一致，点击能响应。

### REQ-905 · Inspector Panel

- 根据 selected node，查询其所有组件
- 每个组件用 `component.describe` + 反射 schema 生成字段 UI
- 字段类型 → Vue 组件映射：
  - `float` / `int` → `<input type="number">` 或 slider
  - `Vec3f` → 三个数字 input
  - `Quatf` → 欧拉角显示（转换在前端做）
  - `StringID` → `<input type="text">`
  - `AssetGuid` → drag-drop asset picker
  - `enum` → `<select>`
  - `boolean` → checkbox
- 修改字段 → 调 `component.setField` 命令

**验收**：改字段值后引擎侧立即生效。

### REQ-906 · Viewport Panel

关键设计：**viewport 是一个真正的 `<canvas>`，由引擎的 WASM 构建直接驱动浏览器原生 GPU API**，不是 readback + 图片 stream。

- 编辑器 SPA 嵌入引擎的 WASM 产物（Phase 1 REQ-115）
- WASM 获取 canvas 句柄后直接渲染到 canvas
- 两种部署形态：
  - **同进程**（推荐）：引擎 WASM 和编辑器前端是同一页，RPC 通道只用于 agent / CLI 连接
  - **远程代理**：引擎是独立进程，viewport 通过 frame streaming 展示（延迟较大，仅在特殊部署下使用）

**验收**：viewport 里看到的场景与桌面运行时一致。

### REQ-907 · Gizmo

- 提供 translate / rotate / scale 三态变换小部件
- 在 viewport 上叠加绘制
- 拖动时通过命令层把变换同步回引擎（走 REQ-213 命令）

**选型参考**：web 端有成熟的 gizmo 实现可以参考或裁剪；也可以按需自写（几何固定、代码量不大）。

**验收**：拖动 gizmo 能改 selected 节点的 Transform，与 inspector 中字段同步。

### REQ-908 · Asset Browser

- 通过 `assets.list` / `assets.list_by_type` 命令拉取
- 缩略图：贴图 → 直接 fetch；网格 → 引擎预渲染一张 PNG；材质 → 小球预览
- 双击资产 → 在 inspector 显示其属性
- 拖拽到 viewport → 发 `scene.instantiate` 命令

**验收**：`assets/` 下的所有资产都能被看到并拖进场景。

### REQ-909 · Console + Agent Chat Panel

- 订阅引擎的 log 事件（info / warn / error）
- 前端显示可搜索 / 可过滤的日志列表
- **同时显示 Phase 10 的 agent 对话历史**：这是编辑器最重要的 AI-Native 特性，控制台里人类可以和引擎内置的 agent 直接对话

```
> 把玩家向左移两米
  Agent: 执行 scene.translateNode({path:"player", delta:[-2,0,0]})
  ✓ Done. Player is now at (−2, 0, 0).

> 把玩家旁边加一个红色木箱
  Agent: 正在生成木箱贴图...
  Agent: 执行 assets.generate_texture({prompt:"wooden crate"})
  ...
```

**验收**：控制台里的对话能直接驱动引擎操作。

### REQ-910 · Play Mode

- 工具栏：Play / Pause / Step 按钮
- Play：引擎进入 runtime 模式（组件 update 运行 + 物理 step）
- Stop：快照恢复到进入 play 前的状态（深拷贝 scene）
- Step：单步一帧

**验收**：在 play 模式下游戏跑，退出后场景回到 enter 前。

### REQ-911 · Undo / Redo

每条命令封装成 `{ do, undo, description }`。实现：

- 客户端维护命令栈
- 引擎侧也维护一个 snapshot 栈作为安全网
- 单个编辑器实例的 undo 栈 10 步

**验收**：Ctrl+Z 能撤销最近 10 步操作。

### REQ-912 · Profiler 集成

- 接入一个**通用 CPU profiler**（覆盖 zone / thread / frame 概念，有独立查看器）
- GPU timestamp queries 封装成后端无关的接口，两套数据合到同一时间线
- Frame stats 通过 RPC 通道推送到前端
- 前端 `StatsPanel` 实时显示：FPS / CPU ms / GPU ms / draw call 数 / 顶点数 / 分配次数

**选型参考**：C++ 侧有成熟的 zone-based profiler 提供独立客户端 + 低开销 instrumentation，直接接入即可。

**验收**：`StatsPanel` 与独立 profiler 客户端的数据一致。

### REQ-913 · Debug Draw Overlay

- 物理 shape / raycast / AABB / 光源范围等 debug 图元走一个独立的 `debugDraw` 通道
- 前端 viewport 订阅 `debug.*` 事件，在自己的 canvas 上叠加显示
- Toggle 按钮控制各类显示 on/off

**验收**：toggle 物理 shape 显示能切换。

---

## 里程碑

### M9.1 · WebSocket + 空 SPA

- REQ-901 + REQ-902 + REQ-903 完成
- demo：浏览器空壳编辑器能连引擎

### M9.2 · Hierarchy + Inspector 可用

- REQ-904 + REQ-905 完成
- demo：能看到场景树、能改组件字段

### M9.3 · Viewport + Gizmo

- REQ-906 + REQ-907 完成
- demo：真正可视化编辑

### M9.4 · 资产 + 控制台 + Play mode + Undo

- REQ-908 + REQ-909 + REQ-910 + REQ-911 + REQ-913 完成
- demo：完整 `editor/index.html`，浏览器里能做一个完整小场景

### M9.5 · Profiler 接通

- REQ-912 完成
- demo：前端实时性能面板 + Tracy 客户端都能看到完整帧

---

## 风险 / 未知

- **协议版本化**：随命令集扩展协议会变。解决：协议头带版本号 + 客户端握手时协商，复用 [P-15](principles.md#p-15-重构友好--版本化的一切) 的迁移基础设施。
- **WASM 大小**：引擎编译到 WASM 可能几 MB 到几十 MB。解决：按需加载 + 压缩流。
- **编辑器和 runtime 的状态一致性**：多个命令改同一节点导致 race。解决：引擎侧命令串行化执行，前端乐观 UI + 冲突时从事件流重拉。
- **Hot reload 的状态丢失**：前端 HMR 刷页面会丢连接状态。解决：把会话关键状态持久化到 localStorage，刷新后通过事件流回放恢复。

---

## 与现有架构的契合

- 编辑器的每个操作 = Phase 2 REQ-213 的命令 = Phase 10 的 MCP tool，三处**同一套 API**。
- 编辑器的前端 UI 是用 Phase 8 的 Vue 相同技术栈写的（虽然是完整 Vue 3 而不是子集）。
- 引擎 WASM 构建由 Phase 1 REQ-115 提供。
- Viewport 渲染由 Phase 1 的 Web-capable 后端提供。
- 场景树 + inspector 的数据源是 Phase 2 REQ-209/210 的多分辨率 dump API。
- 控制台的对话功能依赖 Phase 10 的内置 agent。

---

## 与 AI-Native 原则的契合

| 原则 | 本阶段如何落实 |
|------|--------------|
| [P-4 Capability Manifest](principles.md#p-4-单源能力清单capability-manifest) | inspector 字段 UI / asset picker / 命令菜单都由单源 schema 生成 |
| [P-11 时间旅行](principles.md#p-11-时间旅行查询) | Undo / Redo / scrub timeline 走事件流 |
| [P-16 多模态](principles.md#p-16-文本优先--文本唯一) | 控制台 / viewport / 截图 / trace 共存，人类和 agent 共享 |
| [P-19 命令总线](principles.md#p-19-bi-directional-命令总线) | 编辑器是总线的一个客户端，和 agent / CLI 平权 |

---

## 下一步

有了编辑器，引擎的交互面就齐了。下一步是把 **AI agent 明确变成引擎的一等公民** → [Phase 10 · MCP + Agent + CLI](phase-10-ai-agent-mcp.md)。
