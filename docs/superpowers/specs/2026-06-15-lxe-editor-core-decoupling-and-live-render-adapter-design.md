# LXE Editor Core Decoupling And Live Render Adapter Design

Date: 2026-06-15

## Decision

`lxe_editor` will be repaired as an application adapter on top of the current
core/render architecture. The core scene, render path graph, frame graph,
`RenderWorkCompiler`, `RenderInput`, `RenderInputDesc`, and
`SceneResourceTable` contracts are treated as clean and are not redesigned by
this work.

The editor-specific stack currently under `src/core/editor/` will move into
`src/demos/lxe_editor/`. This includes `CommandBus`; command dispatch, undo/redo,
console, editor selection state, panels, gizmo overlay, and built-in editor
commands are editor application behavior, not core renderer behavior.

The live viewport black-frame issue will be fixed by making `lxe_editor` provide
the explicit render-view facts the current renderer expects instead of relying
on old implicit camera-target behavior.

The main rendering flow remains in `src/core/` and `src/backend/`. `lxe_editor`
is not allowed to become a second renderer or a private render-work compiler.
Anything that prepares, validates, schedules, uploads, or executes render work
and is useful to realtime profile output or future offline renderer reuse must
stay in core/backend. The editor may only adapt editor state into the explicit
facts those layers consume.

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

## Goals

1. Move editor application code out of `src/core/editor/` into
   `src/demos/lxe_editor/`.
2. Move `CommandBus` with the editor stack.
3. Keep core/render contracts unchanged except for include/build cleanup made
   necessary by the move.
4. Introduce an `lxe_editor` render-view adapter that translates editor state
   into explicit renderer-facing facts.
5. Make live viewport rendering use the same scene render contract as realtime
   profile output: explicit camera resource, culling mask, render target intent,
   and pass preparation facts.
6. Preserve existing editor workflows: selection, picking, preview toggle,
   scene tree, inspector, command console, recording, API, and MCP command
   surface.
7. Add verification that fails when Helmet profile output succeeds but the live
   editor viewport remains black.
8. Keep reusable render preparation and execution in core/backend so offline
   renderer work can reuse the same flow.

## Non-Goals

- No redesign of `RenderWorkCompiler`, `FrameGraph`, `RenderPathGraph`,
  `RenderInput`, `RenderInputDesc`, `PipelineBuildDesc`, or
  `SceneResourceTable`.
- No compatibility bridge that keeps `src/core/editor` forwarding headers.
- No second public render contract or editor-private render graph.
- No render-work compiler, upload planner, pipeline resolver, frame-graph
  scheduler, or draw executor implementation inside `lxe_editor`.
- No change to Material v3 source contracts or shader ABI.
- No BMW transparent/glass implementation work.
- No package, BC7, pipeline cache serialization, or OfflineRT work.
- No broad UI redesign beyond keeping the existing editor controls working.

## Target Directory Boundary

The editor app owns:

```text
src/demos/lxe_editor/editor/
  command_bus.*
  commands/builtin_commands.*
  console_input_controller.*
  console_panel.*
  editor_config.*
  editor_state.*
  gizmo_adapter.*
  inspector_panel.*
  scene_tree_panel.*
  viewport_overlay.*
```

The exact folder name may be adjusted during implementation if local style
suggests a better split, but these files must no longer live under
`src/core/editor/`.

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
- upload planning and GPU synchronization
- frame graph attachment ownership
- realtime profile output execution
- reusable backend hooks that can be shared by live viewport, profile output,
  and offline renderer paths

After the move, production code under `src/core/` must not include
`demos/lxe_editor/...` headers.

## Editor Render View Adapter

Add an editor-owned adapter that derives a render view from current editor
state. A suggested name is `EditorRenderView`, but the implementation may choose
a nearby local name.

The adapter should expose a small value object containing:

- active camera path
- camera snapshot / `CameraResource`
- viewport extent
- render target intent for the live viewport
- visible mask derived from the active camera culling mask
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

The existing offscreen `realtime-render run <profile>` command stays as a
profile-output path, but the editor live viewport and profile output should
share the same renderer-facing preparation contract. They may still differ in
output sink: live viewport presents to the swapchain; profile output writes EXR,
PNG, and metadata.

