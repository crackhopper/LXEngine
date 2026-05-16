from __future__ import annotations

import json
import queue
import shlex
import shutil
import subprocess
import threading
import time
from typing import Any, Iterable

from scripts.notes.notes_chat_core import ChatError, ChatRequest, REPO_ROOT, build_agent_prompt


class ClaudeCliProtocol:
    def __init__(self, command: str, timeout: float) -> None:
        self.command = command
        self.timeout = timeout

    def health(self) -> dict[str, Any]:
        executable = shlex.split(self.command)[0] if self.command else "claude"
        return {
            "transport": "claude-cli",
            "command": self.command,
            "available": shutil.which(executable) is not None,
            "streaming": True,
        }

    def stream(self, request: ChatRequest) -> Iterable[str]:
        base_cmd = shlex.split(self.command)
        if not base_cmd:
            raise ChatError(500, "Claude command is empty.")
        if shutil.which(base_cmd[0]) is None:
            raise ChatError(
                503,
                f"Claude command not found: {base_cmd[0]}. "
                "Install Claude Code or set NOTES_CHAT_CLAUDE_CMD.",
            )

        cmd = [
            *base_cmd,
            "-p",
            "--verbose",
            "--output-format",
            "stream-json",
            "--include-partial-messages",
            "--permission-mode",
            "plan",
            "--tools",
            "",
            "--no-session-persistence",
        ]
        prompt = build_agent_prompt(request)
        try:
            proc = subprocess.Popen(
                cmd,
                cwd=REPO_ROOT,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
            )
        except OSError as exc:
            raise ChatError(503, f"Failed to start Claude: {exc}") from exc

        assert proc.stdin is not None
        assert proc.stdout is not None
        proc.stdin.write(prompt)
        proc.stdin.close()

        previous_text = ""
        emitted_any = False
        started_at = time.monotonic()
        try:
            for line in proc.stdout:
                if time.monotonic() - started_at > self.timeout:
                    proc.kill()
                    raise ChatError(504, f"Claude request timed out after {self.timeout:g}s.")
                line = line.strip()
                if not line:
                    continue
                try:
                    payload = json.loads(line)
                except json.JSONDecodeError:
                    continue

                text = extract_claude_stream_text(payload)
                if text:
                    if text.startswith(previous_text):
                        delta = text[len(previous_text):]
                        previous_text = text
                    else:
                        delta = text
                        previous_text += text
                    if delta:
                        emitted_any = True
                        yield delta
                    continue

                result_text = extract_claude_result_text(payload)
                if result_text and not emitted_any:
                    emitted_any = True
                    previous_text = result_text
                    yield result_text

            return_code = proc.wait(timeout=5)
        except Exception:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=5)
            raise

        if return_code != 0:
            stderr = ""
            if proc.stderr is not None:
                stderr = proc.stderr.read().strip()
            if len(stderr) > 1200:
                stderr = stderr[:1200] + "\n..."
            raise ChatError(502, f"Claude exited with code {return_code}: {stderr}")


