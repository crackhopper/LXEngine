## Why

REQ-019 is still blocked only by manual acceptance in a real display environment. Code paths exist, but without a formal checklist and closure flow, the demo remains "implemented but not accepted."

This change turns the remaining manual acceptance work into an explicit change artifact so it can be executed, recorded, and archived cleanly.

## What Changes

- Define the required manual acceptance checklist for `demo_scene_viewer`.
- Capture Linux display-environment expectations for running the checklist.
- Define completion evidence needed to archive the remaining REQ-019 work.

## Capabilities

### New Capabilities

### Modified Capabilities
- `demo-scene-viewer`: Manual acceptance requirements for the maintained scene-viewer demo are formalized and archivable.

## Impact

- Affected code: none required by default
- Affected systems: release confidence, requirement closure, demo validation
- Affected process: manual acceptance and archival workflow
