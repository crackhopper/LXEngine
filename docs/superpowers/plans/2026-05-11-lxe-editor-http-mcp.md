# lxe_editor HTTP MCP Plan

1. Extract current MCP request handling from `EditorMcpServer` into a reusable
   handler that can serve HTTP requests.
2. Add authenticated `POST /mcp` support to the existing editor HTTP API
   server.
3. Rename editor HTTP surface types and outward language from `automation` to
   `api`.
4. Update runtime-state writing and discovery to publish `mcpUrl` instead of
   raw TCP MCP host/port.
5. Replace Codex config and helper scripts with direct URL-based MCP
   configuration and local/remote environment helpers.
6. Delete TCP MCP server code and bridge scripts.
7. Replace TCP/bridge tests with direct HTTP MCP tests and rerun affected
   black-box coverage.
