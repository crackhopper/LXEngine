# REQ-043-a: v0.1.1 — Shadow 阶段教程支撑

## 背景

当前 active 队列中原有的 light registry、toolbar registry、custom node registry 更偏向“教程扩展 API”。这些能力有价值，但不是 FrameGraph / shadow / CSM 的前置。v0.1.1 需要先完成多 pass 与阴影主线，再回头补教程中真正需要的支撑内容。

本需求只收口完成 `REQ-042-a/b/c` 后，教程为了讲清楚 shadow-era engine 所需的最小内容。

## 目标

1. 更新教程，让读者能搭建并理解 shadow / CSM 场景。
2. 补齐教程需要但不扩大引擎范围的测试资产、scene 或 editor 小入口。
3. 明确哪些扩展 API 仍在 pending，不把它们写成当前能力。
4. 保持教程围绕 v0.1.1 已实现能力，不提前教学 HDR/G-Buffer/PBR 后续路径。

## 需求

### R1: Shadow tutorial scene

提供或更新一个教程场景，至少包含：

| 对象 | 用途 |
|---|---|
| ground receiver | 展示阴影落点 |
| caster mesh | 展示 shadow caster |
| directional light | 展示 shadow direction 与强度 |
| camera | 固定教程视角 |

场景应可由现有 editor 打开、保存、重载。

### R2: FrameGraph reading path

教程需要解释：

- `FrameGraph` 如何从 scene 生成 pass。
- `Pass_Shadow` 与 `Pass_Forward` 如何通过 resource 连接。
- shadow map 为什么是先写后读。
- CSM 为什么需要多 cascade。

说明应链接到 source analysis / subsystem / spec，而不是复制实现细节。

### R3: Minimal editor affordances

如果教程无法通过现有 UI 调整 shadow 参数，可以增加最小入口，例如：

- directional light shadow enabled。
- shadow strength。
- shadow distance。
- cascade debug mode。

只做教程需要的参数入口，不引入完整 light registry 或插件系统。

### R4: Pending 扩展显式标注

教程中涉及以下能力时必须标注为 pending：

- light kind registry。
- command / toolbar extension registry。
- custom scene node registry。
- Web Editor。
- Engine CLI / MCP。
- AssetRegistry / hot reload。

### R5: 测试覆盖

覆盖：

- tutorial scene 可加载。
- shadow tutorial 所需资产存在。
- 文档链接指向当前 active / pending requirement。
- 教程不把 pending 能力描述成已实现能力。

## 修改范围

- `notes/tutorial/`
- `notes/concepts/`
- `notes/source_analysis/` 中必要链接
- `assets/scenes/` 或测试资产
- `src/demos/lxe_editor/` 中仅限教程必要的小入口
- 相关 tests

## 边界与约束

- 本 REQ 在 `REQ-042-a/b/c` 完成后实施。
- 本 REQ 不实现原 `REQ-042-a/b/c` pending 的扩展注册模型。
- 本 REQ 不实现 HDR/Post、PBR 完整管线、G-Buffer/Deferred 教程。
- 本 REQ 不扩展 Web/CLI/MCP。

## 依赖

- `REQ-042-a`
- `REQ-042-b`
- `REQ-042-c`

## 后续工作

原教程扩展 API 需求已移入 `notes/requirements/pending/`，等待 v0.1.1 之后重新排序。

## 实施状态

未开始。v0.1.1 中排在 CSM 之后。
