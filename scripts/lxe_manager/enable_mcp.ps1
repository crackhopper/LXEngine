param(
    [switch]$Local,
    [string]$Endpoint,
    [Parameter(Mandatory = $true)]
    [string]$Token
)

if ($Local.IsPresent -and -not [string]::IsNullOrWhiteSpace($Endpoint)) {
    throw "enable_mcp: use only one of -Local or -Endpoint"
}

$ScriptDir = $PSScriptRoot
$RepoRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$ConfigPath = if ($Env:LXE_EDITOR_CODEX_CONFIG_PATH) {
    $Env:LXE_EDITOR_CODEX_CONFIG_PATH
} else {
    Join-Path $RepoRoot ".codex/config.toml"
}

$Writer = Join-Path $ScriptDir "enable_mcp_write_config.py"

$mode = "inherit"
$setUrl = $null
if ($Local.IsPresent) {
    $mode = "local"
    $setUrl = "http://127.0.0.1:3880/mcp"
} elseif (-not [string]::IsNullOrWhiteSpace($Endpoint)) {
    $mode = "endpoint"
    $setUrl = $Endpoint
}

if ($mode -eq "inherit") {
    & python3 $Writer --config $ConfigPath --keep-url
} else {
    & python3 $Writer --config $ConfigPath --set-url $setUrl
}
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($mode -eq "local") {
    Write-Host "lxe_manager MCP target: local $setUrl"
} elseif ($mode -eq "endpoint") {
    Write-Host "lxe_manager MCP target: remote $setUrl"
} else {
    Write-Host "lxe_manager MCP target: keep existing url in $ConfigPath"
}

$Env:LXE_MANAGER_MCP_BEARER_TOKEN = $Token
