## Context

The renderer now has richer assets, demos, and shader pipelines, but runtime discovery still leans on development-directory assumptions. The project needs an explicit asset-root contract before packaging/release work can be treated as real engine functionality.

## Goals / Non-Goals

**Goals:**
- Define a formal runtime asset-root discovery contract.
- Remove cwd-guessing as the primary runtime behavior.
- Clarify how packaged asset/shader outputs are expected to be laid out.

**Non-Goals:**
- No full asset database or GUID system in this change.
- No complete installer/distribution pipeline implementation beyond the path contract.

## Decisions

- Treat asset-root discovery as a runtime contract, not a fallback heuristic.
- Keep development and packaged layouts explicit and documented.
- Include shader artifacts in the packaging-path story because runtime rendering depends on them.

## Risks / Trade-offs

- [Packaging contract may constrain future tooling] → Keep the first version simple and explicit rather than tool-specific.
- [Existing local workflows may rely on cwd heuristics] → Provide a clear migration path and helper updates.
