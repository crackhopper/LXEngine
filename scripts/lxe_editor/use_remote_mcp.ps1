param(
    [Parameter(Mandatory = $true)][string]$Host,
    [Parameter(Mandatory = $true)][int]$Port,
    [Parameter(Mandatory = $true)][string]$Token
)

$Env:LXE_EDITOR_REMOTE_MCP_HOST = $Host
$Env:LXE_EDITOR_REMOTE_MCP_PORT = "$Port"
$Env:LXE_EDITOR_REMOTE_MCP_TOKEN = $Token
Write-Host "lxe_editor MCP target: remote $Host`:$Port"
