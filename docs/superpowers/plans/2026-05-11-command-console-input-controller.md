# Command Console Input Controller Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract command-line behavior from `ConsolePanel` into a dedicated controller and make `Tab` / `Up` / `Down` work reliably through ImGui input callbacks.

**Architecture:** Add a focused `ConsoleInputController` under `src/core/editor/` that owns input state, history browsing, draft preservation, completion behavior, and callback handling. Keep `ConsolePanel` as a thin layout/output shell that renders the output region and delegates input behavior to the controller.

**Tech Stack:** C++20, ImGui `InputText` callback flags, existing `CommandBus` history/completion APIs, repo integration tests in `src/test/`.

---

## File Map

- Create: `src/core/editor/console_input_controller.hpp`
- Create: `src/core/editor/console_input_controller.cpp`
- Modify: `src/core/editor/console_panel.hpp`
- Modify: `src/core/editor/console_panel.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`

## Task 1: Add controller-focused failing coverage

**Files:**
- Modify: `src/test/integration/test_command_bus.cpp`
- Test: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Add failing controller-oriented tests**

Add new tests near the existing console-panel coverage for:

```cpp
void testConsoleInputControllerHistoryKeepsDraft();
void testConsoleInputControllerCompletionBehaviors();
void testConsoleInputControllerEscRestoresDraft();
```

The assertions should cover:

- `Up` captures the current draft before browsing history
- `Down` past the newest command restores that draft
- `Tab` completes a unique candidate
- `Tab` extends to the common prefix when multiple candidates share one
- `Tab` with unchanged prefix emits helper output lines
- `Esc` exits history browse and restores draft

- [ ] **Step 2: Build and run just the command-bus test target to confirm failure**

Run:

```bash
cmake --build build --target test_command_bus
./build/src/test/test_command_bus
```

Expected: compile or runtime failure because `ConsoleInputController` does not exist yet.

## Task 2: Introduce `ConsoleInputController`

**Files:**
- Create: `src/core/editor/console_input_controller.hpp`
- Create: `src/core/editor/console_input_controller.cpp`
- Test: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Define the controller interface**

Create `src/core/editor/console_input_controller.hpp` with a focused API similar to:

```cpp
#pragma once

#include "core/editor/command_bus.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core {

class ConsoleInputController final {
public:
  explicit ConsoleInputController(CommandBus &commandBus);

  [[nodiscard]] std::string inputText() const;
  void setInputText(std::string_view text);

  void submitCurrentInput();
  void autocomplete();
  void browseHistoryOlder();
  void browseHistoryNewer();
  void cancelHistoryBrowse();
  void dispatchUndo();
  void dispatchRedo();

  [[nodiscard]] std::string helperOutputText() const;
  void clearHelperOutput();

  [[nodiscard]] int inputTextCallback(struct ImGuiInputTextCallbackData *data);
  [[nodiscard]] int handleImGuiEvent(int eventFlag, struct ImGuiInputTextCallbackData *data);

private:
  void setInputFromHistoryIndex(usize historyIndex);
  void beginHistoryBrowseIfNeeded();
  void clearAfterSubmit();
  void appendHelperLine(std::string line);
  [[nodiscard]] static std::string trim(std::string_view text);
  [[nodiscard]] static std::string commonPrefix(const std::string &a,
                                                const std::string &b);

  CommandBus &m_commandBus;
  std::array<char, 512> m_inputBuffer{};
  std::optional<usize> m_historyBrowseIndex;
  std::optional<std::string> m_historyBrowseDraft;
  std::vector<std::string> m_helperLines;
};

} // namespace LX_core
```

- [ ] **Step 2: Implement the controller state machine**

Create `src/core/editor/console_input_controller.cpp` and move the shell logic out of `ConsolePanel`:

- submit through `CommandBus::dispatch(...)`
- preserve draft before the first `Up`
- restore draft when `Down` moves past the newest history entry
- restore draft on `Esc`
- use `CommandBus::complete(...)` for completion
- emit helper lines when multiple candidates cannot advance the common prefix

The ImGui-facing callback path should support:

```cpp
ImGuiInputTextFlags_CallbackCompletion
ImGuiInputTextFlags_CallbackHistory
```

