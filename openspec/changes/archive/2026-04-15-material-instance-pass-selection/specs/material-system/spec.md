## ADDED Requirements

### Requirement: MaterialInstance owns instance-level pass enable state
`MaterialTemplate` SHALL remain the owner of pass definitions through its `pass -> RenderPassEntry` mapping. `MaterialInstance` SHALL own only the instance-level enabled subset of those template-defined passes.

A newly created `MaterialInstance` MUST enable every pass defined by its template by default.

`MaterialInstance` SHALL provide instance-level APIs whose semantics cover at minimum:

- querying whether a pass is enabled for the instance
- enabling or disabling a specific pass for the instance
- querying the set of currently enabled passes

If a caller attempts to enable or disable a pass that is not defined by the template, the system MUST emit a `FATAL` log and terminate the process immediately.

#### Scenario: New instance starts with all template passes enabled
- **WHEN** a `MaterialTemplate` defines both `Pass_Forward` and `Pass_Shadow` and a `MaterialInstance` is created from that template
- **THEN** the new instance reports both passes as enabled before any explicit pass-state mutation

#### Scenario: Undefined pass enable request terminates
- **WHEN** `setPassEnabled(Pass_Deferred, false)` is called on a `MaterialInstance` whose template does not define `Pass_Deferred`
- **THEN** the system logs a `FATAL` error and terminates immediately

### Requirement: getPassFlag is derived from defined and enabled passes
`MaterialInstance::getPassFlag()` SHALL be derived from the intersection of:

- passes defined by the template
- passes currently enabled on the instance

The implementation MUST NOT treat a separate manually maintained bitmask as the authoritative truth if that value can diverge from the instance's enabled pass set.

#### Scenario: Disabled template pass is absent from getPassFlag
- **WHEN** a template defines `Pass_Forward | Pass_Shadow`, the instance disables `Pass_Shadow`, and `getPassFlag()` is queried
- **THEN** the returned `ResourcePassFlag` includes `Forward` and excludes `Shadow`

### Requirement: Material render-state queries are pass-aware
Material render-state queries SHALL be pass-aware. The system MUST provide a material render-state lookup path keyed by `StringID pass`, and `MaterialInstance` render-state access MUST resolve the `RenderState` from the corresponding template-defined `RenderPassEntry` for that pass.

The implementation MUST NOT preserve a Forward-only transitional meaning as the authoritative material render-state contract.

#### Scenario: Forward and shadow passes return different render states
- **WHEN** a template defines distinct `RenderState` values for `Pass_Forward` and `Pass_Shadow`
- **THEN** querying the material render state for `Pass_Forward` returns the forward state and querying it for `Pass_Shadow` returns the shadow state

### Requirement: Ordinary material parameter updates are not structural pass changes
Instance-level runtime parameter updates such as `setFloat`, `setInt`, `setVec*`, `setTexture`, and `updateUBO` SHALL NOT be treated as structural pass-state changes. These operations MUST continue to affect only runtime material data and resource-dirty propagation.

Only pass enable/disable mutations are structural changes within `MaterialInstance` for the purposes of pass participation and `SceneNode` revalidation.

#### Scenario: UBO write leaves enabled pass set unchanged
- **WHEN** `setFloat(...)` and `updateUBO()` are called on a `MaterialInstance`
- **THEN** the instance's enabled pass set and derived `getPassFlag()` value remain unchanged
