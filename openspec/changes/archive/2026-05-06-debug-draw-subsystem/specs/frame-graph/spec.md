## ADDED Requirements

### Requirement: FrameGraph supports a debug overlay pass identity
The frame-graph capability SHALL define a `Pass_DebugOverlay` `StringID` alongside the existing pass constants. `FrameGraph` SHALL preserve configured pass order so renderers can schedule `Pass_DebugOverlay` after the main forward pass and before UI overlay work.

#### Scenario: Debug overlay pass is carried through frame-graph scheduling
- **WHEN** a `FrameGraph` is configured with `FramePass{Pass_Forward, target, {}}` followed by `FramePass{Pass_DebugOverlay, target, {}}`
- **THEN** `buildFromScene(scene)` rebuilds both queues in that same order
- **AND** `collectAllPipelineBuildDescs()` includes pipeline build descriptions from both passes
