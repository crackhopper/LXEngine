# LXE Editor Promotion, Core Decoupling, And Live Bindless Viewport Design

Date: 2026-06-15

## Decision

The editor will be promoted out of the demo tree into a first-class application
layer under `src/editor/`. The executable should keep the target name
`lxe_editor`, but its production sources should no longer be owned by
`src/demos/lxe_editor/`.

The promoted editor remains an application adapter on top of the current
core/render architecture. The core scene, render path graph, frame graph,
`RenderWorkCompiler`, `RenderInput`, `RenderInputDesc`, bindless scene resource
tables, and `SceneResourceTable` contracts are treated as clean and are not
redesigned by this work.

The editor-specific stack currently split between `src/core/editor/` and
`src/demos/lxe_editor/` will move into `src/editor/`. This includes
`CommandBus`; command dispatch, undo/redo, console, editor selection state,
panels, gizmo overlay, built-in editor commands, project/session state, API
service, recording, and display launch state are editor application behavior,
not core renderer behavior.

The live viewport black-frame issue will be fixed by making the promoted editor
provide the explicit render-view facts the current renderer expects instead of
relying on old implicit camera-target behavior.

The main rendering flow remains in `src/core/` and `src/backend/`. The editor
is not allowed to become a second renderer or a private render-work compiler.
Anything that prepares, validates, schedules, uploads, or executes render work
and is useful to realtime profile output or future offline renderer reuse must
stay in core/backend. The editor may only adapt editor state into the explicit
facts those layers consume.

The live viewport must render through the current bindless scene submission
flow. Fixing the black frame by reviving old per-draw descriptors, legacy
`MaterialUBO` paths, fallback materials, or editor-private render submission is
out of scope and must be rejected by tests or audits.

This migration should hard-cut obsolete editor integration code whenever the
replacement path is in place. Do not keep old include paths, forwarding headers,
duplicate CMake source lists, demo-owned production targets, or compatibility
commands just to reduce churn. If a legacy path cannot be removed in this slice,
the implementation must name the blocker, the owner, and the exact removal
condition.

## Current Evidence

Remote MCP diagnostics on the Helmet scene showed:

- `realtime-render run preview` produced a valid 192x192 `render-cpu_srgb.png`.
- The same loaded scene in the live editor viewport had
  `hdr.color` stats `min=0 max=0 mean=0 nonZeroRatio=0`.
- The live `depth.main` attachment stayed at clear depth
  `min=1 max=1 mean=1`.
- The scene was loaded and contained 4 nodes, 2 cameras, 1 light, and selectable
  `/damaged_helmet` bounds.
- `preview toggle` changed the active camera to `/game_cam` but did not make
  live `hdr.color` or `depth.main` non-clear.

This proves the failure is not missing assets, an unloaded scene, a bad selected
object, or a display-only tone-mapping issue. It is a live viewport adapter
problem: the offscreen profile path passes explicit profile render facts, while
the live viewport still relies on older implicit state.

Related code facts:

- Offscreen profile generation explicitly builds a profile camera resource and
  visible mask before calling the renderer path:
  `src/backend/vulkan/vulkan_realtime_renderer.cpp`.
- Live render work currently falls back through `RenderTarget`-based camera
  resource and visible-mask discovery.
- `CameraComponent::matchesTarget()` treats an unset camera target as matching
  only the default `RenderTarget{}`. The live forward pass uses an HDR offscreen
  target, so this old assumption can filter live renderables even though the
  profile path succeeds.
- `src/core/editor/` contains ImGui panels, editor overlay, command bus, command
  handlers, console, and editor selection state. These depend on editor
  behavior and do not belong in core.
- `src/demos/lxe_editor/` currently owns most of the editor product code even
  though it is no longer a throwaway demo: API server/service, project/session
  state, commands, scene runtime, recording, and UI overlay all live there.
