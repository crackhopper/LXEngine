# Scene Viewer Automation And Command Surface Design

Date: 2026-05-11

## Context

`lxe_editor` 现在已经具备：

- 场景加载/保存与 `asset` / `local` 工作流
- 浮动 toolbar + `Selection / Orbit / FreeFly / Preview`
- `editor_config.yaml` / `editor_data.yaml` 本地持久化
- command console
- 主路径 scene picking 与 debug overlay

但当前仍存在三类缺口：

1. 交互层仍有一部分能力只在 UI 里可用，没有完整命令面。
2. 自动化与排障缺少稳定探针，导致很多行为只能靠人工看 UI 判断。
3. 缺少正式的网络 api 接口，无法稳定地从进程外驱动 `lxe_editor` 做集成测试，也不利于后续 MCP 接入。

本设计把这些问题合并为一条统一原则：

- 编辑器能力先命令化
- 状态先结构化
- 自动化先收口到协议无关的 service
- HTTP / WebSocket 只是 transport
- MCP 后续只做 adapter，不直接碰 editor 内核

## Goals

- 修正主场景点击 picking 与实际可见主画面的坐标偏差。
- toolbar 改成纯 icon 面板，并新增“editor camera 对齐到 game camera”的按钮。
- 让所有关键 editor 操作都能通过 command console 触发。
- 增加足够的状态/探针命令，支持纯文本排障与自动化断言。
- 为 `lxe_editor` 增加默认可用的 HTTP + WebSocket 自动化接口。
- 默认全网卡开放时仍要求 token 认证。
- 为后续 MCP adapter 保留稳定的 API domain 与 JSON schema。

## Non-Goals

- 本次不恢复旧的 `Viewport` 面板为主交互入口。
- 本次不把 api 能力直接做成 MCP server。
- 本次不引入复杂用户/权限系统；网络接口只做 token 认证。
- 本次不把命中点升级成 mesh triangle 级表面 picking。
- 本次不新增另一套绕过 command system 的隐藏调试 RPC。

## Design Principles

### Command system is the capability source

所有 editor 行为能力都优先通过 command system 暴露。  
如果以后自动化、自测、排障缺少观测点，优先增加命令，再由网络层复用。

### Transport is not truth

HTTP、WebSocket、未来 MCP 都不直接拥有 editor 真相状态，也不直接绕过 command system 修改状态。  
唯一真相仍是 `CommandBus`、`EditorState`、`SceneViewerSession`、`UiOverlay` 等现有 editor/runtime 对象。

### Structured state beats console scraping

任何关键命令除了人类可读 message 外，都应返回稳定 structured JSON。  
自动化查询优先使用结构化 state API，而不是解析 console 文本输出。

### Protocol-agnostic API core

`lxe_editor` 内部先形成稳定 API domain。  
HTTP / WebSocket / MCP 都只是这层 domain 的不同 adapter。

## Functional Design

### 1. Scene view rect picking fix

当前主路径 selection picking 直接拿整窗坐标和整窗尺寸去构造 `pickRay(...)`。  
在左侧 Scene Tree、右侧 Inspector、底部 Console、顶部 Toolbar/Stats 这些浮动面板存在时，用户实际看到的主场景画面与整窗坐标系并不一致，这会导致：

- 命中射线偏移
- AABB 交点 marker 和真实点击位置不一致

修正方案：

- 新增一个 `scene view rect` 计算函数。
- 它根据当前 editor 布局推导“实际可见的主场景矩形”。
- selection picking 只在鼠标位于该 rect 内时执行。
- 执行时把整窗鼠标坐标转换为 rect-local 坐标。
- 传给 `pickRay(...)` 的 viewport size 也改成 rect 的宽高。

本次仍保持“主渲染画面就是交互区”，不恢复独立 viewport 面板。

### 2. Toolbar becomes icon-only

Toolbar 保留为独立浮动面板，但最终形态改成：

- 只显示 icon 按钮
- 不显示任何静态模式文字
- hover tooltip 保留，作为唯一文字提示来源

按钮集合：

- `Selection`
- `Orbit`
- `FreeFly`
- `Preview`
- `Reset Editor Cam To Game Cam`
- `Preferences`

不再在 toolbar 末尾显示：

- 当前 edit mode 文本
- `| Preview` 文本

