#!/usr/bin/env bash
# serve_site.sh — 重启本地 MkDocs 开发服务器预览 notes/
#
# 用法:
#   scripts/notes/serve_site.sh                 # 默认后台重启 0.0.0.0:8110
#   scripts/notes/serve_site.sh 0.0.0.0:8110    # 指定绑定地址:端口
#   scripts/notes/serve_site.sh --foreground    # 前台运行（便于调试）
#   scripts/notes/serve_site.sh --chat          # 同时启动只读文档 Chat 服务
#   scripts/notes/serve_site.sh --no-chat       # 临时关闭 mkdocs.yml 中启用的 Chat
#   scripts/notes/serve_site.sh --chat --chat-host 192.168.1.10
#   scripts/notes/serve_site.sh --build         # 只 build 静态站到 .site/ 不启动服务
#
# 行为:
#   - 每次执行都重新生成 mkdocs.gen.yml
#   - 若目标端口已有旧服务，自动停止旧进程
#   - 默认在后台拉起 notes supervisor，由它管理 mkdocs serve 和热加载
#   - 指定 --chat 时启动本机只读 Chat 服务（默认 Claude）
#   - 输出 PID、日志路径、访问地址

set -euo pipefail

cd "$(dirname "$0")/../.."

LOG_DIR=".tmp"
LOG_FILE="${LOG_DIR}/notes-serve.log"
PID_FILE="${LOG_DIR}/notes-serve.pid"
WATCH_LOG_FILE="${LOG_DIR}/notes-watch.log"
WATCH_PID_FILE="${LOG_DIR}/notes-watch.pid"
CHAT_LOG_FILE="${LOG_DIR}/notes-chat.log"
CHAT_PID_FILE="${LOG_DIR}/notes-chat.pid"

if ! command -v mkdocs >/dev/null 2>&1; then
    echo "Error: mkdocs not found in PATH" >&2
    echo "安装方式:" >&2
    echo "  pipx install mkdocs-material     # 推荐" >&2
    echo "  pip install --user mkdocs-material" >&2
    exit 1
fi

if [[ ! -f mkdocs.yml ]]; then
    echo "Error: mkdocs.yml not found at $(pwd)" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "Error: python3 not found in PATH" >&2
    exit 1
fi

regen_site_config() {
    NOTES_CHAT_ENABLED="${CHAT_ENABLED:-0}" python3 scripts/notes/generate_site_config.py
}

read_config_chat_host() {
    python3 - <<'PY'
from pathlib import Path

try:
    import yaml

    cfg = yaml.safe_load(Path("mkdocs.yml").read_text(encoding="utf-8")) or {}
    extra = cfg.get("extra") or {}
    notes_chat = extra.get("notes_chat") or {}
    host = notes_chat.get("host") or ""
    print(host if isinstance(host, str) else "")
except Exception:
    print("")
PY
}

read_config_chat_enabled() {
    python3 - <<'PY'
from pathlib import Path

try:
    import yaml

    cfg = yaml.safe_load(Path("mkdocs.yml").read_text(encoding="utf-8")) or {}
    extra = cfg.get("extra") or {}
    notes_chat = extra.get("notes_chat") or {}
    enabled = notes_chat.get("enabled")
    if isinstance(enabled, bool):
        print("1" if enabled else "0")
    elif isinstance(enabled, str):
        print("1" if enabled.lower() in {"1", "true", "yes", "on"} else "0")
    else:
        print("")
except Exception:
    print("")
PY
}

