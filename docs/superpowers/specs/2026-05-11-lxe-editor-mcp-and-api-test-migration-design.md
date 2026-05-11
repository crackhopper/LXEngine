# LXE Editor MCP And API Test Migration Design

## Context

当前仓库里原来的 `lxe_editor` 已经不只是一个 demo。它已经具备：

- 完整 scene asset / local workspace 工作流
- 浮动 toolbar、selection / orbit / freefly / preview 编辑模式
- command-first 的 editor command surface
- HTTP / WebSocket API surface
- `LxeEditorApiService` 这类可复用的自动化核心

因此继续沿用 `lxe_editor` 这个名字已经不合适。新的统一名称应该是
`lxe_editor`，即 `LX Engine Editor`。

同时，当前自动化与测试还缺两层收口：

1. Codex 缺一个面向诊断的原生 MCP 接入层
2. 一部分 editor 相关测试仍然是 C++ in-process 风格，而未来更稳的主路径应当是 API 黑盒测试

这份设计把三件事放在一次连续迁移里处理：

- 产品与工程命名统一：`lxe_editor` -> `lxe_editor`
- 在 `lxe_editor` 进程内提供 MCP server
- 把 `lxe_editor` 的测试主路径迁到 API 黑盒测试，MCP 主要用于 Codex 诊断

## Goals

- `lxe_editor` 不再作为长期产品名存在；编辑器统一改名为 `lxe_editor`
- `lxe_editor` 进程内提供 MCP server，并通过本地 `.codex` 配置接入 Codex
- Codex 后续调试时可以直接使用 `lxe_editor` 的 MCP tools/resources
- `lxe_editor` 的官方自动化测试主路径切换为 Python API 黑盒测试
- 保留底层纯逻辑 C++ 测试，不盲目删除所有 C++ 测试

## Non-Goals

- 这次不废弃现有 HTTP / WebSocket API surface
- 这次不把 MCP 做成独立外部进程
- 这次不把所有 engine 测试都迁成 Python；只迁 `lxe_editor` 相关的行为测试主路径
- 这次不强制把所有 namespace 泛化成 `editor`；第一版统一到 `lxe_editor`

## Approaches

### Approach A: Full rename + in-process MCP + Python API tests

这是推荐方案。

- 代码、target、可执行名、窗口标题、文档、api/MCP 命名全部统一为
  `lxe_editor`
- MCP server 在 `lxe_editor` 进程内起独立线程，对 `127.0.0.1` 提供服务
- HTTP / WebSocket 继续作为正式自动化 API
- 仓库内官方黑盒测试使用 Python，通过 API 驱动 editor
- MCP 主要给 Codex 和人工诊断使用

优点：

- 命名一次到位，后续不会再背 `lxe_editor` 的历史包袱
- MCP 和 API 共享同一个自动化核心，不会出现两套真相状态
- Python 很适合拉起 GUI 进程、读 token、发请求、轮询状态、做断言

缺点：

- 迁移面较大，必须严格控制改名顺序和测试替换节奏

### Approach B: External MCP adapter over HTTP / WebSocket

- `lxe_editor` 继续只暴露 HTTP / WebSocket
- 另起一个 MCP adapter 进程，把 API 包装成 MCP

优点：

- `lxe_editor` 本体改动更少

缺点：

- 进程、协议和生命周期都多一层
- Codex 调试链更长，不符合“editor 内部单独线程做 MCP server”的目标

### Approach C: Keep C++ tests as primary, MCP only for tooling

- 做改名和 MCP
- 但 editor 的测试主路径仍以 C++ integration 为主，API 测试只做补充

优点：

- 短期工作量最小

缺点：

- 与“后续关于 `lxe_editor` 的测试直接基于 API 来测”的目标相反

## Recommendation

选择 Approach A。

理由很直接：

- 命名统一越早做越便宜
- `lxe_editor` 已经具备 API core，最适合继续长成 editor，而不是继续挂在 demo 名下
- API 黑盒测试更接近真实用户行为，也更方便 Codex 和人工排障复用
- MCP 放进进程内线程最贴合“Codex 诊断”这个用途

## Design 1: Naming Unification

`lxe_editor` 作为正式名称被废弃，新的唯一产品名是 `lxe_editor`。

统一改名覆盖：