### 3. Reset editor camera to game camera

新增 toolbar 按钮，同时补对应 command。

语义：

- 把当前 `editor_cam` 的位姿复制为当前 `game_cam`
- 不切换 preview
- 不修改 active camera 语义
- 不改变当前 edit mode

因为 `CameraRig` 内部还维护 orbit/freefly controller 的内部状态，这个动作执行后还必须同步 rig：

- 当前是 `Orbit` 时，从新位姿重建 orbit controller 状态
- 当前是 `FreeFly` 时，从新位姿重建 freefly controller 状态

否则下一帧用户一操作相机，会因为 controller 仍停留在旧状态而跳回错误视角。

## Command Surface

### Existing commands remain valid

保留现有命令体系，例如：

- `scene load/save/list`
- `select` / `deselect`
- `move` / `rotate` / `scale`
- `set` / `get`
- `preview on/off/toggle`
- `admin on/off/status`

### UI-only actions become commands

把当前只存在于 UI 的 editor 行为补为命令：

- `mode selection`
- `mode orbit`
- `mode freefly`
- `cam reset-editor-to-game`

这些命令的行为必须与 toolbar 完全一致，避免 UI 路径和 command 路径出现双份逻辑。

### Probe and test-helper commands

新增状态/探针命令。第一版至少包括：

- `state summary`
- `state selection`
- `state cameras`
- `state scene`
- `state toolbar`
- `pick <x> <y>`
- `debug overlay`

这些命令必须返回：

- 人类可读 `message`
- 稳定 `structured` JSON

后续如发现自动化和排障缺少观测点，继续沿同一体系补命令，而不是增加网络层私有接口。

## Automation Core

### LxeEditorApiService

新增独立模块 `LxeEditorApiService`。

职责：

- 执行命令
- 提供结构化状态快照
- 提供少量结构化 editor action
- 生成事件流
- 统一错误模型

它不拥有 editor 真相状态，只持有对这些对象的非拥有引用：

- `CommandBus`
- `EditorState`
- `SceneViewerSession`
- `UiOverlay`

### State snapshot responsibilities

第一版结构化状态至少应覆盖：

- scene summary
  - current scene path
  - source kind (`asset` / `local`)
  - dirty
  - permission level
- selection
  - selected paths
  - primary selection
  - selected node world AABB
  - last successful hit point
- cameras
  - active camera
  - editor camera pose
  - game camera pose
- toolbar/editor mode
  - current edit mode
  - preview enabled

### Event model

WebSocket 和未来 MCP 订阅都复用同一套事件模型。  
第一版事件至少覆盖：

- `command.executed`
- `scene.loaded`
- `scene.saved`
- `selection.changed`
- `mode.changed`
- `preview.changed`
- `dirty.changed`

统一 envelope：

```json
{
  "type": "selection.changed",
  "seq": 42,
  "payload": {}
}
```

## Network Automation Server

### LxeEditorApiServer

新增独立 `LxeEditorApiServer`。

职责：

- HTTP server
- WebSocket server
- token authentication
- JSON 编解码
- connection / session 管理

它本身不实现 editor 逻辑，只转调 `LxeEditorApiService`。

### HTTP API

第一版建议端点：

- `POST /api/command`
- `GET /api/state/summary`
- `GET /api/state/selection`
- `GET /api/state/cameras`
- `GET /api/state/scene`
- `GET /api/state/toolbar`
- `POST /api/mode`
- `POST /api/preview`
- `POST /api/camera/reset-editor-to-game`
- `POST /api/pick`

约束：

- 结构化 action endpoint 如果本质是 editor action，内部仍应走 command 或等价 command-backed service 逻辑。
- 不允许 HTTP endpoint 直接偷偷改 editor 状态而绕过 command surface。

### WebSocket API

WebSocket 第一版同时支持：

- 接收命令消息
- 推送事件流

示例消息：

```json
{
  "type": "command",
  "line": "scene list"
}
```

返回统一 JSON event / result shape，而不是自由文本流。

## Authentication

### Default exposure

第一版默认全网卡开放：

- default host = `0.0.0.0`

因此 token 认证必须默认开启，且不能是可选装饰。

### Token source

启动时：

- 若 `data/lxe_editor/api_token.txt` 存在，则读取
- 不存在则生成随机 token 并写入该文件