start_background_watcher() {
    local addr="$1"
    local watch_log_file="$2"
    local mkdocs_log_file="$3"
    local mkdocs_pid_file="$4"
    local reload_port="$5"
    local chat_enabled="$6"
    local chat_host="$7"
    local chat_port="$8"
    local chat_log_file="$9"
    local chat_pid_file="${10}"
    local chat_agent="${11}"
    local chat_agent_command="${12}"

    local -a cmd=(
        python3 scripts/notes/watch_site_inputs.py
        --addr "${addr}"
        --mkdocs-log "${mkdocs_log_file}"
        --mkdocs-pid-file "${mkdocs_pid_file}"
        --reload-port "${reload_port}"
    )

    if [[ "${chat_enabled}" == "1" ]]; then
        cmd+=(
            --chat-host "${chat_host}"
            --chat-port "${chat_port}"
            --chat-log "${chat_log_file}"
            --chat-pid-file "${chat_pid_file}"
            --chat-agent "${chat_agent}"
        )
        if [[ -n "${chat_agent_command}" ]]; then
            cmd+=(--chat-agent-command "${chat_agent_command}")
        fi
    fi

    if command -v setsid >/dev/null 2>&1; then
        setsid env NOTES_CHAT_ENABLED="${chat_enabled}" "${cmd[@]}" \
            > "${watch_log_file}" 2>&1 < /dev/null &
    else
        nohup env NOTES_CHAT_ENABLED="${chat_enabled}" "${cmd[@]}" \
            > "${watch_log_file}" 2>&1 < /dev/null &
    fi

    echo $!
}

extract_port() {
    local addr="$1"
    echo "${addr##*:}"
}

extract_host() {
    local addr="$1"
    echo "${addr%:*}"
}

is_port_in_use() {
    local port="$1"
    if command -v ss >/dev/null 2>&1; then
        ss -lnt "( sport = :${port} )" 2>/dev/null | grep -q LISTEN
        return $?
    elif command -v lsof >/dev/null 2>&1; then
        lsof -iTCP:"${port}" -sTCP:LISTEN -t >/dev/null 2>&1
        return $?
    fi
    return 1
}

find_listener_pids() {
    local port="$1"
    if command -v ss >/dev/null 2>&1; then
        ss -lntp "( sport = :${port} )" 2>/dev/null \
            | grep -oE 'pid=[0-9]+' \
            | cut -d= -f2 \
            | sort -u || true
    elif command -v lsof >/dev/null 2>&1; then
        lsof -iTCP:"${port}" -sTCP:LISTEN -t 2>/dev/null | sort -u || true
    fi
}

stop_listener_pids() {
    local port="$1"
    local pids
    pids="$(find_listener_pids "${port}")"

    if [[ -z "${pids}" ]]; then
        return 0
    fi

    echo ">> Port ${port} is in use. Stopping existing notes server..."

    while IFS= read -r pid; do
        [[ -z "${pid}" ]] && continue
        kill "${pid}" 2>/dev/null || true
    done <<< "${pids}"

    for _ in 1 2 3 4 5 6; do
        sleep 0.5
        if ! is_port_in_use "${port}"; then
            return 0
        fi
    done

    echo ">> Existing process did not exit on SIGTERM, sending SIGKILL..."
    while IFS= read -r pid; do
        [[ -z "${pid}" ]] && continue
        kill -9 "${pid}" 2>/dev/null || true
    done <<< "${pids}"

    for _ in 1 2 3 4; do
        sleep 0.5
        if ! is_port_in_use "${port}"; then
            return 0
        fi
    done

    echo "Error: failed to free port ${port}" >&2
    exit 1
}

wait_until_listening() {
    local port="$1"
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        sleep 0.5
        if is_port_in_use "${port}"; then
            return 0
        fi
    done
    return 1
}

ensure_process_alive() {
    local pid="$1"
    kill -0 "${pid}" 2>/dev/null
}

stop_pid_file() {
    local pid_file="$1"
    local label="$2"

    [[ -f "${pid_file}" ]] || return 0

    local pid
    pid="$(cat "${pid_file}" 2>/dev/null || true)"
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
        echo ">> Stopping existing ${label} (${pid})..."
        kill "${pid}" 2>/dev/null || true
        for _ in 1 2 3 4 5 6; do
            sleep 0.2
            if ! kill -0 "${pid}" 2>/dev/null; then
                rm -f "${pid_file}"
                return 0
            fi
        done
        kill -9 "${pid}" 2>/dev/null || true
    fi
    rm -f "${pid_file}"
}

stop_watcher() {
    stop_pid_file "${WATCH_PID_FILE}" "notes watcher"
    stop_pid_file "${CHAT_PID_FILE}" "notes chat"
}

