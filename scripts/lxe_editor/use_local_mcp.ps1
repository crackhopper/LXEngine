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

if (-not (Test-Path $RuntimeStatePath)) {
    throw "lxe_editor MCP target: missing runtime state at $RuntimeStatePath"
}

$state = python3 -c @"
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
"@ $RuntimeStatePath

$parts = $state -split "`r?`n"
$McpUrl = $parts[0].Trim()
$TokenFile = $parts[1].Trim()

if (-not $McpUrl) {
    throw "lxe_editor MCP target: runtime state does not provide mcpUrl"
}
if (-not $TokenFile -or -not (Test-Path $TokenFile)) {
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
