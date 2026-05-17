# REQ-044-a: Roadmap 支撑 — Web Editor Shell 与 IPC 合同

> 2026-05-17：本需求已从 active 队列移入 pending。Web Editor 仍属于 Phase 9，但不进入 v0.1.1 active 队列。

## 背景

当前 `lxe_editor` 的主编辑器是 ImGui overlay。它已经具备 CommandBus、SceneRuntime、HTTP/WebSocket API service、事件流和 recording，这些能力为未来 Web Editor 提供了基础。

Roadmap Phase 9 描述了浏览器 editor，但当前代码还没有 `editor.html`、Vue editor shell，也没有面向 Web Editor 的稳定 IPC schema。为了避免设计文档把 Phase 9 写成当前能力，需要先把 Web Editor 的最小落地点拆成一个 active requirement。

## 目标

1. 定义 Web Editor 首个可落地 shell。
2. 定义浏览器到 engine/editor 的命令、状态、事件 IPC 合同。
3. 复用当前 CommandBus 和 API event 模型，不重写 editor 行为。
4. 让后续 Scene Tree、Inspector、资产面板可以逐步接入。

## 需求

### R1: Web editor shell

新增最小 Web Editor 入口，至少包含：

| 区域 | 首版内容 |
|---|---|
| top bar | 连接状态、active project/scene |
| left dock | scene tree 占位或只读列表 |
| center | 当前 display / viewport 占位 |
| right dock | inspector 占位 |
| bottom dock | command console / event log |

首版可以只服务本地开发，不要求产品级 UI。

### R2: WebSocket IPC schema

定义双向消息形状：

```json
{
  "id": "request-001",
  "type": "command.execute",
  "payload": {
    "line": "select /helmet"
  }
}
```

engine/editor 返回：

```json
{
  "id": "request-001",
  "ok": true,
  "message": "selected /helmet",
  "structured": "{}"
}
```

### R3: 事件订阅

Web Editor 必须能订阅当前 `LxeEditorApiService` 暴露的 event：

| event | 用途 |
|---|---|
| `command.executed` | console / log |
| `selection.changed` | scene tree / inspector 联动 |
| `active_scene.changed` | 切换 scene 后刷新 |
| `dirty.changed` | save 状态提示 |
| `preview.changed` / `mode.changed` | toolbar 状态同步 |

### R4: 不复制 editor 行为

Web Editor 的按钮和输入必须通过 CommandBus 或 API command endpoint 修改状态，不直接维护另一套 scene mutation 逻辑。

### R5: 测试覆盖

覆盖：

- WebSocket command request 可以 dispatch 到 CommandBus。
- WebSocket event stream 能收到 selection / dirty / active scene 变化。
- Web shell 启动时能拉取初始 state snapshot。
- 断线重连后能重新同步 state。

## 修改范围

- `src/demos/lxe_editor/lxe_editor_api_service.*`
- `src/demos/lxe_editor/lxe_editor_api_server.*`
- Web editor 静态资源目录或未来 web UI 目录
- `notes/design/editor-system/*`
- 相关 tests / use cases

## 边界与约束

- 本 REQ 不实现完整 Vue component library。
- 本 REQ 不要求替换 ImGui editor。
- 本 REQ 不实现公网安全模型，只保留本地 token / dev-only 约束。
- 本 REQ 不实现 AssetRegistry 面板；资产面板依赖 `REQ-044-c`。

## 依赖

- 当前 `CommandBus`
- 当前 `LxeEditorApiService`
- 当前 project / scene runtime 状态快照

## 实施状态

Pending，未开始。当前仅作为 Phase 9 Web Editor 的后续最小落地需求。