reset_logs() {
    : > "${LOG_FILE}"
    : > "${WATCH_LOG_FILE}"
    : > "${CHAT_LOG_FILE}"
}

MODE="background"
ADDR="0.0.0.0:8110"
CHAT_ENABLED="${NOTES_CHAT_ENABLED:-}"
CHAT_HOST="${NOTES_CHAT_HOST:-}"
CHAT_AGENT="${NOTES_CHAT_AGENT:-claude}"
CHAT_AGENT_COMMAND="${NOTES_CHAT_AGENT_COMMAND:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)
            MODE="build"
            shift
            ;;
        --foreground)
            MODE="foreground"
            shift
            ;;
        --chat)
            CHAT_ENABLED="1"
            shift
            ;;
        --no-chat)
            CHAT_ENABLED="0"
            shift
            ;;
        --chat-host)
            if [[ -z "${2:-}" ]]; then
                echo "Error: --chat-host requires a host or IP address" >&2
                exit 1
            fi
            CHAT_HOST="$2"
            shift 2
            ;;
        --chat-agent)
            if [[ -z "${2:-}" ]]; then
                echo "Error: --chat-agent requires claude or acp" >&2
                exit 1
            fi
            CHAT_AGENT="$2"
            shift 2
            ;;
        --chat-agent-command)
            if [[ -z "${2:-}" ]]; then
                echo "Error: --chat-agent-command requires a command string" >&2
                exit 1
            fi
            CHAT_AGENT_COMMAND="$2"
            shift 2
            ;;
        -*)
            echo "Error: unknown option $1" >&2
            exit 1
            ;;
        *)
            ADDR="$1"
            shift
            ;;
    esac
done

if [[ "${CHAT_AGENT}" != "claude" && "${CHAT_AGENT}" != "acp" ]]; then
    echo "Error: --chat-agent must be claude or acp" >&2
    exit 1
fi

if [[ -z "${CHAT_ENABLED}" ]]; then
    CHAT_ENABLED="$(read_config_chat_enabled)"
fi
if [[ -z "${CHAT_ENABLED}" ]]; then
    CHAT_ENABLED="0"
fi

if [[ -z "${CHAT_HOST}" ]]; then
    CHAT_HOST="$(read_config_chat_host)"
fi
if [[ -z "${CHAT_HOST}" ]]; then
    CHAT_HOST="127.0.0.1"
fi

if [[ "${MODE}" == "build" ]]; then
    CHAT_ENABLED="0"
    regen_site_config
    echo ">> Building static site to .site/ ..."
    mkdocs build --clean -f mkdocs.gen.yml
    echo ""
    echo "Done. 临时预览:"
    echo "  python3 -m http.server --directory .site 8110"
    exit 0
fi

PORT="$(extract_port "${ADDR}")"
RELOAD_PORT=$((PORT + 1))
CHAT_PORT=$((PORT + 2))

mkdir -p "${LOG_DIR}"

regen_site_config
stop_watcher
stop_listener_pids "${PORT}"
stop_listener_pids "${RELOAD_PORT}"
if [[ "${CHAT_ENABLED}" == "1" ]]; then
    stop_listener_pids "${CHAT_PORT}"
fi
reset_logs

if [[ "${MODE}" == "foreground" ]]; then
    CHAT_ARGS=()
    if [[ "${CHAT_ENABLED}" == "1" ]]; then
        CHAT_ARGS=(
            --chat-host "${CHAT_HOST}"
            --chat-port "${CHAT_PORT}"
            --chat-log "${CHAT_LOG_FILE}"
            --chat-pid-file "${CHAT_PID_FILE}"
            --chat-agent "${CHAT_AGENT}"
        )
        if [[ -n "${CHAT_AGENT_COMMAND}" ]]; then
            CHAT_ARGS+=(--chat-agent-command "${CHAT_AGENT_COMMAND}")
        fi
    fi

    echo ""
    echo ">> Starting notes supervisor in foreground on http://${ADDR}"
    echo "   config: ${PWD}/mkdocs.gen.yml"
    echo "   mkdocs log: ${PWD}/${LOG_FILE}"
    echo "   reload: http://$(extract_host "${ADDR}"):${RELOAD_PORT}/version"
    if [[ "${CHAT_ENABLED}" == "1" ]]; then
        echo "   chat:   http://${CHAT_HOST}:${CHAT_PORT}/health (${CHAT_AGENT})"
        echo "   chat log: ${PWD}/${CHAT_LOG_FILE}"
    fi
    echo "   stop:   Ctrl-C"
    echo ""
    exec env NOTES_CHAT_ENABLED="${CHAT_ENABLED}" python3 scripts/notes/watch_site_inputs.py \
        --addr "${ADDR}" \
        --mkdocs-log "${LOG_FILE}" \
        --mkdocs-pid-file "${PID_FILE}" \
        --reload-port "${RELOAD_PORT}" \
        "${CHAT_ARGS[@]}"
