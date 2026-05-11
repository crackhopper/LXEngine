# LXE Editor MCP And API Test Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename `lxe_editor` into `lxe_editor`, add an in-process MCP server wired into Codex, and migrate `lxe_editor` behavior tests to Python API black-box coverage while pruning redundant C++ end-to-end tests.

**Architecture:** Execute this in three layers. First, perform the source/build/runtime rename so the editor has a single stable identity. Second, extend the existing API core with an in-process localhost MCP server plus `.codex` integration and runtime-state discovery. Third, add Python API black-box tests that drive a real `lxe_editor` instance, then shrink or delete C++ tests that only duplicate full-system behavior already covered by the API path.

**Tech Stack:** C++20, CMake, existing `CommandBus`/`LxeEditorApiService`/HTTP+WebSocket API core, local MCP integration via `.codex`, Python 3 for black-box tests, yaml-cpp, SDL/Vulkan runtime smoke under `xvfb-run`.

---

## File Structure

- Rename: `src/demos/lxe_editor/` -> `src/demos/lxe_editor/`
  Responsibility: make `lxe_editor` the only editor source root.
- Modify: `src/demos/CMakeLists.txt` and `src/demos/lxe_editor/CMakeLists.txt`
  Responsibility: rename the demo target/output from `lxe_editor` to `lxe_editor` and wire renamed sources.
- Modify: `src/demos/lxe_editor/main.cpp`
  Responsibility: update window title/logging/runtime data paths, host the in-process MCP thread, and publish runtime state.
- Rename: `src/demos/lxe_editor/lxe_editor_commands.*` remains the canonical command-surface file set.
  Responsibility: keep editor-specific command registration aligned with the new product name.
- Create: `src/demos/lxe_editor/editor_mcp_protocol.hpp`
  Responsibility: declare MCP request/response domain types or thin adapters over `LxeEditorApiService` outputs.
- Create: `src/demos/lxe_editor/editor_mcp_server.hpp`
  Responsibility: declare localhost TCP MCP server lifecycle and thread wrapper.
- Create: `src/demos/lxe_editor/editor_mcp_server.cpp`
  Responsibility: implement MCP transport, tool/resource dispatch, and service binding.
- Create: `src/demos/lxe_editor/runtime_state.hpp`
  Responsibility: declare the `data/lxe_editor/runtime_state.yaml` document shape.
- Create: `src/demos/lxe_editor/runtime_state.cpp`
  Responsibility: implement load/save helpers for runtime discovery metadata.
- Modify: `.codex/` local MCP configuration files
  Responsibility: register the `lxe_editor` MCP server so Codex sees it on startup.
- Create: `.codex/skills/lxe-editor-debug/SKILL.md`
  Responsibility: define Codex-facing debug workflows over MCP tools/resources.
- Create: `tests/lxe_editor/api_client.py`
  Responsibility: shared Python client for token lookup, runtime-state discovery, HTTP/WS requests, and readiness polling.
- Create: `tests/lxe_editor/test_editor_workflow.py`
  Responsibility: API black-box coverage for mode/preview/camera reset/scene state workflows.
- Create: `tests/lxe_editor/test_scene_io.py`
  Responsibility: API black-box coverage for `scene list/load/save/admin` behavior and local redirect rules.
- Create: `tests/lxe_editor/test_persistence.py`
  Responsibility: API black-box coverage for `editor_config.yaml` / `editor_data.yaml` persistence across restart.
- Modify: `src/test/CMakeLists.txt`
  Responsibility: rename `test_lxe_editor_*` targets, keep low-level C++ tests, and add a Python test entrypoint target if appropriate.
- Rename: `src/test/integration/test_lxe_editor_layout.cpp` -> `src/test/integration/test_lxe_editor_layout.cpp`
  Responsibility: retain only low-level layout/config parsing assertions that are not duplicated by black-box API tests.
- Rename: `src/test/integration/test_lxe_editor_interaction.cpp` -> `src/test/integration/test_lxe_editor_interaction.cpp`
  Responsibility: retain only low-level picking/debug-draw logic assertions.
