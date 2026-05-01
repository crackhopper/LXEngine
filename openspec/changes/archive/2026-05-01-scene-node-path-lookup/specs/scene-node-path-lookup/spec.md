## ADDED Requirements

### Requirement: Scene nodes expose stable human-readable names and paths
The scene system SHALL allow each `SceneNode` to carry a human-readable `std::string` name for editor, command, and debugging workflows. The name SHALL NOT participate in pipeline identity or renderability contracts.

`SceneNode` SHALL expose:

- `setName(std::string)`
- `getName() const`
- `getPath() const -> std::string`

`setName(...)` SHALL reject `/` as a path separator character and SHALL sanitize illegal characters into `_` while asserting in debug-oriented builds. Empty names SHALL remain allowed.

`getPath()` SHALL return a root-based slash-delimited path. Root nodes (nodes without a parent) SHALL report `/`. If an ancestor on the path has an empty name, `getPath()` SHALL use a temporary `<unnamed-node-0xADDR>` placeholder segment for that position.

#### Scenario: named hierarchy produces full path
- **WHEN** a node named `arm` is attached under `player`, which is attached under `world`
- **THEN** `getPath()` returns `/world/player/arm`

#### Scenario: root path is slash
- **WHEN** a node has no parent
- **THEN** `getPath()` returns `/`

### Requirement: Scene can resolve nodes by slash-delimited path
`Scene` SHALL provide `findByPath(const std::string &path) const` to resolve nodes from slash-delimited scene paths.

If the input path does not begin with `/`, the implementation SHALL treat it as rooted at the scene root by prepending `/`. Empty segments created by repeated slashes SHALL be interpreted as empty-name segments rather than being silently collapsed.

`findByPath(...)` SHALL walk downward from the scene root, matching each path segment against child names using strict string equality. On success it SHALL return the matching node; on failure it SHALL return `nullptr`.

#### Scenario: full path lookup succeeds
- **WHEN** the scene contains nodes at `/world/player/arm`
- **THEN** `findByPath("/world/player/arm")` returns the `arm` node

#### Scenario: missing path returns null
- **WHEN** no node exists at `/world/missing`
- **THEN** `findByPath("/world/missing")` returns `nullptr`

#### Scenario: relative-root input is normalized
- **WHEN** `findByPath("world/player")` is called
- **THEN** it behaves identically to `findByPath("/world/player")`

### Requirement: Duplicate sibling names have deterministic lookup behavior
The scene system SHALL allow multiple children under the same parent to share the same name. In this case, path lookup SHALL resolve the first matching child in insertion order.

When the system detects duplicate names under the same parent during registration or traversal-sensitive setup, it SHALL emit a `WARN` log and continue.

#### Scenario: duplicate sibling name resolves first match
- **WHEN** one parent has two children both named `enemy`
- **THEN** `findByPath(...)` returns the first inserted `enemy` child

#### Scenario: duplicate sibling name emits warning
- **WHEN** duplicate child names are introduced under the same parent
- **THEN** the scene layer emits a `WARN` log and keeps the nodes registered

### Requirement: Scene can dump hierarchy as reversible text tree
`Scene` SHALL provide `dumpTree() const -> std::string` to render the current hierarchy as a text tree using box-drawing prefixes.

The dump output SHALL represent only structure and names, not transform or component details. Every concrete node path represented in the dump SHALL remain resolvable through `findByPath(...)`.

#### Scenario: dump tree reflects hierarchy
- **WHEN** a scene contains a root child `world`, with descendants `player` and `arm`
- **THEN** `dumpTree()` emits a text tree that places `arm` under `player` under `world`

#### Scenario: dump output paths are reversible
- **WHEN** a caller derives node paths from the structural lines of `dumpTree()`
- **THEN** each dumped path can be resolved back through `findByPath(...)`
