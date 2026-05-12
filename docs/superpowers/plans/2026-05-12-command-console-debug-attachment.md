# Command Console Debug Attachment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `lxe_editor` command console attach pick debug lines under the newest command entry, visually wrap long output, and visually wrap long input without turning the console into a true multiline command parser.

**Architecture:** Keep `CommandBus` as the canonical executed-command history, and move the console presentation to explicit per-entry rendering inside `ConsolePanel`. Store debug attachments as console-local UI state keyed to visible history entries, while keeping `ConsoleInputController` responsible for input state and callback behavior.

**Tech Stack:** C++20, Dear ImGui, existing `CommandBus` / `ConsoleInputController` / `ConsolePanel` editor infrastructure, CMake, Ninja, repo-local integration tests

---

## File Map

- Modify: `src/core/editor/console_panel.hpp`
  - Add console-local attachment state and any small helper structs/method declarations needed for per-entry rendering and append-to-latest-entry behavior.
- Modify: `src/core/editor/console_panel.cpp`
  - Replace giant-buffer output rendering with per-entry rendering, switch input to multiline display semantics, preserve command submit/history/completion behavior, and update `appendSystemLine()` to attach to the newest visible entry when possible.
- Modify: `src/core/editor/console_input_controller.hpp`
  - Add a narrow helper for sanitizing multiline-widget text back to a single logical command line before submit if the implementation chooses to normalize embedded line breaks at the controller boundary.
- Modify: `src/core/editor/console_input_controller.cpp`
  - Keep submit/history/completion semantics, but ensure the multiline widget path cannot accidentally persist literal newline-separated commands.
- Modify: `src/test/integration/test_command_bus.cpp`
  - Convert console assertions away from “single concatenated output blob” assumptions and add structural coverage for entry formatting, debug attachment placement, and clear/checkpoint behavior.
- Modify: `src/test/CMakeLists.txt`
  - Only if needed to add an additional ImGui-linked test source or keep target dependencies correct after test expansion. Do not touch if the current target graph already covers the edited tests.

## Task 1: Lock In Failing Console Attachment And Formatting Tests

**Files:**
- Modify: `src/test/integration/test_command_bus.cpp`
- Test: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Add a failing test for command/result formatting without the old `<` or `> ` result prefixes**

```cpp
void testConsolePanelFormatsCommandAndResultWithNewPrompts() {
  CommandFixture fixture;
  ConsolePanel panel(fixture.bus);

  panel.submitLine("select /world/cube");

  const std::string consoleText = panel.displayedText();
  EXPECT(consoleText.find("> select /world/cube") != std::string::npos,
         "console should render commands with the new prompt prefix");
  EXPECT(consoleText.find("\nselected: /world/cube") != std::string::npos,
         "console should render result messages without a prompt prefix");
  EXPECT(consoleText.find("< select /world/cube") == std::string::npos,
         "legacy command prefix should be removed");
  EXPECT(consoleText.find("\n> selected: /world/cube") == std::string::npos,
         "legacy result prefix should be removed");
}
```

- [ ] **Step 2: Add a failing test proving pick debug lines attach to the newest visible command entry**

```cpp
void testSceneViewerPickDebugLogsAttachToNewestCommandEntry() {
  SceneViewerPickFixture fixture;
  const CommandResult enable = fixture.bus.dispatch("debug on");
  EXPECT(enable.ok, "debug on should succeed before pick logging");

  const CommandResult pick = fixture.bus.dispatch("pick 400 300");
  EXPECT(pick.ok, "pick command should succeed while debug logging is enabled");

  const std::string consoleText = fixture.consolePanel.displayedText();
  const std::string commandLine = "> pick 400 300";
  const usize commandPos = consoleText.find(commandLine);
  const usize resultPos = consoleText.find(pick.message, commandPos);
  const usize debugPos = consoleText.find("pick_debug", commandPos);

  EXPECT(commandPos != std::string::npos,
         "pick command should remain visible in console output");
  EXPECT(resultPos != std::string::npos,
         "pick result should remain visible in console output");
  EXPECT(debugPos != std::string::npos,
         "pick debug output should remain visible in console output");
  EXPECT(commandPos < resultPos && resultPos < debugPos,
         "pick debug output should appear after the owning command result");
}
```

