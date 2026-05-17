# Phase 2 · Foundation Layer：输入、时间与文本内省

> 目标：把编辑器已有的场景对象基础，补成游戏运行时和 agent 都能稳定消费的基础层。

v0.1.0 已经完成 transform、层级、path、component、camera-as-component、picking 和 command bus。Phase 2 不再重复这些内容，聚焦仍缺的运行时基础：输入抽象升级、时间步进、结构化内省和空间查询。

## 当前缺口

| 缺口 | 当前状态 | 为什么需要 |
|---|---|---|
| Action mapping | `IInputState` 有键鼠状态，缺动作层 | gameplay 和 editor shortcut 不应直接绑设备 |
| Gamepad | SDL3 输入有键鼠，手柄未成体系 | 小游戏原型需要 |
| Fixed step / pause / time scale | `Clock` 有 delta/smoothed delta | 物理、replay、慢动作需要稳定时间模型 |
| Structured dump | `Scene::dumpTree()` 有人读树形文本 | MCP/CLI 需要 JSON/schema 级输出 |
| `describe(...)` API | 部分 editor 面板能读字段 | agent 需要对象、材质、pipeline 的分层解释 |
| Spatial query | picking 有最小路径 | 物理前的 queryBox/queryRay 需要统一接口 |

## 工作顺序

| 顺序 | 主题 | 说明 |
|---|---|---|
| 1 | Action mapping | key/mouse/gamepad → semantic action |
| 2 | Time step | variable update + fixed update + pause/timeScale |
| 3 | Structured dump | scene/node/component/material/pipeline JSON |
| 4 | Describe API | summary/outline/full 三档 |
| 5 | Spatial query | world bounds、queryRay、queryBox、简单 broadphase |

## 与 Phase 1 的关系

Phase 1 shadow/G-Buffer 不阻塞在 Phase 2 上。两者可以并行。

Phase 1 会消费 Phase 2 的两个结果：

| Phase 2 输出 | Phase 1 消费 |
|---|---|
| Structured dump / describe pipeline | 调试 FrameGraph、PipelineKey、G-Buffer |
| Spatial query / bounds | Frustum culling、debug picking |

## 继续阅读

- [Scene System](../../scene-system/index.md)
- [Editor System](../../design/editor-system/index.md)
- [Phase 10 · Agent / MCP / CLI](phase-10-ai-agent-mcp.md)