- The repo already has bindless contract tests such as
  `test_bindless_indirect_contract` and `test_bindless_validation_contract`;
  the live viewport regression test should complement those by proving the
  startup editor path uses the same bindless render route that profile output
  relies on.

## Goals

1. Promote editor application code into `src/editor/`.
2. Move the editor application code out of both `src/core/editor/` and
   `src/demos/lxe_editor/`.
3. Move `CommandBus` with the editor stack.
4. Keep core/render contracts unchanged except for include/build cleanup made
   necessary by the move.
5. Introduce an editor render-view adapter that translates editor state
   into explicit renderer-facing facts.
6. Make live viewport rendering use the same scene render contract as realtime
   profile output: explicit camera resource, culling mask, render target intent,
   pass preparation facts, and bindless scene resource submission.
7. Preserve existing editor workflows: selection, picking, preview toggle,
   scene tree, inspector, command console, recording, API, and MCP command
   surface.
8. Add startup verification that loads the Helmet scene in the editor and fails
   if the live viewport remains black after scene load.
9. Add verification that fails when Helmet profile output succeeds but the live
   editor viewport remains black.
10. Keep reusable render preparation and execution in core/backend so offline
   renderer work can reuse the same flow.
11. Hard-cut superseded editor integration code in the same implementation
    slice instead of leaving compatibility shims behind.

## Non-Goals

- No redesign of `RenderWorkCompiler`, `FrameGraph`, `RenderPathGraph`,
  `RenderInput`, `RenderInputDesc`, `PipelineBuildDesc`, or
  `SceneResourceTable`.
- No compatibility bridge that keeps `src/core/editor` forwarding headers.
- No long-lived compatibility bridge that keeps production editor code under
  `src/demos/lxe_editor`.
- No old include path aliases such as `core/editor/...` after the move.
- No duplicate registration of the editor executable through both
  `src/demos/lxe_editor` and `src/editor`.
- No second public render contract or editor-private render graph.
- No render-work compiler, upload planner, pipeline resolver, frame-graph
  scheduler, bindless descriptor uploader, or draw executor implementation
  inside `src/editor`.
- No change to Material v3 source contracts or shader ABI.
- No legacy per-draw descriptor, `MaterialUBO`, or fallback-material route to
  make the viewport nonblack.
- No BMW transparent/glass implementation work.
- No package, BC7, pipeline cache serialization, or OfflineRT work.
- No broad UI redesign beyond keeping the existing editor controls working.

## Target Directory Boundary

The editor app owns a new first-class source root:

```text
src/editor/
  CMakeLists.txt
  main.cpp
  api/
    lxe_editor_api_protocol.*
    lxe_editor_api_server.*
    lxe_editor_api_service.*
    api_token_state.*
  app/
    editor_session.*
    runtime_state.*
    display_launch_options.*
    editor_config_state.*
    editor_data_state.*
    editor_log_file.*
    editor_scene_state.*
  commands/
    command_bus.*
    builtin_commands.*
    display_commands.*
    lxe_editor_command_helpers.*
    project_commands.*
    realtime_render_commands.*
    recording_commands.*
    render_debug_commands.*
    scene_project_commands.*
  panels/
    console_input_controller.*
    console_panel.*
    inspector_panel.*
    scene_tree_panel.*
    viewport_overlay.*
  project/
    builtin_asset_catalog.*
    project_catalog.*
    project_document.*
    project_session.*
    realtime_render_profile.*
    scene_builder.*
    scene_document.*
  render/
    editor_render_view.*
  runtime/
    camera_rig.*
    editor_camera_state.*
    scene_interaction_controller.*
    scene_input_routing.*
    scene_runtime.*
    scene_view_rect.*
    selection_camera_input.*
  ui/
    gizmo_adapter.*
    ui_overlay.*
```

The exact subfolder names may be adjusted during implementation if local style
suggests a better split, but production editor code must no longer live under
`src/core/editor/` or `src/demos/lxe_editor/`.

