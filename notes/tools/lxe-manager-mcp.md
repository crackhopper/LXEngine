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
# 必须用 source，不能用 ./enable_mcp.sh（子 shell 里的 export Codex 看不到）
source scripts/lxe_manager/enable_mcp.sh --local
codex
```

`--local` 可省略 `--token`：脚本生成 token、写入当前 shell 的
`LXE_MANAGER_MCP_BEARER_TOKEN`，在 stderr 打印 `export LXE_MANAGER_MCP_BEARER_TOKEN=...`，
更新 `.codex/config.toml` 中的本机 URL。若本机 MCP 端口（`LXE_MANAGER_PORT`，默认
3880）已被占用，会先用 `fuser` / `lsof` **结束监听进程**，再后台启动
`start_mcp.sh` 并传入**同一 token**。若只想改配置、自己起 manager，用
`--no-start-manager`。

仅关闭本机 manager、不改 `.codex/config.toml`（**不必** `source`）：

```bash
./scripts/lxe_manager/enable_mcp.sh --stop-manager
```

`--stop-manager` 不要与 `--local` 同用。

PowerShell:

```powershell
cd C:\path\to\LXEngine
scripts/lxe_manager/enable_mcp.ps1 -Local
codex
```

`-Local` 时 `-Token` 可选；`-NoStartManager` 对应 `--no-start-manager`；
`-StopManager` 仅停止监听（与上面的 `--stop-manager` 一致）。

远程 Codex 客户端：

```bash
cd /home/lixiang/proj/LXEngine
source scripts/lxe_manager/enable_mcp.sh --endpoint "http://<server-ip>:3880/mcp" --token "<token-from-manager-output>"
codex
```

PowerShell:

```powershell
scripts/lxe_manager/enable_mcp.ps1 -Endpoint "http://<server-ip>:3880/mcp" -Token "<token-from-manager-output>"
codex
```

若不想改动 `.codex/config.toml` 里已有的 `url`，只更新当前 shell 的 token：

```bash
source scripts/lxe_manager/enable_mcp.sh --token "<token-from-manager-output>"
```

`enable_mcp` 只把 token **环境变量名**写进 `.codex/config.toml`；真实 token 由
脚本导出为 `LXE_MANAGER_MCP_BEARER_TOKEN`。

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
| `debug_image_prepare` | 先把远端调试图片缩成待传输小图，默认最长边 160px、base64 低于 100KB，只返回元数据和备份路径，不直接发送图片内容 |
| `debug_image_read` | 读取 `debug_image_prepare` 生成的小图；兼容直接传 `path` 的旧用法，但调试流程应优先走 prepare、用户确认、read 三步 |
| `lxe-editor://summary` 等资源 | 暴露 editor 状态快照 |

录制能力以调试复现为目标，不追求逐帧输入确定性。第一版主要记录语义命令和
MCP 来源操作；后续可以在 editor 内部继续扩展 toolbar、pick、drag 和输入摘要。

调试图片读取默认分两步。Codex 先调用 `debug_image_prepare`，manager 会在远端读取
原图、生成最长边受限的小图，并把这份待发送图保存到
`data/debug/mcp_image_cache/`。Codex 只把元数据和 `backupPath` 告诉用户；用户确认
这就是要看的图之后，再调用 `debug_image_read` 并传入同一个 `preparedPath`。这样
用户和 Codex 看到的是完全同一份小图，也避免未确认时把图片内容直接塞进上下文。

## Codex Skill 套装

我们把 repo-local Codex skills 收敛成 `lxe-` 前缀的一组入口。`lxe-debug`
承担 MCP 调试主入口：状态读取、轻量 command、pick、wait-for、命令语法查证和
build identity 判断都放在这里；进程、拉取和构建仍交给 `lxe-manager-ops`。

| Skill | 何时使用 | 主要 MCP 面 |
|---|---|---|
| `lxe-help` | 需要选择或解释 lxe skill 家族 | 当前 skill 索引 |
| `lxe-manager-ops` | 需要查询 editor 是否启动，或需要 stop/start、pull、configure、build、查日志、处理资源守护失败 | `ops.editor_*`、`ops.repo_pull`、`ops.build_*` |
| `lxe-debug` | 需要诊断用户遇到的 editor 问题，读取状态、查 command 语法、轻量 command、pick、wait-for 或 build identity | `lxe_editor_*`、`lxe-editor://...`、必要时读取 `ops.editor_logs` / `ops.build_state` |
| `lxe-recording` | 需要录制、读取、回放或 probe 调试录制文件 | `recording_*` |
| `lxe-use-case-runner` | 需要执行 `notes/use_cases/lxe_editor/` 下的复杂业务场景 | 组合使用状态、command、recording、ops skills |
| `lxe-remote-refresh-restore` | 需要保留当前场景并远端 pull / build / restart / reload | `ops.*` + `lxe_editor_command` |
| `lxe-verify-implement` | 需要证明实现已推送、远端拉取、构建、启动、命令 smoke，并提示用户目检 | `ops.*` + `lxe_editor_*` |

典型顺序是先用 `lxe-debug` 读取 summary / cameras / selection，并在需要时比对
运行中的 editor commit 或 `ops.build_state`。如果版本不匹配，再切到
`lxe-manager-ops` 完成停止、拉取、构建和启动。当问题需要复现证据时，再加载
`lxe-recording`；复杂业务路径使用 `lxe-use-case-runner`。

如果任何 editor-facing 工具返回 `editor_unavailable`，我们先切到
`lxe-manager-ops` 调 `ops.editor_status`。`{ "running": false }` 表示 manager
可达但没有启动 editor；这时不应继续调用录制、debug 或 build-info 工具，除非
随后用 `ops.editor_start` 启动了 editor。

远端实现验证使用 `lxe-verify-implement` 作为总入口。默认由 Codex 通过 MCP 完成
push / pull / build / start / smoke / visual-check prompt；只有修改了 manager MCP
server 自身工具注册或 manager 无法热加载的代码时，才需要用户协助重启 MCP
server。`ops.build_state` 是判断远端最近一次 build 对应 Git 版本的首选事实来源，
不需要为了刷新 editor 编译宏而每次重新 configure。

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
`notes/use_cases/lxe_editor/*.md`，由 `lxe-use-case-runner` 读取后调用
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