- [ ] **Step 3: Add a failing test for clear-checkpoint behavior with attached debug lines**

```cpp
void testConsoleClearDropsOldDebugAttachmentsFromVisibleOutput() {
  SceneViewerPickFixture fixture;
  EXPECT(fixture.bus.dispatch("debug on").ok,
         "debug on should succeed before clear behavior test");
  EXPECT(fixture.bus.dispatch("pick 400 300").ok,
         "pick should succeed before clear behavior test");

  fixture.consolePanel.clearDisplay();
  const std::string afterClear = fixture.consolePanel.displayedText();
  EXPECT(afterClear.find("pick_debug") == std::string::npos,
         "clearDisplay should hide prior attached debug lines");
  EXPECT(afterClear.find("> pick 400 300") == std::string::npos,
         "clearDisplay should hide prior visible command entries");

  EXPECT(fixture.bus.dispatch("pick 400 300").ok,
         "pick should still work after clear");
  const std::string afterNewPick = fixture.consolePanel.displayedText();
  EXPECT(afterNewPick.find("> pick 400 300") != std::string::npos,
         "new command should be visible after clear");
  EXPECT(afterNewPick.find("pick_debug") != std::string::npos,
         "newly attached debug output should be visible after clear");
}
```

- [ ] **Step 4: Run the focused test binary and confirm the new assertions fail before implementation**

Run: `cmake --build build --target test_command_bus -j4 && ./build/src/test/test_command_bus`

Expected: `test_command_bus` fails in the new formatting / attachment assertions because `ConsolePanel::displayedText()` still uses the old concatenated output model.

- [ ] **Step 5: Commit the failing-test checkpoint**

```bash
git add src/test/integration/test_command_bus.cpp
git commit -m "test: lock command console attachment behavior"
```

## Task 2: Refactor ConsolePanel To Render Entries And Attach Debug Output

**Files:**
- Modify: `src/core/editor/console_panel.hpp`
- Modify: `src/core/editor/console_panel.cpp`
- Test: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Add console-local attachment state and helper declarations in the header**

```cpp
class ConsolePanel final {
public:
  struct DisplayEntry final {
    CommandBus::HistoryEntry historyEntry;
    std::vector<std::string> attachments;
  };

  [[nodiscard]] std::vector<DisplayEntry> displayedDisplayEntries() const;

private:
  void appendAttachmentToLatestVisibleEntry(std::string_view line);
  void drawOutputRegion(float reservedInputHeight);
  void drawDisplayEntry(const DisplayEntry& entry);

  std::vector<std::string> m_orphanSystemLines;
  std::vector<std::vector<std::string>> m_entryAttachments;
};
```

- [ ] **Step 2: Implement attachment ownership and clear semantics in `console_panel.cpp`**

```cpp
void ConsolePanel::clearDisplay() {
  m_displayStartIndex = m_commandBus.history().size();
  m_entryAttachments.clear();
  m_orphanSystemLines.clear();
  m_inputController.clearHelperOutput();
  m_scrollToBottom = false;
}

void ConsolePanel::appendAttachmentToLatestVisibleEntry(std::string_view line) {
  const auto& history = m_commandBus.history();
  if (m_displayStartIndex < history.size()) {
    const usize visibleIndex = history.size() - m_displayStartIndex - 1;
    if (m_entryAttachments.size() < history.size() - m_displayStartIndex) {
      m_entryAttachments.resize(history.size() - m_displayStartIndex);
    }
    m_entryAttachments[visibleIndex].emplace_back(line);
    return;
  }
  m_orphanSystemLines.emplace_back(line);
}

void ConsolePanel::appendSystemLine(std::string_view line) {
  if (line.empty()) {
    return;
  }
  appendAttachmentToLatestVisibleEntry(line);
  m_scrollToBottom = true;
}
```

- [ ] **Step 3: Replace the read-only giant output textbox with explicit per-entry rendering**

