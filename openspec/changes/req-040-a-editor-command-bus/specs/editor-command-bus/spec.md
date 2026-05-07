## ADDED Requirements

### Requirement: CommandBus dispatches text commands with in-band errors
The editor command subsystem SHALL expose a `CommandBus` that registers handlers by verb, tokenizes one-line text commands, dispatches the matching handler, and records the result into history. Unknown verbs, parse failures, and handler exceptions SHALL return `CommandResult{ok=false, ...}` instead of throwing to the caller.

#### Scenario: Dispatch records a successful command in history
- **WHEN** a caller registers verb `echo` and dispatches `echo hello`
- **THEN** the handler receives one argument `hello`
- **AND** `history().back().line` equals `echo hello`
- **AND** `history().back().result.ok` is `true`

#### Scenario: Handler exception is converted to a failed result
- **WHEN** a registered handler throws `std::runtime_error("boom")`
- **THEN** `dispatch(...)` returns `ok=false`
- **AND** the result message contains `exception:`

### Requirement: CommandBus supports quoted tokens and script dispatch
The tokenizer SHALL support whitespace-delimited verbs/arguments, double-quoted multi-word tokens, and the escapes `\\`, `\"`, and `\n`. The subsystem SHALL also expose `dispatchScript(std::string_view)` that splits by line, skips empty lines and `#`-prefixed comment lines, and continues after command failures.

#### Scenario: Quoted token stays intact
- **WHEN** a caller dispatches `select "node with spaces"`
- **THEN** the selected handler receives exactly one argument with value `node with spaces`

#### Scenario: Script dispatch continues after a failure
- **WHEN** a script contains three commands and the middle command fails
- **THEN** `dispatchScript(...)` returns three results
- **AND** the third command still runs

### Requirement: EditorState tracks nullable selection without ownership
The editor subsystem SHALL expose an `EditorState` object that stores the currently selected `SceneNode` without taking ownership. Querying the selection after the underlying node expires SHALL return no selection.

#### Scenario: Expired selected node clears naturally
- **WHEN** `EditorState` selects a `SceneNodeSharedPtr` and that shared owner is later destroyed
- **THEN** `getSelected()` returns an empty shared pointer
