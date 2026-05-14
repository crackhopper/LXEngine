# lxe_manager MCP 服务

`lxe_manager` 是给 Codex 使用的后台 MCP 服务。我们把它当作
`lxe_editor` 的管家：它自己常驻运行，通过 editor HTTP API 读取状态，
也能按需启动 / 停止 editor、执行 `git pull` 和 CMake build。

## 服务边界

| 部分 | 作用 |
|---|---|
| `lxe_editor` | 只提供 HTTP / WebSocket API，并写出 `runtime_state.yaml` |
| `lxe_manager` | 提供 `/mcp`，动态发现 editor HTTP API，并管理后台进程 |
| Codex MCP 配置 | 指向 `lxe_manager`，token 从 manager 启动输出复制到客户端环境变量 |

`lxe_manager` 默认只监听 `127.0.0.1:3880`。如果我们需要让另一台机器访问
MCP，需要显式传 `--host 0.0.0.0` 或内网 IP。manager 会在启动时生成并打印
bearer token，也可以用环境变量或参数提供固定 token。

## 启动服务

先准备 Node 依赖和构建产物：

```bash
cd /home/lixiang/proj/LXEngine
npm --prefix tools/lxe_manager install
npm --prefix tools/lxe_manager run build
```

本机访问：

```sh
scripts/lxe_manager/start_mcp.sh
```

PowerShell:

```powershell
scripts/lxe_manager/start_mcp.ps1
```

远程访问：

```sh
scripts/lxe_manager/start_mcp.sh 0.0.0.0 3880
```

PowerShell:

```powershell
scripts/lxe_manager/start_mcp.ps1 0.0.0.0 3880
```

我们也可以传完整参数名，例如 `scripts/lxe_manager/start_mcp.sh --host
0.0.0.0 --port 3880 --token <token>`。不过位置参数形式在不同 shell 下更稳定：
第一个参数是 host，第二个参数是 port，第三个可选参数是 token。

启动输出会打印 `endpoint`、`bearerTokenEnvVar`、`bearerToken` 和
`bearerTokenGenerated`。上面的 manager 进程会占用当前终端；我们通常在另一个
终端里配置 Codex。

启动脚本会把 manager wrapper 与 Node 子进程输出追加到
`data/lxe_manager/mcp.log`。如果需要改日志位置，可以设置
`LXE_MANAGER_MCP_LOG_FILE`。这个文件用于排查 manager 启动失败、
`ops.manager_restart` 后没有重新监听、Node/tsx 启动错误等问题。

## 连接 Codex

本机 Codex：

```bash
cd /home/lixiang/proj/LXEngine
export LXE_MANAGER_MCP_BEARER_TOKEN="<token-from-manager-output>"
source scripts/lxe_manager/use_local_mcp.sh
codex
```

PowerShell:

```powershell
$Env:LXE_MANAGER_MCP_BEARER_TOKEN = "<token-from-manager-output>"
scripts/lxe_manager/use_local_mcp.ps1
codex
```

远程 Codex 客户端：

```bash
cd /home/lixiang/proj/LXEngine
export LXE_MANAGER_MCP_BEARER_TOKEN="<token-from-manager-output>"
source scripts/lxe_manager/use_remote_mcp.sh http://<server-ip>:3880/mcp
codex
```

PowerShell:

```powershell
$Env:LXE_MANAGER_MCP_BEARER_TOKEN = "<token-from-manager-output>"
scripts/lxe_manager/use_remote_mcp.ps1 http://<server-ip>:3880/mcp
codex
```

`use_remote_mcp.sh` 只把 token 名称写进 `.codex/config.toml`，真实 token
保留在当前 shell 的 `LXE_MANAGER_MCP_BEARER_TOKEN` 中。

## 常用启动参数

