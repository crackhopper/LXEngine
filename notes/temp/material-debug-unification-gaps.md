# Material Debug Unification Gaps

## Gap 1: Core-only DebugDraw fallback still bypasses authored material assets

- Logic: `lxe_editor` now injects `assets/materials/debug_line.material` into `DebugDraw`, so editor debug lines use the same material asset loading path as authored materials.
- Remaining gap: `src/core/debug_draw/debug_draw.cpp` still has a code-created fallback material for core-only callers that do not provide a material provider. This keeps `core` independent from `infra/material_loader`, but it is still a second definition of the debug line shader contract and render state.
- Risk: If the fallback remains long-term, its render state can drift from `debug_line.material`.
- Follow-up: Either require all DebugDraw users to inject a material provider, or move an asset-loading boundary above `DebugDraw` so the fallback can be deleted without making `core` depend on `infra`.
