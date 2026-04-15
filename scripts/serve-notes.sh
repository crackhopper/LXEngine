#!/usr/bin/env bash
# serve-notes.sh — 重启本地 MkDocs 开发服务器预览 notes/
#
# 用法:
#   scripts/serve-notes.sh                 # 默认后台重启 0.0.0.0:8110
#   scripts/serve-notes.sh 0.0.0.0:8110    # 指定绑定地址:端口
#   scripts/serve-notes.sh --foreground    # 前台运行（便于调试）
#   scripts/serve-notes.sh --build         # 只 build 静态站到 .site/ 不启动服务
#
# 行为:
#   - 每次执行都重新生成 mkdocs.gen.yml
#   - 若目标端口已有旧服务，自动停止旧进程
#   - 默认在后台拉起新的 mkdocs serve
#   - 输出 PID、日志路径、访问地址

set -euo pipefail

cd "$(dirname "$0")/.."

LOG_DIR=".tmp"
LOG_FILE="${LOG_DIR}/notes-serve.log"
PID_FILE="${LOG_DIR}/notes-serve.pid"

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
    python3 scripts/_gen_notes_site.py
}

extract_port() {
    local addr="$1"
    echo "${addr##*:}"
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

start_background_server() {
    local addr="$1"
    local log_file="$2"

    if command -v setsid >/dev/null 2>&1; then
        setsid mkdocs serve --dev-addr "${addr}" -f mkdocs.gen.yml \
            > "${log_file}" 2>&1 < /dev/null &
    else
        nohup mkdocs serve --dev-addr "${addr}" -f mkdocs.gen.yml \
            > "${log_file}" 2>&1 < /dev/null &
    fi

    echo $!
}

ensure_process_alive() {
    local pid="$1"
    kill -0 "${pid}" 2>/dev/null
}

MODE="background"
ADDR="0.0.0.0:8110"

if [[ "${1:-}" == "--build" ]]; then
    regen_site_config
    echo ">> Building static site to .site/ ..."
    mkdocs build --clean -f mkdocs.gen.yml
    echo ""
    echo "Done. 临时预览:"
    echo "  python3 -m http.server --directory .site 8110"
    exit 0
fi

if [[ "${1:-}" == "--foreground" ]]; then
    MODE="foreground"
    shift
fi

if [[ -n "${1:-}" ]]; then
    ADDR="$1"
fi

PORT="$(extract_port "${ADDR}")"

mkdir -p "${LOG_DIR}"

regen_site_config
stop_listener_pids "${PORT}"

if [[ "${MODE}" == "foreground" ]]; then
    echo ""
    echo ">> Starting mkdocs serve in foreground on http://${ADDR}"
    echo "   config: ${PWD}/mkdocs.gen.yml"
    echo "   stop:   Ctrl-C"
    echo ""
    exec mkdocs serve --dev-addr "${ADDR}" -f mkdocs.gen.yml
fi

echo ">> Starting mkdocs serve in background on http://${ADDR}"
NEW_PID="$(start_background_server "${ADDR}" "${LOG_FILE}")"
echo "${NEW_PID}" > "${PID_FILE}"

if ! wait_until_listening "${PORT}"; then
    echo "Error: mkdocs serve did not start successfully on port ${PORT}" >&2
    echo "Last log lines:" >&2
    tail -n 40 "${LOG_FILE}" >&2 || true
    exit 1
fi

sleep 1
if ! ensure_process_alive "${NEW_PID}"; then
    echo "Error: mkdocs serve exited immediately after startup" >&2
    echo "Last log lines:" >&2
    tail -n 40 "${LOG_FILE}" >&2 || true
    exit 1
fi

echo ">> Notes server restarted."
echo "   url:  http://${ADDR}"
echo "   pid:  ${NEW_PID}"
echo "   log:  ${PWD}/${LOG_FILE}"
echo "   stop: kill ${NEW_PID}"
