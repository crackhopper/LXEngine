param(
    [Parameter(Mandatory = $true)]
    [string]$ManagerUrl,

    [Parameter(Mandatory = $false)]
    [string]$Token
)

$ResolvedToken = if ($Env:LXE_MANAGER_MCP_BEARER_TOKEN) {
    $Env:LXE_MANAGER_MCP_BEARER_TOKEN
} else {
    $Token
}

if (-not $ManagerUrl -or -not $ResolvedToken) {
    throw "lxe_manager MCP target: manager MCP URL and LXE_MANAGER_MCP_BEARER_TOKEN are required"
}

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ConfigPath = if ($Env:LXE_EDITOR_CODEX_CONFIG_PATH) {
    $Env:LXE_EDITOR_CODEX_CONFIG_PATH
} else {
    Join-Path $RepoRoot ".codex/config.toml"
}

$Env:LXE_MANAGER_MCP_BEARER_TOKEN = $ResolvedToken
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ConfigPath) | Out-Null
$escapedUrl = ConvertTo-Json $ManagerUrl -Compress
$block = "[mcp_servers.lxe_manager]`nurl = $escapedUrl`nbearer_token_env_var = `"LXE_MANAGER_MCP_BEARER_TOKEN`"`n"
$text = if (Test-Path $ConfigPath) { Get-Content $ConfigPath -Raw } else { "" }
$pattern = "(?ms)^\[mcp_servers\.lxe_manager\]`n.*?(?=^\[|\z)"
if ($text -match $pattern) {
    $text = [regex]::Replace($text, $pattern, $block)
} elseif ($text.Trim()) {
    $text = $text.TrimEnd() + "`n`n" + $block
} else {
    $text = $block
}
if (-not $text.EndsWith("`n")) {
    $text += "`n"
}
Set-Content -Encoding UTF8 $ConfigPath $text

Write-Host "lxe_manager MCP target: remote $ManagerUrl"