The shared preparation contract must live below `lxe_editor`. If profile output,
live viewport, and future offline renderer need the same render-view facts or
the same pass preparation entry point, that reusable logic belongs in core or
backend. `lxe_editor` should only choose which editor camera/view is active and
pass the resulting facts down.

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

### Step 1: Move Editor Stack Without Behavior Change

Use `git mv` for the editor files currently under `src/core/editor/` into
`src/demos/lxe_editor/editor/`.

Update:

- include paths
- namespaces if needed
- `src/core/CMakeLists.txt`
- `src/demos/lxe_editor/CMakeLists.txt`
- editor-specific tests that include old headers

This step should keep behavior unchanged. It is a boundary move, not a render
fix.

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

Introduce the adapter in `src/demos/lxe_editor/` and wire it into live rendering
and debug rendering. The adapter should produce explicit live render facts from
`EditorState`, `SceneRuntime`, camera components, and the current viewport
extent.

The adapter output should be a small value object or callback payload. Reusable
render code that consumes that payload must stay in core/backend.

### Step 4: Repair Live Viewport Submission

Update the live viewport path so the renderer receives explicit active camera
and visible-mask facts. The exact call shape should follow existing renderer
interfaces as much as possible. If a thin backend hook is required, it must be
adapter-facing and should not alter core render contracts.

Any touched backend hook must remain generic: it should accept explicit render
view facts, not editor-specific state. If this work reveals reusable pass
preparation logic currently duplicated between profile output and live viewport,
move that logic into backend-side reusable helpers rather than copying it into
`lxe_editor`.

### Step 5: Close The Regression With A Live Smoke

Add an automated smoke that loads the Helmet scene into `lxe_editor` and checks
live viewport render targets, not only offscreen profile output.

Minimum acceptance:

- active scene is `Helmet Standard PBR`
- `hdr.color.nonZeroRatio > 0`
- `depth.main.min < 1`
- no required draw path depends on fallback material
- remote or local MCP/debug command can still read a nonblack Helmet output

The existing offscreen smoke remains useful but is not sufficient for this
regression.

## Tests And Audits

Required verification commands:

```bash
cmake --build build --target lxe_editor BuildTest
ctest --test-dir build --output-on-failure -R "test_command_bus|test_editor_multi_select|test_inspector_panel|test_scene_tree_panel|test_viewport_overlay|test_lxe_editor_api_service|test_lxe_editor_api_server|test_lxe_editor_interaction|test_realtime_render_profile_commands|test_helmet_standard_pbr_realtime_smoke"
```

If Vulkan live viewport verification requires a video device:

```bash
xvfb-run -a ctest --test-dir build --output-on-failure -R "helmet.*live|lxe_editor.*viewport"
```

Required source audits:

```bash
rg -n "core/editor" src
rg -n "#include \"core/editor" src
rg -n "CommandBus|EditorState|ViewportOverlay|InspectorPanel|SceneTreePanel" src/core
```

Expected result after completion:

- no `core/editor` includes remain
- `CommandBus` and editor UI/state classes do not live in or get included by
  `src/core`
- any remaining mentions in docs or historical requirements are explicitly
  historical

## Risks

The largest mechanical risk is include/CMake churn. The safest implementation
sequence is to move the editor stack first with behavior unchanged, then repair
live rendering.

The largest behavioral risk is accidentally hiding the live viewport failure by
only fixing `render debug dump` or only fixing offscreen profile output. The new
smoke must check live frame graph attachments.

The largest architecture risk is adding a new editor-specific render contract.
The adapter must translate editor state into existing renderer-facing facts
rather than creating a parallel graph or material system.

A second architecture risk is accidentally moving renderer orchestration into
`lxe_editor` while fixing the viewport. The implementation must keep the main
render flow in core/backend so offline renderer work can continue to reuse it.

## Completion Criteria

The work is complete only when:

- `src/core/editor/` no longer exists as production code.
- `CommandBus` is owned by `lxe_editor`.
- `src/core` has no dependency on editor app headers.
- Existing editor command/UI/API tests pass after migration.
- Helmet offscreen realtime smoke still passes.
- Helmet live editor viewport smoke proves nonblack color and non-clear depth.
- Remote MCP can load the Helmet scene in `lxe_editor` and observe nonblack live
  viewport output.
- Vulkan validation output for the touched live path does not introduce new
  attachment-format or render-pass compatibility warnings.
- No reusable render-work preparation or execution logic was moved from
  core/backend into `lxe_editor`.