- Rename: `src/test/integration/test_lxe_lxe_editor_api_service.cpp` -> `src/test/integration/test_lxe_lxe_editor_api_service.cpp`
  Responsibility: keep api-core unit coverage under the new name.
- Rename: `src/test/integration/test_lxe_lxe_editor_api_server.cpp` -> `src/test/integration/test_lxe_lxe_editor_api_server.cpp`
  Responsibility: keep transport-level C++ server coverage if it still provides unique value after Python tests land.
- Modify: `src/demos/lxe_editor/README.md`
  Responsibility: document the renamed editor, runtime-state file, MCP, and Python API test workflow.
- Modify: `notes/subsystems/scene.md`
  Responsibility: describe the `lxe_editor` naming, MCP diagnostics, and API-first test strategy.

## Task 1: Rename `lxe_editor` to `lxe_editor`

**Files:**
- Rename: `src/demos/lxe_editor/` -> `src/demos/lxe_editor/`
- Rename: `src/test/integration/test_lxe_editor_layout.cpp` -> `src/test/integration/test_lxe_editor_layout.cpp`
- Rename: `src/test/integration/test_lxe_editor_interaction.cpp` -> `src/test/integration/test_lxe_editor_interaction.cpp`
- Rename: `src/test/integration/test_lxe_lxe_editor_api_service.cpp` -> `src/test/integration/test_lxe_lxe_editor_api_service.cpp`
- Rename: `src/test/integration/test_lxe_lxe_editor_api_server.cpp` -> `src/test/integration/test_lxe_lxe_editor_api_server.cpp`
- Modify: `src/demos/lxe_editor/CMakeLists.txt`
- Modify: `src/test/CMakeLists.txt`
- Modify: all includes and strings referencing `lxe_editor` in the renamed subtree

- [ ] **Step 1: Perform the directory and test-file renames**

```bash
mv src/demos/lxe_editor src/demos/lxe_editor
mv src/test/integration/test_lxe_editor_layout.cpp src/test/integration/test_lxe_editor_layout.cpp
mv src/test/integration/test_lxe_editor_interaction.cpp src/test/integration/test_lxe_editor_interaction.cpp
mv src/test/integration/test_lxe_lxe_editor_api_service.cpp src/test/integration/test_lxe_lxe_editor_api_service.cpp
mv src/test/integration/test_lxe_lxe_editor_api_server.cpp src/test/integration/test_lxe_lxe_editor_api_server.cpp
```

- [ ] **Step 2: Update the editor target name and output binary**

In `src/demos/lxe_editor/CMakeLists.txt`, change the executable stanza from the old demo target to:

```cmake
add_executable(lxe_editor
  api_token_state.cpp
  main.cpp
  camera_rig.cpp
  lxe_editor_api_protocol.cpp
  lxe_editor_api_server.cpp
  lxe_editor_api_service.cpp
  editor_config_state.cpp
  editor_data_state.cpp
  editor_camera_state.cpp
  scene_catalog.cpp
  scene_builder.cpp
  scene_document.cpp
  scene_interaction_controller.cpp
  scene_input_routing.cpp
  scene_runtime.cpp
  scene_session.cpp
  scene_view_rect.cpp
  lxe_editor_commands.cpp
  ui_overlay.cpp
)
```

And update link rules to target `lxe_editor` instead of `lxe_editor`.

- [ ] **Step 3: Rename the editor-specific command files and includes**

```bash
`src/demos/lxe_editor/lxe_editor_commands.hpp` and
`src/demos/lxe_editor/lxe_editor_commands.cpp` are the canonical command-surface files.
```

Then update include lines such as:

```cpp
#include "lxe_editor_commands.hpp"
```

in `src/demos/lxe_editor/main.cpp` and the renamed command implementation file.

- [ ] **Step 4: Update runtime strings and data roots**

In `src/demos/lxe_editor/main.cpp`, change the visible/runtime-facing strings and paths to `lxe_editor`:

```cpp
auto window = std::make_shared<LX_infra::Window>(
    "lxe_editor", kWindowWidth, kWindowHeight,
    editorConfig.windowPlacement);

renderer->initialize(window, "lxe_editor");

demo::EditorConfigState configState(resolveRuntimePath("data/lxe_editor"));
demo::ApiTokenState apiTokenState(
    resolveRuntimePath("data/lxe_editor"));
```