- 源码目录：`src/demos/lxe_editor/` -> `src/demos/lxe_editor/`
- CMake target：`lxe_editor` -> `lxe_editor`
- 最终可执行名：`lxe_editor`
- 窗口标题：`lxe_editor`
- 本地数据目录：`data/lxe_editor/` -> `data/lxe_editor/`
- README、notes、spec、plan、测试说明、api 文案
- MCP server 名称、MCP tool/resource 命名

代码内部也要同步清理：

- `SceneViewerSession` 这类类型名改成 `LxeEditorSession` 或等价语义名
- `lxe_editor_commands.*` 是规范命名
- `test_lxe_editor_*` 改成 `test_lxe_editor_*`

兼容策略：

- 这次不保留旧 `lxe_editor` 名称的长期别名
- 如果过渡期需要，可以在单次变更里临时保留重定向或文件移动说明
- 变更完成后，仓库里不应再把 `lxe_editor` 当正式入口名继续传播

## Design 2: Automation And MCP Layering

`lxe_editor` 内部继续只有一套自动化真相状态：

- command surface
- `LxeEditorApiService`
- structured snapshots
- events

在这套自动化核心之上同时暴露三类 transport：

1. HTTP
2. WebSocket
3. MCP

职责划分：

- HTTP / WebSocket：正式自动化接口，服务脚本化测试和黑盒回归
- MCP：诊断接口，主要服务 Codex 与人工调试

关键约束：

- MCP 不允许直接绕过 API core 偷改状态
- 结构化操作如果本质是 editor action，最终仍应走 command surface 或同一自动化服务路径
- Codex 看见的状态与脚本测试看见的状态必须是同一份

## Design 3: In-Process MCP Server

MCP server 放在 `lxe_editor` 进程内，以独立线程运行。

选择本地 TCP：

- 默认只绑定 `127.0.0.1`
- 不复用 HTTP api 的“全网卡开放”策略
- 不再增加额外 token 层，依赖本机地址边界控制

原因：

- GUI editor 已经是长期运行进程，MCP 放进内部线程最自然
- TCP 比 stdio 更适合常驻 GUI 进程
- 比 named pipe / Unix socket 更容易做跨平台实现和 Codex 配置

运行状态应写到本地状态文件，例如：

- `data/lxe_editor/runtime_state.yaml`

至少包含：

- `pid`
- `http.host`
- `http.port`
- `ws.host`
- `ws.port`
- `mcp.host`
- `mcp.port`
- `tokenFile`
- `startedAt`

这样 Codex 或脚本不需要猜 editor 是否在线。

## Design 4: MCP Tools And Resources

MCP 第一版主要提供高层诊断能力，而不是简单复刻底层 HTTP path。

### Tools

- `lxe_editor_command`
  - 执行任意 command console 命令
- `lxe_editor_get_summary`
  - 读取当前 scene / mode / preview / dirty / active camera
- `lxe_editor_get_selection`
  - 读取 selection、AABB、last hit point
- `lxe_editor_get_cameras`
  - 读取 editor/game/active camera 状态
- `lxe_editor_pick`
  - 以屏幕坐标触发一次 pick
- `lxe_editor_wait_for`
  - 轮询等待某个状态成立
- `lxe_editor_ensure_running`
  - 确保后台 `lxe_editor` 已存在并可连接

### Resources

- `lxe-editor://summary`
- `lxe-editor://selection`
- `lxe-editor://cameras`
- `lxe-editor://toolbar`
- `lxe-editor://scene`

设计原则：

- tool 用于动作
- resource 用于稳定读取
- 命名统一以 `lxe_editor` 为前缀

## Design 5: Codex Integration

需要把 `lxe_editor` MCP server 集成到仓库本地 `.codex` 配置里，使 Codex 启动后就能访问它。

第一版建议：

- 在 `.codex` 下注册名为 `lxe_editor` 的 MCP server
- 连接目标来自 `data/lxe_editor/runtime_state.yaml`
- 如果 editor 未运行，则由 skill 或连接前置逻辑提示或尝试启动

在此基础上，再增加少量本地 skills，专门服务 Codex 调试：

- `lxe-editor-debug`
  - `ensure_running`
  - `get_summary`
  - `get_selection`
  - `get_cameras`
  - `command`
  - `wait_for`

这些 skills 只是对 MCP tool/resource 的薄封装，不应再维护另一套 editor 调试逻辑。

## Design 6: Background Editor Lifecycle

`lxe_editor` 支持以“后台调试实例”的角色常驻运行。

建议增加显式启动模式，例如：

- `--api-background`
- 或 `--profile codex`

该模式下：

