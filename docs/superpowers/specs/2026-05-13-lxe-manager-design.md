# lxe_manager Design

Date: 2026-05-13

## Context

`lxe_editor` 当前同时承担了三类职责：

- 编辑器本体与实时状态
- HTTP / WebSocket API
- MCP protocol surface

这导致两个问题：

1. 协议适配与编辑器本体耦合过深。MCP 只是对外控制协议，不应成为
   `lxe_editor` 的核心职责。
2. 缺少独立的本机运维编排层。诸如 `git pull`、`cmake configure/build`、
   editor 拉起/停止/探活，以及机器资源异常时的保护动作，都不适合继续塞在
   editor 进程内。

当前仓库已经有 `lxe_editor` HTTP API、WebSocket 事件流、运行时发现文件
`runtime_state.yaml`、以及用于本地/远程接入的 helper 脚本。这为进一步拆分
MCP 与运维职责提供了现成基础。

本设计面向本机开发机场景：常驻后台服务、源码仓库、构建目录和
`lxe_editor` 进程位于同一台机器。第一阶段优先无状态运行；如后续需要，再扩展
状态或会话模型。

## Goals

- 将 MCP protocol support 从 `lxe_editor` 中完全拆出。
- 让 `lxe_editor` 只保留 editor 本体和 HTTP / WebSocket API。
- 引入独立常驻后台服务 `lxe_manager`，统一承载：
  - MCP server
  - editor 生命周期管理
  - repo/build 运维工具
  - 资源监控与熔断保护
- 让 manager 在 `lxe_editor` 崩溃时继续存活，并可执行重启、构建和诊断。
- 优先选择适合 MCP 和跨平台进程编排的技术栈。
- 第一阶段不引入复杂持久化状态或多会话模型。

## Non-Goals

- 第一阶段不实现多 workspace 或多 editor instance 会话管理。
- 第一阶段不把 editor 内部状态改造成共享内存或复杂 IPC 模型。
- 第一阶段不引入数据库、消息队列或集中式控制平面。
- 第一阶段不做资源限额的内核级强隔离；只做用户态监控与熔断。
- 第一阶段不改变 `lxe_editor` 现有编辑语义、命令语义或场景语义。

## Approaches Considered

### Option A: Node.js manager + editor 保留 HTTP / WebSocket

独立 Node.js 常驻服务对外暴露 MCP，对内通过 HTTP / WebSocket 调用
`lxe_editor`，并统一管理 `git`、`cmake`、`ninja` 与 editor 进程。

Pros:

- 更贴合 MCP server 与流式/事件式桥接场景
- 跨平台子进程管理能力更成熟
- 后续把 editor HTTP/WS 事件映射成 MCP tool/resource 更自然
- 便于把运维与协议适配集中在一个独立后台服务中

Cons:

- 仓库需要新增 Node.js 运行时与依赖管理

### Option B: Python manager + editor 保留 HTTP / WebSocket

Pros:

- 本地脚本化与命令编排简单直接
- 团队对 shell / Python 组合通常较熟悉

Cons:

- MCP、长连接和流式交互能力更容易演变成零散脚本集合
- 后续扩展到更完整的后台服务时，模块边界更容易松散

### Option C: 保留 editor 内嵌 MCP，只额外加运维 sidecar

Pros:

- 对现有 editor 改动最小

Cons:

- MCP 与运维职责继续割裂
- “主程序崩了但协议服务还在”的目标仍然不彻底
- 后续很容易出现重复发现逻辑与重复控制面

### Recommendation

采用 Option A。

本项目的中心问题不是单纯执行几条 shell 命令，而是要把 MCP、editor 控制、
事件等待、进程生命周期和机器保护整合成一个长期可维护的后台服务。Node.js
比 Python 更适合作为第一阶段的主实现语言。

## Architecture

### High-level split

系统拆分为两个进程：

- `lxe_editor`
  - 负责渲染、交互、编辑器状态
  - 保留 HTTP API
  - 保留 WebSocket 事件流
  - 发布 runtime discovery 文件
  - 不再暴露 MCP endpoint
- `lxe_manager`
  - 常驻后台服务
  - 对外暴露 MCP server
  - 对内管理 editor、repo 与 build 操作
  - 负责资源守护与熔断

MCP 从此不再是 editor 本体协议，而是 manager 对 agent/Codex 暴露的控制协议。

### Responsibilities

`lxe_editor` SHALL:

