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
| `ops.editor_start` / `ops.editor_stop` / `ops.editor_status` | 管理 editor 进程 |
| `ops.repo_pull` | 在仓库根目录执行 `git pull --ff-only` |
| `ops.build_configure` / `ops.build_target` | 执行 CMake configure / build |
| `lxe-editor://summary` 等资源 | 暴露 editor 状态快照 |

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