class CliJsonProtocol:
    def __init__(self, command: str, timeout: float) -> None:
        self.command = command
        self.timeout = timeout

    def health(self) -> dict[str, Any]:
        executable = shlex.split(self.command)[0] if self.command else ""
        return {
            "transport": "cli-json",
            "command": self.command,
            "available": bool(executable and shutil.which(executable)),
            "streaming": True,
        }

    def stream(self, request: ChatRequest) -> Iterable[str]:
        base_cmd = shlex.split(self.command)
        if not base_cmd:
            raise ChatError(500, "CLI JSON command is empty.")
        if shutil.which(base_cmd[0]) is None:
            raise ChatError(503, f"CLI JSON command not found: {base_cmd[0]}.")

        try:
            proc = subprocess.Popen(
                base_cmd,
                cwd=REPO_ROOT,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except OSError as exc:
            raise ChatError(503, f"Failed to start CLI JSON agent: {exc}") from exc

        prompt = build_agent_prompt(request)
        try:
            stdout, stderr = proc.communicate(prompt, timeout=self.timeout)
        except subprocess.TimeoutExpired as exc:
            proc.kill()
            proc.communicate()
            raise ChatError(504, f"CLI JSON request timed out after {self.timeout:g}s.") from exc

        if proc.returncode != 0:
            detail = stderr.strip()
            if len(detail) > 1200:
                detail = detail[:1200] + "\n..."
            suffix = f": {detail}" if detail else ""
            raise ChatError(502, f"CLI JSON agent exited with code {proc.returncode}{suffix}")

        delta_text: list[str] = []
        final_text = ""
        for line in stdout.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                payload = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ChatError(502, f"Invalid JSONL from CLI JSON agent: {line[:200]}") from exc
            if not isinstance(payload, dict):
                continue
            text = extract_cli_json_text(payload)
            if not text:
                continue
            if is_cli_json_final(payload):
                final_text = text
            else:
                delta_text.append(text)

        text = final_text or "".join(delta_text)
        if text:
            yield text


class AcpStdioProtocol:
    def __init__(self, command: str, timeout: float) -> None:
        self.command = command
        self.timeout = timeout

    def health(self) -> dict[str, Any]:
        executable = shlex.split(self.command)[0] if self.command else ""
        return {
            "transport": "acp-stdio",
            "command": self.command,
            "available": bool(executable and shutil.which(executable)),
            "streaming": True,
        }

    def stream(self, request: ChatRequest) -> Iterable[str]:
        if not self.command:
            raise ChatError(
                503,
                "ACP backend is enabled but no command is configured. "
                "Set NOTES_CHAT_ACP_CMD or pass --chat-agent-command.",
            )

        client = AcpStdioClient(shlex.split(self.command), self.timeout)
        try:
            client.start()
            client.request(
                "initialize",
                {
                    "protocolVersion": 1,
                    "clientCapabilities": {
                        "fs": {"readTextFile": False, "writeTextFile": False},
                        "terminal": False,
                    },
                    "clientInfo": {
                        "name": "notes-chat",
                        "title": "Notes Chat",
                        "version": "0.1.0",
                    },
                },
            )
            session = client.request("session/new", {"cwd": str(REPO_ROOT), "mcpServers": []})
            session_id = session.get("sessionId") if isinstance(session, dict) else None
            if not session_id:
                raise ChatError(502, "ACP agent did not return a sessionId.")
            prompt = build_agent_prompt(request)
            yield from client.stream_request(
                "session/prompt",
                {
                    "sessionId": session_id,
                    "prompt": [{"type": "text", "text": prompt}],
                },
            )
        finally:
            client.stop()


class McpStdioProtocol:
    def __init__(
        self,
        command: list[str] | str,
        timeout: float,
        tool_candidates: list[str] | None = None,
    ) -> None:
        self.command = command
        self.timeout = timeout
        self.tool_candidates = tool_candidates or ["codex.prompt", "prompt", "chat", "codex"]

    def health(self) -> dict[str, Any]:
        command = self._command_list()
        executable = command[0] if command else ""
        return {
            "transport": "mcp-stdio",
            "command": self.command,
            "available": bool(executable and shutil.which(executable)),
            "streaming": True,
        }

    def stream(self, request: ChatRequest) -> Iterable[str]:
        prompt = build_agent_prompt(request)
        client = McpStdioClient(self._command_list(), self.timeout)
        try:
            client.start()
            client.request(
                "initialize",
                {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {},
                    "clientInfo": {
                        "name": "notes-chat",
                        "version": "0.1.0",
                    },
                },
            )
            client.notify("notifications/initialized", {})
            tools_result = client.request("tools/list", {})
            available_tools = mcp_tool_names(tools_result)
            tool_name = select_mcp_tool(available_tools, self.tool_candidates)
            if tool_name is None:
                available = ", ".join(available_tools) if available_tools else "(none)"
                raise ChatError(
                    502,
                    "No compatible Codex MCP tool found. "
                    "Expected one of: "
                    f"{', '.join(self.tool_candidates)}. "
                    f"Available tools: {available}. "
                    "Try --chat-agent codex-exec.",
                )
            result = client.request("tools/call", {"name": tool_name, "arguments": {"prompt": prompt}})
            text = extract_content_text(result)
            if text:
                yield text
        finally:
            client.stop()

    def _command_list(self) -> list[str]:
        if isinstance(self.command, str):
            return shlex.split(self.command)
        return list(self.command)


class AcpStdioClient:
    def __init__(self, command: list[str], timeout: float) -> None:
        self.command = command
        self.timeout = timeout
        self.proc: subprocess.Popen[str] | None = None
        self.messages: queue.Queue[dict[str, Any]] = queue.Queue()
        self.next_id = 1

    def start(self) -> None:
        if not self.command:
            raise ChatError(500, "ACP command is empty.")
        if shutil.which(self.command[0]) is None:
            raise ChatError(503, f"ACP command not found: {self.command[0]}.")
        self.proc = subprocess.Popen(
            self.command,
            cwd=REPO_ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        threading.Thread(target=self._read_stdout, daemon=True).start()

    def stop(self) -> None:
        proc = self.proc
        if proc is None:
            return
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)

    def request(self, method: str, params: dict[str, Any]) -> Any:
        request_id = self._send_request(method, params)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            message = self._next_message(deadline)
            if message is None:
                continue
            if "method" in message and "id" in message:
                self._reject_client_request(message)
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise_acp_error(message["error"])
            return message.get("result")
        raise ChatError(504, f"ACP request '{method}' timed out after {self.timeout:g}s.")

    def stream_request(self, method: str, params: dict[str, Any]) -> Iterable[str]:
        request_id = self._send_request(method, params)
        deadline = time.monotonic() + self.timeout
        emitted = False
        while time.monotonic() < deadline:
            message = self._next_message(deadline)
            if message is None:
                continue
            if "method" in message and "id" in message:
                self._reject_client_request(message)
                continue
            if message.get("method") == "session/update":
                text = extract_acp_update_text(message.get("params"))
                if text:
                    emitted = True
                    yield text
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise_acp_error(message["error"])
            if not emitted:
                result_text = extract_content_text(message.get("result"))
                if result_text:
                    yield result_text
            return
        raise ChatError(504, f"ACP request '{method}' timed out after {self.timeout:g}s.")

    def _send_request(self, method: str, params: dict[str, Any]) -> int:
        proc = self.proc
        if proc is None or proc.stdin is None:
            raise ChatError(500, "ACP process is not running.")
        request_id = self.next_id
        self.next_id += 1
        payload = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params,
        }
        proc.stdin.write(json.dumps(payload, separators=(",", ":")) + "\n")
        proc.stdin.flush()
        return request_id

    def _next_message(self, deadline: float) -> dict[str, Any] | None:
        proc = self.proc
        if proc is None:
            raise ChatError(500, "ACP process is not running.")
        if proc.poll() is not None:
            stderr = self._read_stderr()
            raise ChatError(502, f"ACP agent exited early: {stderr}")
        remaining = max(0.1, deadline - time.monotonic())
        try:
            return self.messages.get(timeout=min(0.5, remaining))
        except queue.Empty:
            return None

    def _read_stdout(self) -> None:
        proc = self.proc
        if proc is None or proc.stdout is None:
            return
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                self.messages.put(json.loads(line))
            except json.JSONDecodeError:
                self.messages.put(
                    {
                        "jsonrpc": "2.0",
                        "error": {
                            "code": -32700,
                            "message": f"Invalid ACP JSON line: {line[:200]}",
                        },
                        "id": None,
                    }
                )

    def _read_stderr(self) -> str:
        proc = self.proc
        if proc is None or proc.stderr is None:
            return ""
        try:
            data = proc.stderr.read()
        except OSError:
            return ""
        return data.strip()[:1200]

    def _reject_client_request(self, message: dict[str, Any]) -> None:
        proc = self.proc
        if proc is None or proc.stdin is None:
            return
        response = {
            "jsonrpc": "2.0",
            "id": message.get("id"),
            "error": {
                "code": -32601,
                "message": "notes-chat is read-only and exposes no client methods",
            },
        }
        proc.stdin.write(json.dumps(response, separators=(",", ":")) + "\n")
        proc.stdin.flush()


