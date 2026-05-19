# Material Debug Unification Gaps

## Gap 1: Debug lines bypass authored material assets

- Logic: `src/core/debug_draw/debug_draw.cpp` does create runtime `SceneNode`, `MeshComponent`, `MaterialComponent`, and `MaterialInstance` objects for debug line buckets.
- Reason: The gap is narrower: debug lines construct `DebugLineShader` and `MaterialTemplate` in code, so they bypass authored `.material` files and `GenericMaterialLoader`.
- Risk: Debug overlay line render state, shader binding, and per-vertex color behavior can drift from the asset-driven material path even though rendering still uses runtime material components.
- Follow-up: Add/load a dedicated `debug_line.material`, or extend the material asset format to express an equivalent `Pass_DebugOverlay` line material with per-vertex color. Do not reuse `mesh_debug.material` directly for current debug lines.
