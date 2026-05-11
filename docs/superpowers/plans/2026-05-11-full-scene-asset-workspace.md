# Full Scene Asset Workspace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add full scene import/export, scene listing, local-vs-asset save routing, session-scoped admin mode, empty-scene startup, and close-time save behavior for `scene_viewer`.

**Architecture:** Introduce a real scene document layer that serializes supported `Scene` content plus editor metadata, then place a catalog/session layer on top to classify files as `asset` or `local`, enforce save permissions, and drive command-bus behavior. Keep runtime orchestration in `SceneRuntime` so startup, command-driven load/save, and shutdown prompts all share one path.

**Tech Stack:** C++20, CMake, yaml-cpp, existing `Scene` / `SceneNode` / command bus / `scene_viewer` overlay runtime.

---

## File Map

- Create: `src/demos/scene_viewer/scene_catalog.hpp`
- Create: `src/demos/scene_viewer/scene_catalog.cpp`
- Create: `src/demos/scene_viewer/scene_session.hpp`
- Create: `src/demos/scene_viewer/scene_session.cpp`
- Modify: `src/demos/scene_viewer/scene_document.hpp`
- Modify: `src/demos/scene_viewer/scene_document.cpp`
- Modify: `src/demos/scene_viewer/scene_runtime.hpp`
- Modify: `src/demos/scene_viewer/scene_runtime.cpp`
- Modify: `src/demos/scene_viewer/main.cpp`
- Modify: `src/demos/scene_viewer/CMakeLists.txt`
- Modify: `src/core/editor/commands/builtin_commands.hpp`
- Modify: `src/core/editor/commands/builtin_commands.cpp`
- Modify: `src/core/editor/viewport_overlay.*` only if command output/UI labels need source-kind display
- Modify: `src/test/CMakeLists.txt`
- Create: `src/test/integration/test_scene_catalog.cpp`
- Create: `src/test/integration/test_scene_session.cpp`
- Modify: `src/test/integration/test_scene_document.cpp`
- Modify: `src/test/integration/test_scene_runtime.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`
- Modify: `src/demos/scene_viewer/README.md`
- Modify: `.gitignore` if `data/` coverage needs tightening

## Task 1: Expand Scene Document Tests First

**Files:**
- Modify: `src/test/integration/test_scene_document.cpp`
- Modify: `src/demos/scene_viewer/scene_document.hpp`
- Modify: `src/demos/scene_viewer/scene_document.cpp`

- [ ] Add failing tests that demand full-node serialization for the currently supported scene_viewer types:
  - scene name
  - node hierarchy
  - transforms
  - mesh/material references
  - camera nodes
  - directional light node
  - editor metadata

- [ ] Run `./build/src/test/test_scene_document` and verify the new expectations fail because the current document only handles camera metadata.

- [ ] Implement the minimal `SceneDocument` model and YAML read/write support needed to make the new tests pass.

- [ ] Re-run `./build/src/test/test_scene_document` and make sure it passes cleanly.

- [ ] Commit with:

```bash
git add src/demos/scene_viewer/scene_document.hpp src/demos/scene_viewer/scene_document.cpp src/test/integration/test_scene_document.cpp
git commit -m "feat: expand scene document schema"
```

## Task 2: Add Scene Catalog Classification

**Files:**
- Create: `src/demos/scene_viewer/scene_catalog.hpp`
- Create: `src/demos/scene_viewer/scene_catalog.cpp`
- Create: `src/test/integration/test_scene_catalog.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] Write failing tests for:
  - scanning both `assets/scenes/` and `data/scenes/`
  - classifying entries as `asset` or `local`
  - resolving `scene load` names from catalog entries
  - preserving paths for explicit file-path loads

- [ ] Run the new catalog test target and confirm it fails because no catalog implementation exists.

- [ ] Implement `SceneCatalogEntry`, catalog scanning, kind classification, and simple lookup helpers.

- [ ] Re-run the catalog tests and ensure they pass.

- [ ] Commit with:

```bash
git add src/demos/scene_viewer/scene_catalog.hpp src/demos/scene_viewer/scene_catalog.cpp src/test/integration/test_scene_catalog.cpp src/test/CMakeLists.txt
git commit -m "feat: add scene catalog classification"
```

## Task 3: Add Session Permission And Save-Routing Rules

**Files:**
- Create: `src/demos/scene_viewer/scene_session.hpp`
- Create: `src/demos/scene_viewer/scene_session.cpp`
- Create: `src/test/integration/test_scene_session.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] Write failing tests for session rules:
  - default permission is `user`
  - `asset` save in `user` mode redirects to timestamped `data/scenes/...`
  - `asset` save in `admin` mode stays on the asset path
  - `local` save remains in place
  - empty scene save creates a timestamped local file

- [ ] Run the new session tests and verify failure before implementation.

- [ ] Implement a focused session helper responsible for:
  - current permission level
  - current source kind
  - dirty state
  - local timestamped path generation
  - save-target decision logic

- [ ] Re-run `test_scene_session` and confirm green.

- [ ] Commit with:

```bash
git add src/demos/scene_viewer/scene_session.hpp src/demos/scene_viewer/scene_session.cpp src/test/integration/test_scene_session.cpp src/test/CMakeLists.txt
git commit -m "feat: add scene session save routing"
```