class McpStdioClient:
    def __init__(self, command: list[str], timeout: float) -> None:
        self.command = command
        self.timeout = timeout
        self.proc: subprocess.Popen[bytes] | None = None
        self.messages: queue.Queue[dict[str, Any]] = queue.Queue()
        self.next_id = 1
        self.stderr_buffer = bytearray()
        self.stderr_lock = threading.Lock()
        self.stderr_thread: threading.Thread | None = None

    def start(self) -> None:
        if not self.command:
            raise ChatError(502, "MCP command is empty.")
        if shutil.which(self.command[0]) is None:
            raise ChatError(502, f"MCP command not found: {self.command[0]}.")
        try:
            self.proc = subprocess.Popen(
                self.command,
                cwd=REPO_ROOT,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=0,
            )
        except OSError as exc:
            raise ChatError(502, f"Failed to start MCP server: {exc}") from exc
        threading.Thread(target=self._read_stdout, daemon=True).start()
        self.stderr_thread = threading.Thread(target=self._drain_stderr, daemon=True)
        self.stderr_thread.start()

    def stop(self) -> None:
        proc = self.proc
        if proc is None:
            return
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)
        for stream in (proc.stdin, proc.stdout, proc.stderr):
            if stream is not None and not stream.closed:
                stream.close()

    def request(self, method: str, params: dict[str, Any]) -> Any:
        request_id = self._send_message(method, params, include_id=True)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            message = self._next_message(deadline)
            if message is None:
                continue
            if "method" in message and "id" in message:
                self._reject_server_request(message)
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise_mcp_error(message["error"])
            return message.get("result")
        raise ChatError(504, f"MCP request '{method}' timed out after {self.timeout:g}s.")

    def notify(self, method: str, params: dict[str, Any]) -> None:
        self._send_message(method, params, include_id=False)

    def _send_message(self, method: str, params: dict[str, Any], include_id: bool) -> int | None:
        proc = self.proc
        if proc is None or proc.stdin is None:
            raise ChatError(502, "MCP process is not running.")
        request_id: int | None = None
        payload: dict[str, Any] = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params,
        }
        if include_id:
            request_id = self.next_id
            self.next_id += 1
            payload["id"] = request_id
        write_mcp_message(proc.stdin, payload)
        proc.stdin.flush()
        return request_id

    def _next_message(self, deadline: float) -> dict[str, Any] | None:
        proc = self.proc
        if proc is None:
            raise ChatError(502, "MCP process is not running.")
        if proc.poll() is not None:
            stderr = self._read_stderr()
            detail = f": {stderr}" if stderr else ""
            raise ChatError(502, f"MCP server exited early{detail}")
        remaining = max(0.1, deadline - time.monotonic())
        try:
            return self.messages.get(timeout=min(0.5, remaining))
        except queue.Empty:
            return None

    def _read_stdout(self) -> None:
        proc = self.proc
        if proc is None or proc.stdout is None:
            return
        while True:
            line = proc.stdout.readline()
            if line == b"":
                return
            line = line.strip()
            if not line:
                continue
            if line.lower().startswith(b"content-length:"):
                message = self._read_content_length_message(line)
            else:
                message = line.decode("utf-8", errors="replace")
            if not message:
                continue
            try:
                self.messages.put(json.loads(message))
            except json.JSONDecodeError:
                self.messages.put(
                    {
                        "jsonrpc": "2.0",
                        "error": {
                            "code": -32700,
                            "message": f"Invalid MCP JSON message: {message[:200]}",
                        },
                        "id": None,
                    }
                )

    def _read_content_length_message(self, first_header: bytes) -> str:
        proc = self.proc
        if proc is None or proc.stdout is None:
            return ""
        length = parse_content_length(first_header)
        while True:
            header = proc.stdout.readline()
            if header in {b"", b"\n", b"\r\n"}:
                break
            if header.lower().startswith(b"content-length:"):
                length = parse_content_length(header.strip())
        if length <= 0:
            return ""
        data = read_exact_bytes(proc.stdout, length)
        if data is None:
            return ""
        return data.decode("utf-8", errors="replace")

    def _read_stderr(self) -> str:
        if self.stderr_thread is not None:
            self.stderr_thread.join(timeout=0.2)
        with self.stderr_lock:
            data = bytes(self.stderr_buffer)
        return data.decode("utf-8", errors="replace").strip()[:1200]

    def _drain_stderr(self) -> None:
        proc = self.proc
        if proc is None or proc.stderr is None:
            return
        while True:
            try:
                chunk = proc.stderr.read(4096)
            except OSError:
                return
            if not chunk:
                return
            with self.stderr_lock:
                self.stderr_buffer.extend(chunk)
                if len(self.stderr_buffer) > 1200:
                    del self.stderr_buffer[:-1200]

    def _reject_server_request(self, message: dict[str, Any]) -> None:
        proc = self.proc
        if proc is None or proc.stdin is None:
            return
        response = {
            "jsonrpc": "2.0",
            "id": message.get("id"),
            "error": {
                "code": -32601,
                "message": "notes-chat does not expose MCP client methods",
            },
        }
        write_mcp_message(proc.stdin, response)
        proc.stdin.flush()


