$EnableScript = Join-Path $PSScriptRoot "../lxe_manager/enable_mcp.ps1"
$Token = $Env:LXE_MANAGER_MCP_BEARER_TOKEN
if ([string]::IsNullOrWhiteSpace($Token)) {
    throw "lxe_manager MCP: set LXE_MANAGER_MCP_BEARER_TOKEN or use scripts/lxe_manager/enable_mcp.ps1 -Token ..."
}
if (-not [string]::IsNullOrWhiteSpace($Env:LXE_MANAGER_URL)) {
    & $EnableScript -Endpoint $Env:LXE_MANAGER_URL -Token $Token
} else {
    & $EnableScript -Local -Token $Token
}