Also migrate `EditorDataState` and any remaining `data/lxe_editor` usage to `data/lxe_editor`.

- [ ] **Step 5: Update renamed test targets in `src/test/CMakeLists.txt`**

Replace old target names like `test_lxe_editor_layout` with:

```cmake
set(TEST_INTEGRATION_EXE_LIST
  ...
  test_lxe_editor_layout
  test_lxe_editor_interaction
  test_lxe_lxe_editor_api_service
  test_lxe_lxe_editor_api_server
  ...
)
```

and update their source attachments accordingly.

- [ ] **Step 6: Verify the rename builds cleanly**

Run:

```bash
cmake --build build --target lxe_editor test_lxe_editor_layout test_lxe_editor_interaction test_lxe_lxe_editor_api_service test_lxe_lxe_editor_api_server
```

Expected: build succeeds with no remaining `lxe_editor` include-path or target-name errors.

- [ ] **Step 7: Commit the rename layer**

```bash
git add src/demos/lxe_editor src/test/CMakeLists.txt src/test/integration/test_lxe_editor_*.cpp
git commit -m "refactor: rename scene viewer to lxe editor"
```

## Task 2: Add runtime-state publication and in-process MCP server

**Files:**
- Create: `src/demos/lxe_editor/runtime_state.hpp`
- Create: `src/demos/lxe_editor/runtime_state.cpp`
- Create: `src/demos/lxe_editor/editor_mcp_protocol.hpp`
- Create: `src/demos/lxe_editor/editor_mcp_server.hpp`
- Create: `src/demos/lxe_editor/editor_mcp_server.cpp`
- Modify: `src/demos/lxe_editor/main.cpp`
- Modify: `src/demos/lxe_editor/CMakeLists.txt`

- [ ] **Step 1: Add a failing runtime-state persistence test**

Create a focused C++ test case in the renamed api-server test file:

```cpp
void testRuntimeStateRoundTripsYaml() {
  const auto root = std::filesystem::temp_directory_path() /
                    "lxengine_lxe_editor_runtime_state";
  std::filesystem::remove_all(root);

  const LxeEditorRuntimeState expected{
      .pid = 1234,
      .httpHost = "0.0.0.0",
      .httpPort = 3768,
      .mcpHost = "127.0.0.1",
      .mcpPort = 3769,
      .tokenFile = (root / "api_token.txt").string(),
  };

  saveLxeEditorRuntimeState(root, expected);
  const auto loaded = loadLxeEditorRuntimeState(root);
  EXPECT(loaded.has_value(), "runtime state should reload from yaml");
  EXPECT(loaded == expected, "runtime state yaml should round-trip");
}
```

- [ ] **Step 2: Define the runtime-state document type**

In `src/demos/lxe_editor/runtime_state.hpp` add:

```cpp
struct LxeEditorRuntimeState final {
  int pid = 0;
  std::string httpHost;
  std::uint16_t httpPort = 0;
  std::string wsHost;
  std::uint16_t wsPort = 0;
  std::string mcpHost;
  std::uint16_t mcpPort = 0;
  std::string tokenFile;
  std::string startedAt;

  bool operator==(const LxeEditorRuntimeState&) const = default;
};

void saveLxeEditorRuntimeState(const std::filesystem::path& root,
                               const LxeEditorRuntimeState& state);
std::optional<LxeEditorRuntimeState>
loadLxeEditorRuntimeState(const std::filesystem::path& root);
```

- [ ] **Step 3: Implement YAML load/save for runtime state**

In `src/demos/lxe_editor/runtime_state.cpp`, write the YAML serialization helpers using `yaml-cpp`, mirroring the existing local config/data style and storing to:

```cpp
root / "runtime_state.yaml"
```

- [ ] **Step 4: Define the MCP tool/resource surface**

In `src/demos/lxe_editor/editor_mcp_protocol.hpp`, define a narrow protocol layer like:

```cpp
struct LxeEditorMcpRequest final {
  std::string method;
  std::string toolName;
  std::string resourceUri;
  std::string argumentsJson;
};

struct LxeEditorMcpResponse final {
  bool ok = false;
  std::string resultJson;
  std::string errorCode;
  std::string errorMessage;
};
```

Keep it thin; the MCP server should adapt these onto `LxeEditorApiService` and the existing command/state tools.

- [ ] **Step 5: Implement the localhost MCP server thread wrapper**

In `src/demos/lxe_editor/editor_mcp_server.hpp/.cpp`, add a class that:

- owns a localhost TCP listener on `127.0.0.1`
- runs a worker thread
- exposes `start()`, `stop()`, and `boundPort()`
- dispatches tool calls such as:
  - `lxe_editor_command`
  - `lxe_editor_get_summary`
  - `lxe_editor_get_selection`
  - `lxe_editor_get_cameras`
  - `lxe_editor_pick`
  - `lxe_editor_wait_for`

The server should call into the already-live `LxeEditorApiService` rather than re-implementing command logic.

- [ ] **Step 6: Publish runtime state from `main.cpp`**

When `lxe_editor` starts api and MCP successfully, write:

```cpp
saveLxeEditorRuntimeState(resolveRuntimePath("data/lxe_editor"), state);
```

Populate `state` from the bound HTTP/MCP ports, token path, PID, and startup timestamp. On clean shutdown, either remove the file or overwrite it with `pid=0`; pick one behavior and document it consistently.

- [ ] **Step 7: Verify MCP/runtime-state coverage**

Run:

```bash
cmake --build build --target test_lxe_lxe_editor_api_server lxe_editor
./build/src/test/test_lxe_lxe_editor_api_server
```

Expected: PASS, including the new runtime-state round-trip case and the existing transport coverage.

- [ ] **Step 8: Commit the MCP/runtime-state layer**

```bash
git add src/demos/lxe_editor/runtime_state.* src/demos/lxe_editor/editor_mcp_* src/demos/lxe_editor/main.cpp src/test/integration/test_lxe_lxe_editor_api_server.cpp
git commit -m "feat: add lxe editor mcp server and runtime state"
```

## Task 3: Register the `lxe_editor` MCP server in `.codex` and add a debug skill

**Files:**
- Modify: local `.codex` MCP configuration files in this repo
- Create: `.codex/skills/lxe-editor-debug/SKILL.md`

- [ ] **Step 1: Inspect the current local `.codex` config layout**

Run:

```bash
find .codex -maxdepth 3 -type f | sort
```

Expected: identify the repo-local MCP registration file to extend instead of inventing a second config tree.

- [ ] **Step 2: Add an MCP registration for `lxe_editor`**

Extend the discovered `.codex` config with one MCP entry named `lxe_editor` whose connection target is derived from:

```text
data/lxe_editor/runtime_state.yaml
```

The config should point Codex at the local TCP MCP endpoint instead of trying to spawn an external adapter process.

- [ ] **Step 3: Add a thin debug skill**

Create `.codex/skills/lxe-editor-debug/SKILL.md` documenting workflows such as:

```markdown
- Ensure the editor is running by checking `data/lxe_editor/runtime_state.yaml`
- Use the `lxe_editor_command` tool for command-bus actions
- Use `lxe_editor_get_summary`, `lxe_editor_get_selection`, and `lxe_editor_get_cameras` before and after mutations
- Prefer MCP for diagnosis and HTTP for scripted regression tests
```

- [ ] **Step 4: Validate Codex-facing config shape locally**

Run a narrow check such as:

```bash
rg -n "lxe_editor|runtime_state.yaml|mcp" .codex
```

Expected: the MCP registration and skill both reference `lxe_editor`, not `lxe_editor`.

- [ ] **Step 5: Commit the Codex integration**

```bash
git add .codex
git commit -m "feat: register lxe editor mcp for codex"
```

## Task 4: Add shared Python API black-box test utilities

**Files:**
- Create: `tests/lxe_editor/api_client.py`
- Create: `tests/lxe_editor/__init__.py`
- Modify: repo test/docs wiring as needed