- HTTP / WebSocket / MCP 默认开启
- `runtime_state.yaml` 总会被写出
- 窗口仍然可以存在，不做 headless 假设

目标不是把 editor 做成守护进程，而是让它成为一个可被脚本和 Codex 复用的长期运行实例。

## Design 7: Test Strategy Migration

测试不能“一刀切全删”。应按层级重新分工。

### 保留为 C++ 测试的内容

这些仍然应该保留为 C++ unit/integration：

- 数学、scene graph、transform、path lookup
- `CommandBus` 的纯解析和 undo/redo 核心
- `SceneDocument` / `SceneCatalog` / `SceneSession`
- `LxeEditorApiProtocol`
- `LxeEditorApiService`
- 渲染 backend 与资源系统测试

原因是这些测试快、定位准，不依赖整个 editor 生命周期。

### 迁移为 Python API 黑盒测试的内容

这些应成为 `lxe_editor` 的主测试路径：

- mode 切换最终效果
- preview on/off
- `cam reset-editor-to-game`
- `scene load/save/list/admin`
- selection / pick / debug marker 状态
- config / data 的跨重启持久化
- token / auth / endpoint 行为
- 后台常驻实例发现与连接

这些测试通过：

1. 拉起 `lxe_editor`
2. 读 `runtime_state.yaml` 和 token 文件
3. 通过 HTTP / WebSocket 发命令与查状态
4. 做黑盒断言

### MCP 在测试中的角色

MCP 不作为主 CI 回归入口。

MCP 的主要角色是：

- Codex 调试
- 人工诊断
- 在复杂回归失败时提供更高层探针

也就是说：

- API 是正式测试主路径
- MCP 是诊断辅助路径

## Design 8: Official Test Language

官方黑盒测试语言选 Python。

原因：

- 更适合拉起子进程
- 更适合读 token / state 文件
- 更适合发 HTTP / WebSocket 请求和做轮询
- 相比 Node.js，引入成本更低，也更容易写跨平台脚本

仓库可以允许个人用 JS 调试，但官方只维护一套 Python 测试实现。

## Design 9: Migration Order

为了降低风险，按下面顺序执行：

1. 统一改名：`lxe_editor` -> `lxe_editor`
2. 迁移本地数据目录：`data/lxe_editor` -> `data/lxe_editor`
3. 在 `lxe_editor` 内部接入 MCP server 线程
4. 把 `.codex` 配置接上本地 MCP server
5. 补本地 Codex 调试 skills
6. 新增 Python API 黑盒测试框架
7. 迁移 `lxe_editor` 相关 UI-heavy / end-to-end C++ 测试
8. 删除已被黑盒测试稳定覆盖、且不再提供独特诊断价值的旧测试

删除判断标准：

- 新测试已覆盖相同行为
- 旧测试只重复验证系统级行为
- 旧测试不再提供更低层、更快定位的价值

## Error Handling

- `runtime_state.yaml` 缺失：
  - Codex / skill 应明确报告 editor 未运行，而不是隐式失败
- MCP server 启动失败：
  - `lxe_editor` 日志要明确写出端口绑定或线程启动错误
- `.codex` 配置存在但连接不上：
  - skill 应优先检查 `runtime_state.yaml` 和进程状态，再给出失败原因
- API 黑盒测试如果 editor 未就绪：
  - 使用显式 wait-for-ready，而不是固定 sleep

## Validation

完成后至少应满足：

- 仓库正式入口名已经统一为 `lxe_editor`
- `lxe_editor` 内部提供 HTTP / WebSocket / MCP 三层接口
- Codex 启动后能通过本地 `.codex` 配置访问 `lxe_editor` MCP
- `lxe_editor` 可作为后台长期运行实例被复用
- 新增 Python API 黑盒测试能覆盖 editor 主行为
- 一部分旧的 `lxe_editor` 风格系统级 C++ 测试被迁移或删除
- 保留的 C++ 测试只承担纯逻辑 / 核心模块验证

## Open Questions Resolved

- 名称：统一改成 `lxe_editor`
- MCP 形态：进程内 MCP server
- MCP 传输：`localhost` TCP
- 官方黑盒测试语言：Python
- 测试主路径：API
- MCP 主要用途：Codex 诊断与调试

## Follow-Up

这份设计的下一步实现计划应拆成两个连续任务：

1. `lxe_editor` rename + MCP + `.codex` integration
2. Python API black-box test migration + C++ test cleanup