- 保留当前 HTTP API 与 WebSocket 事件流能力
- 保留 `runtime_state.yaml` 作为发现入口
- 删除进程内 MCP transport、MCP tool/resource 拼装和 `mcpUrl` 发布逻辑

`lxe_manager` SHALL:

- 作为唯一 MCP provider
- 通过 editor HTTP / WebSocket 重建现有 editor 侧 MCP 功能
- 提供 repo/build/editor 运维能力
- 在 editor 崩溃后继续运行
- 保护本机免于被受管进程拖死

## Functional Design

### MCP surface

`lxe_manager` 对外暴露两类 MCP tools/resources：

- `editor.*`
  - 将现有 `lxe_editor` HTTP / WebSocket 能力映射到 MCP
  - 例如：
    - `editor.get_summary`
    - `editor.get_selection`
    - `editor.get_cameras`
    - `editor.pick`
    - `editor.command`
    - `editor.wait_for`
- `ops.*`
  - 提供本机运维操作
  - 例如：
    - `ops.repo_pull`
    - `ops.build_configure`
    - `ops.build_target`
    - `ops.editor_start`
    - `ops.editor_stop`
    - `ops.editor_status`
    - `ops.editor_logs`

第一阶段目标不是发明全新 editor 语义，而是先保证当前 editor 侧 MCP 能力可以由
manager 通过 HTTP / WebSocket 重新实现。

### Editor transport

manager 不直接触碰 editor 内部状态。它只通过两种 transport 与 editor 通信：

- HTTP：拉取结构化状态、发送命令、执行 pick、切换 preview 等
- WebSocket：订阅事件流，实现 `wait_for` 一类阻塞等待能力

这要求 `lxe_editor` 的 HTTP / WebSocket API 继续作为 editor 对外唯一控制面。

### Runtime discovery

`runtime_state.yaml` 继续保留，但回到“editor 运行时发现文件”的角色。第一阶段
应保留并信任以下信息：

- `pid`
- `httpHost`
- `httpPort`
- `wsHost`
- `wsPort`
- `tokenFile`
- `startedAt`

`mcpUrl` SHALL 从该文件中删除。

manager 不得仅凭 runtime_state 文件就认定 editor 可用。它必须在读取发现信息后：

1. 检查对应 pid / 端口是否仍有效
2. 对 editor HTTP health 或已知稳定 API 做二次健康检查

### Lifecycle flow

`ops.editor_start` 的目标流程：

1. 用 `ProcessSupervisor` 拉起 `lxe_editor`
2. 等待 runtime_state 文件出现或更新
3. 读取 token file 与 HTTP / WebSocket 地址
4. 做健康检查
5. 标记 editor 为 running

`ops.editor_stop` 的目标流程：

1. 优先请求正常退出
2. 若 editor 未在短超时内退出，则终止进程
3. 必要时强制 kill 整个进程树

`editor.*` 调用流程：

1. MCP tool 进入 `McpServerHost`
2. 分发到 `EditorClient`
3. `EditorClient` 调用 editor HTTP / WebSocket
4. manager 将结果转换为 MCP response

`ops.build_*` / `ops.repo_pull` 调用流程：

1. MCP tool 进入 `McpServerHost`
2. 分发到 `WorkspaceOps`
3. `WorkspaceOps` 启动受管命令
4. `ProcessSupervisor` 和 `ResourceGuardian` 共同监管该任务
5. manager 返回结构化结果

## Module Design

### McpServerHost

职责：

- MCP protocol host
- tool/resource 注册
- 请求分发
- 结果与错误映射

非职责：

- 不直接执行 git/build/editor 逻辑
- 不直接采样系统资源

### EditorClient

职责：

- runtime_state 读取与校验
- HTTP client
- WebSocket event client
- editor health 检查
- 将 editor API 结果转换为 manager 内部统一结果对象

### WorkspaceOps

职责：

- 执行 `git pull`
- 执行 `cmake configure`
- 执行 `cmake --build`
- 执行测试命令
- 组织工作目录、参数和输出格式

### ProcessSupervisor

职责：

- 统一拉起/停止受管进程
- 跟踪 pid 与进程树
- 管理标准输出/标准错误收集
- 向上层暴露可观察的任务状态

### ResourceSampler

职责：

- 采样受管进程树的 CPU / RSS / IO
- 采样系统级剩余资源与负载信号
- 隔离 Linux / Windows 的平台差异

### ResourceGuardian

职责：

- 基于采样结果做阈值判断
- 识别持续超限而不是瞬时尖峰
- 触发优雅终止与后续强杀

