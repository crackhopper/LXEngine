#!/usr/bin/env python3
"""Watch notes site inputs, rebuild generated config, and supervise MkDocs."""

from __future__ import annotations

import argparse
import json
import signal
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
NOTES_DIR = REPO_ROOT / "notes"
MKDOCS_SRC = REPO_ROOT / "mkdocs.yml"
NAV_CONFIG = NOTES_DIR / "nav.yml"
GENERATOR = REPO_ROOT / "scripts" / "notes" / "generate_site_config.py"
CHAT_SCRIPT = REPO_ROOT / "scripts" / "notes" / "notes_chat_server.py"
SOURCE_ANALYSIS_SCRIPT = (
    REPO_ROOT / "scripts" / "source_analysis" / "extract_sections.py"
)

IGNORED_DIR_NAMES = {
    ".git",
    ".site",
    ".tmp",
    "__pycache__",
}
IGNORED_REL_PATHS = {
    "mkdocs.gen.yml",
    "notes/tools/index.md",
    "notes/temporary/index.md",
}
WATCH_SUFFIXES = {".md", ".yml", ".yaml", ".py"}


class VersionState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._version = int(time.time())

    def bump(self) -> int:
        with self._lock:
            self._version += 1
            return self._version

    def get(self) -> int:
        with self._lock:
            return self._version