def write_mcp_message(stream: Any, payload: dict[str, Any]) -> None:
    body = json.dumps(payload, separators=(",", ":"))
    body_bytes = body.encode("utf-8")
    header = f"Content-Length: {len(body_bytes)}\r\n\r\n".encode("ascii")
    stream.write(header + body_bytes)


def read_exact_bytes(stream: Any, length: int) -> bytes | None:
    chunks: list[bytes] = []
    remaining = length
    while remaining > 0:
        chunk = stream.read(remaining)
        if not chunk:
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def parse_content_length(header: bytes) -> int:
    _, _, value = header.partition(b":")
    try:
        return int(value.strip())
    except ValueError:
        return 0


def mcp_tool_names(tools_result: Any) -> list[str]:
    tools = tools_result.get("tools") if isinstance(tools_result, dict) else tools_result
    if not isinstance(tools, list):
        return []
    return sorted({
        tool.get("name")
        for tool in tools
        if isinstance(tool, dict) and isinstance(tool.get("name"), str)
    })


def select_mcp_tool(available: list[str], tool_candidates: list[str]) -> str | None:
    available_set = set(available)
    for candidate in tool_candidates:
        if candidate in available_set:
            return candidate
    return None


def raise_mcp_error(error: Any) -> None:
    if isinstance(error, dict):
        raise ChatError(502, f"MCP error: {error.get('message', error)}")
    raise ChatError(502, f"MCP error: {error}")


