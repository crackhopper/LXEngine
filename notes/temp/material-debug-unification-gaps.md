# Material Debug Unification Gaps

## Gap 1: Legacy debug line queue remains outside material path

- Logic: `src/core/debug_draw/debug_draw.cpp::pushLine` queues debug line vertices through the existing debug draw singleton instead of a material-authored scene renderable.
- Reason: The current implementation is frame-transient and does not own a scene node or material instance.
- Risk: Long-lived mesh debug visuals could split between material-driven renderables and the frame-transient debug line path.
- Follow-up: Introduce a scene-owned debug line renderable that uses `mesh_debug.material`, then keep `debug_draw` only for temporary gizmo/selection lines.
