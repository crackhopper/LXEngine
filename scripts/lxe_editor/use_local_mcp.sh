#!/usr/bin/env bash

_lxe_editor_compat_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${_lxe_editor_compat_script_dir}/../lxe_manager/use_local_mcp.sh" "$@"
_lxe_editor_compat_status=$?
unset _lxe_editor_compat_script_dir
return "${_lxe_editor_compat_status}" 2>/dev/null || exit "${_lxe_editor_compat_status}"