fi

echo ">> Starting notes supervisor in background on http://${ADDR}"
WATCH_PID="$(start_background_watcher \
    "${ADDR}" \
    "${WATCH_LOG_FILE}" \
    "${LOG_FILE}" \
    "${PID_FILE}" \
    "${RELOAD_PORT}" \
    "${CHAT_ENABLED}" \
    "${CHAT_HOST}" \
    "${CHAT_PORT}" \
    "${CHAT_LOG_FILE}" \
    "${CHAT_PID_FILE}" \
    "${CHAT_AGENT}" \
    "${CHAT_AGENT_COMMAND}")"
echo "${WATCH_PID}" > "${WATCH_PID_FILE}"

if ! wait_until_listening "${PORT}"; then
    echo "Error: mkdocs serve did not start successfully on port ${PORT}" >&2
    kill "${WATCH_PID}" 2>/dev/null || true
    rm -f "${WATCH_PID_FILE}"
    echo "Last log lines:" >&2
    tail -n 40 "${LOG_FILE}" >&2 || true
    exit 1
fi

NEW_PID="$(cat "${PID_FILE}" 2>/dev/null || true)"

sleep 1
if [[ -z "${NEW_PID}" ]] || ! ensure_process_alive "${NEW_PID}"; then
    echo "Error: mkdocs serve exited immediately after startup" >&2
    kill "${WATCH_PID}" 2>/dev/null || true
    rm -f "${WATCH_PID_FILE}"
    echo "Last log lines:" >&2
    tail -n 40 "${LOG_FILE}" >&2 || true
    exit 1
fi

NEW_CHAT_PID=""
if [[ "${CHAT_ENABLED}" == "1" ]]; then
    if ! wait_until_listening "${CHAT_PORT}"; then
        echo "Error: notes chat did not start successfully on port ${CHAT_PORT}" >&2
        kill "${WATCH_PID}" 2>/dev/null || true
        rm -f "${WATCH_PID_FILE}"
        echo "Last chat log lines:" >&2
        tail -n 40 "${CHAT_LOG_FILE}" >&2 || true
        exit 1
    fi
    NEW_CHAT_PID="$(cat "${CHAT_PID_FILE}" 2>/dev/null || true)"
fi

if ! ensure_process_alive "${WATCH_PID}"; then
    echo "Error: notes watcher exited immediately after startup" >&2
    echo "Last watcher log lines:" >&2
    tail -n 40 "${WATCH_LOG_FILE}" >&2 || true
    exit 1
fi

echo ">> Notes server restarted."
echo "   url:         http://${ADDR}"
echo "   mkdocs pid: ${NEW_PID}"
echo "   mkdocs log: ${PWD}/${LOG_FILE}"
echo "   watch pid:  ${WATCH_PID}"
echo "   watch log:  ${PWD}/${WATCH_LOG_FILE}"
echo "   reload:     http://$(extract_host "${ADDR}"):${RELOAD_PORT}/version"
if [[ "${CHAT_ENABLED}" == "1" ]]; then
    echo "   chat pid:   ${NEW_CHAT_PID}"
    echo "   chat log:   ${PWD}/${CHAT_LOG_FILE}"
    echo "   chat:       http://${CHAT_HOST}:${CHAT_PORT}/health (${CHAT_AGENT})"
fi
echo "   stop:        kill ${WATCH_PID} ${NEW_PID}"
