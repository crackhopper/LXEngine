---
name: dead-code-prune
description: Remove dead code by starting from an apparently unused function, proving it has no live inbound callers, then expanding through its callees to find a removable dead subgraph. Use when the user wants to delete unused functions, stale helper chains, or unreachable implementation paths instead of only reporting them.
---

Remove dead code by proving either unreachability or redundancy, not by guessing from one missing call site.

## When To Use

Use this skill when the user asks to:

- 删除 dead code / 无用代码
- remove unused functions or stale helper chains
- prune unreachable implementation paths
- clean up old code after a refactor where some entry points may no longer be reachable

Do not use this skill for:

- simple code review with no deletion request
- ABI-preserving deprecations
- broad architectural rewrites where reachability is unclear and the user has not asked for deletion

## Core Idea

Treat dead-code removal as two related cleanup modes:

1. **Unreachable dead code**: a suspicious root function has no live inbound callers.
2. **Redundant dead code**: a function still has callers, but it is only a duplicate or obsolete path and should be removed in favor of one canonical implementation.

For unreachable dead code, inspect the functions it calls and expand only when their remaining callers are already inside the dead set.

For redundant dead code, first identify the canonical surviving path, then migrate callers away from the redundant path, and only then remove the obsolete function chain.

This is stronger than “find one unused function” and safer than “delete everything it touches”.

## Guardrails

- Never delete code based on one `rg` result alone.
- Always check both declarations and definitions.
- Be careful with:
  - virtual overrides
  - interface implementations
  - function pointers and callback registration
  - macro-generated references
  - template instantiations
  - static initialization and registries
  - serialization / reflection / string-based lookup
  - test-only entry points that are intentionally kept
- Do not delete exported or public API surface unless the repository clearly treats it as internal and all current call sites are gone.
- If reachability is ambiguous, stop and report the ambiguity instead of forcing the deletion.
- Do not delete a currently unused function if it still serves a clear repository purpose, such as:
  - required extension point or interface slot
  - planned-but-current design hook documented in current specs
  - symmetric API surface the subsystem intentionally keeps
  - platform split where one branch is idle on the current platform but required overall
- Do not keep a function merely because it still has callers. If the function is redundant with a better canonical path, treat it as dead-path cleanup: migrate callers first, then delete it.

## Workflow

### 1. Choose a candidate root

Start from a function that appears either unused or redundant:

- a static/private helper with no callers
- a class member that lost all call sites after refactor
- a utility path replaced by a new implementation
- an older variant that overlaps a newer canonical implementation
- a function named in TODO / cleanup comments or review findings

Prefer internal functions first. They are cheaper to prove dead than public API.

Useful searches:

```bash
rg -n "FunctionName|ClassName::FunctionName" src test
rg -n "function_name" src test
rg -n "vtable|register|callback|bind|lookup|reflect" src test
```

### 2. Classify the root: unreachable, redundant, or justified

Build an inbound-call inventory and role summary for the candidate:

- direct call sites
- address-taken sites
- registration into tables, callbacks, or factories
- interface / virtual dispatch relationships
- references from tests
- references from docs or specs only matter if they describe current required behavior

Then classify:

- **unreachable dead**: current code shows no live inbound edge
- **redundant dead**: current code still calls it, but there is a preferred replacement path and this function adds no distinct behavior worth keeping
- **justified dormant**: current code does not call it, but the repository still has a concrete reason to keep it

Only the first two categories are removable targets.

### 3. Handle justified dormant code correctly

If the candidate is unused but justified, keep it.

Typical keep cases:

- required override or interface completeness
- cross-platform branch not exercised on the current machine
- hook for a current spec-required feature boundary
- intentionally symmetric API where removal would make the surface inconsistent

Document briefly why it stays, then stop pruning from that root.

### 4. Build the removable set through callees

There are two variants.

#### Unreachable variant

For each dead candidate, inspect the functions it calls.

For every callee:

- if it still has a live caller outside the dead set, keep it
- if all callers are already in the dead set, add it to the dead set
- if uncertain because of indirection or generated code, keep it until proven dead

Think in terms of this rule:

```text
removable(callee) = all current inbound callers of callee are already removable
```

This is the key rule behind chain-style dead-code cleanup.

#### Redundant variant

If the root is still used but redundant:

1. identify the canonical implementation that should survive
2. list all callers of the redundant path
3. migrate those callers to the canonical path
4. re-check that the redundant path now has no live inbound callers
5. then apply the unreachable-closure rule above to remove its obsolete helper chain

Treat this as dead-code elimination by convergence: once callers are moved off the redundant branch, the old branch becomes ordinary unreachable dead code.

### 5. Delete the whole dead slice, not one line at a time

Once the removable set is stable, remove:

- function declarations
- function definitions
- dead private members
- dead helper structs/enums/constants used only by the dead slice
- dead tests that only exercised the removed implementation

Also clean up:

- now-unused includes
- forward declarations
- obsolete comments
- stale notes/spec text if it still describes removed code as current behavior

### 6. Re-check for second-order dead code

After the first deletion pass, search again:

- a helper kept alive only by the deleted slice may now have zero callers
- a private field or constant may now be unused
- a tiny subsystem may collapse after one entry point is removed

Run a second pass before stopping.

### 7. Validate aggressively

At minimum:

```bash
cmake --build build -j2
```

When the change touches tests or a subsystem with existing tests, also run the relevant targets or binaries. Prefer the narrowest meaningful validation set that proves the deletion did not remove live behavior.

## Decision Rules

Delete when all of these are true:

- the root is either already unreachable, or has been proven redundant and all callers were safely migrated away
- each added callee is only reachable from the current dead set
- no spec requires the code as current behavior
- build/tests still pass after deletion

Do not delete yet when any of these are true:

- the function participates in virtual dispatch and the dynamic callers are not fully understood
- the symbol is registered indirectly
- the code is referenced by string name or reflection metadata
- there is a plausible external contract but the repo does not make it explicit
- the function is unused but still has a concrete and current reason to exist

Treat as redundant dead code when these are true:

- another function or path already owns the same responsibility
- keeping both paths increases maintenance cost without preserving distinct behavior
- the redundant path can be replaced at call sites without changing required behavior
- after migration, the redundant branch can be deleted as an unreachable slice

## Recommended Execution Pattern

1. Search for the candidate root and all inbound references.
2. Classify the root as unreachable, redundant, or justified dormant.
3. If redundant, choose the canonical surviving path and migrate callers first.
4. Open the root implementation and list its callees.
5. For each callee, search inbound callers and classify:
   - live outside dead set
   - removable with dead set
   - uncertain
6. Form the smallest safe removable set.
7. Delete with `apply_patch`.
8. Re-search for newly orphaned symbols.
9. Build and run relevant tests.

## Output Expectations

When you finish a dead-code cleanup, report:

- the root function or entry point you started from
- whether it was unreachable dead code or redundant dead code
- why it was considered removable
- which dependent functions were also removed and why
- any unused functions intentionally kept because they still serve a concrete purpose
- any functions intentionally kept because they still had live callers or ambiguous reachability
- what build/tests were run

## Example Framing

Good framing:

- “`FooBuilder::buildLegacyPath()` has no live callers. Its only callees are `parseLegacyHeader()` and `emitLegacyBindings()`, and those have no callers outside the same dead slice, so all three can be removed.”
- “`buildWithOldReflection()` still has callers, but `buildWithRenderSignature()` is now the canonical path and preserves required behavior. After migrating the remaining callers, the old path and its helper chain become unreachable and can be removed.”

Bad framing:

- “`parseLegacyHeader()` looks old, so delete it.”
- “`legacyHelper()` still has callers, so it must stay.”

The root matters. Start from either unreachability or provable redundancy, then prove the closure.
