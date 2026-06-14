# REQ-073-e Opaque Indirect Batching Implementation Plan

Date: 2026-06-14

## Superseded

This implementation plan has been superseded by
`docs/superpowers/plans/2026-06-14-073-e2-render-work-compiler-single-path-hard-cut.md`
and by the completed Task 8/9 render input compiler hard cut.

Do not execute the original low-level steps from this plan. They targeted the
removed public queue/batch architecture and are no longer a valid code path.

## Current Implementation Surface

Current realtime opaque geometry work must use:

```text
RenderPathGraph input
  -> FramePass input contract
  -> RenderWorkCompiler raster policy
  -> typed RenderInput[] payloads
  -> RenderInputDesc[] facts
  -> Vulkan desc-backed execution
```

The active extension point is `RenderWorkCompiler` plus typed `RenderInput`
payloads and `RenderInputDesc` facts. `RenderInputDesc` stores validation,
pipeline, binding, resource dependency, diagnostic, and stats facts with
`inputIndex`; it does not own typed draw payloads.

Opaque and transparent behavior may add raster policy logic inside the current
compiler path. They must not recreate a separate public compiler or result model
for geometry submission.

## Remaining Use

Keep this file only as a pointer away from the obsolete plan. For implementation
work, use the e2 plan and current requirement docs instead:

- `docs/superpowers/plans/2026-06-14-073-e2-render-work-compiler-single-path-hard-cut.md`
- `notes/requirements/073-e-render-work-compiler-single-path-hard-cut.md`
- `notes/requirements/073-e-indirect-material-batching-and-diagnostics.md`
