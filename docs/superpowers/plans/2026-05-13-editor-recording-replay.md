# Editor Recording Replay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add a first-version debug recording and replay surface for `lxe_editor` and expose it through `lxe_manager` MCP.

**Architecture:** The editor owns recording state, saved JSON files, semantic command recording, and basic replay. The existing editor HTTP API exposes recording operations; `lxe_manager` forwards those operations as MCP tools. The first version records command/MCP operations and supports basic status/list/read/replay, leaving richer UI/input sinks for later incremental work.

**Tech Stack:** C++20 editor code, existing YAML/JSON helper patterns, Node/TypeScript `lxe_manager`, Vitest, CMake/Ninja.

---

### Task 1: Editor Recording Core

**Files:**
- Create: `src/demos/lxe_editor/recording_controller.hpp`
- Create: `src/demos/lxe_editor/recording_controller.cpp`
- Modify: `src/demos/lxe_editor/CMakeLists.txt`
- Test: `src/test/integration/test_lxe_editor_recording.cpp`

- [x] **Step 1: Write a failing integration test for enable/start/append/stop/save**

Create `src/test/integration/test_lxe_editor_recording.cpp` with tests that instantiate `RecordingController`, verify disabled append is a no-op, enable/start a session, append a `command` step, stop with save under a temp directory, and read the saved JSON text.

- [x] **Step 2: Run the new test target and verify it fails to build**

Run: `cmake --build build --target test_lxe_editor_recording` if the build tree exists, otherwise run the closest available CMake configure/build flow.

Expected: failure because `RecordingController` does not exist.

- [x] **Step 3: Implement `RecordingController`**

Implement:
- `RecordingDetailLevel { Basic, Diagnostic, Trace }`
- `RecordingSource { UserUi, Mcp, System }`
- `RecordingStepInput { kind, source, payloadJson }`
- `RecordingController::enable`, `disable`, `status`, `start`, `appendStep`, `stop`, `list`, `read`
- JSON save to `data/lxe_editor/recordings/<timestamp>-<session>.json`

Keep disabled append as a cheap branch.

- [x] **Step 4: Add CMake target and run the test**

Add the new source file to `lxe_editor_lib` and the new integration executable to the test CMake. Run the new test until it passes.

### Task 2: Editor Session and HTTP API

