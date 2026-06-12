---
name: render-071-agent-guardrails
description: Use when scoping, prompting, implementing, or reviewing LXEngine REQ-071 material/rendering architecture work involving Material v2, RenderPathGraph, RenderFeature, SceneResourceTable, bindless validation, legacy bridge removal, or multi-render-path DAG construction.
---

# Render 071 Agent Guardrails

## Overview

Use this skill to keep REQ-071 rendering work narrow, test-first, and architecture-aligned. The goal is a real Material v2 + RenderPathGraph + SceneResourceTable + bindless path, not renamed compatibility layers.

## Mandatory Prefix

Add this prefix to every 071 agent task:

```text
Use current repo facts only. This task is not a rename-only task.

First write or identify a failing negative test/audit that proves the current
legacy path, silent fallback, ignored field, placeholder resource, or mixed
renderer graph path can leak through. Then implement the smallest fix that makes
that test pass.

Hard constraints:
- Every parser allowlist field must be consumed into the target model. Unknown,
  legacy, or not-yet-modeled fields must fail-fast with diagnostics.
- No placeholder payloads may satisfy resource dependencies. A dependency must be
  truly parsed/registered, or the load/upload path must fail with diagnostics.
- Validation strictness must be selected by an explicit profile/property, not by
  path/name substring heuristics. Helmet, BMW, and 071 validation paths must not
  be able to opt out of Material v2 strict mode.
- Do not introduce a second public graph/contract system beside RenderPathGraph
  and RenderPassNode. If a temporary DTO is needed, keep it parser-local.
- Do not fix only the Forward happy path. Audit same-kind paths: Deferred,
  Shadow, PostProcess/Bloom, DebugOverlay, OfflineRT, validation profiles.
- Runtime-only FrameGraph branches are unfinished work unless the branch is
  explicitly dynamic and has a named removal/follow-up target. Bloom,
  PostProcess, Shadow, and DebugOverlay must either come from RenderPathGraph or
  fail-fast/diagnose why they are unsupported today.
- Shader/RenderFeature dependencies must resolve to live typed payloads before
  graph export/upload. Metadata-only resources may be listed for diagnostics but
  must not satisfy a renderable graph dependency.
- In helmet/BMW/071 validation profiles, legacy MaterialUBO, old per-draw
  descriptors, material tags, non-bindless fallback, and missing typed indices
  must fail-fast; they must not be hidden by a path that still renders.
- Remove superseded legacy implementation and tests in the same slice. Do not
  leave old material techniques, old pass-contract parsers, old material-tag
  checks, compatibility bridges, or their positive tests around as alternate
  truths. If a legacy test remains only as a negative audit, rename/comment it
  so it clearly proves rejection rather than preserving the old behavior.
- `src/test` is not a legacy-token exemption zone. Ordinary smoke, command,
  shader, scene-loader, or resource tests that still use `MaterialUBO`,
  `MaterialParams`, `materialTag`, old technique fields, or deleted parser names
  as positive fixtures must be migrated to Material v2/bindless fixtures or
  deleted. Only named negative audits may mention those tokens, with a narrow
  allowlist and an assertion that the old path is rejected.
- Keep verification warning-free for touched test targets. New compiler warnings
  such as ignored `[[nodiscard]]` handles are review findings, not harmless
  cleanup.

Completion report must include:
- the negative test/audit added or strengthened;
- rg audit results for relevant old tokens and same-kind paths;
- exact build/test commands run;
- any remaining unsupported/temporary path, with the call site named.
```

## Review Checklist

When evaluating an agent result, lead with findings:

| Check | Failure Pattern |
|---|---|
| Parser strictness | A field is accepted but not stored, validated, or reflected in output |
| Resource ownership | A graph dependency creates a dummy RenderFeature/Shader/Material instead of loading the real resource |
| Graph source of truth | Renderer parses a graph but then rewrites reads, writes, pass order, shaders, or targets in code |
| Scope parity | Forward is migrated but Deferred, Shadow, PostProcess, DebugOverlay, OfflineRT keep equivalent old paths |
| Validation hardness | A validation profile flag/env var can disable the 071 legacy-bridge ban, or strictness is inferred from filename/path tokens |
| Abstraction drift | New public `*ContractSet`, `*Library`, or bridge classes duplicate RenderPathGraph/RenderPassNode |
| Legacy leftovers | Old implementation files, CMake targets, or positive tests for removed material/pass/tag behavior remain after the new path exists |
| Test allowlist drift | A boundary audit allows broad legacy-token mentions in `src/test` instead of forcing ordinary tests off old fixtures |
| Test integrity | Tests check only source strings or positive examples, not behavior-breaking negative cases |
| Payload truth | A graph dependency is considered ready from metadata alone without a live typed Shader/RenderFeature payload |
| Build hygiene | Touched targets pass but emit new warnings that hide ownership or handle bugs |

## Task Template

Use this shape for small parallel tasks:

```text
Task: <one narrow behavior>

Files:
- <expected files>

Required negative test:
- <specific old path / illegal field / missing dependency that must fail before fix>

Implementation constraints:
- <2-5 constraints from the mandatory prefix that matter to this task>

Done when:
- <behavioral assertions>
- rg -n "<old tokens>" <roots> shows no production hits except named legacy tests/docs
- cmake --build build --target <targets>
- ./build/src/test/<tests>
```

## Common Corrections

- If a parser field is intentionally unsupported today, reject it explicitly. Do not accept and ignore it.
- If graph asset data needs runtime render-target objects, make the translation deterministic from graph source/target names. Do not overwrite graph dependencies ad hoc in the renderer after building the graph.
- If a graph references a feature or shader, register the real parsed dependency first. Missing dependencies are author errors.
- If a validation profile has special behavior, encode that behavior as a parsed
  profile field or enum. Do not infer it from asset path substrings.
- If an audit searches source strings, pair it with a behavior test that would fail if the old path still works.
- Delete legacy positive tests with the implementation they exercised. Keep only
  negative compatibility audits that assert old fields or paths are rejected.
- When an audit has an allowlist, make it narrower over time. Do not allow
  `MaterialUBO`, `MaterialParams`, `materialTag`, technique/parser names, or
  deleted test filenames in ordinary positive tests.
- If keeping a temporary runtime path is unavoidable, name it in code or diagnostics and add a failing future audit target for its removal.