def rel_path(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def should_watch(path: Path) -> bool:
    if not path.is_file():
        return False
    if path.suffix not in WATCH_SUFFIXES:
        return False

    rel = rel_path(path)
    if rel in IGNORED_REL_PATHS:
        return False
    if any(part in IGNORED_DIR_NAMES for part in path.relative_to(REPO_ROOT).parts):
        return False

    return (
        path == MKDOCS_SRC
        or path == NAV_CONFIG
        or path == GENERATOR
        or path == SOURCE_ANALYSIS_SCRIPT
        or NOTES_DIR in path.parents
    )


def iter_watch_files() -> list[Path]:
    roots = [NOTES_DIR, REPO_ROOT / "scripts" / "notes"]
    if SOURCE_ANALYSIS_SCRIPT.parent.is_dir():
        roots.append(SOURCE_ANALYSIS_SCRIPT.parent)

    files = []
    for root in roots:
        if root.is_dir():
            files.extend(p for p in root.rglob("*") if should_watch(p))
    for path in (MKDOCS_SRC, NAV_CONFIG):
        if should_watch(path):
            files.append(path)
    return sorted(set(files), key=rel_path)


def snapshot() -> dict[str, tuple[int, int]]:
    state: dict[str, tuple[int, int]] = {}
    for path in iter_watch_files():
        try:
            stat = path.stat()
        except OSError:
            continue
        state[rel_path(path)] = (stat.st_mtime_ns, stat.st_size)
    return state


def changed_paths(
    before: dict[str, tuple[int, int]],
    after: dict[str, tuple[int, int]],
) -> list[str]:
    changed = []
    for key in sorted(set(before) | set(after)):
        if before.get(key) != after.get(key):
            changed.append(key)
    return changed


def run_generator() -> int:
    print(">> notes watcher: regenerating mkdocs.gen.yml", flush=True)
    result = subprocess.run([sys.executable, str(GENERATOR)], cwd=REPO_ROOT)
    if result.returncode != 0:
        print(
            f"!! notes watcher: generator failed with exit code {result.returncode}",
            file=sys.stderr,
            flush=True,
        )
    return result.returncode


def parse_addr(addr: str) -> tuple[str, int]:
    host, _, port = addr.rpartition(":")
    if not host or not port:
        raise ValueError(f"invalid address '{addr}', expected HOST:PORT")
    return host, int(port)


def browser_host(bind_host: str) -> str:
    if bind_host in {"0.0.0.0", "::"}:
        return "127.0.0.1"
    return bind_host


def local_url_for_addr(addr: str) -> str:
    host, port = parse_addr(addr)
    return f"http://{browser_host(host)}:{port}/"


def start_reload_server(bind_host: str, port: int, state: VersionState) -> ThreadingHTTPServer:
    class ReloadHandler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802 - stdlib hook name
            if self.path.split("?", 1)[0] != "/version":
                self.send_response(404)
                self.end_headers()
                return

            body = json.dumps({"version": state.get()}).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt: str, *args: object) -> None:
            return

    server = ThreadingHTTPServer((bind_host, port), ReloadHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    print(f">> notes reload endpoint: http://{browser_host(bind_host)}:{port}/version", flush=True)
    return server


def wait_for_mkdocs_ready(
    proc: subprocess.Popen,
    addr: str,
    timeout: float = 45.0,
) -> bool:
    url = local_url_for_addr(addr)
    deadline = time.monotonic() + timeout
    last_error = ""
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            print(
                f"!! notes watcher: mkdocs pid {proc.pid} exited before ready with code {proc.returncode}",
                file=sys.stderr,
                flush=True,
            )
            return False
        try:
            request = urllib.request.Request(url, method="HEAD")
            with urllib.request.urlopen(request, timeout=1.5) as response:
                if 200 <= response.status < 500:
                    print(f">> notes watcher: mkdocs ready at {url}", flush=True)
                    return True
        except (OSError, urllib.error.URLError, TimeoutError) as exc:
            last_error = str(exc)
            time.sleep(0.25)
    print(
        f"!! notes watcher: mkdocs pid {proc.pid} not ready after {timeout:g}s: {last_error}",
        file=sys.stderr,
        flush=True,
    )
    return False


def start_mkdocs(addr: str, log_path: Path, pid_path: Path) -> subprocess.Popen:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_file = log_path.open("ab")
    cmd = [
        "mkdocs",
        "serve",
        "--dev-addr",
        addr,
        "-f",
        "mkdocs.gen.yml",
        "-w",
        "notes",
        "-w",
        "mkdocs.gen.yml",
    ]
    proc = subprocess.Popen(
        cmd,
        cwd=REPO_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=log_file,
        stderr=subprocess.STDOUT,
    )
    log_file.close()
    pid_path.write_text(f"{proc.pid}\n", encoding="utf-8")
    print(f">> notes watcher: started mkdocs pid {proc.pid}", flush=True)
    wait_for_mkdocs_ready(proc, addr)
    return proc


def start_chat_server(
    *,
    host: str,
    port: int,
    log_path: Path,
    pid_path: Path,
    agent: str,
    agent_command: str,
) -> subprocess.Popen:
    if not CHAT_SCRIPT.is_file():
        raise FileNotFoundError(f"{CHAT_SCRIPT} not found")

    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_file = log_path.open("ab")
    cmd = [
        sys.executable,
        str(CHAT_SCRIPT),
        "--host",
        host,
        "--port",
        str(port),
        "--agent",
        agent,
    ]
    if agent_command:
        cmd.extend(["--agent-command", agent_command])

    proc = subprocess.Popen(
        cmd,
        cwd=REPO_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=log_file,
        stderr=subprocess.STDOUT,
    )
    log_file.close()
    pid_path.write_text(f"{proc.pid}\n", encoding="utf-8")
    print(f">> notes watcher: started chat pid {proc.pid}", flush=True)
    return proc


def stop_mkdocs(proc: subprocess.Popen | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    print(f">> notes watcher: stopping mkdocs pid {proc.pid}", flush=True)
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def stop_chat_server(proc: subprocess.Popen | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    print(f">> notes watcher: stopping chat pid {proc.pid}", flush=True)
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def restart_mkdocs(
    proc: subprocess.Popen | None,
    addr: str,
    log_path: Path,
    pid_path: Path,
) -> subprocess.Popen:
    stop_mkdocs(proc)
    return start_mkdocs(addr, log_path, pid_path)


def supervise(
    *,
    addr: str,
    mkdocs_log: Path,
    mkdocs_pid_file: Path,
    reload_port: int,
    chat_host: str | None,
    chat_port: int | None,
    chat_log: Path | None,
    chat_pid_file: Path | None,
    chat_agent: str,
    chat_agent_command: str,
    interval: float,
    debounce: float,
) -> int:
    if not GENERATOR.is_file():
        print(f"Error: {GENERATOR} not found", file=sys.stderr)
        return 1

    host, _ = parse_addr(addr)
    version = VersionState()
    reload_server = start_reload_server(host, reload_port, version)
    proc: subprocess.Popen | None = None
    chat_proc: subprocess.Popen | None = None

    try:
        if chat_host is not None and chat_port is not None and chat_log is not None and chat_pid_file is not None:
            chat_proc = start_chat_server(
                host=chat_host,
                port=chat_port,
                log_path=chat_log,
                pid_path=chat_pid_file,
                agent=chat_agent,
                agent_command=chat_agent_command,
            )
        proc = start_mkdocs(addr, mkdocs_log, mkdocs_pid_file)
        current = snapshot()
        print(
            f">> notes watcher started: {len(current)} files, interval={interval}s",
            flush=True,
        )

        pending_since: float | None = None
        while True:
            time.sleep(interval)
            if proc.poll() is not None:
                print(
                    f"!! notes watcher: mkdocs exited with code {proc.returncode}, restarting",
                    file=sys.stderr,
                    flush=True,
                )
                proc = start_mkdocs(addr, mkdocs_log, mkdocs_pid_file)
                version.bump()

            if chat_proc is not None and chat_proc.poll() is not None:
                print(
                    f"!! notes watcher: chat exited with code {chat_proc.returncode}, restarting",
                    file=sys.stderr,
                    flush=True,
                )
                if chat_host is not None and chat_port is not None and chat_log is not None and chat_pid_file is not None:
                    chat_proc = start_chat_server(
                        host=chat_host,
                        port=chat_port,
                        log_path=chat_log,
                        pid_path=chat_pid_file,
                        agent=chat_agent,
                        agent_command=chat_agent_command,
                    )

            new_state = snapshot()
            if new_state != current:
                changes = changed_paths(current, new_state)
                shown = ", ".join(changes[:12])
                if len(changes) > 12:
                    shown += f", ... (+{len(changes) - 12} more)"
                print(f">> notes watcher: detected changes: {shown}", flush=True)
                current = new_state
                pending_since = time.monotonic()
                continue

            if pending_since is not None and time.monotonic() - pending_since >= debounce:
                if run_generator() == 0:
                    proc = restart_mkdocs(proc, addr, mkdocs_log, mkdocs_pid_file)
                    version.bump()
                current = snapshot()
                pending_since = None
    finally:
        stop_mkdocs(proc)
        stop_chat_server(chat_proc)
        reload_server.shutdown()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--addr", required=True, help="MkDocs dev address as HOST:PORT")
    parser.add_argument("--mkdocs-log", required=True)
    parser.add_argument("--mkdocs-pid-file", required=True)
    parser.add_argument("--reload-port", type=int, required=True)
    parser.add_argument("--chat-host")
    parser.add_argument("--chat-port", type=int)
    parser.add_argument("--chat-log")
    parser.add_argument("--chat-pid-file")
    parser.add_argument("--chat-agent", default="claude", choices=["claude", "acp"])
    parser.add_argument("--chat-agent-command", default="")
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--debounce", type=float, default=0.5)
    args = parser.parse_args()

    if args.interval <= 0:
        parser.error("--interval must be positive")
    if args.debounce < 0:
        parser.error("--debounce must be non-negative")

    def handle_stop(signum: int, frame: object) -> None:
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, handle_stop)

    try:
        return supervise(
            addr=args.addr,
            mkdocs_log=Path(args.mkdocs_log),
            mkdocs_pid_file=Path(args.mkdocs_pid_file),
            reload_port=args.reload_port,
            chat_host=args.chat_host,
            chat_port=args.chat_port,
            chat_log=Path(args.chat_log) if args.chat_log else None,
            chat_pid_file=Path(args.chat_pid_file) if args.chat_pid_file else None,
            chat_agent=args.chat_agent,
            chat_agent_command=args.chat_agent_command,
            interval=args.interval,
            debounce=args.debounce,
        )
    except KeyboardInterrupt:
        print(">> notes watcher stopped", flush=True)
        return 0


if __name__ == "__main__":
    sys.exit(main())