Prefer a testable `LX_Editor` library plus a thin `lxe_editor` executable target
instead of repeating editor source lists in tests. The top-level build should
add `src/editor` through an editor-specific option such as `LX_BUILD_EDITOR`
instead of treating the editor as a demo-only target. Existing tooling that
looks for the `lxe_editor` executable target should keep working.

`src/core/` remains responsible for:

- scene graph and components
- scene resource table
- frame graph and render work compiler
- pipeline identity and pipeline build descriptors
- renderer interfaces
- math, image, asset, and platform-neutral resource models

`src/backend/` remains responsible for:

- Vulkan renderer orchestration
- pipeline and descriptor binding
- bindless descriptor table upload and validation
- upload planning and GPU synchronization
- frame graph attachment ownership
- realtime profile output execution
- reusable backend hooks that can be shared by live viewport, profile output,
  and offline renderer paths

After the move, production code under `src/core/` must not include
`editor/...` or `demos/lxe_editor/...` headers. Production code under
`src/backend/` should only depend on editor-neutral render facts or interfaces,
not on editor state, panels, commands, or session classes.

## Editor Render View Adapter

Add an editor-owned adapter under `src/editor/render/` that derives a render
view from current editor state. A suggested name is `EditorRenderView`, but the
implementation may choose a nearby local name.

The adapter should expose a small value object containing:

- active camera path
- camera snapshot / `CameraResource`
- viewport extent
- render target intent for the live viewport
- visible mask derived from the active camera culling mask
- bindless scene resource table / upload view identity, or a reference to the
  existing renderer-owned data needed to submit through the bindless path
- preview mode flag
- editor overlay visibility policy

The adapter is the single place where `previewEnabled`, editor camera, game
camera, viewport size, and editor overlay policy are converted into renderer
facts.

The adapter must not:

- change core camera matching semantics
- mutate renderer internals directly
- infer behavior from scene path or scene name
- create fallback materials or fake render resources
- build render inputs, validate render inputs, create pipeline descriptors, own
  frame graph passes, or execute draw/dispatch commands
- upload bindless descriptors or emulate bindless resource lookup

## Live Viewport Rendering Semantics

When preview is off:

- active render camera is `editor_cam`
- editor overlay and editor helper layers may be visible according to existing
  editor rules
- selection and picking remain enabled

When preview is on:

- active render camera is `game_cam`
- editor editing interactions are suppressed as they are today
- scene renderables are still submitted through the same explicit render-view
  path

Both modes must pass explicit camera/culling facts into the live render path.
The live viewport must not depend on `CameraComponent::matchesTarget()` matching
the current frame graph target in order to decide whether scene renderables are
visible.

Both modes must submit scene draw data through the existing bindless resource
flow. A successful live viewport frame must prove that typed material, texture,
object, draw, mesh, primitive, and attribute data came from the current
`SceneResourceTable`/bindless upload path, not from legacy `MaterialUBO` or
fallback material behavior.

The existing offscreen `realtime-render run <profile>` command stays as a
profile-output path, but the editor live viewport and profile output should
share the same renderer-facing preparation contract. They may still differ in
output sink: live viewport presents to the swapchain; profile output writes EXR,
PNG, and metadata.

The shared preparation contract must live outside `src/editor`, in core or
backend. If profile output, live viewport, and future offline renderer need the
same render-view facts or the same pass preparation entry point, that reusable
logic belongs in core or backend. The editor should only choose which editor
camera/view is active and pass the resulting facts down.

## Selection And Picking Compatibility

Selection and picking should continue to operate on editor screen-space
semantics:

- use the same active camera that the editor viewport is displaying
- honor preview mode interaction suppression
- keep selection state in editor-owned `EditorState`
- keep scene graph ownership in `core::Scene`

This means the migration should not move scene path lookup, bounds, transform,
or component ownership out of core. It only moves editor behavior and state out
of core.

## Command Surface

