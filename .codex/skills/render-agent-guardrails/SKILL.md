---
name: render-agent-guardrails
description: Use when scoping, prompting, implementing, or reviewing LXEngine rendering architecture work or requirement analysis involving Material, RenderPathGraph, RenderFeature, SceneResourceTable, FrameGraph, Vulkan render paths, offline/realtime equivalence, bindless validation, legacy bridge removal, 3DGS, or non-mesh rendering.
---

# Render Agent Guardrails

## Overview

Use this skill to keep LXEngine rendering architecture work narrow, evidence-driven, and aligned with the current codebase. It applies to requirement analysis, task prompting, implementation, and review across Material, RenderPathGraph, SceneResourceTable, FrameGraph, Vulkan realtime/offline paths, package/restore, equivalence validation, and non-mesh rendering such as 3DGS.

The goal is a single coherent render architecture, not renamed compatibility layers, hidden fallbacks, or a second public graph/contract system.

## Mandatory Prefix

Add this prefix to rendering architecture agent tasks. For pure requirement analysis, treat tests/audits as acceptance evidence to specify or verify, not as permission to implement code unless the user asked for code changes.

```text
Use current repo facts only. Read the relevant active requirement, subsystem note,
and current code before changing scope. This task is not a rename-only task.

First identify a negative test, audit, diagnostic, or code fact that proves the
current legacy path, silent fallback, ignored field, placeholder resource, or
mixed renderer graph path can leak through. Then implement or specify the
smallest change that closes that path.

Hard constraints:
- Every parser allowlist field must be consumed into the target model. Unknown,
  legacy, or not-yet-modeled fields must fail-fast with diagnostics.
- No placeholder payloads may satisfy resource dependencies. A dependency must be
  truly parsed/registered, or the load/upload path must fail with diagnostics.
- Validation strictness must be selected by an explicit profile/property, not by
  path/name substring heuristics.
- Do not introduce a second public graph/contract system beside RenderPathGraph
  and RenderPassNode. If a temporary DTO is needed, keep it parser-local.
- Do not fix only one happy path. Audit same-kind paths: Forward, Deferred,
  Shadow, PostProcess/Bloom, DebugOverlay, OfflineRT, package restore, editor
  export, validation profiles, and non-mesh/3DGS paths when relevant.
- Runtime-only FrameGraph branches are unfinished work unless the branch is
  explicitly dynamic and has a named removal/follow-up target.
- Shader, RenderFeature, material, graph, and non-mesh resource dependencies must
  resolve to live typed payloads before graph export/upload. Metadata-only
  resources may be listed for diagnostics but must not satisfy renderable graph
  dependencies.
- In strict validation profiles, legacy MaterialUBO, old per-draw descriptors,
  material tags, non-bindless fallback, fake mesh/material resources, and missing
  typed indices must fail-fast; they must not be hidden by a path that still
  renders.
- Remove superseded legacy implementation and tests in the same slice. If a
  legacy test remains only as a negative audit, rename/comment it so it clearly
  proves rejection rather than preserving old behavior.
- Scene authoring, RenderPathGraph authoring, and RenderFeature authoring are
  separate contracts. Do not satisfy a missing graph/feature dependency by
  injecting scene nodes, duplicating skybox/environment nodes, or adding
  filename/path-based C++ branches.
- Finite skybox nodes are ordinary scene geometry with ordinary material
  binding. Infinite skyboxes are graph/render-feature background effects and
  ray-miss resources. Do not special-case finite skybox materials in code.
- IBL lighting and visible skybox background are independent. A render path may
  show an infinite skybox background without surface IBL only when the graph
  explicitly omits `feature.environmentLighting`; do not infer IBL from a
  skybox node or remove IBL from an editor graph without explicit intent.
- Batching mode is a contract, not a performance hint. Raster
  `batching.mode: material` must group by material source/layout and bind the
  matching shader variant plus matching source-material storage. Raster shaders
  must not read a mixed-layout global source-material array. `batching.mode:
  all` is only valid when the shader defines a uniform runtime-dispatch ABI
  such as OfflineRT ray hit tables/material records.
- Ray visibility belongs in ray program data such as hit shader table entries
  and derived primitive flags. Do not fix shadow, miss, or visibility problems
  by checking scene node names, material names, mesh paths, or skybox modes in
  backend/shader code.
- Editor realtime, realtime profile render, and offline render must consume the
  same RenderPathGraph/FrameGraph/RenderWorkCompiler facts. The only accepted
  differences are target/swapchain/readback plumbing. A visual fix is not done
  until editor and offline/profile paths are checked against the same scene and
  render path semantics.
- `src/test` is not a legacy-token exemption zone. Ordinary smoke, command,
  shader, scene-loader, resource, or editor tests that still use old fixtures as
  positive coverage must be migrated or deleted. Only named negative audits may
  mention those tokens, with a narrow allowlist and an assertion that the old
  path is rejected.
- Keep verification warning-free for touched test targets. New compiler warnings
  such as ignored `[[nodiscard]]` handles are review findings, not harmless
  cleanup.

Completion report must include:
- the negative test/audit/diagnostic added, strengthened, or specified;
- rg audit results for relevant old tokens and same-kind paths;
- exact build/test/docs commands run;
- any remaining unsupported or temporary path, with the call site and owner
  requirement named.
```

