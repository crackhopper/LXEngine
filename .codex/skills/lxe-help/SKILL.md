---
name: lxe-help
description: Use when the user asks which LXEngine lxe_* or lxe-* skill to use, wants an overview of editor/manager/remote verification skills, or asks to organize lxe_editor workflows.
---

# lxe_help

Use this as the index for LXEngine editor, manager, remote, and verification
skills. All skills in this family use an `lxe-` directory/frontmatter prefix;
`lxe_help` is the user-facing alias for this `lxe-help` skill.

## Naming Rule

| Rule | Meaning |
|---|---|
| Skill names use `lxe-...` | Codex skill names are hyphenated; avoid underscores in frontmatter |
| User-facing aliases may use `lxe_...` | Users can say `lxe_help` / `lxe_verify_implement`; map them to `lxe-help` / `lxe-verify-implement` |
| Editor/manager/remote skills stay in this family | New lxe_editor workflow skills should start with `lxe-` |

## Which Skill To Use

| Skill | Use when | Does not do |
|---|---|---|
| `lxe-help` | Choosing among lxe skills or explaining the workflow family | Execute editor operations |
| `lxe-manager-ops` | Checking process status, starting/stopping editor, pulling repo, configuring/building, reading logs | Inspect scene contents or command syntax |
| `lxe-debug` | Diagnosing user-visible editor issues through MCP: summary/cameras/selection, small commands, picking, command syntax lookup, build identity checks, logs handoff | Pull/build/restart workflows |
| `lxe-recording` | Recording, reading, replaying, or probing editor interaction recordings | Ordinary state inspection |
| `lxe-use-case-runner` | Running saved agent-readable use cases under `notes/use_cases/lxe_editor/` | Broad process lifecycle management |
| `lxe-remote-refresh-restore` | Preserve current scene, stop remote editor, pull/build/restart, restore scene | Debug an unknown crash |
| `lxe-verify-implement` | Proving implementation changes are pushed, pulled, built, launched, command-smoked, and visually checked | Replace human visual confirmation for rendering features |

## Common Workflows

### Is the editor running?

1. Use `lxe-manager-ops`.
2. Call `ops.editor_status`.
3. If needed, call `ops.editor_start`.
4. Verify with `lxe_editor_ensure_running`.

### I need to inspect or lightly change the current scene

1. Use `lxe-debug`.
2. Read summary first.
3. For any non-trivial command, use `lxe-debug` command syntax lookup.
4. Run one small command.
5. Re-read the affected state.

### I fixed code and need remote verification

1. Use `lxe-verify-implement` for the full push/pull/build/restart/check flow.
2. For renderer visual features, always tell the user exactly what to inspect
   in the editor window.

### I need to preserve the current scene while updating the editor

1. Use `lxe-remote-refresh-restore`.
2. Save or identify the current scene.
3. Stop, pull, build, restart, reload.
4. Verify summary after reload.

### I need a repeatable scenario

1. Use `lxe-recording` for capture/replay artifacts.
2. Use `lxe-use-case-runner` to execute saved use cases.
3. Use `lxe-manager-ops` only for lifecycle prerequisites.

## Boundaries

- Notes site work is not part of this family; use `refresh-notes`,
  `update-notes`, or `writing-notes`.
- Requirement lifecycle work is not part of this family; use `draft-req` or
  `finish-req`.
- Broader design work is not part of this family; use Superpowers
  brainstorming and planning skills.

## Quick Recommendation

When in doubt:

1. Need lifecycle/build/logs: `lxe-manager-ops`.
2. Need scene state, one command, command syntax, picking, or build identity checks: `lxe-debug`.
3. Need deployed implementation proof: `lxe-verify-implement`.
4. Need help choosing: `lxe-help`.