- [ ] **Step 1: Create the Python client skeleton**

In `tests/lxe_editor/api_client.py`, add a minimal client with:

```python
class LxeEditorClient:
    def __init__(self, runtime_root: pathlib.Path):
        self.runtime_root = runtime_root

    def read_runtime_state(self) -> dict: ...
    def read_token(self) -> str: ...
    def wait_until_ready(self, timeout_s: float = 10.0) -> None: ...
    def command(self, line: str) -> dict: ...
    def get_summary(self) -> dict: ...
    def get_selection(self) -> dict: ...
    def get_cameras(self) -> dict: ...
    def set_mode(self, mode: str) -> dict: ...
    def pick(self, x: float, y: float) -> dict: ...
```

- [ ] **Step 2: Implement runtime-state discovery and token auth**

Use:

- `data/lxe_editor/runtime_state.yaml`
- `data/lxe_editor/api_token.txt`

and Python stdlib `urllib.request` or a repo-approved lightweight dependency.

- [ ] **Step 3: Add a launch helper for black-box tests**

Still in `api_client.py`, add a helper like:

```python
def launch_lxe_editor(build_dir: pathlib.Path, port: int) -> subprocess.Popen:
    return subprocess.Popen([
        str(build_dir / "src/demos/lxe_editor/lxe_editor"),
        "--api-port", str(port),
        "--api-background",
    ])
```

plus teardown/wait helpers so tests do not rely on fixed sleeps.

- [ ] **Step 4: Smoke-test the Python client manually**

Run:

```bash
python3 - <<'PY'
from tests.lxe_editor.api_client import LxeEditorClient
print("client import ok")
PY
```

Expected: prints `client import ok`.

- [ ] **Step 5: Commit the Python client utilities**

```bash
git add tests/lxe_editor
git commit -m "test: add lxe editor api client"
```

## Task 5: Add Python API black-box tests for editor workflows

**Files:**
- Create: `tests/lxe_editor/test_editor_workflow.py`
- Create: `tests/lxe_editor/test_scene_io.py`
- Create: `tests/lxe_editor/test_persistence.py`

- [ ] **Step 1: Add a mode/preview/camera-reset workflow test**

In `tests/lxe_editor/test_editor_workflow.py`, add:

```python
def test_mode_preview_and_camera_reset(client):
    assert client.get_summary()["mode"] in {"orbit", "selection", "freefly"}
    assert client.set_mode("selection")["ok"]
    assert client.get_summary()["mode"] == "selection"
    assert client.command("preview on")["ok"]
    assert client.get_summary()["previewEnabled"] is True
    assert client.command("preview off")["ok"]
    assert client.command("cam reset-editor-to-game")["ok"]
```

- [ ] **Step 2: Add a scene I/O workflow test**

In `tests/lxe_editor/test_scene_io.py`, add:

```python
def test_scene_list_and_user_save_redirect(client):
    catalog = client.command("scene list")
    assert catalog["ok"]
    assert "entries" in catalog["structuredJson"] or catalog["message"]

    result = client.command("scene load assets/scenes/lxe_editor.scene.yaml")
    assert result["ok"]

    save_result = client.command("scene save")
    assert save_result["ok"]
```
```

Then harden the assertions around the actual JSON fields returned by your current API wrapper.

- [ ] **Step 3: Add persistence tests for config/data files**

In `tests/lxe_editor/test_persistence.py`, add:

```python
def test_console_history_persists_across_restart(client_factory):
    client = client_factory()
    assert client.command("help")["ok"]
    client.shutdown()

    client2 = client_factory()
    history_text = pathlib.Path("data/lxe_editor/editor_data.yaml").read_text()
    assert "help" in history_text
```

Also add one config-oriented case that changes mode/layout or font-scale through the API/command path and verifies the corresponding config file persists.

- [ ] **Step 4: Run the new Python black-box tests directly**

Run:

```bash
python3 -m pytest tests/lxe_editor -q
```

Expected: PASS with a real `lxe_editor` child process launched and torn down by the tests.

- [ ] **Step 5: Commit the API black-box tests**

```bash
git add tests/lxe_editor
git commit -m "test: add lxe editor api black-box coverage"
```

## Task 6: Prune or shrink redundant C++ editor end-to-end tests

**Files:**
- Modify or delete: `src/test/integration/test_lxe_editor_layout.cpp`
- Modify or delete: `src/test/integration/test_lxe_editor_interaction.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Audit which editor tests still provide unique low-level value**

