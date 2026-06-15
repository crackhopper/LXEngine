# REQ-073-b Scope Split Design

Date: 2026-06-13

## Decision

The original `REQ-073-b` scope was too large for one continuous implementation cycle. It mixed five different failure domains:

- source-reflected material storage and bindless-ready upload data;
- RenderPath material source shader variants;
- `techniques/...` to `render_paths/...` URI migration;
- indirect material batching;
- realtime old-path hard cut and Helmet/BMW smoke;
- OfflineRT RenderPathGraph migration that was originally numbered after `073-b`.

We split the realtime/material work into five consecutive requirements and move OfflineRT later:

| New REQ | Scope |
|---|---|
| `REQ-073-b` | Material storage and bindless upload foundation |
| `REQ-073-c` | Material source shader variant boundary |
| `REQ-073-d` | RenderPath shader URI migration and terminology hard cut |
| `REQ-073-e` | Indirect material batching and diagnostics |
| `REQ-076-b` | Realtime material path hard cut and smoke |
| `REQ-076-c` | OfflineRT RenderPathGraph compute path |
| `REQ-076-d` | OfflineRT config hard cut and smoke |

## Rationale

The user requirement is to keep the contract clean and expose problems early. If rendering cannot proceed, the implementation should stop and diagnose the missing capability rather than hide the problem behind fallback material, debug color, per-material descriptor fallback, or skipped draws.

The split keeps that principle enforceable:

- `073-b` can fail on missing source signature, default texture slot, material record layout, or bindless-ready table data.
- `073-c` can fail on missing source variant, unsupported source capability, or final shader reflection mismatch.
- `073-d` can fail on stale `techniques/...` URI or terminology fallback without also touching batching.
- `073-e` can fail on invalid table indexes or explain batch split reasons without also deleting fallback paths.
- `076-b` deletes/isolates the fallback paths only after the foundation, final shader identity, URI migration, and indirect batching are testable.

Moving OfflineRT to `076-c/d` avoids an ordering problem where OfflineRT would appear before the realtime hard cut it depends on. The old files are kept as the same conceptual work, but renumbered.

## Downstream Impact

Active requirement references were updated so:

- realtime hard cut means `REQ-076-b`, not `REQ-073-b`;
- OfflineRT hard cut means `REQ-076-d`, not old `REQ-073-e`;
- package/canonical readiness checks re-run `REQ-076-b` and `REQ-076-d` audits;
- texture compression and package work happen after both clean gates.

Historical implementation plans may still mention older shader directories as examples, but direct file paths and downstream references should point at the new requirement filenames.

## Non-goals

This design does not implement code. It only reshapes active requirements and dependency references before the next implementation cycle.
