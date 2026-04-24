## Why

Asset startup still depends on cwd heuristics like `cdToWhereAssetsExist()`, and the project has no formal packaging/deployment path for assets and compiled shader outputs. That is acceptable for local experiments but not as a stable engine contract.

This change formalizes runtime asset-path expectations and the path from development assets to packaged/distributed assets.

## What Changes

- Replace cwd-guessing assumptions with an explicit asset-path contract.
- Define how packaged/runtime asset roots are located.
- Define how shader outputs and asset content participate in packaging/distribution.
- Align docs and helper utilities with the formal asset path model.

## Capabilities

### New Capabilities
- `asset-packaging-paths`: Formal runtime asset-root and packaging-path contract for development and packaged execution.

### Modified Capabilities

## Impact

- Affected code: asset path helpers, startup/bootstrap, packaging notes/build flow
- Affected APIs: asset-root discovery helpers
- Affected systems: distribution, startup reliability, docs
