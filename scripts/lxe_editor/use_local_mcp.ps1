Remove-Item Env:LXE_EDITOR_REMOTE_MCP_HOST -ErrorAction SilentlyContinue
Remove-Item Env:LXE_EDITOR_REMOTE_MCP_PORT -ErrorAction SilentlyContinue
Remove-Item Env:LXE_EDITOR_REMOTE_MCP_TOKEN -ErrorAction SilentlyContinue
Write-Host "lxe_editor MCP target: local"
