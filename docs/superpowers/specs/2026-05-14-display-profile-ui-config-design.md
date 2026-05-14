# Per-Display UI Configuration Design

## Context

`lxe_editor` currently persists one editor UI configuration in
`data/lxe_editor/editor_config.yaml`. The file stores native window placement,
floating panel layout, and preferences such as `uiFontScale`.

That single configuration is not enough for users who run the editor on
different physical displays. A compact laptop panel, an external 4K monitor,
and a remote display often need different window sizes, panel placement, and
font scale.

## Goals

- Persist separate UI configuration per physical display.
- Select the startup display from the command line.
- Default to the first available display when no command line display is given
  and no usable configured active display exists.
- Keep `editor_config.yaml` hand-editable.
- Avoid duplicating every UI field into every display profile.
- Make remote display-profile setup testable through the editor command/API/MCP
  surface.
- Do not add a new windowing or monitor library.

## Non-Goals

- No live UI hot-switch when the window is dragged between displays.
- No requirement for hardware serial-number display identity.
- No graphical startup display picker in this iteration.
- No scene-document changes.

## Configuration Format

`editor_config.yaml` moves to `version: 2` and uses a default profile plus
per-display overrides.

```yaml
version: 2
activeDisplay: "sdl:1:DELL U2720Q:3840x2160:1.50"

displayDefault:
  key: "display-default"
  window:
    width: 1280
    height: 720
    maximized: false
  layout:
    windows: []
  preferences:
    uiFontScale: 1.0

displayProfiles:
  - key: "sdl:0:Built-in Display:1920x1080:1.00"
    label: "0: Built-in Display (1920x1080 @ 1.00x)"
    available: true
    overrides: {}

  - key: "sdl:1:DELL U2720Q:3840x2160:1.50"
    label: "1: DELL U2720Q (3840x2160 @ 1.50x)"
    available: true
    overrides:
      window:
        x: 2100
        y: 80
        width: 1800
        height: 1100
      preferences:
        uiFontScale: 1.25
      layout:
        windows:
          - id: Inspector
            x: 1460
            y: 80
            width: 420
            height: 900
```

`displayDefault` is the complete baseline. Each display profile stores only the
fields that differ from the default.

Merge rules:

- `window` fields override independently, so a profile may override only width
  or only position.
- `preferences` fields override independently. `uiFontScale` remains clamped to
  the existing supported range.
- `layout.windows` merges by `id`. A display override updates only the fields it
  includes for that panel. New ids are allowed.
- Missing or invalid override fields are ignored and fall back to the default.

Save rules:

- Saving diffs the effective active display configuration against
  `displayDefault` and writes only changed fields into the active display
  `overrides`.
- `displayDefault` is preserved as an editable baseline.
- The config is written on first startup if missing.
- The config is written on exit after syncing the latest display list and active
  profile state.
- Existing dirty-save behavior from `UiOverlay` continues, but writes only the
  active display override.

Migration:

- A `version: 1` file is read as the legacy single configuration.
- The legacy configuration becomes `displayDefault`.
- Current display profiles are created with empty overrides.
- The next save writes `version: 2`.

Unavailable displays:

- Startup enumeration marks currently present displays as `available: true`.
- Profiles for displays not currently connected are retained and marked
  `available: false`.
- This keeps hand-written or previously tuned display overrides available when
  the display is connected again.

## Display Identity

Display information is represented in the platform layer with a backend-neutral
data structure:

```cpp
struct DisplayInfo final {
  int index = 0;
  std::string backend;
  std::string name;
  WindowUsableBounds bounds;
  WindowUsableBounds usableBounds;
  float contentScale = 1.0f;
  std::string key;
  std::string label;
};
```

The profile key is a stable-enough fallback:

```text
<backend>:<index>:<name>:<usable-width>x<usable-height>:<scale>
```

Examples:

```text
sdl:0:Built-in Display:1920x1080:1.00
sdl:1:DELL U2720Q:3840x2160:1.50
glfw:0:Primary Monitor:1920x1080:1.00
```

This intentionally does not require a hardware serial number. SDL3 and GLFW
both provide enough display name, work area, and scale information for a useful
fallback without introducing another dependency.

## Window System Changes

`LX_infra::Window` gains static display enumeration before a window is created.

SDL implementation:

- `SDL_GetDisplays`
- `SDL_GetDisplayName`
- `SDL_GetDisplayBounds`
- `SDL_GetDisplayUsableBounds`
- `SDL_GetDisplayContentScale`

GLFW implementation:

- `glfwGetMonitors`
- `glfwGetMonitorName`
- `glfwGetMonitorWorkarea`
- `glfwGetMonitorContentScale`

