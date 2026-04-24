## Why

REQ-034 still lacks one real non-`blinnphong_0` material template example that exercises the current material asset path end to end. Without that example, material docs and loaders stay more conceptual than operational.

This change adds one concrete custom material example and uses it to anchor the current material-asset contract in code, demo usage, and notes.

## What Changes

- Add a real repository-owned custom material template example that is not `blinnphong_0`.
- Route the example through the current `.material` loading path.
- Reference the example from notes/tutorials instead of only describing the system abstractly.

## Capabilities

### New Capabilities
- `material-template-example`: A concrete non-`blinnphong_0` material example that exercises the current material-asset pipeline.

### Modified Capabilities

## Impact

- Affected code: material assets/loaders, demo or test consumer, notes
- Affected APIs: none expected
- Affected systems: material documentation, asset validation, example coverage
