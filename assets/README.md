# Runtime Asset Baseline

This directory contains the runtime assets committed with LXEngine. Current
runtime acceptance is intentionally limited to the retained Helmet and BMW M6
asset chains plus shared material, shader, texture, and environment resources.

## Retained Chains

| Asset chain | Location | Purpose |
|---|---|---|
| Damaged Helmet | `models/damaged_helmet/` | Helmet PBR realtime/offline validation |
| BMW M6 | `../data/scenes/bmw-m6/` | BMW M6 scene conversion and rendering validation |
| Shared environment maps | `env/` | Retained lighting/environment inputs |
| Shared materials | `materials/` | Retained Material v2 runtime material files |
| Shared shaders | `shaders/` | Retained GLSL/SPIR-V shader assets |

Legacy demo/sample assets were removed by the material hard cut. New runtime
acceptance assets should be added only with an explicit requirement and budget
review.

## Disallowed Additions

- Large sample scenes without an approved requirement.
- External download scripts or submodules for runtime assets.
- Compatibility assets for deleted material-local demos.

## Path Lookup

Code uses `cdToWhereAssetsExist(subpath)` to locate the repository runtime asset
root:

```cpp
#include "core/utils/filesystem_tools.hpp"

if (cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")) {
    // cwd now points at a directory containing assets/.
}
```
