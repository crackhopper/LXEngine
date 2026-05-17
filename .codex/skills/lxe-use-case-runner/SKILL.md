---
name: lxe-use-case-runner
description: Use when executing agent-readable lxe_editor use cases through lxe_manager MCP, especially complex scene editing, recording, replay, or remote regression scenarios.
---

# lxe_use_case_runner

Use this skill when the user asks to run a saved lxe_editor use case, a complex
business workflow, or a repeatable remote MCP scenario.

## Source

Use case files live under:

```text
notes/use_cases/lxe_editor/
```

Read the target use case first. Treat the Markdown as the business source of
truth: execute its intent, prerequisites, steps, and acceptance criteria rather
than inventing a new ad-hoc scenario.

## Workflow

1. Read the requested use case file.
2. Use `lxe-manager-ops` to confirm manager/editor status.
3. Use `lxe-debug` build identity checks if the use case depends on recent
   code.
4. If remote code is stale, use `lxe-verify-implement`.
5. Use `lxe-debug` command syntax lookup before sending non-trivial command
   lines.
6. Use `lxe-debug` for state reads, command execution, pick, and waits.
7. Use `lxe-recording` for recording, reading, replaying, and probes.
8. Record exact use case deviations, such as substitute node paths or alternate
   pick coordinates.
9. Report the recording id/path, replay result, generated scene path, and any
   failed acceptance criteria.

## Guardrails

- Do not skip scene loading. A use case recording without its target scene is
  not useful.
- Prefer command-console operations for editor-local behavior; add manager MCP
  tools only when the operation is not naturally an editor command.
- If the manager server code itself changed, ask the user to restart manager
  before claiming the new manager behavior was verified.
- If a step cannot be executed exactly, keep the same business intent and state
  the concrete substitution in the result.
