## Context

REQ-040-a spans several layers, but the lowest-risk first increment is to land the pure core contract first: parsing, dispatch, history, and a selection state holder. These pieces are independent from ImGui and lxe_editor wiring, so they are easy to verify headlessly.

## Decisions

### 1. Keep `CommandBus` pure and headless

`CommandBus` depends only on STL plus `u64` timestamp typing. It stores verb metadata, catches handler exceptions, records every dispatch result, and exposes a script helper that splits by line. This keeps the future MCP shim thin and testable.

### 2. Tokenizer implements only REQ v1 surface

Tokenizer supports:
- whitespace separation
- double-quoted tokens
- `\\`, `\"`, `\n` escapes

Invalid syntax (unterminated quote / dangling escape) returns `ok=false` instead of throwing. This matches the requirement that invalid input stays in-band.

### 3. `EditorState` stores non-owning selection via `weak_ptr`

The requirement sketch shows raw pointers, but repository style forbids raw-pointer object references. The implementation therefore stores the selected node as `std::weak_ptr<SceneNode>` and returns `SceneNodeSharedPtr` on query. This preserves nullable non-owning semantics without violating style rules.

## Increment Scope

This change intentionally stops after core contracts + tests. Built-in verbs, console panel, demo integration, and full REQ verification remain for follow-up increments in the same change.
