# Command Console Input Controller Design

## Context

`Command Console` already exposes command submission, output rendering, history
display, and simple shell-like helpers such as autocomplete and history browse.
However, the current implementation keeps those behaviors directly inside
`ConsolePanel::draw()` and relies on per-frame key polling around an ImGui
`InputText` widget.

That structure has two problems:

- it is not reliable for `Tab` / `Up` / `Down` handling because `InputText`
  owns keyboard focus and may consume those keys before outer polling sees them
- it mixes panel layout, shell state, and command-bus interaction into one UI
  class, making the code harder to study and extend

The next change should turn console input into an explicit, isolated subsystem
with shell-grade behavior while keeping `CommandBus` itself UI-agnostic.

## Goals

- Make `Tab`, `Up`, and `Down` work reliably while the command input box has
  focus.
- Stop using outer-frame key polling for completion and history navigation.
- Extract command-line interaction state and behavior out of `ConsolePanel`.
- Keep `CommandBus` responsible only for dispatch, history storage, and
  completion candidates.
- Preserve existing visible editor behavior for submit, undo, redo, and command
  output except where this design explicitly changes it.

## Non-Goals

- Do not add fuzzy search, reverse history search, popup completion menus, or
  shell scripting features in this change.
- Do not redesign the surrounding console panel layout.
- Do not move command history storage out of `CommandBus`.

## Recommended Approach

### Option A: Independent input controller with thin ImGui adapter

Create a dedicated `ConsoleInputController` that owns console input state and a
small ImGui callback bridge.

Pros:

- reliable `InputText` key handling through ImGui callback hooks
- explicit boundary between UI panel and shell behavior
- easiest path for future extensions
- most testable design, because the state machine can be exercised without
  requiring a full ImGui frame

Cons:

- introduces a new class and a small amount of adapter code

### Option B: Keep logic in `ConsolePanel`, but switch to ImGui callbacks

Move from polling to callback handling, but leave the logic in the panel class.

Pros:

- smaller edit set

Cons:

- leaves layout, state machine, and command interaction coupled together
- not aligned with the goal of making the console logic easier to study and
  extend

### Option C: Fully custom command-line widget

Replace `InputText` with a custom text editing widget and own the entire
interaction stack.

Pros:

- maximum long-term control

Cons:

- unnecessary for the current problem
- larger maintenance burden than justified

### Recommendation

Use Option A.

It fixes the current reliability issue through the officially supported ImGui
path while also creating a clean module boundary for future shell features.

## Architecture

### Module boundaries

- `ConsolePanel`
  - owns panel visibility and output-region rendering
  - delegates command-line behavior to a controller
  - remains the integration point between the editor UI and the console widget
- `ConsoleInputController`
  - owns input buffer, draft preservation, history-browse cursor, and completion
    behavior
  - submits commands through `CommandBus`
  - queries completion candidates through `CommandBus`
  - exposes a thin ImGui-facing API used by `ConsolePanel`
- `CommandBus`
  - remains unchanged in role
  - stores canonical command history
  - performs dispatch and completion lookup

### File shape

Add new files under `src/core/editor/`:

- `console_input_controller.hpp`
- `console_input_controller.cpp`

`ConsolePanel` should become a thin consumer of the controller.

## Input Behavior

### Submit

- `Enter` submits the current input through `CommandBus::dispatch(...)`.
- Submission success or failure follows the existing command-bus history model.
- After submit, the input box is cleared.
- After submit, history-browse mode exits and any saved draft is cleared.

### History navigation

- `Up` enters history browse mode and selects the previous command.
- `Down` navigates toward newer commands.
- Before the first history step, if the user has in-progress input, that text is
  saved as a draft.
- When browsing moves past the newest history entry, the original draft is
  restored instead of replacing it with an empty string.
- `Esc` exits history-browse mode and restores the saved draft.

### Completion

- `Tab` triggers completion.
- If there is exactly one candidate, the input is completed and a trailing space
  is appended when appropriate.
- If there are multiple candidates with a longer common prefix, the input is
  extended to that common prefix.
- If there are multiple candidates and the common prefix would not change the
  current input, the controller emits a console-visible candidate listing.
- Candidate listing is appended to the visible console output as console-local
  helper text; it does not create fake `CommandBus` history entries.

### Undo/redo shortcuts

- `Ctrl+Z` and `Ctrl+Y` keep their current command-level undo/redo meaning.
- They remain scoped to the command input focus path.
- This change does not redefine them as text-edit undo/redo.

### Clipboard behavior

- Keep ImGui’s default selection and clipboard behavior for the input widget.
- Do not introduce custom clipboard handling in this change.

## Controller API Shape

The controller should expose a small, explicit API. Exact names may differ, but
the responsibilities should map roughly to:

- set or read current input text
- render or drive the input widget
- submit current text
- clear state after submit
- expose any console-local helper lines generated by completion
- expose testable operations for history and completion handling

The state machine core should be usable without a live ImGui frame where
practical. The ImGui callback layer should stay thin and translate callback
events into controller operations.

## Output Model

The visible console output should continue to include command history entries
from `CommandBus`.

Additionally, the console may append controller-local helper output for UI-only
events such as:

- completion candidate lists

This helper output must stay visually distinct from real command dispatch
results and must not pollute undo/redo or command replay semantics.

## Testing

### New controller-focused coverage

Add tests that cover:

- `Up` history navigation
- `Down` history navigation
- draft preservation before entering history browse
- draft restoration after returning past the newest history entry
- `Esc` exiting history browse
- unique-candidate completion
- multi-candidate completion to common prefix
- unchanged-prefix completion emitting candidate output
- submit clearing input and browse state

### Existing panel coverage

Keep `ConsolePanel` tests focused on integration:

- panel still routes command submission correctly
- displayed output still reflects command-bus history plus any controller helper
  text

Do not leave the detailed shell-state assertions trapped inside `ConsolePanel`
tests once the controller exists.

## Error Handling

- If completion returns no candidates, the input stays unchanged and no helper
  output is emitted.
- Empty submissions remain no-ops, following current trim-and-ignore behavior.
- Controller-local helper output must not throw or corrupt command-bus state
  when history is empty.

## Acceptance Criteria

- On Windows, with the command input focused, `Tab` completion works reliably.
- On Windows, with the command input focused, `Up` and `Down` reliably browse
  command history.
- Browsing history preserves and restores in-progress draft input.
- When completion cannot choose a single candidate and cannot extend the common
  prefix, the console shows a candidate list.
- `ConsolePanel` no longer owns the detailed shell behavior internally.
- The new behavior is covered by dedicated controller-oriented tests.
