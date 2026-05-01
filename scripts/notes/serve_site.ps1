# serve_site.ps1 — 启动本地 MkDocs 开发服务器预览 notes/
#
# 用法:
#   .\scripts\notes\serve_site.ps1                         # 默认 0.0.0.0:8110（LAN 可访问）
#   .\scripts\notes\serve_site.ps1 -Addr 127.0.0.1:8110    # 绑定指定地址:端口
#   .\scripts\notes\serve_site.ps1 -Chat                   # 同时启动只读文档 Chat 服务
#   .\scripts\notes\serve_site.ps1 -NoChat                 # 临时关闭 mkdocs.yml 中启用的 Chat
#   .\scripts\notes\serve_site.ps1 -Chat -ChatHost 192.168.1.10
#   .\scripts\notes\serve_site.ps1 -Build                  # 只 build 静态站到 .site\ 不启动服务
#   .\scripts\notes\serve_site.ps1 -ForceKill              # 检测到端口占用时自动 kill，不交互询问
#
# 特性:
#   - 启动前检测目标端口，若被占用显示占用进程并交互式询问是否 kill
#   - kill 后等待端口真正释放，失败则中止
#   - serve 模式同时启动 notes watcher，自动重建 mkdocs.gen.yml
#   - 指定 -Chat 时启动本机只读 Chat 服务（默认 Claude）
#
# 依赖: mkdocs + mkdocs-material（pipx install mkdocs-material）

[CmdletBinding()]
param(
    [string]$Addr = "0.0.0.0:8110",
    [switch]$Build,
    [switch]$ForceKill,
    [switch]$Chat,
    [switch]$NoChat,
    [string]$ChatHost = $(if ($env:NOTES_CHAT_HOST) { $env:NOTES_CHAT_HOST } else { "" }),
    [ValidateSet("claude", "acp")]
    [string]$ChatAgent = $(if ($env:NOTES_CHAT_AGENT) { $env:NOTES_CHAT_AGENT } else { "claude" }),
    [string]$ChatAgentCommand = $(if ($env:NOTES_CHAT_AGENT_COMMAND) { $env:NOTES_CHAT_AGENT_COMMAND } else { "" })
)

$ErrorActionPreference = "Stop"

# 切到项目根，无论从哪里调用
$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
Set-Location $ProjectRoot

# ---------- 基础检查 ----------

if (-not (Get-Command mkdocs -ErrorAction SilentlyContinue)) {
    Write-Error @"
mkdocs not found in PATH
安装方式:
  pipx install mkdocs-material         # 推荐
  pip install --user mkdocs-material
"@
    exit 1
}

if (-not (Test-Path "mkdocs.yml")) {
    Write-Error "mkdocs.yml not found at $PWD"
    exit 1
}

if (-not (Get-Command python -ErrorAction SilentlyContinue) -and
    -not (Get-Command python3 -ErrorAction SilentlyContinue)) {
    Write-Error "python/python3 not found in PATH"
    exit 1
}

function Invoke-GenerateSiteConfig {
    $oldChatEnabled = $env:NOTES_CHAT_ENABLED
    try {
        if ((Resolve-ChatEnabled) -and -not $Build) {
            $env:NOTES_CHAT_ENABLED = "1"
        } else {
            $env:NOTES_CHAT_ENABLED = "0"
        }

        if (Get-Command python3 -ErrorAction SilentlyContinue) {
            python3 scripts/notes/generate_site_config.py
        } else {
            python scripts/notes/generate_site_config.py
        }
    } finally {
        if ($null -eq $oldChatEnabled) {
            Remove-Item Env:NOTES_CHAT_ENABLED -ErrorAction SilentlyContinue
        } else {
            $env:NOTES_CHAT_ENABLED = $oldChatEnabled
        }
    }

    if ($LASTEXITCODE -ne 0) {
        Write-Error "generate_site_config.py failed"
        exit $LASTEXITCODE
    }
}

function Get-PythonCommand {
    if (Get-Command python3 -ErrorAction SilentlyContinue) {
        return "python3"
    }
    return "python"
}

function Get-ConfiguredChatHost {
    param([string]$PythonCmd)

    if ($ChatHost) {
        return $ChatHost
    }

    $code = @'
from pathlib import Path

try:
    import yaml

    cfg = yaml.safe_load(Path("mkdocs.yml").read_text(encoding="utf-8")) or {}
    extra = cfg.get("extra") or {}
    notes_chat = extra.get("notes_chat") or {}
    host = notes_chat.get("host") or ""
    print(host if isinstance(host, str) else "")
except Exception:
    print("")
'@
    $value = & $PythonCmd -c $code
    if ($LASTEXITCODE -eq 0 -and $value) {
        return "$value".Trim()
    }
    return "127.0.0.1"
}