### Transport usage

- HTTP 使用 `Authorization: Bearer <token>`
- WebSocket 握手通过 header 或 query 参数带 token

未认证请求统一返回明确 unauthorized 错误。

## Startup And Runtime Integration

`lxe_editor` 启动参数第一版建议包含：

- `--api-enable`
- `--api-host <host>`
- `--api-port <port>`
- `--api-token-file <path>`

默认行为建议：

- 程序正常启动即开启 API server
- 默认 host = `0.0.0.0`
- 默认 port 使用固定值

启动日志需要打印：

- host
- port
- token file path

但不直接把 token 明文打印到普通日志。

## MCP Readiness

本次不直接把 `lxe_editor` 做成 MCP server。  
但要为后续 `LxeEditorApiMcpAdapter` 做准备。

约束：

- 先稳定 `LxeEditorApiService`
- 先稳定结构化 state JSON shape
- HTTP / WebSocket 的输入输出 schema 尽量贴近未来 MCP tool/resource schema

后续 MCP 映射方向：

- command / action -> MCP tools
- state query -> MCP tools 或 resources
- events -> MCP 资源刷新或订阅模型

## Error Handling

- command 执行失败必须保留统一 `ok/message/structured/metadata` 结果模型。
- HTTP 返回稳定状态码和 JSON body，不返回只适合人类看的自由文本。
- WebSocket 失败消息也要有统一 envelope。
- token 文件读写失败、端口绑定失败、server 初始化失败时，`lxe_editor` 要有清晰错误输出。

## Testing Strategy

### In-process tests

保留并扩展现有单元/集成测试：

- toolbar 纯 icon 和 reset 行为
- `scene view rect` 坐标映射
- 新命令语义
- API service 状态快照与事件

### Out-of-process API tests

新增一类进程外集成测试：

- 子进程拉起 `lxe_editor`
- 读取 token 文件
- 通过 HTTP 调 command / state query
- 必要时通过 WebSocket 等待事件

第一版至少覆盖：

- 启动成功 + token 认证成功
- `scene list`
- `mode selection/orbit/freefly`
- `preview toggle`
- `cam reset-editor-to-game`
- selection state 查询
- `pick` 命令或 HTTP pick action

### Human script usability

自动化接口同时应保证脚本和手工调试友好：

- `curl` 可直接调用 HTTP
- shell / PowerShell 脚本可直接读取 token 文件发请求

## Parallelization Plan

实现阶段适合并行拆分为至少三条相对独立的工作线：

1. 交互与 toolbar
   - `scene view rect`
   - toolbar 纯 icon
   - reset 按钮
   - mode/reset 命令

2. command / probe / API core
   - 新命令
   - probe 命令
   - `LxeEditorApiService`
   - 事件模型

3. transport
   - HTTP
   - WebSocket
   - token 认证
   - 进程外集成测试

并行前提：

- 命令与状态 schema 先定死
- API core 接口先定死

这样 transport 和交互层可以并行实现而不互相踩逻辑。

## Implementation Order

建议落地顺序：

1. 修 `scene view rect` picking
2. toolbar 纯 icon + reset 按钮
3. 把新增 UI 动作补成命令
4. 增加 probe/state 命令
5. 抽 `LxeEditorApiService`
6. 上 HTTP
7. 上 WebSocket
8. 增加进程外集成测试
9. 后续单独做 MCP adapter

## Acceptance Criteria

- 点击主场景时，pick ray 与实际可见主场景区域对齐，交点 marker 不再明显偏移。
- toolbar 里不再显示静态文字，只保留 icon + tooltip。
- 存在可用的 reset icon，把 `editor_cam` 对齐到 `game_cam`，且不切 preview。
- toolbar 提供的所有关键动作，都能通过 command console 触发。
- 缺观测点时可通过新增命令补足，而不是增加隐藏网络接口。
- `lxe_editor` 可通过 HTTP / WebSocket 从外部进程驱动。
- 默认全网卡开放时，未带 token 的请求无法通过认证。
- 进程外自动测试可以启动 `lxe_editor` 并通过 API 完成核心断言。
- HTTP / WebSocket schema 与后续 MCP adapter 的输入输出模型兼容，不需要重写 editor 内核。