and use `ImGuiInputTextCallbackData::EventFlag` plus `EventKey` to dispatch to
controller methods.

- [ ] **Step 3: Rebuild and rerun the focused test**

Run:

```bash
cmake --build build --target test_command_bus
./build/src/test/test_command_bus
```

Expected: the new controller tests now compile and pass, though `ConsolePanel`
may still fail until the panel is rewired.

## Task 3: Rewire `ConsolePanel` to use the controller

**Files:**
- Modify: `src/core/editor/console_panel.hpp`
- Modify: `src/core/editor/console_panel.cpp`
- Test: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Replace panel-owned shell state with the controller**

Update `src/core/editor/console_panel.hpp` to remove direct shell-state members:

- remove `m_inputBuffer`
- remove `m_historyBrowseIndex`
- remove panel-owned `autocompleteInput()` / history browse internals
- add:

```cpp
#include "core/editor/console_input_controller.hpp"
```

and store:

```cpp
ConsoleInputController m_inputController;
```

- [ ] **Step 2: Update the panel draw path**

In `src/core/editor/console_panel.cpp`:

- render command history output as before
- append controller helper output after command-bus history in `displayedText()`
- replace outer-frame `IsKeyPressed(Tab/Up/Down)` polling with:

```cpp
ImGuiInputTextFlags_EnterReturnsTrue |
ImGuiInputTextFlags_CallbackCompletion |
ImGuiInputTextFlags_CallbackHistory
```

and pass the controller callback into `ImGui::InputText(...)`.

Keep `Ctrl+Z` / `Ctrl+Y` dispatch as explicit console shortcuts, but remove the
old polling path for `Tab`, `Up`, and `Down`.

- [ ] **Step 3: Keep compatibility helpers only where still useful**

Preserve thin compatibility methods if tests or callers still need them:

```cpp
void ConsolePanel::submitLine(std::string_view line);
void ConsolePanel::submitCurrentInput();
void ConsolePanel::setInputText(std::string_view text);
std::string ConsolePanel::getInputText() const;
std::string ConsolePanel::displayedText() const;
```

They should delegate to the controller instead of duplicating logic.

- [ ] **Step 4: Rebuild and rerun the command-bus test**

Run:

```bash
cmake --build build --target test_command_bus
./build/src/test/test_command_bus
```

Expected: `test_command_bus passed`

## Task 4: Extend integration coverage for callback behavior

**Files:**
- Modify: `src/test/integration/test_command_bus.cpp`
- Test: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Add direct callback-path assertions**

Add at least one test that exercises the ImGui-callback-facing controller entry
without requiring a full live editor window, for example by constructing the
controller and calling `handleImGuiEvent(...)` or `inputTextCallback(...)`
through a small fake `ImGuiInputTextCallbackData`.

The test should prove that the callback path, not just the direct methods,
routes:

- completion on `Tab`
- history browse on `Up`
- history browse on `Down`

- [ ] **Step 2: Rebuild and rerun the command-bus test**

Run:

```bash
cmake --build build --target test_command_bus
./build/src/test/test_command_bus
```

Expected: `test_command_bus passed`

## Task 5: Full verification and commit

**Files:**
- Modify: `src/core/editor/console_input_controller.hpp`
- Modify: `src/core/editor/console_input_controller.cpp`
- Modify: `src/core/editor/console_panel.hpp`
- Modify: `src/core/editor/console_panel.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Run verification**

Run:

```bash
cmake --build build --target test_command_bus test_lxe_editor_layout test_debug_ui_smoke lxe_editor
./build/src/test/test_command_bus
./build/src/test/test_lxe_editor_layout
./build/src/test/test_debug_ui_smoke
timeout 8s xvfb-run -a ./build/src/demos/lxe_editor/lxe_editor
```

Expected:

- `test_command_bus passed`
- `lxe_editor` layout test passes
- debug UI smoke passes
- demo starts successfully under `xvfb-run` and exits via timeout without crash

- [ ] **Step 2: Commit**

```bash
git add src/core/editor/console_input_controller.hpp \
        src/core/editor/console_input_controller.cpp \
        src/core/editor/console_panel.hpp \
        src/core/editor/console_panel.cpp \
        src/test/integration/test_command_bus.cpp
git commit -m "feat: add command console input controller"
```