```cpp
void ConsolePanel::drawOutputRegion(const float reservedInputHeight) {
  if (!ImGui::BeginChild("console_output", ImVec2(0.0f, -reservedInputHeight), true)) {
    ImGui::EndChild();
    return;
  }

  for (const DisplayEntry& entry : displayedDisplayEntries()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(("> " + entry.historyEntry.line).c_str());
    ImGui::TextUnformatted(entry.historyEntry.result.message.c_str());
    for (const std::string& attachment : entry.attachments) {
      ImGui::TextUnformatted(attachment.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
  }

  for (const std::string& line : m_orphanSystemLines) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(line.c_str());
    ImGui::PopTextWrapPos();
  }

  const std::string helperText = m_inputController.helperOutputText();
  if (!helperText.empty()) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Separator();
    ImGui::TextUnformatted(helperText.c_str());
    ImGui::PopTextWrapPos();
  }

  if (m_scrollToBottom) {
    ImGui::SetScrollHereY(1.0f);
    m_scrollToBottom = false;
  }
  ImGui::EndChild();
}
```

- [ ] **Step 4: Keep `displayedText()` aligned with the new visible structure for tests and non-ImGui callers**

```cpp
std::string ConsolePanel::displayedText() const {
  std::string output;
  for (const DisplayEntry& entry : displayedDisplayEntries()) {
    if (!output.empty()) {
      output += "\n\n";
    }
    output += "> " + entry.historyEntry.line + '\n';
    output += entry.historyEntry.result.message;
    for (const std::string& attachment : entry.attachments) {
      output += '\n';
      output += attachment;
    }
  }
  for (const std::string& line : m_orphanSystemLines) {
    if (!output.empty()) {
      output += "\n\n";
    }
    output += line;
  }
  const std::string helperText = m_inputController.helperOutputText();
  if (!helperText.empty()) {
    if (!output.empty()) {
      output += "\n\n";
    }
    output += helperText;
  }
  return output;
}
```

- [ ] **Step 5: Run the focused test binary and confirm attachment/formatting tests now pass**

Run: `cmake --build build --target test_command_bus -j4 && ./build/src/test/test_command_bus`

Expected: the new attachment-formatting tests pass, and no existing `test_command_bus` assertions regress.

- [ ] **Step 6: Commit the panel-rendering checkpoint**

```bash
git add src/core/editor/console_panel.hpp src/core/editor/console_panel.cpp src/test/integration/test_command_bus.cpp
git commit -m "feat: attach console debug lines to latest entry"
```

## Task 3: Switch Command Input To Visual Multiline Wrapping Without Multiline Submit

**Files:**
- Modify: `src/core/editor/console_panel.cpp`
- Modify: `src/core/editor/console_input_controller.hpp`
- Modify: `src/core/editor/console_input_controller.cpp`
- Test: `src/test/integration/test_command_bus.cpp`

- [ ] **Step 1: Add a controller-level normalization helper so submit remains single-line**

```cpp
class ConsoleInputController final {
public:
  [[nodiscard]] std::string sanitizedInputText() const;

private:
  [[nodiscard]] static std::string collapseInternalNewlines(std::string_view text);
};
```

```cpp
std::string ConsoleInputController::sanitizedInputText() const {
  return collapseInternalNewlines(inputText());
}
```

- [ ] **Step 2: Use the sanitized single-line text at submit time**

```cpp
void ConsoleInputController::submitCurrentInput() {
  submitLine(sanitizedInputText());
}

void ConsolePanel::submitCurrentInput() {
  m_inputController.submitCurrentInput();
  m_scrollToBottom = true;
}
```

- [ ] **Step 3: Replace single-line `InputText` with multiline input that still submits on `Enter`**

```cpp
const ImVec2 inputSize = ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 3.0f);
const bool submitted = ImGui::InputTextMultiline(
    "##command_input",
    m_inputController.inputBufferData(),
    m_inputController.inputBufferSize(),
    inputSize,
    ImGuiInputTextFlags_CallbackCompletion |
        ImGuiInputTextFlags_CallbackHistory |
        ImGuiInputTextFlags_CallbackAlways,
    &ConsolePanel::inputTextCallback,
    &m_inputController);

if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter, false) &&
    !ImGui::GetIO().KeyShift) {
  submitCurrentInput();
  ImGui::SetKeyboardFocusHere(-1);
}
```

