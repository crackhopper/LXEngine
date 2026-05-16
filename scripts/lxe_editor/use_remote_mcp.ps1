if ($args.Count -ne 1) {
    throw "usage: `$Env:LXE_MANAGER_MCP_BEARER_TOKEN = '<token>'; scripts/lxe_editor/use_remote_mcp.ps1 <manager-mcp-url>"
}
$EnableScript = Join-Path $PSScriptRoot "../lxe_manager/enable_mcp.ps1"
$Token = $Env:LXE_MANAGER_MCP_BEARER_TOKEN
if ([string]::IsNullOrWhiteSpace($Token)) {
    throw "lxe_manager MCP: set LXE_MANAGER_MCP_BEARER_TOKEN or use scripts/lxe_manager/enable_mcp.ps1 -Token ..."
}
& $EnableScript -Endpoint $args[0] -Token $Token