## Requirement Analysis Checklist

When reviewing or rewriting requirements, ground each change in current code:

| Check | What To Do |
|---|---|
| Current state | Name the exact existing code path, asset, parser, test, or diagnostic that proves the feature is present, missing, or obsolete |
| Active scope | Keep only work that belongs to the current implementation window; archive completed, superseded, or intentionally deferred scope |
| Architecture owner | Assign leftover bridges to a specific active REQ instead of leaving broad compatibility language |
| Acceptance evidence | Convert vague goals into tests, audits, smoke scenes, diagnostics, or source-analysis updates |
| Cross-path parity | State whether Forward, Deferred, OfflineRT, editor export, package restore, and non-mesh paths need matching changes |
| Legacy closure | Say what old tokens, positive fixtures, branches, or commands must disappear |
| Non-mesh boundary | For 3DGS or similar resources, do not describe them as mesh/material shortcuts; require explicit resource, pass, and dispatch contracts |

## Review Checklist

When evaluating an agent result, lead with findings:

| Check | Failure Pattern |
|---|---|
| Parser strictness | A field is accepted but not stored, validated, or reflected in output |
| Resource ownership | A graph dependency creates a dummy RenderFeature, Shader, Material, Mesh, or splat resource instead of loading the real resource |
| Graph source of truth | Renderer parses a graph but then rewrites reads, writes, pass order, shaders, targets, or dispatch mode in code |
| Scope parity | One path is migrated while Deferred, Shadow, PostProcess, DebugOverlay, OfflineRT, package restore, editor export, or non-mesh paths keep equivalent old behavior |
| Validation hardness | A validation flag/env var can disable the legacy-bridge ban, or strictness is inferred from filename/path tokens |
| Abstraction drift | New public `*ContractSet`, `*Library`, `*Graph`, or bridge classes duplicate RenderPathGraph/RenderPassNode |
| Legacy leftovers | Old implementation files, CMake targets, commands, assets, or positive tests for removed behavior remain after the new path exists |
| Test allowlist drift | A boundary audit allows broad legacy-token mentions in `src/test` instead of forcing ordinary tests off old fixtures |
| Test integrity | Tests check only source strings or positive examples, not behavior-breaking negative cases |
| Payload truth | A dependency is considered ready from metadata alone without a live typed payload |
| Build hygiene | Touched targets pass but emit new warnings that hide ownership, lifetime, or handle bugs |
| Skybox boundary | Finite skybox is handled by special-case material/texture code, or infinite skybox is represented by duplicate geometry nodes |
| IBL/background coupling | A graph shows skybox but silently drops/adds surface IBL, or editor uses a no-IBL graph where the scene/profile expects IBL |
| Batching/layout mismatch | A raster material batch binds source-material records from a different material source layout, or `all` batching is used without a shader-side runtime dispatch table |
| Ray visibility special-case | A finite skybox or unlit material is skipped for shadows by node/path/material-name checks instead of a hit table / primitive flag contract |
| Parity gap | A fix is verified only in offline/profile render while editor live uses a different graph, or vice versa |

## Task Template

Use this shape for small parallel tasks:

```text
Task: <one narrow behavior>

Files:
- <expected files>

Required negative test or audit:
- <specific old path / illegal field / missing dependency / fake resource that
  must fail before or be proved by current code>

Implementation constraints:
- <2-5 constraints from the mandatory prefix that matter to this task>

Done when:
- <behavioral assertions>
- rg -n "<old tokens>" <roots> shows no production hits except named legacy tests/docs
- cmake --build build --target <targets>
- ./build/src/test/<tests> or ctest --test-dir build --output-on-failure -R "<tests>"
```

## Common Corrections

- If a parser field is intentionally unsupported today, reject it explicitly. Do not accept and ignore it.
- If graph asset data needs runtime render-target objects, make the translation deterministic from graph source/target names. Do not overwrite graph dependencies ad hoc in the renderer after building the graph.
- If a graph references a feature, shader, material, package section, or non-mesh resource, register the real parsed dependency first. Missing dependencies are author errors.
- If a validation profile has special behavior, encode that behavior as a parsed profile field or enum. Do not infer it from asset path substrings.
- If an audit searches source strings, pair it with a behavior test that would fail if the old path still works.
- Delete legacy positive tests with the implementation they exercised. Keep only negative compatibility audits that assert old fields or paths are rejected.
- When an audit has an allowlist, make it narrower over time. Do not allow legacy tokens, deleted parser names, fake mesh/material paths, or deleted test filenames in ordinary positive tests.
- If keeping a temporary runtime path is unavoidable, name it in code or diagnostics and add a dated owner requirement for its removal.