`CommandBus` moves with the editor stack because it owns editor command
semantics:

- undo/redo history
- command history and structured command result metadata
- console dispatch and completion
- editor-specific commands such as `select`, `move`, `preview`, `state`,
  `render debug`, `project`, `scene`, and `realtime-render`

The HTTP API, WebSocket API, MCP integration, recording, and UI must continue to
call the same editor command surface after include paths and namespaces are
updated.

No core subsystem should depend on `CommandBus`.

## Migration Strategy

### Step 1: Promote Editor Stack Without Behavior Change

Use `git mv` for production editor files currently under `src/core/editor/` and
`src/demos/lxe_editor/` into `src/editor/`.

Update:

- include paths
- namespaces if needed
- `src/core/CMakeLists.txt`
- root `CMakeLists.txt`
- new `src/editor/CMakeLists.txt`
- `src/demos/CMakeLists.txt`
- editor-specific tests that include old headers
- tools/tests that refer to the `lxe_editor` executable target

This step should keep behavior unchanged. It is a boundary move, not a render
fix. The executable target name should remain `lxe_editor` unless implementation
reveals a concrete build reason to change it.

Once the moved sources compile from `src/editor`, delete the old
`src/core/editor` production files and remove `src/demos/lxe_editor` from the
production build. Avoid adding temporary forwarding headers or duplicated
source-list compatibility.

### Step 2: Compile And Test Moved Editor Stack

Keep the existing editor command, panel, overlay, API, and interaction tests
passing after the move. Rename tests only where their file names imply core
ownership.

Expected affected tests include:

- `test_command_bus`
- `test_editor_multi_select`
- `test_inspector_panel`
- `test_scene_tree_panel`
- `test_viewport_overlay`
- `test_lxe_editor_api_service`
- `test_lxe_editor_api_server`
- `test_lxe_editor_interaction`
- `test_realtime_render_profile_commands`

### Step 3: Add Editor Render View Adapter

Introduce the adapter in `src/editor/render/` and wire it into live rendering
and debug rendering. The adapter should produce explicit live render facts from
`EditorState`, `SceneRuntime`, camera components, and the current viewport
extent.

The adapter output should be a small value object or callback payload. Reusable
render code that consumes that payload must stay in core/backend.

### Step 4: Repair Live Viewport Submission

Update the live viewport path so the renderer receives explicit active camera
and visible-mask facts, then submit through the existing bindless draw/resource
path. The exact call shape should follow existing renderer interfaces as much as
possible. If a thin backend hook is required, it must be adapter-facing and
should not alter core render contracts.

Any touched backend hook must remain generic: it should accept explicit render
view facts, not editor-specific state. If this work reveals reusable pass
preparation logic currently duplicated between profile output and live viewport,
move that logic into backend-side reusable helpers rather than copying it into
`src/editor`.

The implementation must add a negative test, audit, or diagnostic that fails if
the live path falls back to legacy per-draw descriptors, `MaterialUBO`, fallback
materials, or a non-bindless editor-specific submission path.

Any old live viewport submission branch made unreachable by the explicit
render-view path should be deleted in this step. If the branch is retained only
for diagnostics, it must be named as a negative audit path and assert rejection,
not success.

### Step 5: Close The Regression With Startup And Live Smoke

Add automated smoke coverage that starts `lxe_editor`, loads the Helmet scene,
waits for the first post-load live frame, and checks live viewport render
targets, not only offscreen profile output.

Minimum acceptance:

- active scene is `Helmet Standard PBR`
- `hdr.color.nonZeroRatio > 0`
- `depth.main.min < 1`
- no required draw path depends on fallback material
- bindless scene resource upload/submission is observed by an assertion,
  diagnostic, debug stat, or existing bindless validation hook
- remote or local MCP/debug command can still read a nonblack Helmet output

The existing offscreen smoke remains useful but is not sufficient for this
regression.

## Tests And Audits

