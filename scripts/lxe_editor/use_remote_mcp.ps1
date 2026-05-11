param(
    [Parameter(Mandatory = $true)][string]$McpUrl,
    [Parameter(Mandatory = $true)][string]$Token
)

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ConfigPath = if ($Env:LXE_EDITOR_CODEX_CONFIG_PATH) {
    $Env:LXE_EDITOR_CODEX_CONFIG_PATH
} else {
    Join-Path $RepoRoot ".codex/config.toml"
}

$Env:LXE_EDITOR_MCP_BEARER_TOKEN = $Token
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

Write-Host "lxe_editor MCP target: remote $McpUrl"
