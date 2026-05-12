$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$RuntimeRoot = if ($Env:LXE_EDITOR_RUNTIME_ROOT) {
    $Env:LXE_EDITOR_RUNTIME_ROOT
} elseif ($Env:LX_RUNTIME_ROOT) {
    $Env:LX_RUNTIME_ROOT
} else {
    $RepoRoot
}
$ConfigPath = if ($Env:LXE_EDITOR_CODEX_CONFIG_PATH) {
    $Env:LXE_EDITOR_CODEX_CONFIG_PATH
} else {
    Join-Path $RepoRoot ".codex/config.toml"
}
$RuntimeStatePath = Join-Path $RuntimeRoot "data/lxe_editor/runtime_state.yaml"

function Get-DefaultExecutable {
    param([string]$Root)

    $Candidates = @(
        (Join-Path $Root "build/src/demos/lxe_editor/lxe_editor.exe"),
        (Join-Path $Root "build/src/demos/lxe_editor/lxe_editor"),
        (Join-Path $Root "build-release/src/demos/lxe_editor/lxe_editor.exe"),
        (Join-Path $Root "build-release/src/demos/lxe_editor/lxe_editor")
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path $Candidate) {
            return $Candidate
        }
    }
    return $null
}

function Read-RuntimeStateParts {
    param([string]$Path)

    python3 -c @"
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
values = {}
for raw_line in path.read_text(encoding='utf-8').splitlines():
    line = raw_line.strip()
    if not line or line.startswith('#') or ':' not in line:
        continue
    key, value = line.split(':', 1)
    values[key.strip()] = value.strip().strip("'\"")

url = values.get('mcpUrl', '').strip()
token_file = values.get('tokenFile', '').strip()
print(url)
print(token_file)
"@ $Path
}

function Wait-ForRuntimeState {
    param([string]$Path, [double]$TimeoutS)

    $Deadline = (Get-Date).AddSeconds($TimeoutS)
    while ((Get-Date) -lt $Deadline) {
        if (Test-Path $Path) {
            return $true
        }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Start-LocalEditor {
    param([string]$Root, [string]$RuntimeRootValue)

    $Executable = if ($Env:LXE_EDITOR_EXECUTABLE) {
        $Env:LXE_EDITOR_EXECUTABLE
    } else {
        Get-DefaultExecutable $Root
    }
    if (-not $Executable) {
        throw "lxe_editor MCP target: runtime state is missing and no local lxe_editor executable was found. Set LXE_EDITOR_EXECUTABLE or build build/src/demos/lxe_editor/lxe_editor first."
    }
    if (-not (Test-Path $Executable)) {
        throw "lxe_editor MCP target: configured lxe_editor executable is not runnable ($Executable)"
    }

    $LogPath = if ($Env:LXE_EDITOR_AUTOSTART_LOG) {
        $Env:LXE_EDITOR_AUTOSTART_LOG
    } else {
        Join-Path $RuntimeRootValue "data/lxe_editor/autostart.log"
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null

    $ArgumentList = @()
    if ($Env:LXE_EDITOR_AUTOSTART_ARGS) {
        $ArgumentList = @($Env:LXE_EDITOR_AUTOSTART_ARGS)
    }

    $PreviousRuntimeRoot = $Env:LX_RUNTIME_ROOT
    try {
        $Env:LX_RUNTIME_ROOT = $RuntimeRootValue
        Start-Process -FilePath $Executable -ArgumentList $ArgumentList -RedirectStandardOutput $LogPath -RedirectStandardError $LogPath | Out-Null
    } finally {
        if ($null -eq $PreviousRuntimeRoot) {
            Remove-Item Env:LX_RUNTIME_ROOT -ErrorAction SilentlyContinue
        } else {
            $Env:LX_RUNTIME_ROOT = $PreviousRuntimeRoot
        }
    }
}

if (-not (Test-Path $RuntimeStatePath)) {
    Start-LocalEditor $RepoRoot $RuntimeRoot
    $TimeoutS = if ($Env:LXE_EDITOR_AUTOSTART_TIMEOUT_S) {
        [double]$Env:LXE_EDITOR_AUTOSTART_TIMEOUT_S
    } else {
        15.0
    }
    if (-not (Wait-ForRuntimeState $RuntimeStatePath $TimeoutS)) {
        throw "lxe_editor MCP target: timed out waiting for runtime state at $RuntimeStatePath after auto-start"
    }
}

$state = Read-RuntimeStateParts $RuntimeStatePath

$parts = $state -split "`r?`n"
$McpUrl = $parts[0].Trim()
$TokenFile = $parts[1].Trim()

if (-not $McpUrl) {
    throw "lxe_editor MCP target: runtime state does not provide mcpUrl"
}
if (-not $TokenFile -or -not (Test-Path $TokenFile)) {
    if ($TokenFile.EndsWith("automation_token.txt")) {
        throw "lxe_editor MCP target: runtime state still points at legacy automation token ($TokenFile); restart with the current lxe_editor build so it writes api_token.txt"
    }
    throw "lxe_editor MCP target: token file is missing ($TokenFile)"
}

$Env:LXE_EDITOR_MCP_BEARER_TOKEN = (Get-Content $TokenFile -Raw).Trim()
if (-not $Env:LXE_EDITOR_MCP_BEARER_TOKEN) {
    throw "lxe_editor MCP target: token file is empty ($TokenFile)"
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ConfigPath) | Out-Null
@"
model = "gpt-5.4"
model_reasoning_effort = "medium"
approval_policy = "never"
sandbox_mode = "danger-full-access"
trust_level = "trusted"

[mcp_servers.lxe_editor]
url = "$McpUrl"
bearer_token_env_var = "LXE_EDITOR_MCP_BEARER_TOKEN"
"@ | Set-Content -Encoding UTF8 $ConfigPath

Write-Host "lxe_editor MCP target: local $McpUrl"