**Files:**
- Modify: `src/demos/lxe_editor/editor_session.hpp`
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_protocol.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_protocol.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_server.cpp`
- Test: existing lxe editor API/session tests

- [x] **Step 1: Add failing tests for command recording through session/API where practical**

Extend the existing session/API tests so a command executed through the editor service can be recorded with source `mcp`.

- [x] **Step 2: Wire `RecordingController` into `LxeEditorSession`**

Expose `recording()` accessors. Record successful command dispatches in `executeCommandLine`/command polling paths with source `user_ui` or `mcp` depending on call site.

- [x] **Step 3: Add editor HTTP endpoints**

Add endpoints under `/recording/*`:
- `GET /recording/status`
- `POST /recording/enable`
- `POST /recording/disable`
- `POST /recording/start`
- `POST /recording/stop`
- `GET /recording/list`
- `GET /recording/read?id=...`
- `POST /recording/replay`
- `GET /recording/probe?target=summary`

- [x] **Step 4: Implement basic replay**

Support replaying `command` steps. Stop on first failure and return failed step details plus current summary where available.

### Task 3: lxe_manager Client and MCP Tools

**Files:**
- Modify: `tools/lxe_manager/src/editor/client.ts`
- Modify: `tools/lxe_manager/src/mcp/server.ts`
- Test: `tools/lxe_manager/tests/editor-client.test.ts`
- Test: `tools/lxe_manager/tests/mcp-server.test.ts`

- [x] **Step 1: Add failing TypeScript tests for recording client methods and MCP tool exposure**

Verify `recording_status`, `recording_start`, `recording_stop`, `recording_list`, `recording_read`, `recording_replay`, and `recording_probe` appear in `tools/list` and call the editor client.

- [x] **Step 2: Add editor client recording methods**

Forward to the new editor HTTP endpoints and return parsed JSON.

- [x] **Step 3: Add MCP handlers**

Expose the recording tools with argument validation matching the existing tool style.

- [x] **Step 4: Run `npm test` and `npm run build`**

Both must pass.

### Task 4: Verification and Documentation

**Files:**
- Modify: `notes/tools/lxe-manager-mcp.md`
- Modify: `src/demos/lxe_editor/README.md`

- [x] **Step 1: Document recording tools**

Add the recording MCP tools to the tool list and document the default disabled state and saved recording path.

- [x] **Step 2: Run verification**

Run focused C++ tests, `npm test`, `npm run build`, `python3 scripts/notes/generate_site_config.py`, `git diff --check`.

- [x] **Step 3: Commit**

Commit the implementation with a concise message after verification passes.

### Task 5: Editor Build Identity Surface

**Files:**
- Create: `src/demos/lxe_editor/lxe_editor_build_info.hpp`
- Create: `src/demos/lxe_editor/lxe_editor_build_info.cpp`
- Modify: `src/demos/lxe_editor/CMakeLists.txt`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.hpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_service.cpp`
- Modify: `src/demos/lxe_editor/lxe_editor_api_server.cpp`
- Modify: `src/demos/lxe_editor/recording_controller.hpp`
- Modify: `src/demos/lxe_editor/recording_controller.cpp`
- Modify: `src/test/CMakeLists.txt`
- Test: `src/test/integration/test_lxe_editor_api_server.cpp`

- [x] **Step 1: Add build-info module**

Expose `gitCommit`, `gitCommitShort`, `gitDirty`, `buildType`, and optional
`builtAt` as JSON. CMake should inject Git information when available, while the
C++ module must compile with safe `unknown` defaults when definitions are absent.

- [x] **Step 2: Add editor HTTP endpoint**

Expose `GET /api/build` through the existing token-protected editor HTTP API.
Add a focused API server test that verifies the endpoint returns build identity
fields.

- [x] **Step 3: Include build identity in recordings**

Pass build identity into recording start metadata and serialize it inside the
recording JSON metadata so saved recordings remain self-describing.

### Task 6: lxe_manager Build Info MCP Surface

**Files:**
- Modify: `tools/lxe_manager/src/editor/editor-client.ts`
- Modify: `tools/lxe_manager/src/mcp/server.ts`
- Test: `tools/lxe_manager/tests/editor-client.test.ts`
- Test: `tools/lxe_manager/tests/mcp-server.test.ts`

- [x] **Step 1: Add editor client method**

Add `buildInfo()` that forwards to `GET /api/build`.

- [x] **Step 2: Add MCP tools**

Expose `editor.get_build_info` and legacy alias `lxe_editor_get_build_info`.

- [x] **Step 3: Test tool routing and accepted surface**

Extend Vitest coverage for editor-client request forwarding and MCP tool list /
handler routing.

### Task 7: Split Codex Skills and Workflow Docs

**Files:**
- Modify: `.codex/skills/lxe-editor-debug/SKILL.md`
- Create: `.codex/skills/lxe-editor-recording/SKILL.md`
- Create: `.codex/skills/lxe-editor-build-sync/SKILL.md`
- Create: `.codex/skills/lxe-manager-ops/SKILL.md`
- Create: `.codex/skills/lxe-editor-command-reference/SKILL.md`
- Modify: `notes/tools/lxe-manager-mcp.md`

- [x] **Step 1: Narrow the existing debug skill**

Keep `lxe-editor-debug` focused on state reads, lightweight commands, pick, and
wait-for. Move recording, build-sync, and ops workflow text out into separate
skills.

- [x] **Step 2: Add focused skills**

Create small, strict skills for recording/replay, build identity comparison,
manager operations, and command reference loading. Each skill should list only
the relevant MCP tools/resources and the minimum workflow guardrails.

- [x] **Step 3: Document how to choose skills**

Update the manager MCP note with the split skill suite and recommended workflow
order so future agents can load only the context needed for the current phase.

### Task 8: Final Integration Verification

**Files:**
- All touched files

- [x] **Step 1: Fix integration drift**

Repair any compile or test failures introduced by combining the recording,
build identity, MCP, and skill documentation changes.

- [x] **Step 2: Run verification**

Run focused C++ editor/API tests, `npm --prefix tools/lxe_manager test`,
`npm --prefix tools/lxe_manager run build`,
`python3 scripts/notes/generate_site_config.py`, and `git diff --check`.

- [x] **Step 3: Commit**

Create one implementation commit after verification. Push only if explicitly
requested for this final state.
