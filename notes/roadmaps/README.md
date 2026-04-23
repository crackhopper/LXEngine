# Roadmap · 走向 AI-Native 小型游戏引擎

> 把 `renderer-demo` 从"教学型 Vulkan 渲染器"推进到一个 **AI-Native 小型游戏引擎**。本文档是整套 roadmap 的总入口与阅读地图。

## 什么是 "AI-Native" 游戏引擎

传统游戏引擎假设**人类开发者**是唯一的 first-class 用户：脚本语言小众、编辑器全 GUI、资源组织靠目录规范、场景结构靠可视化编辑。这些假设对当下的 AI Agent 并不友好 —— agent 读不懂专有编辑器、写不了 UnityScript、看不懂节点图。

本 roadmap 对 AI-Native 引擎的定义包含 5 条硬性条件：

1. **引擎自带 Agent + Skills**：引擎内部有一个可驻留的 AI Agent，持有大量与引擎交互的"技能"（场景操作、资产加载、构建、profile、发布）。CLI 可进入互动模式与引擎对话。
2. **默认 MCP 支持**：引擎对外暴露标准 [Model Context Protocol](https://modelcontextprotocol.io/) 接口，任何外部 agent（Claude Code / Codex / 自建 agent）都能作为客户端接入。
3. **文本优先的内省**：场景树、空间结构、组件状态、资产目录、渲染管线等所有内部状态都提供**结构化文本**输出（JSON / YAML / 自定义 DSL），AI 不需要看渲染画面或 GUI 截图就能理解引擎状态。
4. **前端友好的技术栈**：脚本语言选 **TypeScript**（AI 最熟），UI 使用 **Vue 子集 + HTML + JavaScript 容器**（AI 对前端掌控力最强），编辑器走 **Web + WebGL** 呈现（AI 可以生成并预览）。
5. **AI 深度参与资产生产**：引擎集成贴图 / 角色 / 动画 / 3D 模型 / NeRF / 3DGS 等生成管线，AI 能一句话产出可用资产。

**定位**：不是"多加一个 MCP 插件"，而是**从一开始就把 AI 当作与人类开发者平权的使用者**，所有设计决策都考量"AI 是否好用"这个维度。

---

## 架构级原则

本 roadmap 的架构纲领收拢到独立文档：

**→ [principles.md](principles.md)** · 20 条 AI-Native 引擎的跨阶段不变量

这份文件是本 roadmap 的**宪法**。每个 phase 都会在"与 AI-Native 原则的契合"一节回指到这里对照。实施时如果某个阶段的具体方案与某条原则抵触，以 principles 为准，改方案不改原则。

核心 20 条（完整内容见 principles.md）：

| # | 标题 | 一句话 |
|---|------|-------|
| P-1  | 确定性                      | 同输入同输出 |
| P-2  | 状态即事件流                | 当前状态写成 \( \mathrm{state}_t = \mathrm{fold}(\mathrm{events}_{0..t}, \mathrm{initial}) \) |
| P-3  | Query / Command / Primitive | 三层 API 职责分离，agent 只触达前两层 |
| P-4  | 单源能力清单                | 一次声明，派生所有外部面 |
| P-5  | 语义查询层                  | 按意图过滤，不按结构遍历 |
| P-6  | Intent Graph                | Agent 工作 = 目标→计划→步骤→动作树 |
| P-7  | 多分辨率观察                | summary / outline / full 三档 |
| P-8  | Dry-run / 影子状态          | 命令可预演 |
| P-9  | 成本模型                    | 每个操作估算 token/time/money |
| P-10 | 资产血统 / Provenance       | 每份资产记得自己从哪来 |
| P-11 | 时间旅行查询                | 任意历史点可查询 |
| P-12 | 错误即教学                  | 错误带 fix_hint + agent_tip |
| P-13 | HITL 类型级契约             | confirm / review 级别由类型系统强制 |
| P-14 | 能力发现优于能力配置        | Agent 运行期问"我能做什么" |
| P-15 | 重构友好 / 版本化           | 所有持久化格式带 schema version + 迁移链 |
| P-16 | 文本优先≠文本唯一           | 多模态通道并存 |
| P-17 | Eval Harness 内建           | 挑战集 + 评分器，接入 CI |
| P-18 | 沙箱进程模型                | 可一次性重置 |
| P-19 | 命令总线                    | 编辑器/agent/CLI 共享同一套 API |
| P-20 | 渲染/模拟可分离             | headless 模式完整可用 |

### 补充原则（从原路线图延续）

- `core/` 不 include `infra/` 或 `backend/`；构造函数注入，禁止 setter DI
- 每个能力先写 `openspec/specs/<capability>/spec.md`
- 不追求"完整"，追求"够用"
- **优先描述接口和能力，把具体库 / 算法 / 协议当成可替换的选型**（这份 roadmap 刻意避免锁定具体实现，方便重构）

---

## 完整路线总览

```mermaid
flowchart TD
    current["现状<br/>Vulkan 教学渲染器 + PBR Tutorial<br/>00-gap-analysis.md"]
    p1["Phase 1<br/>渲染深度 + Web 后端"]
    p2["Phase 2<br/>基础层 + 文本内省"]
    p3["Phase 3<br/>资产管线"]
    p4["Phase 4<br/>动画"]
    p5["Phase 5<br/>物理"]
    p8["Phase 8<br/>Vue UI 容器"]
    p6["Phase 6<br/>Gameplay (TypeScript)"]
    p7["Phase 7<br/>音频"]
    p9["Phase 9<br/>Web 编辑器"]
    p10["Phase 10<br/>MCP + Agent + CLI<br/>AI-Native 核心"]
    p11["Phase 11<br/>AI 资产生成"]
    p12["Phase 12<br/>打包 / 发布"]

    current --> p1
    current --> p2
    p1 --> p3
    p2 --> p3
    p3 --> p4
    p3 --> p5
    p3 --> p8
    p4 --> p6
    p5 --> p6
    p8 --> p6
    p6 --> p7
    p6 --> p9
    p8 --> p9
    p7 --> p10
    p9 --> p10
    p6 --> p10
    p10 --> p11
    p10 --> p12
```

**关键并行窗口**：

- **Phase 1 + Phase 2 + Phase 8 三路并行**：渲染后端、基础层、Vue UI 容器三者之间解耦，可以分头动工。
- **Phase 4 + Phase 5 + Phase 8 同样并行**：只要 Phase 2 和 Phase 3 已就位。
- **Phase 10 之后 Phase 11 + Phase 12 并行**：资产生成与发布打包互不依赖。

---

## 阶段索引

| Phase | 标题 | 一句话目标 | 依赖 |
|-------|------|----------|------|
| [**principles**](principles.md) | **AI-Native 核心原则（宪法）** | 跨阶段的 20 条架构不变量 | — |
| [00](00-gap-analysis.md)      | Gap Analysis            | 盘点当前状态与 AI-Native 引擎的差距 | — |
| [1](phase-1-rendering-depth.md)   | 渲染深度 + Web 后端    | shadow/IBL/HDR + WebGPU/WebGL 后端 | 现状 |
| [2](phase-2-foundation-layer.md)  | 基础层 + 文本内省      | transform / input / time + `dumpScene()` 族 API | 现状 |
| [3](phase-3-asset-pipeline.md)    | 资产管线               | GUID + 序列化 + 热重载 | Phase 2 |
| [4](phase-4-animation.md)         | 动画                   | 骨骼动画播放 + 状态机 | Phase 2, 3 |
| [5](phase-5-physics.md)           | 物理                   | Jolt 刚体 / 射线 / 角色 | Phase 2 |
| [6](phase-6-gameplay-layer.md)    | Gameplay (TypeScript)  | 组件 + TS 脚本 + 事件总线 | Phase 2, 3, 4, 5 |
| [7](phase-7-audio.md)             | 音频                   | miniaudio + 3D 声场 + mixer | Phase 2, 3 |
| [8](phase-8-web-ui.md)            | Vue UI 容器            | HTML+JS 容器 + Vue 子集 | Phase 1, 6 |
| [9](phase-9-web-editor.md)        | Web 编辑器             | 浏览器里跑的编辑器 + WebSocket IPC | Phase 1, 2, 8 |
| [10](phase-10-ai-agent-mcp.md)    | MCP + Agent + CLI      | 引擎自带 Agent，暴露 MCP tools，CLI 交互模式 | Phase 2, 3, 6, 9 |
| [11](phase-11-ai-asset-generation.md) | AI 资产生成        | 贴图 / 模型 / 动画 / NeRF / 3DGS 生成管线 | Phase 3, 10 |
| [12](phase-12-release.md)         | 打包 / 发布            | Win / Linux / WASM 三目标 | 全部 |
| [技术预研 (research/)](research/) | 技术预研留档 | 未列入阶段、仅记录未来可能引入的技术方向 | — |

---

## 如何读这份 roadmap

### "我从零开始，要看大图"

1. 先读本文件（你正在读）
2. **读 [principles.md](principles.md) 一遍，建立心智模型**（15 分钟即可）
3. 读 [00-gap-analysis.md](00-gap-analysis.md) 了解起点和缺口
4. 按 Phase 1 → 12 顺序浏览每个 phase 的 "目标 / 可交付 / 里程碑" 三段

### "我只关心 AI-Native 的核心改动"

按推荐阅读顺序：
1. [Phase 10 · MCP + Agent + CLI](phase-10-ai-agent-mcp.md) — 核心概念
2. [Phase 2 · 文本内省](phase-2-foundation-layer.md) — Agent 能读懂引擎的前提
3. [Phase 6 · Gameplay (TypeScript)](phase-6-gameplay-layer.md) — Agent 能写代码的入口
4. [Phase 11 · AI 资产生成](phase-11-ai-asset-generation.md) — Agent 能产资产的能力
5. [Phase 9 · Web 编辑器](phase-9-web-editor.md) — Agent 与人类共享的可视化面

### "我手上已有工程，下一步动工什么"

- 图形 / 画面 → [Phase 1](phase-1-rendering-depth.md)
- 游戏逻辑 / 交互 → [Phase 2](phase-2-foundation-layer.md)
- 让 AI 参与开发 → [Phase 10](phase-10-ai-agent-mcp.md)（需要先有 Phase 2 的内省 + Phase 6 的 gameplay）

---

## 工作量总览（粗估）

| 模块集合 | 乐观 | 悲观 | 说明 |
|---------|------|------|------|
| 渲染基础 + Web 后端（P1）                     | 6 周  | 12 周 | WebGPU 支持是额外成本 |
| 基础层 + 内省 + 资产 + 动画 + 物理（P2–P5）   | 10 周 | 20 周 | 与原 roadmap 基本一致 |
| Gameplay (TS) + 音频 + Vue UI（P6–P8）        | 8 周  | 16 周 | TS 集成 + Vue 子集比 Lua 更重 |
| Web 编辑器（P9）                              | 6 周  | 12 周 | 从零写 web 前端 |
| **AI Engine Core（P10）**                    | 6 周  | 12 周 | MCP server + agent runtime |
| **AI 资产生成（P11）**                       | 6 周  | 14 周 | 模型服务集成 + 生成流水线 |
| 发布（P12）                                  | 2 周  | 4 周  | + WASM 构建链 |
| **合计**                                    | 44 周 | 90 周 | 约 10 月 – 1 年 10 月 全职 |

一个人的话多半走不完。实际可行的模式：

- **3 个月 MVP**：Phase 1 前半（仅 Vulkan）+ Phase 2 + Phase 6 基础 TS + Phase 10 MCP server 最小集 → 能接 Claude Code 对场景做简单操作。
- **6 个月 Alpha**：加上 Phase 3 / 4 / 5 / 8 / 9，有 web 编辑器和完整 gameplay。
- **10 个月 Beta**：加上 Phase 7 / 11 / 12，完整 AI 资产生成与三目标发布。

---

## 与原 roadmap 的差异（增量）

本路线图基于早期的 9 阶段 roadmap，做了下列关键改动：

| 改动 | 位置 | 原因 |
|------|------|------|
| **Web 后端升到 P1** | Phase 1 | 编辑器 + agent demo 都依赖它 |
| **文本内省 API** | Phase 2 | AI 读文本比看 GUI 有效 |
| **脚本从 Lua 改 TypeScript** | Phase 6 | AI 对 TS 生态最熟 |
| **UI 拆出来独立阶段** | Phase 8 | Vue 容器是完整工程量 |
| **编辑器改成 Web-based** | Phase 9 | AI 可生成并可预览 |
| **新增 MCP + Agent + CLI** | Phase 10 | AI-Native 核心定义 |
| **新增 AI 资产生成** | Phase 11 | 让 AI 产出可用资产 |
| **发布增加 WASM** | Phase 12 | Web 分发路径 |

---

## 下一步

如果你刚读完本页：

- **没读过 principles.md？→ [principles.md](principles.md)（必读，建立心智模型）**
- 想动手？→ [Phase 1](phase-1-rendering-depth.md) 或 [Phase 2](phase-2-foundation-layer.md)
- 想先看清 AI-Native 愿景的落点？→ [Phase 10 · MCP + Agent + CLI](phase-10-ai-agent-mcp.md)
- 想了解和现状的差距？→ [00 · Gap Analysis](00-gap-analysis.md)
