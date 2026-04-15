## ADDED Requirements

### Requirement: SceneNode validates only currently enabled material passes
`SceneNode` structural validation SHALL cover every pass that is both:

- defined by the current `MaterialTemplate`
- currently enabled on the bound `MaterialInstance`

Passes that are defined on the template but disabled on the instance SHALL NOT participate in the node's structural validity requirements for that moment.

If any enabled pass fails validation, the system MUST emit a `FATAL` log and terminate the process immediately.

#### Scenario: Disabled shadow pass no longer blocks a valid forward node
- **WHEN** a template defines both `Pass_Forward` and `Pass_Shadow`, the instance disables `Pass_Shadow`, and the node is structurally valid only for `Pass_Forward`
- **THEN** the node remains valid and `Pass_Shadow` does not invalidate it while disabled

### Requirement: Material pass-state changes are structural revalidation events
Changes to a `MaterialInstance` pass enable/disable state SHALL be treated as structural changes for every `SceneNode` that references that instance. After such a change, every affected node MUST be revalidated against the new enabled-pass set before further pass participation queries are trusted.

Ordinary material parameter updates MUST NOT trigger this structural revalidation path.

#### Scenario: Enabling a previously disabled pass forces revalidation
- **WHEN** a node references a shared `MaterialInstance` and that instance enables `Pass_Shadow`
- **THEN** the node is revalidated for `Pass_Shadow` before `supportsPass(Pass_Shadow)` may return `true`

#### Scenario: Non-structural material write does not revalidate nodes
- **WHEN** `setTexture(...)` or `setFloat(...)` is called on a shared `MaterialInstance`
- **THEN** the affected nodes are not structurally revalidated solely because of that parameter write

### Requirement: Scene propagates shared MaterialInstance pass-state changes
`Scene` or an equivalent scene-level owner SHALL be responsible for propagating `MaterialInstance` pass-state changes to every `SceneNode` that references that instance. The material instance itself SHALL NOT own reverse references to nodes.

The scene-level API MAY use scanning or an index internally, but the externally observable behavior MUST be equivalent to a semantic operation such as `revalidateNodesUsing(materialInstance)`.

If any affected node fails validation under the new enabled-pass set, the system MUST emit a `FATAL` log and terminate immediately.

#### Scenario: Shared instance pass disable affects all referencing nodes
- **WHEN** two `SceneNode` objects in the same scene share one `MaterialInstance` and that instance disables `Pass_Shadow`
- **THEN** the scene propagates the change to both nodes and both nodes stop reporting support for `Pass_Shadow`