| 参数 | 环境变量 | 默认值 | 用途 |
|---|---|---|---|
| `--host` / `--mcp-host` | `LXE_MANAGER_HOST` | `127.0.0.1` | MCP HTTP bind 地址 |
| `--port` / `--mcp-port` | `LXE_MANAGER_PORT` | `3880` | MCP HTTP 端口 |
| `--repo-root` | `LXE_MANAGER_REPO_ROOT` | 自动发现 | LXEngine 仓库根目录 |
| `--runtime-root` | `LXE_MANAGER_RUNTIME_ROOT` | repo root | editor 运行时数据根目录 |
| `--editor-executable` | `LXE_MANAGER_EDITOR_EXECUTABLE` | build 内的 `lxe_editor` | editor 可执行文件 |
| `--token` / `--bearer-token` | `LXE_MANAGER_MCP_BEARER_TOKEN` | 自动生成 | MCP bearer token |

`--token` 主要用于一次性本地调试。远程运行时更推荐
`LXE_MANAGER_MCP_BEARER_TOKEN` 或自动生成的启动输出，避免 token 出现在 shell
history 或进程列表里。

## MCP 能力

| 工具 / 资源 | 用途 |
|---|---|
| `lxe_editor_command` | 向 editor command bus 发送命令 |
| `lxe_editor_get_summary` | 读取场景、dirty、模式、相机和 preview 摘要 |
| `lxe_editor_get_selection` | 读取当前选择和 pick 状态 |
| `lxe_editor_get_cameras` | 读取 editor / gameplay camera 状态 |
| `lxe_editor_pick` | 在 editor 主视图坐标执行 pick |
| `lxe_editor_wait_for` | 轮询资源直到出现指定内容 |
| `lxe_editor_ensure_running` | 做非破坏性的 editor health 检查 |
| `editor.get_build_info` / `lxe_editor_get_build_info` | 读取当前 editor 二进制的 Git 构建信息 |
| `ops.editor_start` / `ops.editor_stop` / `ops.editor_restart` / `ops.editor_status` | 管理 editor 进程 |
| `ops.repo_pull` | 在仓库根目录执行 `git pull --ff-only` |
| `ops.manager_restart` | 重启 manager MCP 服务本身，用于 `ops.repo_pull` 后应用 manager tool 变更 |
| `ops.build_configure` / `ops.build_target` | 执行 CMake configure / build |
| `ops.build_state` | 读取 manager 记录的最近一次成功 build 动作，包括 Git HEAD、dirty 状态、target 和时间 |
| `recording_enable` / `recording_disable` / `recording_status` | 控制 editor 录制开关，默认关闭 |
| `recording_start` / `recording_stop` | 开始或停止一次调试录制，可保存到 `data/lxe_editor/recordings/` |
| `recording_list` / `recording_read` | 枚举和读取已保存或 active 的录制 JSON |
| `recording_replay` / `recording_probe` | 回放录制并在失败点读取 summary、selection、cameras、toolbar 等状态 |
| `display_list` / `display_active` | 读取 editor display profile 列表和当前启动绑定 display |
| `display_config_get` / `display_config_set` / `display_select` | 读取、修改 display default/override，并设置下次启动 display |
| `lxe-editor://summary` 等资源 | 暴露 editor 状态快照 |

录制能力以调试复现为目标，不追求逐帧输入确定性。第一版主要记录语义命令和
MCP 来源操作；后续可以在 editor 内部继续扩展 toolbar、pick、drag 和输入摘要。

## Codex Skill 套装

我们把 repo-local Codex skills 严格拆开，不维护总控 skill。每个 skill 只加载
当前阶段需要的 MCP 面和工作流，避免 editor command 与 MCP 工具继续扩展后让
单个上下文过大。

