## ADDED Requirements

### Requirement: Root renderer executable has one documented role
The project SHALL define the root `Renderer` executable as either a bootstrap/probe utility or a formal user-facing app entrypoint. The codebase and docs MUST describe the same role.

#### Scenario: entry contract is unambiguous
- **WHEN** a developer inspects the build targets and notes
- **THEN** the purpose of the root executable is explicit and does not conflict with `scene_viewer`

### Requirement: Bootstrap helper plumbing is centralized
Environment/debug helper logic used during renderer bootstrap SHALL be centralized or removed if unused. Duplicate helper paths with the same semantic role MUST NOT remain as parallel dead code.

#### Scenario: bootstrap helper source of truth is unique
- **WHEN** the renderer bootstrap path reads environment/debug toggles
- **THEN** that behavior comes from one maintained helper path rather than duplicated ad hoc utilities
