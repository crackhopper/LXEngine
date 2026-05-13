---
name: lxe-editor-command-reference
description: Use when lxe_editor command-bus syntax, command names, arguments, or examples must be verified before issuing a command.
---

# lxe-editor-command-reference

Use this skill as a lightweight reference step before sending non-trivial
`lxe_editor_command` payloads. It does not execute workflows by itself.

## Source Of Truth

Prefer current code over memory or old notes. Search the command registration
and command parsing sites first:

```bash
rg -n "register.*command|Command|executeCommand|command bus|lxe_editor_command" src/demos/lxe_editor src/core src/infra
```

If docs mention a command but code does not, trust code and report the mismatch.

## Workflow

1. Identify the intended editor action in plain language.
2. Search current command registrations and parser branches.
3. Extract the exact command name, argument order, valid values, and side
   effects.
4. Return the smallest safe command string to the caller skill.
5. Let `lxe-editor-debug` or `lxe-editor-recording` execute and verify it.

## Guardrails

- Do not guess command names from UI labels.
- Do not bulk-load unrelated command code; search narrowly by action or noun.
- Prefer examples backed by tests or current registration code.
- If no command exists, say so and suggest the nearest MCP tool or editor API
  surface instead.