| Skill | 何时使用 | 主要 MCP 面 |
|---|---|---|
| `lxe-editor-build-sync` | 需要确认远端 editor 是否由当前 Git 版本构建 | `editor.get_build_info` / `lxe_editor_get_build_info` |
| `lxe-manager-ops` | 需要查询 editor 是否启动，或需要 stop/start、pull、configure、build、查日志、处理资源守护失败 | `ops.editor_*`、`ops.repo_pull`、`ops.build_*` |
| `lxe-editor-debug` | 需要读取状态、轻量 command、pick 或 wait-for | `lxe_editor_*` 和 `lxe-editor://...` 资源 |
| `lxe-editor-recording` | 需要录制、读取、回放或 probe 调试录制文件 | `recording_*` |
| `lxe-editor-command-reference` | 需要确认 editor command 名称、参数和示例 | 当前代码里的 command 注册与解析处 |
| `lxe-editor-use-case-runner` | 需要执行 `notes/use_cases/lxe_editor/` 下的复杂业务场景 | 组合使用状态、command、recording、ops skills |

典型顺序是先用 `lxe-editor-build-sync` 确认运行中的 editor commit；如果版本不匹配，
再切到 `lxe-manager-ops` 完成停止、拉取、构建和启动。普通状态诊断只加载
`lxe-editor-debug`。当问题需要复现证据时，再加载 `lxe-editor-recording`。只有
准备发送非平凡 command 时，才加载 `lxe-editor-command-reference` 查证语法。

如果任何 editor-facing 工具返回 `editor_unavailable`，我们先切到
`lxe-manager-ops` 调 `ops.editor_status`。`{ "running": false }` 表示 manager
可达但没有启动 editor；这时不应继续调用录制、debug 或 build-info 工具，除非
随后用 `ops.editor_start` 启动了 editor。

远端修复闭环使用 `lxe-remote-fix-rebuild-retest` 作为总入口。默认由 Codex 通过
MCP 完成 stop / pull / build / start / retest；只有修改了 manager MCP server
自身工具注册或 manager 无法热加载的代码时，才需要用户协助重启 MCP server。
`ops.build_state` 是判断远端最近一次 build 对应 Git 版本的首选事实来源，不需要
为了刷新 editor 编译宏而每次重新 configure。

manager 代码变更后：

1. `ops.repo_pull`
2. `ops.manager_restart`
3. 重新连接 MCP endpoint
4. 再执行 build/editor/display 验证

`scripts/lxe_manager/start_mcp.sh` 和 `scripts/lxe_manager/start_mcp.ps1`
只负责启动 Node supervisor：`node --import tsx ./src/supervisor.ts`。
supervisor 是 manager MCP server 的父进程，负责拉起
`node --import tsx ./src/index.ts`。`ops.manager_restart` 的语义是让当前
manager child 在返回响应后以退出码 `75` 退出；supervisor 看到 `75` 后会启动
新版 supervisor 并退出，由新版 supervisor 拉起新版 manager child。这样
`tools/lxe_manager/src/` 里的 manager 与 supervisor 代码更新都能在一次
`ops.manager_restart` 后生效。

遇到 manager 重启后无法连接时，先看 `data/lxe_manager/mcp.log`，确认是否出现
`lxe_manager child exited code=75`、`starting replacement lxe_manager supervisor`
以及下一次 `starting lxe_manager`。

复杂场景验证不要临时口头编排。把稳定流程写成
`notes/use_cases/lxe_editor/*.md`，由 `lxe-editor-use-case-runner` 读取后调用
MCP 执行。当前固定录制场景是
`notes/use_cases/lxe_editor/record-complex-scene-edit.md`。

## 安全边界

- MCP 默认始终启用 bearer token；未提供 token 时由 manager 启动入口生成。
- token 不应写入 `.codex/config.toml` 或提交到仓库。
- MCP 当前是 HTTP 明文；跨不可信网络时应放在 VPN、SSH 隧道或反向代理 TLS
  后面。
- 资源守护会监控被 manager 拉起的进程；当内存、CPU 或 IO 策略触发时，
  manager 会尝试终止对应进程，避免机器被异常任务拖死。

## 继续阅读

- [lxe_editor README](../../src/demos/lxe_editor/README.md)
- [Notes 工具链说明](notes-tooling.md)
- [Phase 10 AI Agent MCP](../roadmaps/main-roadmap/phase-10-ai-agent-mcp.md)
