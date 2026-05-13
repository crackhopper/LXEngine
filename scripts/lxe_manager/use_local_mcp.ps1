$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ManagerUrl = if ($Env:LXE_MANAGER_URL) {
    $Env:LXE_MANAGER_URL
} else {
    "http://127.0.0.1:3880/mcp"
}
$ConfigPath = if ($Env:LXE_EDITOR_CODEX_CONFIG_PATH) {
    $Env:LXE_EDITOR_CODEX_CONFIG_PATH
} else {
    Join-Path $RepoRoot ".codex/config.toml"
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ConfigPath) | Out-Null
@"
model = "gpt-5.4"
model_reasoning_effort = "medium"
approval_policy = "never"
sandbox_mode = "danger-full-access"
trust_level = "trusted"

[mcp_servers.lxe_manager]
url = "$ManagerUrl"
bearer_token_env_var = "LXE_MANAGER_MCP_BEARER_TOKEN"
"@ | Set-Content -Encoding UTF8 $ConfigPath

Write-Host "lxe_manager MCP target: local $ManagerUrl"
