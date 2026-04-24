# Asset Packaging Paths

## Requirements

### Requirement: Runtime asset lookup uses an explicit asset-root contract
Runtime asset lookup SHALL use an explicit asset-root contract and MUST NOT rely on current-working-directory guessing as the primary discovery mechanism.

#### Scenario: runtime starts without cwd heuristics
- **WHEN** the engine starts in a directory that differs from the source-tree working directory
- **THEN** asset lookup still follows an explicit asset-root contract rather than cwd guessing

### Requirement: Development and packaged asset layouts are both defined
The project SHALL define the expected asset/shader layout for both development execution and packaged/distributed execution.

#### Scenario: packaged layout expectations are documented
- **WHEN** a developer prepares a packaged-style run
- **THEN** the required runtime layout for assets and shader outputs is explicit

### Requirement: Asset path helpers reflect the formal contract
Asset path helper utilities SHALL reflect the formal asset-root and packaging-path contract rather than embedding undocumented fallback heuristics.

#### Scenario: helper behavior matches documented path model
- **WHEN** code requests runtime asset paths through helper utilities
- **THEN** the returned paths follow the documented asset-root contract
