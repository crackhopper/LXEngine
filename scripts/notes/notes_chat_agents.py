from __future__ import annotations

import os
from typing import Any, Iterable, Protocol

from scripts.notes.notes_chat_core import ChatError, ChatRequest
from scripts.notes.notes_chat_protocols import AcpStdioProtocol, ClaudeCliProtocol, CliJsonProtocol, McpStdioProtocol


class AgentAdapter:
    name = "agent"

    def answer(self, request: ChatRequest) -> str:
        return "".join(self.stream(request)).strip()

    def stream(self, request: ChatRequest) -> Iterable[str]:
        raise NotImplementedError

    def health(self) -> dict[str, Any]:
        return {"name": self.name}


class AgentProtocol(Protocol):
    def stream(self, request: ChatRequest) -> Iterable[str]:
        ...

    def health(self) -> dict[str, Any]:
        ...


class ProtocolAgentAdapter(AgentAdapter):
    def __init__(self, name: str, protocol: AgentProtocol) -> None:
        self.name = name
        self.protocol = protocol

    def stream(self, request: ChatRequest) -> Iterable[str]:
        yield from self.protocol.stream(request)

    def health(self) -> dict[str, Any]:
        return {"name": self.name, **self.protocol.health()}


def make_adapter(agent: str, command: str | None, timeout: float) -> AgentAdapter:
    if agent == "codex":
        protocol = McpStdioProtocol(command or os.environ.get("NOTES_CHAT_CODEX_CMD", "codex mcp-server"), timeout)
        return ProtocolAgentAdapter("codex", protocol)
    if agent == "codex-exec":
        protocol = CliJsonProtocol(
            command
            or os.environ.get(
                "NOTES_CHAT_CODEX_EXEC_CMD",
                "codex exec --json --ephemeral --sandbox read-only --ask-for-approval never",
            ),
            timeout,
        )
        return ProtocolAgentAdapter("codex-exec", protocol)
    if agent == "claude":
        protocol = ClaudeCliProtocol(command or os.environ.get("NOTES_CHAT_CLAUDE_CMD", "claude"), timeout)
        return ProtocolAgentAdapter("claude", protocol)
    if agent == "acp":
        protocol = AcpStdioProtocol(command or os.environ.get("NOTES_CHAT_ACP_CMD", ""), timeout)
        return ProtocolAgentAdapter("acp", protocol)
    raise ChatError(500, f"Unsupported agent backend: {agent}")