def raise_acp_error(error: Any) -> None:
    if isinstance(error, dict):
        raise ChatError(502, f"ACP error: {error.get('message', error)}")
    raise ChatError(502, f"ACP error: {error}")


def extract_acp_update_text(params: Any) -> str:
    if not isinstance(params, dict):
        return ""
    update = params.get("update")
    if not isinstance(update, dict):
        return ""
    if update.get("sessionUpdate") != "agent_message_chunk":
        return ""
    return extract_content_text(update.get("content"))


def extract_content_text(content: Any) -> str:
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "".join(extract_content_text(item) for item in content)
    if isinstance(content, dict):
        if isinstance(content.get("text"), str):
            return content["text"]
        if isinstance(content.get("content"), str):
            return content["content"]
        if isinstance(content.get("delta"), str):
            return content["delta"]
        if isinstance(content.get("message"), dict):
            return extract_content_text(content["message"])
        if isinstance(content.get("resource"), dict):
            return extract_content_text(content["resource"])
        if isinstance(content.get("content"), list):
            return extract_content_text(content["content"])
    return ""


def is_cli_json_final(payload: dict[str, Any]) -> bool:
    return payload.get("type") == "result" or "result" in payload or payload.get("final") is True


def extract_cli_json_text(payload: dict[str, Any]) -> str:
    if payload.get("type") == "result":
        return extract_content_text(payload.get("result"))
    if "result" in payload:
        return extract_content_text(payload.get("result"))
    if isinstance(payload.get("delta"), str):
        return payload["delta"]
    if isinstance(payload.get("message"), str):
        return payload["message"]
    if isinstance(payload.get("text"), str):
        return payload["text"]
    return extract_content_text(payload.get("message"))


def extract_claude_stream_text(payload: dict[str, Any]) -> str:
    if payload.get("type") == "stream_event" and isinstance(payload.get("event"), dict):
        event = payload["event"]
        if event.get("type") == "content_block_delta":
            return extract_content_text(event.get("delta"))
        if event.get("type") == "content_block_start":
            return extract_content_text(event.get("content_block"))
    if payload.get("type") == "assistant" and isinstance(payload.get("message"), dict):
        return extract_content_text(payload["message"].get("content"))
    if payload.get("type") in {"assistant_delta", "content_block_delta"}:
        return extract_content_text(payload)
    return ""


def extract_claude_result_text(payload: dict[str, Any]) -> str:
    if payload.get("type") == "result" and isinstance(payload.get("result"), str):
        return payload["result"]
    if isinstance(payload.get("result"), dict):
        return extract_content_text(payload["result"])
    return ""