## Task 4: Make Scene Runtime Own Empty Startup And Full Round-Trip

**Files:**
- Modify: `src/demos/scene_viewer/scene_runtime.hpp`
- Modify: `src/demos/scene_viewer/scene_runtime.cpp`
- Modify: `src/test/integration/test_scene_runtime.cpp`

- [ ] Add failing runtime tests for:
  - empty-scene startup
  - full scene document load/save round-trip
  - asset/local source tracking
  - preserved preview camera semantics after full serialization

- [ ] Run `./build/src/test/test_scene_runtime` and verify the new cases fail.

- [ ] Refactor `SceneRuntime` so it can:
  - create an empty valid scene
  - build a runtime scene from the expanded `SceneDocument`
  - capture the runtime scene back into a `SceneDocument`
  - surface current document path and source kind

- [ ] Re-run `test_scene_runtime` and keep existing preview tests green.

- [ ] Commit with:

```bash
git add src/demos/scene_viewer/scene_runtime.hpp src/demos/scene_viewer/scene_runtime.cpp src/test/integration/test_scene_runtime.cpp
git commit -m "feat: add full scene runtime roundtrip"
```

## Task 5: Extend Command Bus For Scene List And Admin Mode

**Files:**
- Modify: `src/core/editor/commands/builtin_commands.hpp`
- Modify: `src/core/editor/commands/builtin_commands.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`

- [ ] Write failing command-bus tests for:
  - `scene list`
  - `scene load` resolving both catalog names and explicit paths
  - `admin on`
  - `admin off`
  - `admin status`
  - `scene save` result messaging when asset protection redirects to local

- [ ] Run `./build/src/test/test_command_bus` and verify red.

- [ ] Extend builtin command registration with:
  - scene listing callback
  - admin state callbacks
  - updated scene save/load messaging and structured payloads

- [ ] Re-run `test_command_bus` and confirm green.

- [ ] Commit with:

```bash
git add src/core/editor/commands/builtin_commands.hpp src/core/editor/commands/builtin_commands.cpp src/test/integration/test_command_bus.cpp
git commit -m "feat: add scene list and admin commands"
```

## Task 6: Integrate Scene Viewer Session Behavior

**Files:**
- Modify: `src/demos/scene_viewer/main.cpp`
- Modify: `src/demos/scene_viewer/CMakeLists.txt`
- Modify: `src/test/integration/test_scene_runtime.cpp`
- Modify: `src/test/integration/test_command_bus.cpp`

- [ ] Add failing integration coverage for:
  - startup opens empty scene instead of sample content
  - `scene load` can load both asset and local entries
  - save redirects from asset to local in `user` mode
  - runtime admin toggle changes subsequent save behavior

- [ ] Run targeted tests and verify red.

- [ ] Update `SceneViewerSession` orchestration to:
  - initialize empty scene
  - own a scene catalog
  - own a scene session/permission state
  - wire `scene list/load/save` and `admin` commands into the same runtime state
  - keep deferred load behavior

- [ ] Re-run the targeted tests plus `test_scene_runtime` and `test_command_bus`.

- [ ] Commit with:

```bash
git add src/demos/scene_viewer/main.cpp src/demos/scene_viewer/CMakeLists.txt src/test/integration/test_scene_runtime.cpp src/test/integration/test_command_bus.cpp
git commit -m "feat: integrate full scene workspace flow"
```

## Task 7: Close-Time Save And Documentation

**Files:**
- Modify: `src/demos/scene_viewer/main.cpp`
- Modify: `src/demos/scene_viewer/README.md`
- Modify: `.gitignore` if needed for `data/`

- [ ] Add failing coverage or a minimal harness for close-time save routing if practical; otherwise add a narrow unit around the session save decision and keep smoke validation manual.

- [ ] Implement close-time dirty-scene save prompt routing through the same session save path used by `scene save`.

- [ ] Update README to explain:
  - empty-scene startup
  - `scene list`
  - `scene load`
  - `scene save`
  - asset/local markers
  - `admin` commands
  - default `data/` save behavior

- [ ] Run manual demo smoke validation and confirm startup, listing, loading, saving, and close-save behavior.

- [ ] Commit with:

```bash
git add src/demos/scene_viewer/main.cpp src/demos/scene_viewer/README.md .gitignore
git commit -m "docs: explain full scene workspace flow"
```

## Task 8: Final Verification

**Files:**
- Verify only

- [ ] Run:

```bash
cmake --build build --target demo_scene_viewer test_scene_document test_scene_catalog test_scene_session test_scene_runtime test_command_bus
./build/src/test/test_scene_document
./build/src/test/test_scene_catalog
./build/src/test/test_scene_session
./build/src/test/test_scene_runtime
./build/src/test/test_command_bus
```

- [ ] Run controller and overlay regressions:

```bash
./build/src/test/test_viewport_overlay
./build/src/test/test_orbit_camera_controller
./build/src/test/test_freefly_camera_controller
```

- [ ] Run a bounded demo smoke:

```bash
xvfb-run -a bash -lc 'timeout 5s ./build/src/demos/scene_viewer/demo_scene_viewer >/tmp/demo_scene_viewer.log 2>&1; status=$?; if [ "$status" -eq 124 ]; then exit 0; else exit "$status"; fi'
```

- [ ] Inspect `git status --short` and ensure only expected non-repo-local files remain unstaged.