The existing placement sanitizer remains in use. When a selected display
profile has no explicit window `x` / `y`, startup places the default window
inside the selected display's usable bounds.

## Startup Behavior

Startup order:

1. Initialize the window system.
2. Enumerate displays.
3. Load or create `editor_config.yaml`.
4. Sync `displayProfiles` with current displays.
5. If `--display-list` is present, print the display list and exit before
   creating Vulkan resources.
6. Select a startup display:
   - `--display <index-or-key>` wins.
   - Otherwise use `activeDisplay` from the config when it is available.
   - Otherwise use display index `0`.
7. Compose the selected display effective config from `displayDefault` plus its
   overrides.
8. Create the main window on the selected display using that effective window
   placement.
9. Bind the editor session to that display profile for the whole process.
10. On exit, save the active display override and update `activeDisplay`.

`--display-list` output includes index, key, label, bounds, usable bounds,
content scale, availability, and whether the display is the configured active
display.

Invalid startup cases:

- No displays: fail startup with a clear error.
- `--display` is not a valid index or key: fail startup and suggest
  `--display-list`.
- Config parse failure: print the error and continue with a default v2 document
  generated from current displays.

## Command, API, And MCP Surface

Display configuration should be remotely inspectable and editable through the
same command-first surface used by HTTP, WebSocket, and MCP. MCP must not bypass
the command/API service and write the file directly.

Commands:

- `display list`
  Returns the synced display profiles with `key`, `label`, `available`, and
  active flags.
- `display active`
  Returns the display key bound to the current editor process.
- `display config get [key|active|default]`
  Returns the default profile or a display profile, including both `overrides`
  and the composed effective config.
- `display config set <key|default> <json-or-yaml-patch>`
  Updates `displayDefault` or a display override and saves
  `editor_config.yaml`.
- `display select <key>`
  Updates `activeDisplay` for the next launch and saves the config. It returns
  a message that restart is required. It does not hot-switch the current UI.

Manager restart integration:

- The existing manager MCP operation `ops.editor_restart` remains responsible
  for restarting the managed editor process.
- Display commands do not add another restart command. They return restart
  guidance when a change affects the next launch.
- Remote tests that need to validate startup display selection should call the
  display command/API/MCP surface first, then call `ops.editor_restart`, then
  verify the restarted editor state.

Remote test flow:

1. Start `lxe_editor`.
2. Query `display list` through MCP.
3. Update `displayDefault` or a display override through `display config set`.
4. Set the next startup display through `display select`.
5. Restart the editor through manager MCP `ops.editor_restart`.
6. Verify startup selection and UI config.

## Component Responsibilities

- `DisplayInfo` and display-key helpers live in the platform/window boundary.
- `LX_infra::Window` owns SDL/GLFW display enumeration and selected-display
  window placement.
- `EditorConfigState` owns YAML v1/v2 loading, migration, profile syncing,
  effective config composition, and override diff saving.
- `main.cpp` owns command-line parsing and startup orchestration.
- `UiOverlay` continues to read and mutate the effective `EditorConfigDocument`.
  It does not need to know about display selection.
- `LxeEditorApiService` and command registration expose display config commands
  to HTTP/WebSocket/MCP.

## Tests

Focused tests should cover:

- v2 config round-trip.
- v1 config migration into `displayDefault`.
- syncing current displays into `displayProfiles`.
- preserving unavailable display profiles.
- composing `displayDefault` plus display overrides.
- saving only changed override fields for the active display.
- display key and label generation from fake display data.
- command-line parsing for `--display-list` and `--display <index-or-key>`.
- command/API/MCP display config behavior through the command surface.
- existing layout and `uiFontScale` tests still pass with composed effective
  config.

Hardware-dependent multi-monitor tests are not required for CI. Display catalog
logic should be testable with fake `DisplayInfo` values.

## Acceptance Criteria

- First startup with no config creates `data/lxe_editor/editor_config.yaml` with
  `version: 2`, `displayDefault`, and one profile per available display.
- With no command-line display, startup uses config `activeDisplay` when
  available, otherwise display index `0`.
- `--display-list` prints available displays and exits before Vulkan renderer
  creation.
- `--display 0` and `--display <key>` select the requested display.
- Per-display window layout and `uiFontScale` persist independently through
  overrides.
- Dragging the window to another display during a run does not hot-switch
  profiles.
- MCP can list displays, inspect effective/default config, update display
  overrides, and select the next active display.
- Remote validation can use the existing manager MCP `ops.editor_restart` after
  changing `activeDisplay`.