- [ ] **Step 4: Add a focused test that long input still submits as one logical command**

```cpp
void testConsolePanelLongInputSubmitsAsSingleCommand() {
  CommandFixture fixture;
  ConsolePanel panel(fixture.bus);

  panel.setInputText("select   /world/cube");
  panel.submitCurrentInput();

  ASSERT(!fixture.bus.history().empty());
  EXPECT(fixture.bus.history().back().line == "select   /world/cube",
         "submitCurrentInput should keep the command as a single logical line");
}
```

- [ ] **Step 5: Run focused console tests after the multiline widget change**

Run: `cmake --build build --target test_command_bus -j4 && ./build/src/test/test_command_bus`

Expected: `test_command_bus` stays green after the controller normalization and multiline widget switch.

- [ ] **Step 6: Commit the input-wrapping checkpoint**

```bash
git add src/core/editor/console_panel.cpp src/core/editor/console_input_controller.hpp src/core/editor/console_input_controller.cpp src/test/integration/test_command_bus.cpp
git commit -m "feat: wrap command console input visually"
```

## Task 4: Run Broader Verification And Repair Any Test Assumptions

**Files:**
- Modify: `src/test/integration/test_command_bus.cpp`
- Modify: `src/test/CMakeLists.txt` if target wiring changes were required
- Test: `src/test/integration/test_imgui_overlay.cpp`
- Test: `src/test/integration/test_lxe_editor_interaction.cpp`

- [ ] **Step 1: Build the related integration targets**

Run: `cmake --build build --target test_command_bus test_lxe_editor_interaction test_imgui_overlay -j4`

Expected: all three targets compile successfully with the updated console code.

- [ ] **Step 2: Run the headless console and interaction coverage**

Run: `./build/src/test/test_command_bus && ./build/src/test/test_lxe_editor_interaction`

Expected: both binaries report `[PASS]` and no new attachment or input regressions appear in scene-picking coverage.

- [ ] **Step 3: Run the ImGui smoke test**

Run: `./build/src/test/test_imgui_overlay`

Expected: on a configured desktop environment, the binary passes or cleanly emits its existing `[SKIP]` paths when graphics/display prerequisites are unavailable.

- [ ] **Step 4: If any assertions still depend on legacy prefixes or giant-buffer assumptions, update them in the same patch**

```cpp
EXPECT(consoleText.find("> help") != std::string::npos,
       "console should keep the command prompt prefix");
EXPECT(consoleText.find("\nunknown command: missing") != std::string::npos,
       "console should render bare result messages under the command");
```

- [ ] **Step 5: Review the diff for accidental scope creep**

Run: `git diff --stat HEAD~1..HEAD && git diff`

Expected: only console presentation, input handling, and directly related tests are touched; no unrelated scene/editor behavior changes appear.

- [ ] **Step 6: Commit the verification checkpoint**

```bash
git add src/core/editor/console_panel.hpp src/core/editor/console_panel.cpp src/core/editor/console_input_controller.hpp src/core/editor/console_input_controller.cpp src/test/integration/test_command_bus.cpp src/test/CMakeLists.txt
git commit -m "test: verify command console wrapping behavior"
```

## Spec Coverage Check

- Debug attachment under newest command entry: covered by Task 1 Step 2, Task 2 Steps 1-5.
- Remove legacy command/result prompt formatting: covered by Task 1 Step 1 and Task 2 Step 4.
- Output visual wrapping: covered by Task 2 Step 3.
- Input visual wrapping without real multiline command semantics: covered by Task 3 Steps 1-5.
- Clear/checkpoint semantics for attachments: covered by Task 1 Step 3 and Task 2 Step 2.
- Preserve autocomplete/history/undo/redo behavior: guarded in Task 3 Step 5 and Task 4 broader verification.

