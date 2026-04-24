## Why

`SceneNode` still keeps a raw `Scene*` back-reference even though the project's style guide rejects raw object-reference ownership patterns. The current design works only as long as destruction ordering stays benign and every caller remains careful.

This change hardens the scene back-reference contract so the relationship is explicit and safer.

## What Changes

- Replace or wrap the raw `Scene*` back-reference with an ownership-safe relationship.
- Define how back-references behave across attach/detach/destruction.
- Keep the public scene-node interaction model simple for current callers.

## Capabilities

### New Capabilities
- `scene-backref-ownership`: Ownership-safe back-reference contract between scene nodes and their parent scene.

### Modified Capabilities

## Impact

- Affected code: `src/core/scene/`
- Affected APIs: scene/node parent linkage internals
- Affected systems: ownership safety, lifecycle clarity