Required verification commands:

```bash
cmake --build build --target lxe_editor BuildTest
ctest --test-dir build --output-on-failure -R "test_command_bus|test_command_bus_v2|test_editor_multi_select|test_inspector_panel|test_scene_tree_panel|test_viewport_overlay|test_lxe_editor_layout|test_lxe_editor_api_service|test_lxe_editor_api_server|test_lxe_editor_interaction|test_realtime_render_profile_commands|test_bindless_indirect_contract|test_bindless_validation_contract|test_helmet_standard_pbr_realtime_smoke"
```

If Vulkan live viewport verification requires a video device:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -R "helmet.*live|lxe_editor.*startup|lxe_editor.*viewport"
```

Required source audits:

```bash
rg -n "core/editor" src
rg -n "#include \"core/editor" src
rg -n "CommandBus|EditorState|ViewportOverlay|InspectorPanel|SceneTreePanel" src/core
rg -n "demos/lxe_editor|src/demos/lxe_editor" CMakeLists.txt src
rg -n "MaterialUBO|LegacyPerItem|fallback material|fallback.*material|non-bindless" src/editor src/backend src/core
rg -n "core/editor|demos/lxe_editor|lxe_editor/editor|forwarding header|old editor|legacy editor|compat.*editor" CMakeLists.txt src
```

Expected result after completion:

- no `core/editor` includes remain
- `CommandBus` and editor UI/state classes do not live in or get included by
  `src/core`
- production editor code lives under `src/editor`, not `src/demos/lxe_editor`
- no production editor path references legacy material or non-bindless fallback
  tokens except named negative tests/audits
- no production forwarding headers or duplicate build entries remain for the
  old editor locations
- any remaining mentions in docs or historical requirements are explicitly
  historical

## Risks

The largest mechanical risk is include/CMake churn from promoting a demo target
into a first-class source root. The safest implementation sequence is to move
the editor stack first with behavior unchanged, then repair live rendering.

The largest behavioral risk is accidentally hiding the live viewport failure by
only fixing `render debug dump` or only fixing offscreen profile output. The new
smoke must check live frame graph attachments.

The largest architecture risk is adding a new editor-specific render contract.
The adapter must translate editor state into existing renderer-facing facts
rather than creating a parallel graph or material system.

A second architecture risk is accidentally moving renderer orchestration into
`src/editor` while fixing the viewport. The implementation must keep the main
render flow in core/backend so offline renderer work can continue to reuse it.

A third architecture risk is making the viewport nonblack by using a legacy
non-bindless route. The fix must prove it uses the current bindless scene
resource path.

A fourth risk is leaving old code behind as a shadow path. The implementation
must prefer deletion over compatibility, with tests and source audits proving
the old route is not still buildable as a positive path.

## Completion Criteria

The work is complete only when:

- `src/core/editor/` no longer exists as production code.
- production editor code no longer lives under `src/demos/lxe_editor/`.
- `src/editor/` owns the editor application layer and builds the `lxe_editor`
  executable target.
- `CommandBus` is owned by `src/editor`.
- `src/core` has no dependency on editor app headers.
- old editor include paths and old demo-owned production CMake entries are
  deleted, not forwarded.
- Existing editor command/UI/API tests pass after migration.
- Helmet offscreen realtime smoke still passes.
- Editor startup smoke loads the Helmet scene and renders at least one nonblack
  live viewport frame after scene load.
- Helmet live editor viewport smoke proves nonblack color and non-clear depth.
- Helmet live editor viewport smoke proves bindless resource submission, or a
  paired audit/diagnostic fails if a non-bindless fallback path is used.
- Remote MCP can load the Helmet scene in `lxe_editor` and observe nonblack live
  viewport output.
- Vulkan validation output for the touched live path does not introduce new
  attachment-format or render-pass compatibility warnings.
- No reusable render-work preparation or execution logic was moved from
  core/backend into `src/editor`.