function Get-ConfiguredChatEnabled {
    param([string]$PythonCmd)

    if ($env:NOTES_CHAT_ENABLED) {
        if ($env:NOTES_CHAT_ENABLED -match '^(1|true|yes|on)$') {
            return $true
        }
        return $false
    }

    $code = @'
from pathlib import Path

try:
    import yaml

    cfg = yaml.safe_load(Path("mkdocs.yml").read_text(encoding="utf-8")) or {}
    extra = cfg.get("extra") or {}
    notes_chat = extra.get("notes_chat") or {}
    enabled = notes_chat.get("enabled")
    if isinstance(enabled, bool):
        print("1" if enabled else "0")
    elif isinstance(enabled, str):
        print("1" if enabled.lower() in {"1", "true", "yes", "on"} else "0")
    else:
        print("")
except Exception:
    print("")
'@
    $value = & $PythonCmd -c $code
    return ($LASTEXITCODE -eq 0 -and "$value".Trim() -eq "1")
}

function Resolve-ChatEnabled {
    if ($Build) {
        return $false
    }
    if ($NoChat) {
        return $false
    }
    if ($Chat) {
        return $true
    }
    if (-not $script:PythonCmdForConfig) {
        $script:PythonCmdForConfig = Get-PythonCommand
    }
    return Get-ConfiguredChatEnabled -PythonCmd $script:PythonCmdForConfig
}

# ---------- --Build 模式 ----------

if ($Build) {
    Invoke-GenerateSiteConfig
    Write-Host ">> Building static site to .site\ ..." -ForegroundColor Cyan
    mkdocs build --clean -f mkdocs.gen.yml
    if ($LASTEXITCODE -ne 0) {
        Write-Error "mkdocs build failed"
        exit $LASTEXITCODE
    }
    Write-Host ""
    Write-Host "Done. 临时预览:" -ForegroundColor Green
    Write-Host "  python -m http.server --directory .site 8110"
    exit 0
}

# ---------- 端口占用检测 helpers ----------

function Get-ListenerProcessIds {
    param([int]$Port)

    # PowerShell 5.1+ / Windows 8+ 都有 Get-NetTCPConnection
    if (Get-Command Get-NetTCPConnection -ErrorAction SilentlyContinue) {
        $conns = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
        if ($conns) {
            return @($conns | Select-Object -ExpandProperty OwningProcess -Unique)
        }
        return @()
    }

    # Fallback: 解析 netstat -ano 的 LISTENING 行
    $pattern = ":$Port\s+"
    $lines = netstat -ano 2>$null | Where-Object { $_ -match "LISTENING" -and $_ -match $pattern }
    $ids = @()
    foreach ($line in $lines) {
        if ($line -match '(\d+)\s*$') {
            $ids += [int]$Matches[1]
        }
    }
    return @($ids | Select-Object -Unique)
}

function Test-PortInUse {
    param([int]$Port)
    return ((Get-ListenerProcessIds -Port $Port).Count -gt 0)
}

function Get-ProcessSummary {
    param([int]$ProcessId)
    try {
        $p = Get-Process -Id $ProcessId -ErrorAction Stop
        $path = ""
        try { $path = $p.Path } catch {}
        if ($path) {
            return "$($p.ProcessName)  ($path)"
        }
        return "$($p.ProcessName)"
    } catch {
        return "PID $ProcessId (info unavailable)"
    }
}