`ResourceGuardian` 必须是独立模块，不得把这部分逻辑分散到
`WorkspaceOps` 或 `ProcessSupervisor` 中。

### KillPolicy

职责：

- 封装“先优雅终止，再强杀”的策略
- 根据任务类型决定优雅终止方式
- 在超时后升级到强制 kill 进程树

## Resource Protection

### Purpose

资源守护的目标不是性能调优，而是防止 manager 启动或管理的任务把机器拖入
不可交互状态。

第一阶段只关注 manager 自己创建和监管的进程：

- `lxe_editor`
- `cmake`
- `ninja`
- 测试进程
- 这些进程派生出的子进程

### Trigger model

不得采用“瞬时超线立即 kill”的策略。第一阶段应采用“持续超限”模型，并结合
系统级危险信号，降低误杀概率。

建议纳入的信号：

- 进程树 RSS 异常增长，且系统剩余内存逼近危险线
- 进程树持续占满多个 CPU 核心，且持续时间超过阈值
- 进程树持续高 IO，且磁盘忙碌度接近危险区间

只有当“受管进程自身异常占用”与“整机资源逼近危险线”同时满足时，才进入熔断。

### Response policy

默认策略固定为：

1. 优雅终止
2. 短超时等待释放资源
3. 仍未退出则强制 kill 整个进程树

不同任务的优雅终止方式可以不同：

- build/test：先终止对应命令或进程组
- editor：优先正常退出；无可用退出通道时再发终止信号

资源熔断属于预期控制行为，不应伪装成普通任务失败。

## State Model

第一阶段采用无状态优先设计。

manager 不维护复杂持久化状态，也不引入多会话表。它只保留：

- 当前进程内的受管任务句柄
- 最近一次探测/操作结果
- 当前日志路径

重启后恢复方式：

- 扫描工作区
- 读取 runtime_state 文件
- 校验 editor 存活性
- 重建最小运行态

如果未来需要支持多个 workspace、多个 editor profile 或远程主机，再引入显式
session / instance 模型。

## Error Handling

错误必须按层次清晰暴露：

- editor 未启动
  - `editor.*` MCP tools 返回明确的 `editor unavailable`
- runtime_state 缺失、过期或不匹配
  - 返回发现失败，并说明是文件缺失、端口失效还是 health check 失败
- `git` / `cmake` / `ninja` 失败
  - 返回结构化错误，包括命令、工作目录、exit code、关键 stderr
- 资源熔断触发
  - 返回明确的 `killed_by_guardian` 类原因与触发指标摘要

manager 不得把这些错误全部压扁成“command failed”。

## Testing

### Unit tests

应覆盖：

- MCP tool 到内部操作的映射
- runtime_state 解析与校验
- 命令构造
- 阈值判断与持续时间判断
- KillPolicy 状态机

### Integration tests

应构造 fake editor HTTP / WebSocket server，验证：

- `editor.get_summary`
- `editor.command`
- `editor.pick`
- `editor.wait_for`
- editor unavailable 分支

### End-to-end tests

应覆盖最小真实链路：

- 启动 manager
- `ops.editor_start`
- `editor.get_summary`
- `ops.build_target`
- 对受控假负载进程触发资源守护

第一阶段不要求在所有平台上自动完成所有 e2e，但架构必须为 Linux 与 Windows
保留清晰的适配边界。

## Migration

建议实现顺序：

1. 在仓库中引入独立 `lxe_manager` 项目骨架与 Node.js 依赖
2. 抽出 editor 发现与 HTTP / WebSocket client
3. 在 manager 中重建当前 editor MCP surface
4. 从 `lxe_editor` 删除 MCP endpoint 与相关发布逻辑
5. 新增 repo/build/editor 生命周期运维 tools
6. 新增 `ProcessSupervisor` 与 `ResourceGuardian`
7. 更新本地 helper、README 和 Codex 配置流

## Acceptance Criteria

- `lxe_editor` 不再直接暴露 MCP protocol surface。
- `lxe_editor` 继续提供 HTTP / WebSocket API 与 runtime discovery。
- `lxe_manager` 成为唯一 MCP server。
- 当前 editor 侧 MCP 能力可通过 manager + editor HTTP / WebSocket 保持可用。
- manager 可执行 `git pull`、configure/build、editor start/stop/status。
- 当受管进程持续异常占用资源并威胁整机可用性时，manager 会先优雅终止，再升级
  为强制 kill。
- manager 在 `lxe_editor` 崩溃后仍可继续运行并提供诊断与重启能力。
