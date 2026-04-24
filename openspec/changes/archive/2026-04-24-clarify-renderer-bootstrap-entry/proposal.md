## Why

The root `Renderer` executable is now only a thin environment bootstrap, but its role relative to `scene_viewer` is still ambiguous. The same part of the code also contains half-connected helper functions and duplicated environment toggles that obscure the intended entry flow.

This change gives the top-level executable a clear contract and removes misleading dead helper behavior around renderer bootstrap/debug plumbing.

## What Changes

- Define whether the top-level `Renderer` target is a bootstrap probe or a real app entry.
- Align docs and build targets with that decision.
- Remove or consolidate dead/duplicated helper logic around environment toggles and unused renderer helpers.

## Capabilities

### New Capabilities
- `renderer-bootstrap-entry`: Contract for the root executable, bootstrap responsibilities, and shared debug/env helper plumbing.

### Modified Capabilities

## Impact

- Affected code: root `CMakeLists.txt`, `src/main.cpp`, backend helper utilities
- Affected APIs: executable role, internal helper structure
- Affected systems: developer entrypoint expectations, documentation clarity