function Invoke-PortCheckOrPrompt {
    param([int]$Port)

    $ids = Get-ListenerProcessIds -Port $Port
    if ($ids.Count -eq 0) {
        return
    }

    Write-Host ""
    Write-Host "!! Port $Port 已被占用，监听进程:" -ForegroundColor Yellow
    foreach ($id in $ids) {
        Write-Host ("     PID {0}  {1}" -f $id, (Get-ProcessSummary -ProcessId $id))
    }
    Write-Host ""

    # 判断是否需要询问
    $doKill = $false
    if ($ForceKill) {
        Write-Host "   -ForceKill 已指定，直接 kill" -ForegroundColor Yellow
        $doKill = $true
    } else {
        $answer = Read-Host "   Kill 这些进程并继续? [y/N]"
        if ($answer -match '^(y|Y|yes|YES)$') {
            $doKill = $true
        }
    }

    if (-not $doKill) {
        Write-Host ""
        Write-Host "已取消启动。手动处理:" -ForegroundColor Red
        Write-Host ("   Stop-Process -Id {0} -Force" -f ($ids -join ','))
        exit 1
    }

    # 正常终止
    Write-Host "   sending Stop-Process..."
    foreach ($id in $ids) {
        try {
            Stop-Process -Id $id -ErrorAction Stop
        } catch {
            # 可能已经退出，忽略
        }
    }

    # 最多等 ~1.5s 让 socket 真正释放
    for ($i = 0; $i -lt 3; $i++) {
        Start-Sleep -Milliseconds 500
        if (-not (Test-PortInUse -Port $Port)) {
            Write-Host "   端口 $Port 已释放" -ForegroundColor Green
            return
        }
    }

    # 还没退 → 强制
    Write-Host "   Stop-Process 超时，尝试 -Force..." -ForegroundColor Yellow
    foreach ($id in $ids) {
        try {
            Stop-Process -Id $id -Force -ErrorAction Stop
        } catch {
        }
    }
    Start-Sleep -Milliseconds 500

    if (Test-PortInUse -Port $Port) {
        Write-Error "端口 $Port 仍然被占用，放弃启动"
        exit 1
    }
    Write-Host "   端口 $Port 已释放" -ForegroundColor Green
}

# ---------- 启动 serve ----------

$script:PythonCmdForConfig = Get-PythonCommand
$ChatEnabled = Resolve-ChatEnabled
$PortValue = [int]($Addr.Split(":")[-1])
$ReloadPort = $PortValue + 1
$ChatPort = $PortValue + 2
Invoke-GenerateSiteConfig
Invoke-PortCheckOrPrompt -Port $PortValue
Invoke-PortCheckOrPrompt -Port $ReloadPort
if ($ChatEnabled) {
    Invoke-PortCheckOrPrompt -Port $ChatPort
}

$PythonCmd = $script:PythonCmdForConfig
$ResolvedChatHost = Get-ConfiguredChatHost -PythonCmd $PythonCmd

New-Item -ItemType Directory -Force -Path ".tmp" | Out-Null
Set-Content -Path ".tmp\notes-serve.log" -Value "" -NoNewline
Set-Content -Path ".tmp\notes-watch.log" -Value "" -NoNewline
Set-Content -Path ".tmp\notes-chat.log" -Value "" -NoNewline

$WatcherArgs = @(
    "scripts/notes/watch_site_inputs.py",
    "--addr", $Addr,
    "--mkdocs-log", ".tmp/notes-serve.log",
    "--mkdocs-pid-file", ".tmp/notes-serve.pid",
    "--reload-port", "$ReloadPort"
)
if ($ChatEnabled) {
    $WatcherArgs += @(
        "--chat-host", $ResolvedChatHost,
        "--chat-port", "$ChatPort",
        "--chat-log", ".tmp/notes-chat.log",
        "--chat-pid-file", ".tmp/notes-chat.pid",
        "--chat-agent", $ChatAgent
    )
    if ($ChatAgentCommand) {
        $WatcherArgs += @("--chat-agent-command", $ChatAgentCommand)
    }
}

Write-Host ">> Starting notes supervisor on http://$Addr" -ForegroundColor Cyan
Write-Host "   docs_dir: notes\"
Write-Host "   config:   mkdocs.gen.yml"
Write-Host "   mkdocs log: .tmp\notes-serve.log"
Write-Host "   reload:  http://127.0.0.1:$ReloadPort/version"
if ($ChatEnabled) {
    Write-Host "   chat:    http://$ResolvedChatHost`:$ChatPort/health ($ChatAgent)"
    Write-Host "   chat log: .tmp\notes-chat.log"
}
Write-Host "   stop:     Ctrl-C"
Write-Host ""
$oldChatEnabled = $env:NOTES_CHAT_ENABLED
try {
    if ($ChatEnabled) {
        $env:NOTES_CHAT_ENABLED = "1"
    } else {
        $env:NOTES_CHAT_ENABLED = "0"
    }
    & $PythonCmd @WatcherArgs
} finally {
    if ($null -eq $oldChatEnabled) {
        Remove-Item Env:NOTES_CHAT_ENABLED -ErrorAction SilentlyContinue
    } else {
        $env:NOTES_CHAT_ENABLED = $oldChatEnabled
    }
}
exit $LASTEXITCODE