Review the renamed files and classify each case:

```text
keep if it validates low-level logic without a full editor process
remove or shrink if it only duplicates a user-visible workflow now covered by Python API tests
```

- [ ] **Step 2: Keep only low-level interaction assertions in the C++ interaction test**

Retain cases like:

- scene-view-rect picking math
- outside-rect rejection
- debug hit-point bookkeeping

Delete or move API-equivalent end-to-end assertions such as mode/preview workflows that are now covered by Python.

- [ ] **Step 3: Keep only low-level layout/config parsing assertions in the C++ layout test**

Retain cases like:

- invalid YAML fallback
- layout/config document round-trip helpers

Delete fragile window/panel behavior checks that are now covered through real editor startup and API-visible state.

- [ ] **Step 4: Split `test_command_bus.cpp` between core bus coverage and `lxe_editor` command surface**

Keep:

- parse/history/undo/redo core behavior
- generic built-in command coverage
- a minimal set of `lxe_editor` command-surface assertions that protect structured JSON contracts

Delete any cases that only restage whole-editor behaviors already asserted through Python.

- [ ] **Step 5: Re-run the surviving C++ suite plus Python black-box tests**

Run:

```bash
cmake --build build --target test_command_bus test_lxe_editor_interaction test_lxe_editor_layout test_lxe_lxe_editor_api_service test_lxe_lxe_editor_api_server lxe_editor
./build/src/test/test_command_bus
./build/src/test/test_lxe_editor_interaction
./build/src/test/test_lxe_editor_layout
./build/src/test/test_lxe_lxe_editor_api_service
./build/src/test/test_lxe_lxe_editor_api_server
python3 -m pytest tests/lxe_editor -q
```

Expected: all retained C++ low-level tests and the new Python API tests pass together.

- [ ] **Step 6: Commit the test-pruning layer**

```bash
git add src/test tests/lxe_editor
git commit -m "test: migrate lxe editor workflows to api coverage"
```

## Task 7: Update docs and final verification

**Files:**
- Modify: `src/demos/lxe_editor/README.md`
- Modify: `notes/subsystems/scene.md`
- Modify: any rename-sensitive notes or docs discovered by `rg`

- [ ] **Step 1: Update the README to the new editor identity**

Document:

- the `lxe_editor` executable name
- new data roots under `data/lxe_editor/`
- runtime-state file location
- MCP availability and intended use for Codex diagnostics
- API-first Python black-box testing workflow

- [ ] **Step 2: Update subsystem notes**

In `notes/subsystems/scene.md`, replace the remaining `lxe_editor` product wording with `lxe_editor`, and describe:

- MCP as a diagnosis layer
- API as the official test surface
- the split between retained low-level C++ tests and API black-box tests

- [ ] **Step 3: Run a final repository-level validation slice**

Run:

```bash
cmake --build build --target lxe_editor test_command_bus test_lxe_editor_interaction test_lxe_editor_layout test_lxe_lxe_editor_api_service test_lxe_lxe_editor_api_server
./build/src/test/test_command_bus
./build/src/test/test_lxe_editor_interaction
./build/src/test/test_lxe_editor_layout
./build/src/test/test_lxe_lxe_editor_api_service
./build/src/test/test_lxe_lxe_editor_api_server
python3 -m pytest tests/lxe_editor -q
xvfb-run -a ./build/src/demos/lxe_editor/lxe_editor --api-port 3768
```

Expected:

- build succeeds
- all retained C++ tests pass
- Python black-box tests pass
- `lxe_editor` launches under `xvfb-run` and prints api/MCP readiness

- [ ] **Step 4: Commit docs + final integration**

```bash
git add src/demos/lxe_editor/README.md notes/subsystems/scene.md
 git commit -m "docs: document lxe editor mcp and api testing"
```
