---
name: lxe-editor-command-reference
description: Use when lxe_editor command-bus syntax, command names, arguments, or examples must be verified before issuing a command.
---

# lxe-editor-command-reference

Use this skill as a lightweight reference step before sending non-trivial
`lxe_editor_command` payloads. It does not execute workflows by itself.

Prefer command-console expansion for editor-specific actions when practical.
If a capability can be represented as a command-bus verb, document and use that
command through existing MCP command forwarding instead of requiring a new
manager MCP tool and manager restart.

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
3. For commands that operate on scene contents, identify whether a scene must
   already be loaded. If the caller has not confirmed a scene, return that
   precondition with the command string.
4. Extract the exact command name, argument order, valid values, and side
   effects.
5. Return the smallest safe command string to the caller skill.
6. Let `lxe-editor-debug` or `lxe-editor-recording` execute and verify it.

Common scene setup command:

```text
scene load assets/scenes/lxe_editor.scene.yaml
```

Verify the path from the current repo before using it in a remote workflow.

Common recording and camera commands:

```text
preview off
cam control orbit
cam look-at 2.8 2.0 4.5 0.0 0.6 0.0
recording enable
recording start basic
recording stop save
```

With preview off, `cam look-at` changes `editor_cam`. Use it before pick-heavy
use cases so the default view does not keep hitting `game_cam`.

## Guardrails

- Do not guess command names from UI labels.
- Do not bulk-load unrelated command code; search narrowly by action or noun.
- Prefer examples backed by tests or current registration code.
- If no command exists, say so and suggest the nearest MCP tool or editor API
  surface instead.
- If no command exists but the operation is naturally editor-local, recommend
  adding a command-bus command before adding manager MCP protocol surface.
