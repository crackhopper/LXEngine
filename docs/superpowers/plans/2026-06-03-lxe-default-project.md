# LXE Default Project Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Start `lxe_editor` in a stable built-in project named `lxe_default` and make internal diagnostic scenes switchable through `scene open`.

**Architecture:** Reuse the existing project-template and project-session path instead of adding a separate builtin resolver. The `lxe_default` template registers all built-in scene files copied under its `scenes/` directory, and session startup falls back to a project initialized from that template when no saved project can load.

**Tech Stack:** C++20, yaml-cpp, LXEngine lxe_editor command bus, existing C++ integration tests and Python HTTP black-box tests.

---

### Task 1: Register Multiple Template Scenes

**Files:**
- Modify: `src/demos/lxe_editor/project_document.hpp`
- Modify: `src/demos/lxe_editor/project_document.cpp`
- Modify: `src/demos/lxe_editor/project_session.cpp`
- Test: `src/test/integration/test_project_session.cpp`

- [ ] **Step 1: Write failing test**

Add a project template fixture with `scenes:` entries and assert `initProject` registers both scenes, opens the configured default, and allows `openScene` by registered id.

- [ ] **Step 2: Verify RED**

Run: `cmake --build build --target test_project_session && ./build/src/test/test_project_session`

Expected: FAIL because `ProjectTemplateDocument` does not load `scenes:` and `initProject` registers only the default scene.

- [ ] **Step 3: Implement template scene entries**

Load optional `scenes:` from template YAML as `{id, path}` entries. During `initProject`, use those entries when present, validate each path is copied and contained, and fall back to the existing single-default behavior for old templates.

- [ ] **Step 4: Verify GREEN**

Run the same `test_project_session` command and confirm PASS.

### Task 2: Add `lxe_default` Template

**Files:**
- Create: `assets/project_templates/lxe_default/project_template.yaml`
- Create/copy scene files under: `assets/project_templates/lxe_default/scenes/`
- Test: `src/test/integration/test_project_session.cpp`

- [ ] **Step 1: Write failing test**

Add a test that initializes `lxe_default`, expects project id `lxe_default`, active scene `scenes/lxe_editor.scene.yaml`, and scene ids for every current `assets/scenes/*.scene.yaml`.

- [ ] **Step 2: Verify RED**

Run: `cmake --build build --target test_project_session && ./build/src/test/test_project_session`

Expected: FAIL because the template does not exist.

- [ ] **Step 3: Add template assets**

Create `assets/project_templates/lxe_default/project_template.yaml` with `defaultScene: scenes/lxe_editor.scene.yaml`, `copy: [scenes/]`, and explicit `scenes:` entries for built-in scenes. Copy current built-in scene YAMLs into the template `scenes/` directory.

- [ ] **Step 4: Verify GREEN**

Run the same `test_project_session` command and confirm PASS.

### Task 3: Startup Opens Default Project

**Files:**
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Test: `src/test/integration/test_lxe_editor_session.cpp`

- [ ] **Step 1: Write failing tests**

Update the broken-last-project test so startup falls back to `lxe_default` instead of no project. Add a no-last-project startup test that expects current project `lxe_default`, active scene `scenes/lxe_editor.scene.yaml`, and a loaded runtime scene.

- [ ] **Step 2: Verify RED**

Run: `cmake --build build --target test_lxe_editor_session && ./build/src/test/test_lxe_editor_session`

Expected: FAIL because startup currently creates an empty runtime scene.

- [ ] **Step 3: Implement startup fallback**

Add a private/default helper in `editor_session.cpp` that closes any broken project, clears `lastProject`, initializes or opens `lxe_default`, loads its active scene synchronously during startup, and persists `lastProject`.

- [ ] **Step 4: Verify GREEN**

Run the same `test_lxe_editor_session` command and confirm PASS.

### Task 4: Full Reload Cleanup

**Files:**
- Modify: `src/demos/lxe_editor/editor_session.cpp`
- Modify if needed: `src/demos/lxe_editor/scene_interaction_controller.*`
- Test: `src/test/integration/test_lxe_editor_session.cpp`

- [ ] **Step 1: Write failing test**

Select a node, queue a different built-in scene, flush it, and assert selection is empty and the new scene has no selected stale node. Also assert a second `scene open` replaces any previous pending scene open.

- [ ] **Step 2: Verify RED**

Run: `cmake --build build --target test_lxe_editor_session && ./build/src/test/test_lxe_editor_session`

Expected: FAIL for whichever stale state is not cleared.

- [ ] **Step 3: Implement cleanup**

Before accepting a new pending runtime and before flushing it, reset pending runtime/path/editor sidecar. On rebuild, keep editor command history and console but clear selection and transient interaction state.

- [ ] **Step 4: Verify GREEN**

Run the same `test_lxe_editor_session` command and confirm PASS.

### Task 5: Black-Box Command Workflow

**Files:**
- Modify: `tests/lxe_editor/test_scene_io.py`

- [ ] **Step 1: Write failing black-box test**

Add a test that starts from default project state, runs `project open lxe_default`, then `scene open ibl_metal_sphere`, then `scene open lxe_editor`, asserting project and runtime scene state after each switch.

- [ ] **Step 2: Verify RED/GREEN**

Run: `python -m unittest tests.lxe_editor.test_scene_io`

Expected before implementation: FAIL if default project/open behavior is missing. Expected after implementation: PASS, or SKIP if the editor binary cannot launch in the local environment.

### Final Verification

- [ ] Run `cmake --build build --target test_project_session test_lxe_editor_session`
- [ ] Run `./build/src/test/test_project_session`
- [ ] Run `./build/src/test/test_lxe_editor_session`
- [ ] Run `python -m unittest tests.lxe_editor.test_scene_io`
