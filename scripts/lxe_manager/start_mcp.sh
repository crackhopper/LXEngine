#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manager_dir="${repo_root}/tools/lxe_manager"

if [ ! -f "${manager_dir}/package.json" ]; then
  echo "lxe_manager MCP start failed: package.json not found under ${manager_dir}" >&2
  exit 1
fi

exec npm --prefix "${manager_dir}" run dev -- "$@"
