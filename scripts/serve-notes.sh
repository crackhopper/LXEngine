#!/usr/bin/env bash
# serve-notes.sh — 启动本地 MkDocs 开发服务器预览 notes/
#
# 用法:
#   scripts/serve-notes.sh                 # 默认 0.0.0.0:8110（LAN 可访问）
#   scripts/serve-notes.sh 127.0.0.1:8110  # 绑定指定地址:端口
#   scripts/serve-notes.sh --build         # 只 build 静态站到 .site/ 不启动服务
#
# 特性:
#   - 启动前检测目标端口，若被占用显示占用进程并交互式询问是否 kill
#   - kill 后等待端口真正释放（SIGTERM → 必要时 SIGKILL），失败则中止
#
# 依赖: mkdocs + mkdocs-material（pipx install mkdocs-material）

set -euo pipefail

# 切到项目根，无论从哪里调用
cd "$(dirname "$0")/.."

# ---------- 基础检查 ----------

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

# ---------- --build 模式（不起服务，无需检查端口）----------

if [[ "${1:-}" == "--build" ]]; then
    echo ">> Building static site to .site/ ..."
    mkdocs build --clean
    echo ""
    echo "Done. 临时预览:"
    echo "  python3 -m http.server --directory .site 8110"
    exit 0
fi

# ---------- 端口占用检测 helpers ----------

# 从 "host:port" 形式中提取端口
extract_port() {
    local addr="$1"
    echo "${addr##*:}"
}

# 返回 0 表示端口正在被监听
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

# 输出监听该端口的 PID（每行一个）；本用户无权限看到时可能为空
find_listener_pids() {
    local port="$1"
    if command -v ss >/dev/null 2>&1; then
        ss -lntp "( sport = :${port} )" 2>/dev/null \
            | grep -oE 'pid=[0-9]+' \
            | cut -d= -f2 \
            | sort -u
    elif command -v lsof >/dev/null 2>&1; then
        lsof -iTCP:"${port}" -sTCP:LISTEN -t 2>/dev/null | sort -u
    fi
}

# 输出一个 PID 的进程命令行（尽量短）
describe_pid() {
    local pid="$1"
    if [[ -r /proc/${pid}/cmdline ]]; then
        tr '\0' ' ' < "/proc/${pid}/cmdline" | cut -c1-180
    elif command -v ps >/dev/null 2>&1; then
        ps -p "${pid}" -o args= 2>/dev/null | cut -c1-180
    else
        echo "(no info)"
    fi
}

# 检测端口 → 如被占用询问是否 kill → kill → 等待释放
check_port_or_prompt() {
    local port="$1"

    if ! is_port_in_use "${port}"; then
        return 0
    fi

    local pids
    pids="$(find_listener_pids "${port}")"

    if [[ -z "${pids}" ]]; then
        echo "!! Port ${port} 已被占用，但无法识别持有进程（可能属于其他用户）" >&2
        echo "   请用 root 权限排查：" >&2
        echo "     sudo ss -lntp 'sport = :${port}'" >&2
        exit 1
    fi

    echo ""
    echo "!! Port ${port} 已被占用，监听进程:"
    while IFS= read -r pid; do
        [[ -z "${pid}" ]] && continue
        echo "     PID ${pid}  $(describe_pid "${pid}")"
    done <<< "${pids}"
    echo ""

    local answer=""
    read -r -p "   Kill 这些进程并继续? [y/N] " answer || answer=""
    case "${answer}" in
        [yY]|[yY][eE][sS])
            ;;
        *)
            echo ""
            echo "已取消启动。手动处理:" >&2
            echo "   kill $(echo "${pids}" | tr '\n' ' ')" >&2
            exit 1
            ;;
    esac

    # 发送 SIGTERM
    echo "   sending SIGTERM..."
    while IFS= read -r pid; do
        [[ -z "${pid}" ]] && continue
        kill "${pid}" 2>/dev/null || true
    done <<< "${pids}"

    # 最多等 ~1.5s 让 socket 真正释放
    for _ in 1 2 3; do
        sleep 0.5
        if ! is_port_in_use "${port}"; then
            echo "   端口 ${port} 已释放"
            return 0
        fi
    done

    # SIGTERM 没效果 → SIGKILL
    echo "   SIGTERM 超时，尝试 SIGKILL..."
    while IFS= read -r pid; do
        [[ -z "${pid}" ]] && continue
        kill -9 "${pid}" 2>/dev/null || true
    done <<< "${pids}"
    sleep 0.5

    if is_port_in_use "${port}"; then
        echo "Error: 端口 ${port} 仍然被占用，放弃启动" >&2
        exit 1
    fi
    echo "   端口 ${port} 已释放"
}

# ---------- 启动 serve ----------

ADDR="${1:-0.0.0.0:8110}"
PORT="$(extract_port "${ADDR}")"

check_port_or_prompt "${PORT}"

echo ">> Starting mkdocs serve on http://${ADDR}"
echo "   docs_dir: notes/"
echo "   stop:     Ctrl-C"
echo ""
exec mkdocs serve --dev-addr "${ADDR}"
